#include "cpujitter/cpujitter.h"

#include <stdio.h>
#include <string.h>

#ifndef CPUJITTER_SOURCE_DIR
#define CPUJITTER_SOURCE_DIR "."
#endif

static void paths(char *profiles, size_t profiles_sz, char *cache, size_t cache_sz) {
    snprintf(profiles, profiles_sz, "%s/profiles/index.json", CPUJITTER_SOURCE_DIR);
    snprintf(cache, cache_sz, "test_local_profile.json");
}

static int replace_in_file(const char *path, const char *needle, const char *replacement) {
    FILE *f;
    char buf[8192];
    char out[8192];
    char *pos;
    size_t n;

    f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    n = fread(buf, 1U, sizeof(buf) - 1U, f);
    fclose(f);
    if (n == 0) {
        return -1;
    }
    buf[n] = '\0';

    pos = strstr(buf, needle);
    if (!pos) {
        return -1;
    }

    *pos = '\0';
    snprintf(out, sizeof(out), "%s%s%s", buf, replacement, pos + strlen(needle));

    f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    if (fputs(out, f) < 0) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static int test_init_and_bytes(void) {
    cpujitter_ctx *ctx = NULL;
    cpujitter_err err;
    unsigned char b[16];
    cpujitter_runtime_config cfg;
    char profiles[512];
    char cache[256];

    paths(profiles, sizeof(profiles), cache, sizeof(cache));
    err = cpujitter_init(&ctx, profiles, cache);
    if (err != CPUJITTER_OK) {
        fprintf(stderr, "init failed: %s\n", cpujitter_strerror(err));
        return 1;
    }

    err = cpujitter_get_bytes(ctx, b, sizeof(b));
    if (err != CPUJITTER_OK) {
        fprintf(stderr, "get_bytes failed: %s\n", cpujitter_strerror(err));
        cpujitter_shutdown(ctx);
        return 1;
    }

    err = cpujitter_get_runtime_config(ctx, &cfg);
    if (err != CPUJITTER_OK || cfg.profile_id[0] == '\0') {
        fprintf(stderr, "get_runtime_config failed\n");
        cpujitter_shutdown(ctx);
        return 1;
    }

    cpujitter_shutdown(ctx);
    return 0;
}

static int test_cache_valid_reuse(void) {
    cpujitter_ctx *ctx = NULL;
    cpujitter_err err;
    cpujitter_runtime_config cfg;
    char profiles[512];
    char cache[256];

    paths(profiles, sizeof(profiles), cache, sizeof(cache));
    (void)remove(cache);

    err = cpujitter_init(&ctx, profiles, cache);
    if (err != CPUJITTER_OK) {
        fprintf(stderr, "first init failed: %s\n", cpujitter_strerror(err));
        return 1;
    }
    cpujitter_shutdown(ctx);

    ctx = NULL;
    err = cpujitter_init(&ctx, profiles, cache);
    if (err != CPUJITTER_OK) {
        fprintf(stderr, "second init failed: %s\n", cpujitter_strerror(err));
        return 1;
    }
    err = cpujitter_get_runtime_config(ctx, &cfg);
    if (err != CPUJITTER_OK || cfg.source != 1) {
        fprintf(stderr, "expected cache source, got: %d\n", cfg.source);
        cpujitter_shutdown(ctx);
        return 1;
    }

    cpujitter_shutdown(ctx);
    return 0;
}

static int test_cache_mismatch_rejected(void) {
    cpujitter_ctx *ctx = NULL;
    cpujitter_err err;
    cpujitter_runtime_config cfg;
    char profiles[512];
    char cache[256];

    paths(profiles, sizeof(profiles), cache, sizeof(cache));
    (void)remove(cache);

    err = cpujitter_init(&ctx, profiles, cache);
    if (err != CPUJITTER_OK) {
        return 1;
    }
    cpujitter_shutdown(ctx);

    if (replace_in_file(cache, "\"cpu_vendor\": \"generic-x86\"", "\"cpu_vendor\": \"wrong-vendor\"") != 0) {
        fprintf(stderr, "failed to mutate cache\n");
        return 1;
    }

    ctx = NULL;
    err = cpujitter_init(&ctx, profiles, cache);
    if (err != CPUJITTER_OK) {
        fprintf(stderr, "init failed unexpectedly: %s\n", cpujitter_strerror(err));
        return 1;
    }
    err = cpujitter_get_runtime_config(ctx, &cfg);
    if (err != CPUJITTER_OK || cfg.source == 1) {
        fprintf(stderr, "mismatched cache should not be used\n");
        cpujitter_shutdown(ctx);
        return 1;
    }
    cpujitter_shutdown(ctx);
    return 0;
}

static int test_cache_corrupted_rejected(void) {
    cpujitter_ctx *ctx = NULL;
    cpujitter_err err;
    cpujitter_runtime_config cfg;
    char profiles[512];
    char cache[256];
    FILE *f;

    paths(profiles, sizeof(profiles), cache, sizeof(cache));
    f = fopen(cache, "wb");
    if (!f) {
        return 1;
    }
    fputs("{ bad json", f);
    fclose(f);

    err = cpujitter_init(&ctx, profiles, cache);
    if (err != CPUJITTER_OK) {
        fprintf(stderr, "init should recover from corrupt cache: %s\n", cpujitter_strerror(err));
        return 1;
    }
    err = cpujitter_get_runtime_config(ctx, &cfg);
    if (err != CPUJITTER_OK || cfg.source == 1) {
        fprintf(stderr, "corrupted cache should not be used\n");
        cpujitter_shutdown(ctx);
        return 1;
    }
    cpujitter_shutdown(ctx);
    return 0;
}

static int test_die_roll_range(void) {
    cpujitter_ctx *ctx = NULL;
    cpujitter_err err;
    int i;
    char profiles[512];
    char cache[256];

    paths(profiles, sizeof(profiles), cache, sizeof(cache));
    err = cpujitter_init(&ctx, profiles, cache);
    if (err != CPUJITTER_OK) {
        fprintf(stderr, "init failed: %s\n", cpujitter_strerror(err));
        return 1;
    }

    for (i = 0; i < 1000; i++) {
        unsigned char die = 0;
        err = cpujitter_roll_die(ctx, &die);
        if (err != CPUJITTER_OK || die < 1 || die > 6) {
            fprintf(stderr, "die roll out of range\n");
            cpujitter_shutdown(ctx);
            return 1;
        }
    }

    cpujitter_shutdown(ctx);
    return 0;
}

static int test_status_json(void) {
    cpujitter_ctx *ctx = NULL;
    cpujitter_err err;
    char status[1024];
    size_t written = 0;
    char profiles[512];
    char cache[256];

    paths(profiles, sizeof(profiles), cache, sizeof(cache));
    err = cpujitter_init(&ctx, profiles, cache);
    if (err != CPUJITTER_OK) {
        return 1;
    }

    err = cpujitter_get_status_json(ctx, status, sizeof(status), &written);
    if (err != CPUJITTER_OK || written == 0 || strstr(status, "profile_id") == NULL ||
        strstr(status, "match_explanation") == NULL) {
        fprintf(stderr, "status json failed\n");
        cpujitter_shutdown(ctx);
        return 1;
    }

    cpujitter_shutdown(ctx);
    return 0;
}

static int test_invalid_args(void) {
    cpujitter_err err;
    err = cpujitter_init(NULL, "profiles/index.json", "cache/local.json");
    if (err != CPUJITTER_ERR_INVALID_ARG) {
        fprintf(stderr, "expected invalid arg from init\n");
        return 1;
    }
    return 0;
}

int main(void) {
    int failed = 0;
    failed |= test_init_and_bytes();
    failed |= test_cache_valid_reuse();
    failed |= test_cache_mismatch_rejected();
    failed |= test_cache_corrupted_rejected();
    failed |= test_die_roll_range();
    failed |= test_status_json();
    failed |= test_invalid_args();

    if (failed) {
        fprintf(stderr, "Tests failed\n");
        return 1;
    }

    printf("All tests passed\n");
    return 0;
}
