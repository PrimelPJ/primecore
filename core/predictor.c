#include "../include/predictor.h"
#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Tournament branch predictor
 *
 * Uses a 2-bit saturating counter idiom:
 *   0 = strongly not-taken
 *   1 = weakly not-taken
 *   2 = weakly taken
 *   3 = strongly taken
 * ------------------------------------------------------------------------- */

static inline int predict_2bit(uint8_t counter) {
    return counter >= 2;
}

static inline uint8_t update_2bit(uint8_t counter, int taken) {
    if (taken && counter < 3) return counter + 1;
    if (!taken && counter > 0) return counter - 1;
    return counter;
}

void bp_init(BranchPredictor *bp) {
    memset(bp, 0, sizeof(*bp));
    /* Initialise all counters weakly taken */
    memset(bp->global_table, 2, sizeof(bp->global_table));
    memset(bp->local_pred,   2, sizeof(bp->local_pred));
    memset(bp->meta,         2, sizeof(bp->meta));
    bp->ras_top = 0;
}

BranchDir bp_predict(BranchPredictor *bp, uint64_t pc,
                     int is_call, int is_ret, uint64_t *target) {
    bp->predictions++;

    /* Return address stack */
    if (is_ret) {
        if (bp->ras_top > 0) {
            *target = bp->ras[--bp->ras_top];
            bp->ras_hits++;
        }
        return BP_TAKEN;
    }

    if (is_call && bp->ras_top < RAS_DEPTH) {
        bp->ras[bp->ras_top++] = pc + 4;
    }

    /* Global predictor index */
    uint32_t global_idx = (uint32_t)(pc ^ bp->ghr) & (GLOBAL_TABLE_SIZE - 1);
    int global_pred = predict_2bit(bp->global_table[global_idx]);

    /* Local predictor index */
    uint32_t local_hist_idx = (uint32_t)(pc >> 2) & (LOCAL_HIST_SIZE - 1);
    uint32_t local_pred_idx = bp->local_hist[local_hist_idx] & (LOCAL_PRED_SIZE - 1);
    int local_pred = predict_2bit(bp->local_pred[local_pred_idx]);

    /* Meta selector */
    uint32_t meta_idx = (uint32_t)(pc ^ (bp->ghr << 2)) & (META_TABLE_SIZE - 1);
    int use_global = predict_2bit(bp->meta[meta_idx]);

    return (use_global ? global_pred : local_pred) ? BP_TAKEN : BP_NOT_TAKEN;
}

void bp_update(BranchPredictor *bp, uint64_t pc, BranchDir actual,
               uint64_t actual_target, int was_call, int was_ret) {
    (void)actual_target;
    (void)was_call;

    if (was_ret) return;

    int taken = (actual == BP_TAKEN);

    /* Compute both predictions again for meta update */
    uint32_t global_idx    = (uint32_t)(pc ^ bp->ghr) & (GLOBAL_TABLE_SIZE - 1);
    uint32_t local_hist_idx = (uint32_t)(pc >> 2) & (LOCAL_HIST_SIZE - 1);
    uint32_t local_pred_idx = bp->local_hist[local_hist_idx] & (LOCAL_PRED_SIZE - 1);
    uint32_t meta_idx      = (uint32_t)(pc ^ (bp->ghr << 2)) & (META_TABLE_SIZE - 1);

    int global_pred = predict_2bit(bp->global_table[global_idx]);
    int local_pred  = predict_2bit(bp->local_pred[local_pred_idx]);

    /* Update meta: favour the predictor that was correct */
    if (global_pred != local_pred) {
        bp->meta[meta_idx] = update_2bit(bp->meta[meta_idx],
                                         global_pred == taken);
    }

    /* Update global predictor */
    bp->global_table[global_idx] = update_2bit(bp->global_table[global_idx], taken);

    /* Update local history and local predictor */
    bp->local_pred[local_pred_idx] = update_2bit(bp->local_pred[local_pred_idx], taken);
    bp->local_hist[local_hist_idx] = (uint16_t)
        ((bp->local_hist[local_hist_idx] << 1) | taken);

    /* Shift GHR */
    bp->ghr = (uint16_t)((bp->ghr << 1) | taken);

    /* Accuracy tracking */
    int use_global = predict_2bit(bp->meta[meta_idx]);
    int final_pred = use_global ? global_pred : local_pred;
    if (final_pred == taken) {
        bp->correct++;
    } else {
        bp->mispredicts++;
    }
}

double bp_accuracy(const BranchPredictor *bp) {
    return bp->predictions
           ? 100.0 * (double)bp->correct / bp->predictions
           : 0.0;
}

void bp_dump_stats(const BranchPredictor *bp) {
    printf("\n=== Branch predictor statistics ===\n");
    printf("  Predictions   : %llu\n", (unsigned long long)bp->predictions);
    printf("  Correct       : %llu\n", (unsigned long long)bp->correct);
    printf("  Mispredicts   : %llu\n", (unsigned long long)bp->mispredicts);
    printf("  Accuracy      : %.3f%%\n", bp_accuracy(bp));
    printf("  RAS hits      : %llu\n", (unsigned long long)bp->ras_hits);
    printf("  RAS misses    : %llu\n", (unsigned long long)bp->ras_misses);
    printf("\n");
}
