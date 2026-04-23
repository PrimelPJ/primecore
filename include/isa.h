#ifndef ISA_H
#define ISA_H

#include <stdint.h>
#include <stddef.h>

/*
 * PrimeCore ISA  --  32-bit fixed-width RISC instruction set
 *
 * Register file: 64 x 64-bit general purpose (x0..x63), x0 = zero
 * Vector file:   32 x 512-bit vector registers (v0..v31)
 * Special:       PC, FLAGS, VL (vector length), VT (vector type)
 *
 * Instruction formats
 * -------------------
 *  R-type  [31:26 opcode][25:20 rd][19:14 rs1][13:8 rs2][7:0 func]
 *  I-type  [31:26 opcode][25:20 rd][19:14 rs1][13:0 imm14]
 *  L-type  [31:26 opcode][25:20 rd][19:14 base][13:0 offset14]
 *  S-type  [31:26 opcode][25:20 src][19:14 base][13:0 offset14]
 *  B-type  [31:26 opcode][25:20 rs1][19:14 rs2][13:0 rel14]
 *  J-type  [31:26 opcode][25:0 rel26]
 *  V-type  [31:26 opcode][25:20 vd][19:14 vs1][13:8 vs2][7:0 vfunc]
 */

/* ---- opcodes (6 bits) --------------------------------------------------- */

typedef enum {
    /* Integer ALU */
    OP_ADD  = 0x00, OP_ADDX = 0x01,  /* add / add-extended           */
    OP_SUB  = 0x02, OP_SUBX = 0x03,
    OP_MUL  = 0x04, OP_MULH = 0x05,  /* multiply low / high          */
    OP_DIV  = 0x06, OP_MOD  = 0x07,
    OP_AND  = 0x08, OP_OR   = 0x09,
    OP_XOR  = 0x0A, OP_NOT  = 0x0B,
    OP_SHL  = 0x0C, OP_SHR  = 0x0D,  /* logical shift                */
    OP_SAR  = 0x0E,                   /* arithmetic shift right       */
    OP_ROL  = 0x0F, OP_ROR  = 0x10,  /* rotate                       */

    /* Immediate ALU */
    OP_ADDI = 0x11, OP_SUBI = 0x12,
    OP_ANDI = 0x13, OP_ORI  = 0x14,
    OP_XORI = 0x15, OP_SHLI = 0x16,
    OP_SHRI = 0x17, OP_LUI  = 0x18,  /* load upper immediate         */
    OP_AUIPC= 0x19,                   /* add upper imm to PC          */

    /* Comparison */
    OP_CMP  = 0x1A, OP_CMPI = 0x1B,
    OP_SEQ  = 0x1C, OP_SNE  = 0x1D,  /* set if equal / not equal     */
    OP_SLT  = 0x1E, OP_SGE  = 0x1F,  /* set if less / greater-equal  */

    /* Memory */
    OP_LD   = 0x20, OP_LDH  = 0x21,  /* load 64 / 32 bit             */
    OP_LDB  = 0x22, OP_LDW  = 0x23,  /* load 8 / 16 bit              */
    OP_ST   = 0x24, OP_STH  = 0x25,
    OP_STB  = 0x26, OP_STW  = 0x27,
    OP_LDPAIR=0x28, OP_STPAIR=0x29,  /* load/store pair (2x64)       */
    OP_PREF = 0x2A,                   /* cache prefetch               */
    OP_FENCE= 0x2B,                   /* memory fence                 */

    /* Control flow */
    OP_JMP  = 0x2C, OP_JMPR = 0x2D,  /* jump / jump register         */
    OP_CALL = 0x2E, OP_RET  = 0x2F,
    OP_BEQ  = 0x30, OP_BNE  = 0x31,
    OP_BLT  = 0x32, OP_BGE  = 0x33,
    OP_BLTU = 0x34, OP_BGEU = 0x35,
    OP_LOOP = 0x36,                   /* hardware loop counter        */

    /* Vector */
    OP_VSET = 0x37,                   /* set vector length / type     */
    OP_VADD = 0x38, OP_VSUB = 0x39,
    OP_VMUL = 0x3A, OP_VDOT = 0x3B,  /* element-wise mul / dot prod  */
    OP_VFMA = 0x3C,                   /* fused multiply-accumulate    */
    OP_VLD  = 0x3D, OP_VST  = 0x3E,
    OP_VRED = 0x3F,                   /* vector reduction             */

    /* Cryptographic extensions */
    OP_AESE = 0x40, OP_AESD = 0x41,  /* AES encrypt / decrypt round  */
    OP_SHA2 = 0x42, OP_SHA3 = 0x43,
    OP_CLMUL= 0x44,                   /* carry-less multiply (GCM)    */
    OP_RAND = 0x45,                   /* hardware RNG                 */

    /* System */
    OP_NOP  = 0x3E,
    OP_HALT = 0x3F,
} Opcode;

