#ifndef PIPELINE_H
#define PIPELINE_H

#include "isa.h"
#include "cache.h"
#include "predictor.h"

/*
 * PrimeCore 12-stage superscalar pipeline
 *
 * Stage    Name         Description
 * ------   ----------   -----------------------------------------
 *  1       IF1          Program counter latch, I-cache tag lookup
 *  2       IF2          I-cache data return, instruction alignment
 *  3       PD           Pre-decode: instruction type classification
 *  4       ID           Full decode, register alias table (RAT) lookup
 *  5       RN           Register renaming (physical register file, 256 pregs)
 *  6       DP           Dispatch: route to issue queues
 *  7       IS           Out-of-order issue from unified scheduler (128 entries)
 *  8       EX1          Execute cycle 1 (ALU, address gen, vector lane 0)
 *  9       EX2          Execute cycle 2 (mul/div, vector lanes 1-7)
 * 10       EX3          Execute cycle 3 (FMA, crypto, vector lane reduce)
 * 11       WB1          Write-back / result bus broadcast
 * 12       WB2          Reorder buffer commit, retire up to 8 instr/cycle
 *
 * Issue width   : 12 instructions per cycle (fetch)
 * Retire width  : 8  instructions per cycle
 * Scheduler     : unified, 128 entry out-of-order
 * ROB size      : 512 entries
 * Physical regs : 256 integer + 128 vector
 */

#define PIPE_STAGES     12
#define FETCH_WIDTH     12
#define RETIRE_WIDTH     8
#define ROB_SIZE        512
#define SCHED_SIZE      128
#define PHYS_REGS_INT   256
#define PHYS_REGS_VEC   128

typedef enum {
    PIPE_OK          = 0,
    PIPE_STALL       = 1,
    PIPE_FLUSH       = 2,
    PIPE_HALT        = 3,
} PipeStatus;

typedef struct {
    Instr    instr;
    uint64_t pc;
    int      valid;
    int      flushed;

    /* decoded fields */
    Opcode   opcode;
    int      rd, rs1, rs2;
    int64_t  imm;

    /* renamed registers */
    int      phys_rd, phys_rs1, phys_rs2;

    /* execution results */
    uint64_t alu_result;
    uint64_t mem_addr;
    int      branch_taken;
    uint64_t branch_target;

    /* rob slot */
    int      rob_idx;
} PipelineSlot;

typedef struct {
    PipelineSlot stages[PIPE_STAGES][FETCH_WIDTH];

    /* rename table: logical -> physical */
    int     rat[NUM_GREGS];
    int     free_list[PHYS_REGS_INT];
    int     free_head;
    int     free_tail;

    /* reorder buffer */
    PipelineSlot rob[ROB_SIZE];
    int          rob_head;
    int          rob_tail;
    int          rob_count;

    /* scheduler */
    PipelineSlot sched[SCHED_SIZE];
    int          sched_count;

    /* physical register file */
    uint64_t phys_regs[PHYS_REGS_INT];
    int      phys_ready[PHYS_REGS_INT];

    /* statistics */
    uint64_t cycles;
    uint64_t instrs_fetched;
    uint64_t instrs_retired;
    uint64_t stall_cycles;
    uint64_t flush_cycles;
    uint64_t branch_flushes;

    CacheHierarchy *cache;
    BranchPredictor bp;
} Pipeline;

/* ---- API ---------------------------------------------------------------- */

void       pipeline_init(Pipeline *p, CacheHierarchy *cache);
void       pipeline_destroy(Pipeline *p);
PipeStatus pipeline_cycle(Pipeline *p, PrimeCoreState *s);
void       pipeline_run(Pipeline *p, PrimeCoreState *s);
double     pipeline_ipc(const Pipeline *p);
void       pipeline_dump_stats(const Pipeline *p);

#endif /* PIPELINE_H */
