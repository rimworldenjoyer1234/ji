#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "jitterentropy.h"

#ifndef AUTOJITTER_SAMPLE_SIZE
#define AUTOJITTER_SAMPLE_SIZE (1024u * 1024u)
#endif

#ifndef AUTOJITTER_SMOKE_SIZE
#define AUTOJITTER_SMOKE_SIZE (64u * 1024u)
#endif

struct candidate {
    const char *name;
    unsigned int osr;
    unsigned int flags;
};

struct result {
    const struct candidate *candidate;
    double seconds;
    char status[2048];
};

static const struct candidate candidates[] = {
    {"native_osr1_l1_h1", 1, JENT_HASHLOOP_1 | JENT_MAX_MEMSIZE_32kB},
    {"native_osr2_l1_h1", 2, JENT_HASHLOOP_1 | JENT_MAX_MEMSIZE_32kB},
    {"native_osr3_l1_h1", 3, JENT_HASHLOOP_1 | JENT_MAX_MEMSIZE_32kB},
    {"native_osr4_l1_h1", 4, JENT_HASHLOOP_1 | JENT_MAX_MEMSIZE_32kB},
    {"native_osr2_l2_h2", 2, JENT_HASHLOOP_2 | JENT_MAX_MEMSIZE_64kB},
    {"native_osr3_l2_h2", 3, JENT_HASHLOOP_2 | JENT_MAX_MEMSIZE_64kB},
    {"native_osr4_l3_h4", 4, JENT_HASHLOOP_4 | JENT_MAX_MEMSIZE_256kB},
    {"native_osr4_cacheall_h2", 4, JENT_CACHE_ALL | JENT_HASHLOOP_2 | JENT_MAX_MEMSIZE_256kB},
    {"notimer_osr3_l2_h2", 3, JENT_FORCE_INTERNAL_TIMER | JENT_HASHLOOP_2 | JENT_MAX_MEMSIZE_64kB},
    {"nomem_osr3_h2", 3, JENT_DISABLE_MEMORY_ACCESS | JENT_HASHLOOP_2},
};

static double monotonic_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode))
            return 0;
        errno = ENOTDIR;
        return -1;
    }
    return mkdir(path, 0755);
}

static int generate_bytes(struct rand_data *ec, FILE *fp, size_t count)
{
    unsigned char buf[4096];
    size_t remaining = count;

    while (remaining > 0) {
        size_t chunk = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        ssize_t got = jent_read_entropy(ec, (char *)buf, chunk);
        if (got != (ssize_t)chunk)
            return -1;
        if (fp && fwrite(buf, 1, chunk, fp) != chunk)
            return -1;
        remaining -= chunk;
    }
    return 0;
}

static int evaluate_candidate(const struct candidate *cand, struct result *res)
{
    struct rand_data *ec = NULL;
    double t0, t1;
    int rc;

    memset(res, 0, sizeof(*res));
    res->candidate = cand;

    rc = jent_entropy_init_ex(cand->osr, cand->flags);
    if (rc != 0) {
        snprintf(res->status, sizeof(res->status), "init_failed:%d", rc);
        return -1;
    }

    ec = jent_entropy_collector_alloc(cand->osr, cand->flags);
    if (!ec) {
        snprintf(res->status, sizeof(res->status), "alloc_failed");
        return -1;
    }

    if (jent_status(ec, res->status, sizeof(res->status)) != 0)
        snprintf(res->status, sizeof(res->status), "status_unavailable");

    t0 = monotonic_seconds();
    rc = generate_bytes(ec, NULL, AUTOJITTER_SMOKE_SIZE);
    t1 = monotonic_seconds();
    res->seconds = t1 - t0;

    jent_entropy_collector_free(ec);
    return rc;
}

static int save_profile(const char *path, const struct result *best, const char *random_file)
{
    FILE *fp = fopen(path, "w");
    if (!fp)
        return -1;

    fprintf(fp, "NAME=%s\n", best->candidate->name);
    fprintf(fp, "OSR=%u\n", best->candidate->osr);
    fprintf(fp, "FLAGS=0x%x\n", best->candidate->flags);
    fprintf(fp, "SMOKE_SECONDS=%.9f\n", best->seconds);
    fprintf(fp, "RANDOM_FILE=%s\n", random_file);
    fprintf(fp, "STATUS_JSON=%s\n", best->status);
    fclose(fp);
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [--out-dir DIR] [--sample-bytes N]\n"
            "Default output dir: .\n",
            prog);
}

