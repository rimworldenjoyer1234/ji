#include "cpujitter_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

/*
 * TODO(jitterentropy-integration): replace this fallback backend with calls into
 * Stephan Mueller's jitterentropy-library through external/jitterentropy.
 */
static int fill_from_urandom(unsigned char *out, size_t len) {
#if defined(_WIN32)
    (void)out;
    (void)len;
    return -1;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) {
        return -1;
    }
    if (fread(out, 1U, len, f) != len) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
#endif
}

static void fill_from_prng(unsigned char *out, size_t len) {
    size_t i;
    static int seeded = 0;
    if (!seeded) {
        seeded = 1;
        srand((unsigned int)time(NULL));
    }
    for (i = 0; i < len; i++) {
        out[i] = (unsigned char)(rand() & 0xFF);
    }
}

cpujitter_err cpujitter_backend_init(cpujitter_ctx *ctx, const profile_entry *profile) {
    (void)profile;
    if (!ctx) {
        return CPUJITTER_ERR_INVALID_ARG;
    }
    ctx->backend_initialized = 1;
    return CPUJITTER_OK;
}

cpujitter_err cpujitter_backend_get_bytes(cpujitter_ctx *ctx, unsigned char *out, size_t len) {
    if (!ctx || !out) {
        return CPUJITTER_ERR_INVALID_ARG;
    }
    if (!ctx->backend_initialized) {
        return CPUJITTER_ERR_STATE;
    }
    if (len == 0) {
        return CPUJITTER_OK;
    }

    if (fill_from_urandom(out, len) != 0) {
        fill_from_prng(out, len);
    }

    return CPUJITTER_OK;
}

void cpujitter_backend_shutdown(cpujitter_ctx *ctx) {
    if (ctx) {
        ctx->backend_initialized = 0;
    }
}
