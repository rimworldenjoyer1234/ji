#ifndef CPUJITTER_CPUJITTER_H
#define CPUJITTER_CPUJITTER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cpujitter_ctx cpujitter_ctx;

typedef enum cpujitter_err {
    CPUJITTER_OK = 0,
    CPUJITTER_ERR_INVALID_ARG = 1,
    CPUJITTER_ERR_IO = 2,
    CPUJITTER_ERR_PARSE = 3,
    CPUJITTER_ERR_NO_PROFILE = 4,
    CPUJITTER_ERR_SMOKE_TEST = 5,
    CPUJITTER_ERR_RECALIBRATE = 6,
    CPUJITTER_ERR_ENTROPY_BACKEND = 7,
    CPUJITTER_ERR_STATE = 8,
    CPUJITTER_ERR_BUFFER_TOO_SMALL = 9
} cpujitter_err;

typedef struct cpujitter_platform_info {
    char os[32];
    char arch[32];
    char cpu_vendor[32];
    char cpu_model[64];
    char virtualization[16];
    int logical_cpu_count;
} cpujitter_platform_info;

typedef struct cpujitter_runtime_config {
    char profile_id[64];
    int source; /* 1=cache, 2=index, 3=recalibrated */
    int osr;
    unsigned int flags;
    int disable_memory_access;
    int force_internal_timer;
    int disable_internal_timer;
    int force_fips;
    int ntg1;
    int cache_all;
    int max_memsize_kb;
    int hashloop;
    int smoke_bytes;
} cpujitter_runtime_config;

cpujitter_err cpujitter_init(cpujitter_ctx **out_ctx,
                             const char *profiles_index_path,
                             const char *cache_path);
cpujitter_err cpujitter_init_with_profile(cpujitter_ctx **out_ctx,
                                          const char *profiles_index_path,
                                          const char *cache_path,
                                          const char *profile_id);
cpujitter_err cpujitter_recalibrate(cpujitter_ctx *ctx);
cpujitter_err cpujitter_get_bytes(cpujitter_ctx *ctx, uint8_t *out, size_t len);
cpujitter_err cpujitter_roll_die(cpujitter_ctx *ctx, uint8_t *die_value);
cpujitter_err cpujitter_get_platform_info(cpujitter_ctx *ctx,
                                          cpujitter_platform_info *out_info);
cpujitter_err cpujitter_get_runtime_config(cpujitter_ctx *ctx,
                                           cpujitter_runtime_config *out_cfg);
cpujitter_err cpujitter_get_status_json(cpujitter_ctx *ctx,
                                        char *out_buf,
                                        size_t out_buf_len,
                                        size_t *out_written);
void cpujitter_shutdown(cpujitter_ctx *ctx);
const char *cpujitter_strerror(cpujitter_err err);

#ifdef __cplusplus
}
#endif

#endif