/* ---- register file ------------------------------------------------------ */

#define NUM_GREGS  64
#define NUM_VREGS  32
#define VREG_BYTES 64      /* 512 bits = 64 bytes                     */
#define XZERO      0       /* x0 is always zero                       */
#define XSP        62      /* x62 = stack pointer                     */
#define XLR        63      /* x63 = link register                     */

/* ---- flags register ----------------------------------------------------- */
typedef union {
    uint64_t raw;
    struct {
        uint64_t Z  : 1;   /* zero                                    */
        uint64_t N  : 1;   /* negative                                */
        uint64_t C  : 1;   /* carry                                   */
        uint64_t V  : 1;   /* overflow                                */
        uint64_t S  : 1;   /* saturate                                */
        uint64_t    : 59;
    };
} FlagsReg;

/* ---- instruction word --------------------------------------------------- */

typedef uint32_t Instr;

#define INSTR_OPCODE(i)   (((i) >> 26) & 0x3F)
#define INSTR_RD(i)       (((i) >> 20) & 0x3F)
#define INSTR_RS1(i)      (((i) >> 14) & 0x3F)
#define INSTR_RS2(i)      (((i) >>  8) & 0x3F)
#define INSTR_FUNC(i)     (((i) >>  0) & 0xFF)
#define INSTR_IMM14(i)    ((int32_t)(((i) & 0x3FFF) << 18) >> 18)
#define INSTR_REL26(i)    ((int32_t)(((i) & 0x03FFFFFF) << 6) >> 6)

/* ---- machine state ------------------------------------------------------ */

typedef struct {
    uint64_t  x[NUM_GREGS];         /* general-purpose registers       */
    uint8_t   v[NUM_VREGS][VREG_BYTES]; /* 512-bit vector registers    */
    uint64_t  pc;
    FlagsReg  flags;
    uint32_t  vl;                   /* active vector length            */

    /* memory */
    uint8_t  *mem;
    size_t    mem_size;

    /* statistics */
    uint64_t  instr_retired;
    uint64_t  cycles;
    uint64_t  branches;
    uint64_t  branch_mispredicts;
    uint64_t  cache_l1_hits;
    uint64_t  cache_l1_misses;
    uint64_t  cache_l2_hits;
    uint64_t  cache_l2_misses;
    uint64_t  cache_l3_hits;
    uint64_t  cache_l3_misses;
    uint64_t  vector_ops;
    uint64_t  crypto_ops;

    int       halted;
} PrimeCoreState;

/* ---- ISA API ------------------------------------------------------------ */

PrimeCoreState *pc_state_create(size_t mem_size);
void            pc_state_destroy(PrimeCoreState *s);
void            pc_reset(PrimeCoreState *s);
int             pc_step(PrimeCoreState *s);   /* execute one instruction */
void            pc_run(PrimeCoreState *s);    /* run until HALT          */
void            pc_load(PrimeCoreState *s, const uint8_t *prog,
                        size_t len, uint64_t base_addr);
void            pc_dump_regs(const PrimeCoreState *s);
void            pc_dump_stats(const PrimeCoreState *s);
double          pc_ipc(const PrimeCoreState *s);

#endif /* ISA_H */
