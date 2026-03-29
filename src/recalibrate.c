#include "cpujitter_internal.h"

#include <stdio.h>
#include <string.h>

cpujitter_err cpujitter_run_smoke_test(cpujitter_ctx *ctx, int smoke_bytes) {
    unsigned char buf[64];
    int i;
    unsigned char accum = 0;
    int n;

    if (!ctx || smoke_bytes <= 0) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    n = smoke_bytes;
    if (n > (int)sizeof(buf)) {
        n = (int)sizeof(buf);
    }
    if (cpujitter_backend_get_bytes(ctx, buf, (size_t)n) != CPUJITTER_OK) {
        return CPUJITTER_ERR_SMOKE_TEST;
    }
    for (i = 0; i < n; i++) {
        accum ^= buf[i];
    }

    return accum == 0 ? CPUJITTER_ERR_SMOKE_TEST : CPUJITTER_OK;
}

cpujitter_err cpujitter_apply_profile(cpujitter_ctx *ctx,
                                      const profile_entry *entry,
                                      int source) {
    cpujitter_err err;

    if (!ctx || !entry) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    cpujitter_backend_shutdown(ctx);
    err = cpujitter_backend_init(ctx, entry);
    if (err != CPUJITTER_OK) {
        return err;
    }

    err = cpujitter_run_smoke_test(ctx, entry->smoke_bytes);
    if (err != CPUJITTER_OK) {
        cpujitter_backend_shutdown(ctx);
        return err;
    }

    memset(&ctx->runtime, 0, sizeof(ctx->runtime));
    (void)snprintf(ctx->runtime.profile_id, sizeof(ctx->runtime.profile_id), "%s", entry->id);
    ctx->runtime.osr = entry->osr;
    ctx->runtime.mem_blocks = entry->mem_blocks;
    ctx->runtime.mem_block_size = entry->mem_block_size;
    ctx->runtime.smoke_bytes = entry->smoke_bytes;
    ctx->runtime.source = source;

    return CPUJITTER_OK;
}

cpujitter_err cpujitter_try_recalibrate(cpujitter_ctx *ctx,
                                        const profile_entry *base,
                                        profile_entry *out_tuned) {
    static const int osr_candidates[] = {1, 2, 3};
    static const int mem_blocks_candidates[] = {32, 64};
    static const int mem_block_size_candidates[] = {64, 128};

    size_t i, j, k;
    profile_entry cand;

    if (!ctx || !base || !out_tuned) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    cand = *base;
    for (i = 0; i < sizeof(osr_candidates) / sizeof(osr_candidates[0]); i++) {
        for (j = 0; j < sizeof(mem_blocks_candidates) / sizeof(mem_blocks_candidates[0]); j++) {
            for (k = 0; k < sizeof(mem_block_size_candidates) / sizeof(mem_block_size_candidates[0]); k++) {
                cand.osr = osr_candidates[i];
                cand.mem_blocks = mem_blocks_candidates[j];
                cand.mem_block_size = mem_block_size_candidates[k];
                cand.smoke_bytes = base->smoke_bytes > 0 ? base->smoke_bytes : 32;

                if (cpujitter_apply_profile(ctx, &cand, 3) == CPUJITTER_OK) {
                    *out_tuned = cand;
                    return CPUJITTER_OK;
                }
            }
        }
    }

    return CPUJITTER_ERR_RECALIBRATE;
}
