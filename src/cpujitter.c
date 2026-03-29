#include "cpujitter/cpujitter.h"
#include "cpujitter_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PROFILES 64

static int cpujitter_log_enabled(void) {
    const char *v = getenv("CPUJITTER_LOG");
    return v && v[0] != '\0' && strcmp(v, "0") != 0;
}

static void cpujitter_log(const char *fmt, ...) {
    va_list ap;
    if (!cpujitter_log_enabled()) {
        return;
    }
    va_start(ap, fmt);
    fprintf(stderr, "[cpujitter] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static void build_default_profile(const cpujitter_platform_info *platform, profile_entry *out) {
    if (!platform || !out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->id, sizeof(out->id), "default-%.24s-%.24s", platform->os, platform->arch);
    snprintf(out->os, sizeof(out->os), "%s", platform->os);
    snprintf(out->arch, sizeof(out->arch), "%s", platform->arch);
    snprintf(out->cpu_vendor, sizeof(out->cpu_vendor), "%s", platform->cpu_vendor);
    out->osr = 1;
    out->mem_blocks = 64;
    out->mem_block_size = 64;
    out->smoke_bytes = 32;
}

static cpujitter_err init_from_cache(cpujitter_ctx *ctx, int *out_used_cache) {
    profile_entry cache_entry;
    cpujitter_err err;

    if (!ctx || !out_used_cache) {
        return CPUJITTER_ERR_INVALID_ARG;
    }
    *out_used_cache = 0;

    err = cpujitter_cache_load_validated(ctx->cache_path, &ctx->platform, &cache_entry);
    if (err != CPUJITTER_OK) {
        if (err == CPUJITTER_ERR_NO_PROFILE) {
            cpujitter_log("cache rejected as stale/incompatible: %s", ctx->cache_path);
            return CPUJITTER_OK;
        }
        if (err == CPUJITTER_ERR_IO || err == CPUJITTER_ERR_PARSE) {
            cpujitter_log("cache unavailable/corrupt: %s (%s)", ctx->cache_path, cpujitter_strerror(err));
            return CPUJITTER_OK;
        }
        return err;
    }

    err = cpujitter_apply_profile(ctx, &cache_entry, 1);
    if (err != CPUJITTER_OK) {
        cpujitter_log("cached profile failed smoke test (%s), continuing", cpujitter_strerror(err));
        return CPUJITTER_OK;
    }

    *out_used_cache = 1;
    snprintf(ctx->match_explanation, sizeof(ctx->match_explanation), "cache profile id=%s accepted", cache_entry.id);
    cpujitter_log("using cached validated profile '%s'", cache_entry.id);
    return CPUJITTER_OK;
}

static cpujitter_err init_from_bundled_profiles(cpujitter_ctx *ctx,
                                                const char *force_profile_id,
                                                profile_entry *out_selected) {
    profile_entry entries[MAX_PROFILES];
    size_t count = 0;
    cpujitter_err err;

    if (!ctx || !out_selected) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    err = cpujitter_profiles_load(ctx->profiles_index_path, entries, MAX_PROFILES, &count);
    if (err != CPUJITTER_OK) {
        cpujitter_log("failed loading profile index '%s' (%s)",
                      ctx->profiles_index_path,
                      cpujitter_strerror(err));
        return err;
    }

    if (force_profile_id) {
        err = cpujitter_profiles_find_by_id(entries, count, force_profile_id, out_selected);
        if (err == CPUJITTER_OK) {
            snprintf(ctx->match_explanation, sizeof(ctx->match_explanation), "forced profile id=%s", force_profile_id);
        }
    } else {
        err = cpujitter_profiles_select_best(entries,
                                             count,
                                             &ctx->platform,
                                             out_selected,
                                             ctx->match_explanation,
                                             sizeof(ctx->match_explanation));
    }
    if (err != CPUJITTER_OK) {
        cpujitter_log("no bundled profile selected (%s)", cpujitter_strerror(err));
        return err;
    }

    cpujitter_log("selected bundled profile '%s'", out_selected->id);
    return CPUJITTER_OK;
}

static cpujitter_err init_common(cpujitter_ctx **out_ctx,
                                 const char *profiles_index_path,
                                 const char *cache_path,
                                 const char *force_profile_id) {
    cpujitter_ctx *ctx;
    profile_entry selected;
    profile_entry tuned;
    cpujitter_err err;
    int used_cache = 0;

    if (!out_ctx || !profiles_index_path || !cache_path) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    *out_ctx = NULL;
    ctx = (cpujitter_ctx *)calloc(1U, sizeof(*ctx));
    if (!ctx) {
        return CPUJITTER_ERR_IO;
    }

    cpujitter_detect_platform(&ctx->platform);
    snprintf(ctx->profiles_index_path, sizeof(ctx->profiles_index_path), "%s", profiles_index_path);
    snprintf(ctx->cache_path, sizeof(ctx->cache_path), "%s", cache_path);
    cpujitter_log("init platform os=%s arch=%s vendor=%s model=%s virt=%s cpus=%d",
                  ctx->platform.os,
                  ctx->platform.arch,
                  ctx->platform.cpu_vendor,
                  ctx->platform.cpu_model,
                  ctx->platform.virtualization,
                  ctx->platform.logical_cpu_count);

    if (!force_profile_id) {
        err = init_from_cache(ctx, &used_cache);
        if (err != CPUJITTER_OK) {
            free(ctx);
            return err;
        }
        if (used_cache) {
            *out_ctx = ctx;
            return CPUJITTER_OK;
        }
    }

    err = init_from_bundled_profiles(ctx, force_profile_id, &selected);
    if (err == CPUJITTER_OK) {
        err = cpujitter_apply_profile(ctx, &selected, 2);
        if (err == CPUJITTER_OK) {
            if (ctx->match_explanation[0] == '\0') {
                snprintf(ctx->match_explanation, sizeof(ctx->match_explanation), "bundled profile id=%s accepted", selected.id);
            }
            (void)cpujitter_cache_save(ctx->cache_path, &selected, &ctx->platform);
            cpujitter_log("bundled profile accepted and cached: %s", selected.id);
            *out_ctx = ctx;
            return CPUJITTER_OK;
        }
        cpujitter_log("bundled profile failed smoke test: %s", cpujitter_strerror(err));
    } else if (err != CPUJITTER_ERR_NO_PROFILE && !force_profile_id) {
        free(ctx);
        return err;
    }

    /* Lightweight fallback only (small matrix in recalibrate.c). */
    if (force_profile_id) {
        selected = (profile_entry){0};
        build_default_profile(&ctx->platform, &selected);
    } else if (err == CPUJITTER_ERR_NO_PROFILE) {
        build_default_profile(&ctx->platform, &selected);
    }

    err = cpujitter_try_recalibrate(ctx, &selected, &tuned);
    if (err != CPUJITTER_OK) {
        cpujitter_backend_shutdown(ctx);
        free(ctx);
        return err;
    }

    snprintf(ctx->match_explanation, sizeof(ctx->match_explanation), "recalibrated from profile id=%s", selected.id);
    (void)cpujitter_cache_save(ctx->cache_path, &tuned, &ctx->platform);
    cpujitter_log("recalibration succeeded and cached profile '%s'", tuned.id);

    *out_ctx = ctx;
    return CPUJITTER_OK;
}

cpujitter_err cpujitter_init(cpujitter_ctx **out_ctx,
                             const char *profiles_index_path,
                             const char *cache_path) {
    return init_common(out_ctx, profiles_index_path, cache_path, NULL);
}

cpujitter_err cpujitter_init_with_profile(cpujitter_ctx **out_ctx,
                                          const char *profiles_index_path,
                                          const char *cache_path,
                                          const char *profile_id) {
    return init_common(out_ctx, profiles_index_path, cache_path, profile_id);
}

cpujitter_err cpujitter_recalibrate(cpujitter_ctx *ctx) {
    profile_entry base;
    profile_entry tuned;
    cpujitter_err err;

    if (!ctx) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    memset(&base, 0, sizeof(base));
    snprintf(base.id,
             sizeof(base.id),
             "%.47s-recal-base",
             ctx->runtime.profile_id[0] ? ctx->runtime.profile_id : "runtime");
    snprintf(base.os, sizeof(base.os), "%s", ctx->platform.os);
    snprintf(base.arch, sizeof(base.arch), "%s", ctx->platform.arch);
    snprintf(base.cpu_vendor, sizeof(base.cpu_vendor), "%s", ctx->platform.cpu_vendor);
    base.osr = ctx->runtime.osr > 0 ? ctx->runtime.osr : 1;
    base.mem_blocks = ctx->runtime.mem_blocks > 0 ? ctx->runtime.mem_blocks : 64;
    base.mem_block_size = ctx->runtime.mem_block_size > 0 ? ctx->runtime.mem_block_size : 64;
    base.smoke_bytes = ctx->runtime.smoke_bytes > 0 ? ctx->runtime.smoke_bytes : 32;

    err = cpujitter_try_recalibrate(ctx, &base, &tuned);
    if (err != CPUJITTER_OK) {
        return err;
    }

    (void)cpujitter_cache_save(ctx->cache_path, &tuned, &ctx->platform);
    return CPUJITTER_OK;
}

cpujitter_err cpujitter_get_bytes(cpujitter_ctx *ctx, uint8_t *out, size_t len) {
    return cpujitter_backend_get_bytes(ctx, out, len);
}

cpujitter_err cpujitter_roll_die(cpujitter_ctx *ctx, uint8_t *die_value) {
    uint8_t b = 0;
    cpujitter_err err;

    if (!ctx || !die_value) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    do {
        err = cpujitter_get_bytes(ctx, &b, 1U);
        if (err != CPUJITTER_OK) {
            return err;
        }
    } while (b >= 252U);

    *die_value = (uint8_t)((b % 6U) + 1U);
    return CPUJITTER_OK;
}

cpujitter_err cpujitter_get_platform_info(cpujitter_ctx *ctx,
                                          cpujitter_platform_info *out_info) {
    if (!ctx || !out_info) {
        return CPUJITTER_ERR_INVALID_ARG;
    }
    *out_info = ctx->platform;
    return CPUJITTER_OK;
}

cpujitter_err cpujitter_get_runtime_config(cpujitter_ctx *ctx,
                                           cpujitter_runtime_config *out_cfg) {
    if (!ctx || !out_cfg) {
        return CPUJITTER_ERR_INVALID_ARG;
    }
    *out_cfg = ctx->runtime;
    return CPUJITTER_OK;
}

cpujitter_err cpujitter_get_status_json(cpujitter_ctx *ctx,
                                        char *out_buf,
                                        size_t out_buf_len,
                                        size_t *out_written) {
    int n;
    if (!ctx || !out_buf || out_buf_len == 0) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    n = snprintf(out_buf,
                 out_buf_len,
                 "{\"profile_id\":\"%s\",\"source\":%d,\"match_explanation\":\"%s\",\"platform\":{\"os\":\"%s\",\"arch\":\"%s\",\"cpu_vendor\":\"%s\",\"cpu_model\":\"%s\",\"virtualization\":\"%s\",\"logical_cpu_count\":%d},\"config\":{\"osr\":%d,\"mem_blocks\":%d,\"mem_block_size\":%d,\"smoke_bytes\":%d}}",
                 ctx->runtime.profile_id,
                 ctx->runtime.source,
                 ctx->match_explanation,
                 ctx->platform.os,
                 ctx->platform.arch,
                 ctx->platform.cpu_vendor,
                 ctx->platform.cpu_model,
                 ctx->platform.virtualization,
                 ctx->platform.logical_cpu_count,
                 ctx->runtime.osr,
                 ctx->runtime.mem_blocks,
                 ctx->runtime.mem_block_size,
                 ctx->runtime.smoke_bytes);
    if (n < 0) {
        return CPUJITTER_ERR_IO;
    }
    if ((size_t)n >= out_buf_len) {
        return CPUJITTER_ERR_BUFFER_TOO_SMALL;
    }
    if (out_written) {
        *out_written = (size_t)n;
    }
    return CPUJITTER_OK;
}

void cpujitter_shutdown(cpujitter_ctx *ctx) {
    if (!ctx) {
        return;
    }
    cpujitter_backend_shutdown(ctx);
    free(ctx);
}

const char *cpujitter_strerror(cpujitter_err err) {
    switch (err) {
    case CPUJITTER_OK:
        return "ok";
    case CPUJITTER_ERR_INVALID_ARG:
        return "invalid argument";
    case CPUJITTER_ERR_IO:
        return "i/o error";
    case CPUJITTER_ERR_PARSE:
        return "parse error";
    case CPUJITTER_ERR_NO_PROFILE:
        return "no matching profile";
    case CPUJITTER_ERR_SMOKE_TEST:
        return "smoke test failed";
    case CPUJITTER_ERR_RECALIBRATE:
        return "recalibration failed";
    case CPUJITTER_ERR_ENTROPY_BACKEND:
        return "entropy backend error";
    case CPUJITTER_ERR_STATE:
        return "invalid state";
    case CPUJITTER_ERR_BUFFER_TOO_SMALL:
        return "buffer too small";
    default:
        return "unknown error";
    }
}
