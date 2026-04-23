#include "../include/isa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* -------------------------------------------------------------------------
 * Two-pass assembler for the PrimeCore ISA
 *
 * Syntax:
 *   label:
 *   opcode  rd, rs1, rs2
 *   opcode  rd, rs1, #imm
 *   .org  <addr>
 *   .word <value>
 * ------------------------------------------------------------------------- */

#define MAX_LABELS 512
#define MAX_LINE   256
#define MAX_OUTPUT (1 << 20)   /* 1 MB output buffer */

typedef struct { char name[64]; uint32_t addr; } Label;

static Label  s_labels[MAX_LABELS];
static int    s_nlabels = 0;
static uint8_t s_out[MAX_OUTPUT];
static uint32_t s_pc = 0;

static void label_define(const char *name, uint32_t addr) {
    strncpy(s_labels[s_nlabels].name, name, 63);
    s_labels[s_nlabels].addr = addr;
    s_nlabels++;
}

static int label_lookup(const char *name, uint32_t *addr) {
    for (int i = 0; i < s_nlabels; i++) {
        if (!strcmp(s_labels[i].name, name)) {
            *addr = s_labels[i].addr;
            return 1;
        }
    }
    return 0;
}

static int parse_reg(const char *s) {
    if (s[0] == 'x' || s[0] == 'X') return atoi(s + 1);
    if (!strcasecmp(s, "sp")) return XSP;
    if (!strcasecmp(s, "lr")) return XLR;
    if (!strcasecmp(s, "zero")) return XZERO;
    return -1;
}

static int64_t parse_imm(const char *s) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return (int64_t)strtoll(s, NULL, 16);
    return (int64_t)strtoll(s, NULL, 10);
}

static void emit(uint32_t word) {
    if (s_pc + 4 > MAX_OUTPUT) {
        fprintf(stderr, "[asm] output overflow\n");
        return;
    }
    memcpy(s_out + s_pc, &word, 4);
    s_pc += 4;
}

static uint32_t encode_r(Opcode op, int rd, int rs1, int rs2, int func) {
    return ((op&0x3F)<<26)|((rd&0x3F)<<20)|((rs1&0x3F)<<14)|
           ((rs2&0x3F)<<8)|(func&0xFF);
}

static uint32_t encode_i(Opcode op, int rd, int rs1, int32_t imm) {
    return ((op&0x3F)<<26)|((rd&0x3F)<<20)|((rs1&0x3F)<<14)|(imm&0x3FFF);
}

static uint32_t encode_b(Opcode op, int rs1, int rs2, int32_t rel) {
    return ((op&0x3F)<<26)|((rs1&0x3F)<<20)|((rs2&0x3F)<<14)|(rel&0x3FFF);
}

static uint32_t encode_j(Opcode op, int32_t rel26) {
    return ((op&0x3F)<<26)|(rel26&0x03FFFFFF);
}

typedef struct { const char *name; Opcode op; int type; } OpcodeEntry;
/* type: 0=R, 1=I(rd,rs1,imm), 2=S(rs1,rs2,imm), 3=B(rs1,rs2,rel), 4=J(rel) */
static const OpcodeEntry OPCODES[] = {
    {"add",   OP_ADD,  0}, {"sub",  OP_SUB,  0}, {"mul",  OP_MUL,  0},
    {"div",   OP_DIV,  0}, {"mod",  OP_MOD,  0}, {"and",  OP_AND,  0},
    {"or",    OP_OR,   0}, {"xor",  OP_XOR,  0}, {"shl",  OP_SHL,  0},
    {"shr",   OP_SHR,  0}, {"sar",  OP_SAR,  0}, {"rol",  OP_ROL,  0},
    {"ror",   OP_ROR,  0}, {"seq",  OP_SEQ,  0}, {"sne",  OP_SNE,  0},
    {"slt",   OP_SLT,  0}, {"sge",  OP_SGE,  0},
    {"addi",  OP_ADDI, 1}, {"subi", OP_SUBI, 1}, {"andi", OP_ANDI, 1},
    {"ori",   OP_ORI,  1}, {"xori", OP_XORI, 1}, {"shli", OP_SHLI, 1},
    {"shri",  OP_SHRI, 1}, {"lui",  OP_LUI,  1}, {"auipc",OP_AUIPC,1},
    {"ld",    OP_LD,   1}, {"ldh",  OP_LDH,  1}, {"ldb",  OP_LDB,  1},
    {"st",    OP_ST,   2}, {"stb",  OP_STB,  2},
    {"cmp",   OP_CMP,  3}, {"beq",  OP_BEQ,  3}, {"bne",  OP_BNE,  3},
    {"blt",   OP_BLT,  3}, {"bge",  OP_BGE,  3},
    {"jmp",   OP_JMP,  4}, {"call", OP_CALL, 4},
    {"ret",   OP_RET, -1}, {"nop",  OP_NOP, -1}, {"halt", OP_HALT,-1},
    {NULL, 0, 0}
};

