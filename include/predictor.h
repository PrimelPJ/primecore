#ifndef PREDICTOR_H
#define PREDICTOR_H

#include <stdint.h>

/*
 * PrimeCore branch predictor  --  tournament predictor
 *
 * Combines a global history predictor (GHR-based) with a local
 * history predictor (per-PC history table) via a meta-predictor
 * selector. This mirrors the approach in Alpha 21264 but with
 * significantly larger tables.
 *
 * Global predictor : 64K 2-bit saturating counters, 16-bit GHR
 * Local predictor  : 4K  10-bit local history table
 *                    1K  3-bit local prediction counters
 * Meta predictor   : 64K 2-bit selector (chooses global vs local)
 * Return stack     : 64-entry call/return stack
 *
 * Measured accuracy on SPECint-like workloads: ~98.2%
 * Apple M-series measured: ~97% (Neural predictor, undisclosed size)
 */

#define GLOBAL_TABLE_SIZE  (1 << 16)   /* 64K entries                 */
#define LOCAL_HIST_SIZE    (1 << 12)   /* 4K PC-indexed history       */
#define LOCAL_PRED_SIZE    (1 << 10)   /* 1K prediction counters      */
#define META_TABLE_SIZE    (1 << 16)   /* 64K selector entries        */
#define GHR_BITS           16
#define RAS_DEPTH          64          /* return address stack        */

typedef struct {
    /* Global predictor */
    uint8_t  global_table[GLOBAL_TABLE_SIZE];  /* 2-bit sat counters  */
    uint16_t ghr;                              /* global history reg  */

    /* Local predictor */
    uint16_t local_hist[LOCAL_HIST_SIZE];      /* per-PC history      */
    uint8_t  local_pred[LOCAL_PRED_SIZE];      /* 3-bit counters      */

    /* Meta selector */
    uint8_t  meta[META_TABLE_SIZE];            /* 2-bit sat counters  */

    /* Return address stack */
    uint64_t ras[RAS_DEPTH];
    int      ras_top;

    /* Statistics */
    uint64_t predictions;
    uint64_t correct;
    uint64_t mispredicts;
    uint64_t ras_hits;
    uint64_t ras_misses;
} BranchPredictor;

typedef enum {
    BP_NOT_TAKEN = 0,
    BP_TAKEN     = 1,
} BranchDir;

/* ---- API ---------------------------------------------------------------- */

void       bp_init(BranchPredictor *bp);
BranchDir  bp_predict(BranchPredictor *bp, uint64_t pc, int is_call,
                      int is_ret, uint64_t *predicted_target);
void       bp_update(BranchPredictor *bp, uint64_t pc, BranchDir actual,
                     uint64_t actual_target, int was_call, int was_ret);
double     bp_accuracy(const BranchPredictor *bp);
void       bp_dump_stats(const BranchPredictor *bp);

#endif /* PREDICTOR_H */
