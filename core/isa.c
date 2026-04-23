#include "../include/isa.h"
#include "../include/cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* -------------------------------------------------------------------------
 * State lifecycle
 * ------------------------------------------------------------------------- */

PrimeCoreState *pc_state_create(size_t mem_size) {
    PrimeCoreState *s = calloc(1, sizeof(PrimeCoreState));
    assert(s);
    s->mem      = calloc(1, mem_size);
    s->mem_size = mem_size;
    assert(s->mem);
    return s;
}

void pc_state_destroy(PrimeCoreState *s) {
    if (!s) return;
    free(s->mem);
    free(s);
}

void pc_reset(PrimeCoreState *s) {
    memset(s->x,     0, sizeof(s->x));
    memset(s->v,     0, sizeof(s->v));
    s->pc             = 0;
    s->flags.raw      = 0;
    s->vl             = 0;
    s->instr_retired  = 0;
    s->cycles         = 0;
    s->branches       = 0;
    s->branch_mispredicts = 0;
    s->cache_l1_hits  = 0;
    s->cache_l1_misses= 0;
    s->cache_l2_hits  = 0;
    s->cache_l2_misses= 0;
    s->cache_l3_hits  = 0;
    s->cache_l3_misses= 0;
    s->vector_ops     = 0;
    s->crypto_ops     = 0;
    s->halted         = 0;
}

void pc_load(PrimeCoreState *s, const uint8_t *prog, size_t len,
             uint64_t base_addr) {
    assert(base_addr + len <= s->mem_size);
    memcpy(s->mem + base_addr, prog, len);
    s->pc = base_addr;
}

/* -------------------------------------------------------------------------
 * Memory helpers
 * ------------------------------------------------------------------------- */

static inline uint64_t mem_read64(PrimeCoreState *s, uint64_t addr) {
    assert(addr + 8 <= s->mem_size);
    uint64_t v;
    memcpy(&v, s->mem + addr, 8);
    return v;
}

static inline void mem_write64(PrimeCoreState *s, uint64_t addr, uint64_t v) {
    assert(addr + 8 <= s->mem_size);
    memcpy(s->mem + addr, &v, 8);
}

static inline uint32_t mem_read32(PrimeCoreState *s, uint64_t addr) {
    assert(addr + 4 <= s->mem_size);
    uint32_t v;
    memcpy(&v, s->mem + addr, 4);
    return v;
}

/* -------------------------------------------------------------------------
 * Flag update
 * ------------------------------------------------------------------------- */

static inline void update_flags(PrimeCoreState *s, int64_t result,
                                  int64_t a, int64_t b, int is_sub) {
    s->flags.Z = (result == 0);
    s->flags.N = (result < 0);
    if (is_sub) {
        s->flags.C = ((uint64_t)a < (uint64_t)b);
        s->flags.V = ((a ^ b) < 0) && ((a ^ result) < 0);
    } else {
        s->flags.C = ((uint64_t)result < (uint64_t)a);
        s->flags.V = (!((a ^ b) < 0)) && ((a ^ result) < 0);
    }
}

/* -------------------------------------------------------------------------
 * Single instruction execution
 * ------------------------------------------------------------------------- */

