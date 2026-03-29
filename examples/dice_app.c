#include "cpujitter/cpujitter.h"

#include <stdio.h>

static const char *source_label(int source) {
    switch (source) {
    case 1:
        return "cache";
    case 2:
        return "bundled-profile";
    case 3:
        return "recalibrated";
    default:
        return "unknown";
    }
}

int main(void) {
    cpujitter_ctx *ctx = NULL;
    cpujitter_runtime_config cfg;
    cpujitter_err err;
    char status[512];
    size_t written = 0;
    int i;

    err = cpujitter_init(&ctx, "profiles/index.json", "cache/local_profile.json");
    if (err != CPUJITTER_OK) {
        fprintf(stderr, "cpujitter_init failed: %s\n", cpujitter_strerror(err));
        return 1;
    }

    if (cpujitter_get_runtime_config(ctx, &cfg) == CPUJITTER_OK) {
        printf("profile origin: %s\n", source_label(cfg.source));
    }

    if (cpujitter_get_status_json(ctx, status, sizeof(status), &written) == CPUJITTER_OK && written > 0) {
        printf("status: %.160s\n", status);
    }

    printf("rolling fair d6 values (library uses rejection sampling):\n");
    for (i = 0; i < 10; i++) {
        unsigned char die = 0;
        err = cpujitter_roll_die(ctx, &die);
        if (err != CPUJITTER_OK) {
            fprintf(stderr, "cpujitter_roll_die failed: %s\n", cpujitter_strerror(err));
            cpujitter_shutdown(ctx);
            return 1;
        }
        printf("roll %d: %u\n", i + 1, (unsigned int)die);
    }

    cpujitter_shutdown(ctx);
    return 0;
}