static void assemble_line(const char *line, int pass) {
    char buf[MAX_LINE];
    strncpy(buf, line, MAX_LINE - 1);

    /* Strip comments */
    char *cmt = strchr(buf, ';');
    if (cmt) *cmt = '\0';

    /* Trim */
    char *p = buf;
    while (isspace(*p)) p++;
    int len = strlen(p);
    while (len > 0 && isspace(p[len-1])) p[--len] = '\0';
    if (!*p) return;

    /* Label */
    char *colon = strchr(p, ':');
    if (colon && colon == p + strcspn(p, " \t:")) {
        *colon = '\0';
        if (pass == 1) label_define(p, s_pc);
        p = colon + 1;
        while (isspace(*p)) p++;
        if (!*p) return;
    }

    /* Directive */
    if (p[0] == '.') {
        char dir[32]; uint32_t val;
        if (sscanf(p, ".org %i", &val) == 1) { s_pc = val; return; }
        if (sscanf(p, ".word %i", &val) == 1) { if (pass==2) emit(val); else s_pc+=4; return; }
        (void)dir;
        return;
    }

    /* Mnemonic */
    char mnem[32], a0[32]="", a1[32]="", a2[32]="";
    int n = sscanf(p, "%31s %31[^,], %31[^,], %31s", mnem, a0, a1, a2);
    if (n < 1) return;

    for (int i = 0; OPCODES[i].name; i++) {
        if (!strcasecmp(mnem, OPCODES[i].name)) {
            int type = OPCODES[i].type;
            Opcode op = OPCODES[i].op;

            if (pass == 1) { s_pc += 4; return; }

            if (type == -1) { emit(encode_j(op, 0)); return; }

            int rd  = parse_reg(a0);
            int rs1 = parse_reg(a1);

            if (type == 0) {
                emit(encode_r(op, rd, rs1, parse_reg(a2), 0));
            } else if (type == 1) {
                int32_t imm = (int32_t)parse_imm(a2);
                emit(encode_i(op, rd, rs1, imm));
            } else if (type == 2) {
                int32_t imm = (int32_t)parse_imm(a2);
                emit(encode_b(op, rd, rs1, imm));
            } else if (type == 3) {
                /* branch: rs1, rs2, label or imm */
                uint32_t target;
                int32_t rel;
                if (label_lookup(a2, &target)) {
                    rel = ((int32_t)(target - s_pc) / 4) + 1;
                } else {
                    rel = (int32_t)parse_imm(a2);
                }
                emit(encode_b(op, rd, rs1, rel));
            } else if (type == 4) {
                uint32_t target;
                int32_t rel;
                if (label_lookup(a0, &target)) {
                    rel = (int32_t)(target - s_pc) / 4;
                } else {
                    rel = (int32_t)parse_imm(a0);
                }
                emit(encode_j(op, rel));
            }
            return;
        }
    }
    fprintf(stderr, "[asm] unknown mnemonic: %s\n", mnem);
}

int assemble_file(const char *src_path, const char *out_path) {
    FILE *src = fopen(src_path, "r");
    if (!src) { perror(src_path); return -1; }

    char line[MAX_LINE];
    s_nlabels = 0;

    /* Pass 1: collect labels */
    s_pc = 0;
    while (fgets(line, MAX_LINE, src)) {
        int len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        assemble_line(line, 1);
    }

    /* Pass 2: emit code */
    rewind(src);
    s_pc = 0;
    memset(s_out, 0, sizeof(s_out));
    while (fgets(line, MAX_LINE, src)) {
        int len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        assemble_line(line, 2);
    }
    fclose(src);

    FILE *out = fopen(out_path, "wb");
    if (!out) { perror(out_path); return -1; }
    fwrite(s_out, 1, s_pc, out);
    fclose(out);

    printf("[asm] assembled %s -> %s (%u bytes, %u instructions)\n",
           src_path, out_path, s_pc, s_pc / 4);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: pcasm <source.pca> <output.bin>\n");
        return 1;
    }
    return assemble_file(argv[1], argv[2]) == 0 ? 0 : 1;
}
