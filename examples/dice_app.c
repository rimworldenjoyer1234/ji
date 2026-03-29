#include "cpujitter/cpujitter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static double estimate_entropy_per_bit(cpujitter_ctx *ctx) {
    unsigned char sample[4096];
    size_t i;
    int ones = 0;
    int total_bits = (int)(sizeof(sample) * 8U);
    double p1;
    double p0;

    if (cpujitter_get_bytes(ctx, sample, sizeof(sample)) != CPUJITTER_OK) {
        return -1.0;
    }

    for (i = 0; i < sizeof(sample); i++) {
        unsigned char b = sample[i];
        int bit;
        for (bit = 0; bit < 8; bit++) {
            ones += (b >> bit) & 1;
        }
    }

    p1 = (double)ones / (double)total_bits;
    p0 = 1.0 - p1;
    if (p1 <= 0.0 || p0 <= 0.0) {
        return 0.0;
    }

    return -(p1 * (log(p1) / log(2.0)) + p0 * (log(p0) / log(2.0)));
}

static int write_probe_random_file(cpujitter_ctx *ctx, const char *dump_path, size_t nbytes) {
    unsigned char *buf;
    FILE *f;

    if (!ctx || !dump_path || nbytes == 0) {
        return -1;
    }

    buf = (unsigned char *)malloc(nbytes);
    if (!buf) {
        return -1;
    }

    if (cpujitter_get_bytes(ctx, buf, nbytes) != CPUJITTER_OK) {
        free(buf);
        return -1;
    }

    f = fopen(dump_path, "wb");
    if (!f) {
        free(buf);
        return -1;
    }

    if (fwrite(buf, 1U, nbytes, f) != nbytes) {
        fclose(f);
        free(buf);
        return -1;
    }

    fclose(f);
    free(buf);
    return 0;
}

static int write_probe_meta_file(const char *meta_path,
                                 const cpujitter_runtime_config *cfg,
                                 double entropy_per_bit) {
    FILE *f;

    if (!meta_path || !cfg) {
        return -1;
    }

    f = fopen(meta_path, "wb");
    if (!f) {
        return -1;
    }

    if (fprintf(f,
                "profile_origin=%s\n"
                "profile_id=%s\n"
                "osr=%d\n"
                "flags=%u\n"
                "hashloop=%d\n"
                "max_memsize=%d\n"
                "smoke_bytes=%d\n"
                "entropy_per_bit=%.6f\n",
                source_label(cfg->source),
                cfg->profile_id,
                cfg->osr,
                cfg->flags,
                cfg->hashloop,
                cfg->max_memsize_kb,
                cfg->smoke_bytes,
                entropy_per_bit) < 0) {
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    cpujitter_ctx *ctx = NULL;
    cpujitter_runtime_config cfg;
    cpujitter_err err;
    char status[512];
    size_t written = 0;
    double entropy_per_bit;
    const char *dump_path = NULL;
    const char *meta_path = NULL;
    int probe_only = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--probe") == 0) {
            probe_only = 1;
        } else if (strcmp(argv[i], "--dump") == 0 && (i + 1) < argc) {
            dump_path = argv[++i];
        } else if (strcmp(argv[i], "--meta") == 0 && (i + 1) < argc) {
            meta_path = argv[++i];
        }
    }

    err = cpujitter_init(&ctx, "profiles/index.json", "cache/local_profile.json");
    if (err != CPUJITTER_OK) {
        fprintf(stderr, "cpujitter_init failed: %s\n", cpujitter_strerror(err));
        return 1;
    }

    if (cpujitter_get_runtime_config(ctx, &cfg) == CPUJITTER_OK) {
        printf("profile origin: %s\n", source_label(cfg.source));
        printf("selected vars: profile=%s osr=%d flags=%u hashloop=%d max_memsize=%d smoke_bytes=%d\n",
               cfg.profile_id,
               cfg.osr,
               cfg.flags,
               cfg.hashloop,
               cfg.max_memsize_kb,
               cfg.smoke_bytes);
    }

    entropy_per_bit = estimate_entropy_per_bit(ctx);
    if (entropy_per_bit >= 0.0) {
        printf("estimated entropy per bit: %.6f\n", entropy_per_bit);
    }

    if (cpujitter_get_status_json(ctx, status, sizeof(status), &written) == CPUJITTER_OK && written > 0) {
        printf("status: %.160s\n", status);
    }

    if (probe_only && dump_path) {
        if (write_probe_random_file(ctx, dump_path, 4096U) == 0) {
            printf("probe random file: %s\n", dump_path);
        } else {
            fprintf(stderr, "failed writing probe random file: %s\n", dump_path);
        }
    }

    if (probe_only && meta_path) {
        if (write_probe_meta_file(meta_path, &cfg, entropy_per_bit) == 0) {
            printf("probe metadata file: %s\n", meta_path);
        } else {
            fprintf(stderr, "failed writing probe metadata file: %s\n", meta_path);
        }
    }

    if (!probe_only) {
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
    }

    cpujitter_shutdown(ctx);
    return 0;
}
