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
    char status[512];
    size_t written = 0;
    char profiles[512];
    char cache[256];

    paths(profiles, sizeof(profiles), cache, sizeof(cache));
    err = cpujitter_init(&ctx, profiles, cache);
    if (err != CPUJITTER_OK) {
        return 1;
    }

    err = cpujitter_get_status_json(ctx, status, sizeof(status), &written);
    if (err != CPUJITTER_OK || written == 0 || strstr(status, "profile_id") == NULL) {
        fprintf(stderr, "status json failed\n");
        cpujitter_shutdown(ctx);
        return 1;
    }

    cpujitter_shutdown(ctx);
    return 0;
}

int main(void) {
    int failed = 0;
    failed |= test_init_and_bytes();
    failed |= test_die_roll_range();
    failed |= test_status_json();

    if (failed) {
        fprintf(stderr, "Tests failed\n");
        return 1;
    }

    printf("All tests passed\n");
    return 0;
}