int pc_step(PrimeCoreState *s) {
    if (s->halted) return 0;

    assert(s->pc + 4 <= s->mem_size);
    Instr instr = mem_read32(s, s->pc);
    s->pc += 4;

    Opcode   op   = INSTR_OPCODE(instr);
    int      rd   = INSTR_RD(instr);
    int      rs1  = INSTR_RS1(instr);
    int      rs2  = INSTR_RS2(instr);
    int32_t  imm  = INSTR_IMM14(instr);

    uint64_t va   = s->x[rs1];
    uint64_t vb   = s->x[rs2];
    uint64_t result = 0;

    s->cycles++;
    s->instr_retired++;
    s->x[XZERO] = 0;  /* x0 always zero */

    switch (op) {
    /* Integer ALU */
    case OP_ADD:  result = va + vb;  update_flags(s,(int64_t)result,(int64_t)va,(int64_t)vb,0); break;
    case OP_SUB:  result = va - vb;  update_flags(s,(int64_t)result,(int64_t)va,(int64_t)vb,1); break;
    case OP_MUL:  result = va * vb;  break;
    case OP_MULH: result = ((__int128)(int64_t)va * (int64_t)vb) >> 64; break;
    case OP_DIV:  result = vb ? va / vb : 0; break;
    case OP_MOD:  result = vb ? va % vb : 0; break;
    case OP_AND:  result = va & vb;  break;
    case OP_OR:   result = va | vb;  break;
    case OP_XOR:  result = va ^ vb;  break;
    case OP_NOT:  result = ~va;      break;
    case OP_SHL:  result = va << (vb & 63); break;
    case OP_SHR:  result = va >> (vb & 63); break;
    case OP_SAR:  result = (uint64_t)((int64_t)va >> (vb & 63)); break;
    case OP_ROL:  { int n = vb & 63; result = (va << n) | (va >> (64 - n)); } break;
    case OP_ROR:  { int n = vb & 63; result = (va >> n) | (va << (64 - n)); } break;

    /* Immediate ALU */
    case OP_ADDI: result = va + (int64_t)imm; update_flags(s,(int64_t)result,(int64_t)va,(int64_t)imm,0); break;
    case OP_SUBI: result = va - (int64_t)imm; break;
    case OP_ANDI: result = va & (uint64_t)(int64_t)imm; break;
    case OP_ORI:  result = va | (uint64_t)(int64_t)imm; break;
    case OP_XORI: result = va ^ (uint64_t)(int64_t)imm; break;
    case OP_SHLI: result = va << (imm & 63); break;
    case OP_SHRI: result = va >> (imm & 63); break;
    case OP_LUI:  result = (uint64_t)imm << 32; break;
    case OP_AUIPC:result = (s->pc - 4) + ((uint64_t)imm << 12); break;

    /* Comparison */
    case OP_CMP:  update_flags(s,(int64_t)(va-vb),(int64_t)va,(int64_t)vb,1); s->pc -= 4; s->pc += 4; goto no_rd;
    case OP_CMPI: update_flags(s,(int64_t)(va-(int64_t)imm),(int64_t)va,(int64_t)imm,1); goto no_rd;
    case OP_SEQ:  result = (va == vb) ? 1 : 0; break;
    case OP_SNE:  result = (va != vb) ? 1 : 0; break;
    case OP_SLT:  result = ((int64_t)va < (int64_t)vb) ? 1 : 0; break;
    case OP_SGE:  result = ((int64_t)va >= (int64_t)vb) ? 1 : 0; break;

    /* Memory */
    case OP_LD:   result = mem_read64(s, va + imm); break;
    case OP_LDH:  { uint32_t v; memcpy(&v, s->mem+va+imm, 4); result=v; } break;
    case OP_LDB:  result = s->mem[va + imm]; break;
    case OP_ST:   mem_write64(s, vb + imm, va); goto no_rd;
    case OP_STB:  s->mem[vb + imm] = (uint8_t)va; goto no_rd;
    case OP_FENCE: goto no_rd;
    case OP_PREF: goto no_rd;

    /* Control flow */
    case OP_JMP:  s->pc = (s->pc - 4) + (INSTR_REL26(instr) << 2); goto no_rd;
    case OP_JMPR: s->pc = va;  goto no_rd;
    case OP_CALL: result = s->pc; s->pc = (s->pc-4) + (INSTR_REL26(instr)<<2);
                  s->branches++; break;
    case OP_RET:  s->pc = s->x[XLR]; goto no_rd;

    case OP_BEQ:  s->branches++; if (s->flags.Z) s->pc += (int64_t)imm*4-4; goto no_rd;
    case OP_BNE:  s->branches++; if (!s->flags.Z) s->pc += (int64_t)imm*4-4; goto no_rd;
    case OP_BLT:  s->branches++; if (s->flags.N != s->flags.V) s->pc += (int64_t)imm*4-4; goto no_rd;
    case OP_BGE:  s->branches++; if (s->flags.N == s->flags.V) s->pc += (int64_t)imm*4-4; goto no_rd;
    case OP_BLTU: s->branches++; if (s->flags.C) s->pc += (int64_t)imm*4-4; goto no_rd;
    case OP_BGEU: s->branches++; if (!s->flags.C) s->pc += (int64_t)imm*4-4; goto no_rd;

    /* Vector (simplified -- full 512-bit paths in execution_unit.c) */
    case OP_VSET: s->vl = (uint32_t)va; s->vector_ops++; goto no_rd;
    case OP_VADD:
    case OP_VSUB:
    case OP_VMUL:
    case OP_VFMA:
    case OP_VDOT: s->vector_ops++; goto no_rd;

    /* Crypto */
    case OP_AESE:
    case OP_AESD:
    case OP_SHA2:
    case OP_SHA3:
    case OP_CLMUL: s->crypto_ops++; goto no_rd;

    case OP_NOP:  goto no_rd;
    case OP_HALT: s->halted = 1; goto no_rd;

    default:
        fprintf(stderr, "[isa] unrecognised opcode 0x%02X at PC=0x%llX\n",
                op, (unsigned long long)(s->pc - 4));
        s->halted = 1;
        goto no_rd;
    }

    if (rd != XZERO) s->x[rd] = result;
no_rd:
    s->x[XZERO] = 0;
    return s->halted ? 0 : 1;
}

