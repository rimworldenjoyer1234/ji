#include "cpujitter_internal.h"

#include <stdio.h>
#include <string.h>

#if defined(__has_include)
#if __has_include("jitterentropy.h")
#include "jitterentropy.h"
#define CPUJITTER_HAS_JENT 1
#endif
#endif

#ifndef CPUJITTER_HAS_JENT
#define CPUJITTER_HAS_JENT 0
#endif

#if CPUJITTER_HAS_JENT
cpujitter_err cpujitter_backend_init(cpujitter_ctx *ctx, const profile_entry *profile) {
    int r;
    if (!ctx || !profile) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    ctx->backend_last_error = 0;
    ctx->backend_init_success = 0;
    ctx->backend_alloc_success = 0;

    r = jent_entropy_init_ex((unsigned int)profile->osr, (unsigned int)profile->flags);
    if (r != 0) {
        ctx->backend_last_error = r;
        return CPUJITTER_ERR_ENTROPY_BACKEND;
    }
    ctx->backend_init_success = 1;

    ctx->backend_collector = jent_entropy_collector_alloc((unsigned int)profile->osr,
                                                          (unsigned int)profile->flags);
    if (!ctx->backend_collector) {
        ctx->backend_last_error = -2;
        return CPUJITTER_ERR_ENTROPY_BACKEND;
    }

    ctx->backend_alloc_success = 1;
    ctx->backend_initialized = 1;
    return CPUJITTER_OK;
}

cpujitter_err cpujitter_backend_get_bytes(cpujitter_ctx *ctx, unsigned char *out, size_t len) {
    ssize_t r;
    if (!ctx || !out) {
        return CPUJITTER_ERR_INVALID_ARG;
    }
    if (!ctx->backend_initialized || !ctx->backend_collector) {
        return CPUJITTER_ERR_STATE;
    }
    if (len == 0) {
        return CPUJITTER_OK;
    }

    r = jent_read_entropy((struct rand_data *)ctx->backend_collector, out, len);
    if (r < 0 || (size_t)r != len) {
        ctx->backend_last_error = (int)r;
        return CPUJITTER_ERR_ENTROPY_BACKEND;
    }
    return CPUJITTER_OK;
}

void cpujitter_backend_shutdown(cpujitter_ctx *ctx) {
    if (!ctx) {
        return;
    }
    if (ctx->backend_collector) {
        jent_entropy_collector_free((struct rand_data *)ctx->backend_collector);
    }
    ctx->backend_collector = NULL;
    ctx->backend_initialized = 0;
}

#else
#if !defined(CPUJITTER_ENABLE_MOCK_BACKEND) || CPUJITTER_ENABLE_MOCK_BACKEND != 1
#error "jitterentropy.h not found and CPUJITTER_ENABLE_MOCK_BACKEND is OFF. Provide vendored jitterentropy or enable explicit non-production mock backend."
#endif

#include <stdlib.h>
#include <time.h>

/* NON-PRODUCTION BACKEND: enabled only with CPUJITTER_ENABLE_MOCK_BACKEND=1 */
cpujitter_err cpujitter_backend_init(cpujitter_ctx *ctx, const profile_entry *profile) {
    (void)profile;
    if (!ctx) {
        return CPUJITTER_ERR_INVALID_ARG;
    }
    srand((unsigned int)time(NULL));
    ctx->backend_init_success = 1;
    ctx->backend_alloc_success = 1;
    ctx->backend_initialized = 1;
    return CPUJITTER_OK;
}

cpujitter_err cpujitter_backend_get_bytes(cpujitter_ctx *ctx, unsigned char *out, size_t len) {
    size_t i;
    if (!ctx || !out) {
        return CPUJITTER_ERR_INVALID_ARG;
    }
    if (!ctx->backend_initialized) {
        return CPUJITTER_ERR_STATE;
    }
    for (i = 0; i < len; i++) {
        out[i] = (unsigned char)(rand() & 0xFF);
    }
    return CPUJITTER_OK;
}

void cpujitter_backend_shutdown(cpujitter_ctx *ctx) {
    if (ctx) {
        ctx->backend_initialized = 0;
    }
}
#endif
