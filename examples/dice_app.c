#include "cpujitter/cpujitter.h"

#include <stdio.h>

int main(void) {
    cpujitter_ctx *ctx = NULL;
    cpujitter_err err;
    int i;

    err = cpujitter_init(&ctx, "profiles/index.json", "cache/local_profile.json");
    if (err != CPUJITTER_OK) {
        fprintf(stderr, "cpujitter_init failed: %s\n", cpujitter_strerror(err));
        return 1;
    }

    for (i = 0; i < 10; i++) {
        unsigned char die;
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
