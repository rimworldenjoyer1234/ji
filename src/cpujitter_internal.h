#ifndef CPUJITTER_INTERNAL_H
#define CPUJITTER_INTERNAL_H

#include <stddef.h>

#include "cpujitter/cpujitter.h"

typedef struct profile_entry {
    char id[64];
    char os[32];
    char arch[32];
    char cpu_vendor[32];
    int osr;
    int mem_blocks;
    int mem_block_size;
    int smoke_bytes;
} profile_entry;

struct cpujitter_ctx {
    cpujitter_platform_info platform;
    cpujitter_runtime_config runtime;
    char profiles_index_path[256];
    char cache_path[256];
    int backend_initialized;
};

void cpujitter_detect_platform(cpujitter_platform_info *out_info);

cpujitter_err cpujitter_profiles_load(const char *path,
                                      profile_entry *out_entries,
                                      size_t out_cap,
                                      size_t *out_count);

cpujitter_err cpujitter_profiles_find_by_id(const profile_entry *entries,
                                            size_t count,
                                            const char *id,
                                            profile_entry *out_match);

cpujitter_err cpujitter_profiles_select_best(const profile_entry *entries,
                                             size_t count,
                                             const cpujitter_platform_info *platform,
                                             profile_entry *out_match);

cpujitter_err cpujitter_cache_load(const char *path, profile_entry *out_entry);
cpujitter_err cpujitter_cache_save(const char *path,
                                   const profile_entry *entry,
                                   const cpujitter_platform_info *platform);

cpujitter_err cpujitter_backend_init(cpujitter_ctx *ctx, const profile_entry *profile);
cpujitter_err cpujitter_backend_get_bytes(cpujitter_ctx *ctx, unsigned char *out, size_t len);
void cpujitter_backend_shutdown(cpujitter_ctx *ctx);

cpujitter_err cpujitter_run_smoke_test(cpujitter_ctx *ctx, int smoke_bytes);

cpujitter_err cpujitter_apply_profile(cpujitter_ctx *ctx,
                                      const profile_entry *entry,
                                      int source);

cpujitter_err cpujitter_try_recalibrate(cpujitter_ctx *ctx,
                                        const profile_entry *base,
                                        profile_entry *out_tuned);

#endif