void pc_run(PrimeCoreState *s) {
    while (pc_step(s));
}

double pc_ipc(const PrimeCoreState *s) {
    return s->cycles ? (double)s->instr_retired / s->cycles : 0.0;
}

void pc_dump_regs(const PrimeCoreState *s) {
    printf("\n--- PrimeCore register file ---\n");
    for (int i = 0; i < NUM_GREGS; i += 4) {
        printf("  x%-2d %016llX  x%-2d %016llX  x%-2d %016llX  x%-2d %016llX\n",
               i,   (unsigned long long)s->x[i],
               i+1, (unsigned long long)s->x[i+1],
               i+2, (unsigned long long)s->x[i+2],
               i+3, (unsigned long long)s->x[i+3]);
    }
    printf("  PC  %016llX  FLAGS Z=%d N=%d C=%d V=%d\n",
           (unsigned long long)s->pc,
           s->flags.Z, s->flags.N, s->flags.C, s->flags.V);
}

void pc_dump_stats(const PrimeCoreState *s) {
    printf("\n=== PrimeCore execution statistics ===\n");
    printf("  Instructions retired : %llu\n",  (unsigned long long)s->instr_retired);
    printf("  Cycles               : %llu\n",  (unsigned long long)s->cycles);
    printf("  IPC                  : %.3f\n",  pc_ipc(s));
    printf("  Branches             : %llu\n",  (unsigned long long)s->branches);
    printf("  Mispredicts          : %llu\n",  (unsigned long long)s->branch_mispredicts);
    double bpa = s->branches
                 ? 100.0*(1.0-(double)s->branch_mispredicts/s->branches)
                 : 100.0;
    printf("  Branch accuracy      : %.2f%%\n", bpa);
    printf("  L1 hits/misses       : %llu / %llu\n",
           (unsigned long long)s->cache_l1_hits,
           (unsigned long long)s->cache_l1_misses);
    printf("  L2 hits/misses       : %llu / %llu\n",
           (unsigned long long)s->cache_l2_hits,
           (unsigned long long)s->cache_l2_misses);
    printf("  Vector ops           : %llu\n",  (unsigned long long)s->vector_ops);
    printf("  Crypto ops           : %llu\n",  (unsigned long long)s->crypto_ops);
    printf("\n");
}
