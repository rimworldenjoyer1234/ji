#include "cpujitter_internal.h"

#include <stdio.h>
#include <string.h>

cpujitter_err cpujitter_run_smoke_test(cpujitter_ctx *ctx, int smoke_bytes) {
    unsigned char buf[256];
    int n;

    if (!ctx || smoke_bytes <= 0) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    n = smoke_bytes;
    if (n > (int)sizeof(buf)) {
        n = (int)sizeof(buf);
    }

    if (cpujitter_backend_get_bytes(ctx, buf, (size_t)n) != CPUJITTER_OK) {
        ctx->backend_smoke_success = 0;
        return CPUJITTER_ERR_SMOKE_TEST;
    }

    ctx->backend_smoke_success = 1;
    return CPUJITTER_OK;
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
    snprintf(ctx->runtime.profile_id, sizeof(ctx->runtime.profile_id), "%s", entry->id);
    ctx->runtime.source = source;
    ctx->runtime.osr = entry->osr;
    ctx->runtime.flags = entry->flags;
    ctx->runtime.disable_memory_access = entry->disable_memory_access;
    ctx->runtime.force_internal_timer = entry->force_internal_timer;
    ctx->runtime.disable_internal_timer = entry->disable_internal_timer;
    ctx->runtime.force_fips = entry->force_fips;
    ctx->runtime.ntg1 = entry->ntg1;
    ctx->runtime.cache_all = entry->cache_all;
    ctx->runtime.max_memsize_kb = entry->max_memsize_kb;
    ctx->runtime.hashloop = entry->hashloop;
    ctx->runtime.smoke_bytes = entry->smoke_bytes;

    return CPUJITTER_OK;
}

cpujitter_err cpujitter_try_recalibrate(cpujitter_ctx *ctx,
                                        const profile_entry *base,
                                        profile_entry *out_tuned) {
    static const int osr_candidates[] = {3, 4, 6, 8};
    static const int hashloop_candidates[] = {1, 4, 8};
    static const int max_mem_kb_candidates[] = {64, 256, 1024};
    static const int timer_mode[] = {0, 1, 2}; /* 0 native, 1 disable_internal_timer, 2 force_internal_timer */

    size_t i, j, k, t;
    profile_entry cand;

    if (!ctx || !base || !out_tuned) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    cand = *base;
    for (t = 0; t < sizeof(timer_mode) / sizeof(timer_mode[0]); t++) {
        for (i = 0; i < sizeof(osr_candidates) / sizeof(osr_candidates[0]); i++) {
            for (j = 0; j < sizeof(hashloop_candidates) / sizeof(hashloop_candidates[0]); j++) {
                for (k = 0; k < sizeof(max_mem_kb_candidates) / sizeof(max_mem_kb_candidates[0]); k++) {
                    cand.osr = osr_candidates[i];
                    cand.hashloop = hashloop_candidates[j];
                    cand.max_memsize_kb = max_mem_kb_candidates[k];
                    cand.disable_internal_timer = (timer_mode[t] == 1);
                    cand.force_internal_timer = (timer_mode[t] == 2);
                    cand.flags = 0;
                    if (cand.disable_memory_access) cand.flags |= 0x0001U;
                    if (cand.force_internal_timer) cand.flags |= 0x0002U;
                    if (cand.disable_internal_timer) cand.flags |= 0x0004U;
                    if (cand.force_fips) cand.flags |= 0x0008U;
                    if (cand.ntg1) cand.flags |= 0x0010U;
                    if (cand.cache_all) cand.flags |= 0x0020U;

                    if (cpujitter_apply_profile(ctx, &cand, 3) == CPUJITTER_OK) {
                        *out_tuned = cand;
                        return CPUJITTER_OK;
                    }
                }
            }
        }
    }

    return CPUJITTER_ERR_RECALIBRATE;
}
