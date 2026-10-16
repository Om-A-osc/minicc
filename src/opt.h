/* opt.h - IR optimisation passes.
 *
 *   constant folding + propagation   (temps are SSA, so propagation is a map)
 *   local common subexpression elimination with store/call invalidation
 *   dead code elimination            (unused pure temps, dead local stores)
 *   control-flow simplification      (fold constant branches, thread empty
 *                                     blocks, merge straight-line blocks)
 *
 * The passes run to a fixpoint. Level 0 still removes unreachable blocks so
 * every backend sees well-formed functions. */
#ifndef MINICC_OPT_H
#define MINICC_OPT_H

#include "ir.h"

typedef struct {
    int folded;         /* constant expressions folded */
    int cse;            /* redundant computations removed */
    int dce;            /* dead instructions removed */
    int branches;       /* constant branches resolved */
    int blocks;         /* blocks threaded or merged */
} OptStats;

void opt_run(IrModule *m, int level, OptStats *stats);

#endif
