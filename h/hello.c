#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jitterentropy.h"

#ifndef AUTOJITTER_PROFILE_PATH
#define AUTOJITTER_PROFILE_PATH "./autojitter_profile.env"
#endif

struct profile_cfg {
    unsigned int osr;
    unsigned int flags;
    char sample_path[1024];
};

static int load_profile(const char *path, struct profile_cfg *cfg)
{
    FILE *fp;
    char line[2048];

    if (!cfg)
        return -1;

    cfg->osr = 0;
    cfg->flags = 0;
    cfg->sample_path[0] = '\0';

    fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open profile '%s': %s\n", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *eq = strchr(line, '=');
        if (!eq)
            continue;

        *eq = '\0';
        char *key = line;
        char *value = eq + 1;
        size_t n = strlen(value);
        while (n > 0 && (value[n - 1] == '\n' || value[n - 1] == '\r')) {
            value[n - 1] = '\0';
            --n;
        }

        if (strcmp(key, "OSR") == 0) {
            cfg->osr = (unsigned int)strtoul(value, NULL, 10);
        } else if (strcmp(key, "FLAGS") == 0) {
            cfg->flags = (unsigned int)strtoul(value, NULL, 0);
        } else if (strcmp(key, "RANDOM_FILE") == 0) {
            snprintf(cfg->sample_path, sizeof(cfg->sample_path), "%s", value);
        }
    }

    fclose(fp);
    return 0;
}

static int bounded_dice_roll(struct rand_data *ec)
{
    unsigned char byte = 0;
    ssize_t ret;

    do {
        ret = jent_read_entropy(ec, (char *)&byte, sizeof(byte));
        if (ret != (ssize_t)sizeof(byte))
            return -1;
    } while (byte >= 252);

    return (byte % 6) + 1;
}

int main(int argc, char **argv)
{
    const char *profile_path = AUTOJITTER_PROFILE_PATH;
    struct profile_cfg cfg;
    struct rand_data *ec = NULL;
    char status[2048];
    int roll;

    if (argc > 1)
        profile_path = argv[1];

    if (load_profile(profile_path, &cfg) != 0)
        return 1;

    if (jent_entropy_init_ex(cfg.osr, cfg.flags) != 0) {
        fprintf(stderr, "jent_entropy_init_ex failed for osr=%u flags=0x%x\n",
                cfg.osr, cfg.flags);
        return 1;
    }

    ec = jent_entropy_collector_alloc(cfg.osr, cfg.flags);
    if (!ec) {
        fprintf(stderr, "jent_entropy_collector_alloc failed\n");
        return 1;
    }

    if (jent_status(ec, status, sizeof(status)) == 0)
        printf("Jitter status: %s\n", status);

    roll = bounded_dice_roll(ec);
    if (roll < 0) {
        fprintf(stderr, "Failed to generate a dice roll\n");
        jent_entropy_collector_free(ec);
        return 1;
    }

    printf("Using profile: %s\n", profile_path);
    printf("OSR=%u FLAGS=0x%x\n", cfg.osr, cfg.flags);
    if (cfg.sample_path[0] != '\0')
        printf("Validation sample: %s\n", cfg.sample_path);
    printf("Dice roll: %d\n", roll);

    jent_entropy_collector_free(ec);
    return 0;
}