int main(int argc, char **argv)
{
    const char *out_dir = ".";
    size_t sample_bytes = AUTOJITTER_SAMPLE_SIZE;
    char profile_path[PATH_MAX];
    char random_path[PATH_MAX];
    char report_path[PATH_MAX];
    FILE *sample_fp = NULL;
    struct rand_data *ec = NULL;
    struct result best;
    int best_idx = -1;
    size_t i;
    FILE *report_fp = NULL;

    for (i = 1; i < (size_t)argc; ++i) {
        if (strcmp(argv[i], "--out-dir") == 0 && i + 1 < (size_t)argc) {
            out_dir = argv[++i];
        } else if (strcmp(argv[i], "--sample-bytes") == 0 && i + 1 < (size_t)argc) {
            sample_bytes = (size_t)strtoull(argv[++i], NULL, 10);
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (ensure_dir(out_dir) != 0 && errno != EEXIST) {
        fprintf(stderr, "Cannot create output directory '%s': %s\n", out_dir, strerror(errno));
        return 1;
    }

    snprintf(profile_path, sizeof(profile_path), "%s/autojitter_profile.env", out_dir);
    snprintf(random_path, sizeof(random_path), "%s/autojitter_random.bin", out_dir);
    snprintf(report_path, sizeof(report_path), "%s/autojitter_report.txt", out_dir);

    report_fp = fopen(report_path, "w");
    if (!report_fp) {
        fprintf(stderr, "Cannot open report '%s': %s\n", report_path, strerror(errno));
        return 1;
    }

    fprintf(report_fp, "Autojitter candidate sweep\n");
    fprintf(report_fp, "sample_bytes=%zu smoke_bytes=%u\n\n", sample_bytes, AUTOJITTER_SMOKE_SIZE);

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        struct result current;
        int ok = (evaluate_candidate(&candidates[i], &current) == 0);

        fprintf(report_fp,
                "candidate=%s osr=%u flags=0x%x ok=%d seconds=%.9f status=%s\n",
                candidates[i].name,
                candidates[i].osr,
                candidates[i].flags,
                ok,
                current.seconds,
                current.status[0] ? current.status : "n/a");

        if (!ok)
            continue;

        if (best_idx < 0 || current.seconds < best.seconds) {
            best = current;
            best_idx = (int)i;
        }
    }

    fclose(report_fp);

    if (best_idx < 0) {
        fprintf(stderr, "No candidate passed the smoke test. See %s\n", report_path);
        return 1;
    }

    if (jent_entropy_init_ex(best.candidate->osr, best.candidate->flags) != 0) {
        fprintf(stderr, "Re-init failed for winning candidate\n");
        return 1;
    }

    ec = jent_entropy_collector_alloc(best.candidate->osr, best.candidate->flags);
    if (!ec) {
        fprintf(stderr, "Collector allocation failed for winning candidate\n");
        return 1;
    }

    sample_fp = fopen(random_path, "wb");
    if (!sample_fp) {
        fprintf(stderr, "Cannot open random sample '%s': %s\n", random_path, strerror(errno));
        jent_entropy_collector_free(ec);
        return 1;
    }

    if (generate_bytes(ec, sample_fp, sample_bytes) != 0) {
        fprintf(stderr, "Failed to generate validation sample\n");
        fclose(sample_fp);
        jent_entropy_collector_free(ec);
        return 1;
    }

    fclose(sample_fp);
    jent_entropy_collector_free(ec);

    if (save_profile(profile_path, &best, random_path) != 0) {
        fprintf(stderr, "Cannot save profile '%s': %s\n", profile_path, strerror(errno));
        return 1;
    }

    printf("Best candidate: %s\n", best.candidate->name);
    printf("OSR=%u FLAGS=0x%x\n", best.candidate->osr, best.candidate->flags);
    printf("Profile: %s\n", profile_path);
    printf("Sample : %s\n", random_path);
    printf("Report : %s\n", report_path);
    return 0;
}
