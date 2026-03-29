#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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

#ifndef AUTOJITTER_CHUNK_SIZE
#define AUTOJITTER_CHUNK_SIZE 4096u
#endif

struct candidate {
    const char *name;
    unsigned int osr;
    unsigned int flags;
};

struct result {
    const struct candidate *candidate;
    int ok;
    double seconds;
    char status[2048];
};

static const struct candidate candidates[] = {
    {"native_osr1_mem32k_h1", 1, JENT_HASHLOOP_1 | JENT_MAX_MEMSIZE_32kB},
    {"native_osr2_mem32k_h1", 2, JENT_HASHLOOP_1 | JENT_MAX_MEMSIZE_32kB},
    {"native_osr3_mem32k_h1", 3, JENT_HASHLOOP_1 | JENT_MAX_MEMSIZE_32kB},
    {"native_osr4_mem32k_h1", 4, JENT_HASHLOOP_1 | JENT_MAX_MEMSIZE_32kB},
    {"native_osr2_mem64k_h2", 2, JENT_HASHLOOP_2 | JENT_MAX_MEMSIZE_64kB},
    {"native_osr3_mem64k_h2", 3, JENT_HASHLOOP_2 | JENT_MAX_MEMSIZE_64kB},
    {"native_osr4_mem256k_h4", 4, JENT_HASHLOOP_4 | JENT_MAX_MEMSIZE_256kB},
    {"native_osr4_cacheall_h2", 4, JENT_CACHE_ALL | JENT_HASHLOOP_2 | JENT_MAX_MEMSIZE_256kB},
    {"internal_timer_osr3_h2", 3, JENT_FORCE_INTERNAL_TIMER | JENT_HASHLOOP_2 | JENT_MAX_MEMSIZE_64kB},
    {"no_mem_osr3_h2", 3, JENT_DISABLE_MEMORY_ACCESS | JENT_HASHLOOP_2}
};

static double monotonic_seconds(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;

    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int ensure_dir(const char *path)
{
    struct stat st;

    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }

    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode))
            return 0;
        errno = ENOTDIR;
        return -1;
    }

    if (mkdir(path, 0755) == 0)
        return 0;

    if (errno == EEXIST)
        return 0;

    return -1;
}

static void show_progress(size_t current, size_t total, const char *label)
{
    const int bar_width = 40;
    int filled = 0;
    int percent = 0;
    int i;

    if (total > 0) {
        filled = (int)((current * (size_t)bar_width) / total);
        percent = (int)((current * 100u) / total);
    }

    if (filled > bar_width)
        filled = bar_width;
    if (percent > 100)
        percent = 100;

    fprintf(stderr, "\r[");
    for (i = 0; i < bar_width; ++i)
        fputc(i < filled ? '#' : '-', stderr);

    fprintf(stderr, "] %3d%% (%zu/%zu)", percent, current, total);

    if (label && label[0])
        fprintf(stderr, " %s", label);

    fflush(stderr);

    if (current == total)
        fputc('\n', stderr);
}

static void print_phase(int current, int total, const char *text)
{
    fprintf(stderr, "\n[%d/%d] %s\n", current, total, text ? text : "");
    fflush(stderr);
}

static int generate_bytes(struct rand_data *ec, FILE *fp, size_t count)
{
    unsigned char buf[AUTOJITTER_CHUNK_SIZE];
    size_t remaining = count;

    while (remaining > 0) {
        size_t chunk = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
        ssize_t got = jent_read_entropy(ec, (char *)buf, chunk);

        if (got != (ssize_t)chunk)
            return -1;

        if (fp && fwrite(buf, 1, chunk, fp) != chunk)
            return -1;

        remaining -= chunk;
    }

    return 0;
}

static void set_status_message(char *dst, size_t dst_len, const char *msg)
{
    if (!dst || dst_len == 0)
        return;

    if (!msg)
        msg = "unknown";

    snprintf(dst, dst_len, "%s", msg);
}

static int evaluate_candidate(const struct candidate *cand, struct result *res)
{
    struct rand_data *ec = NULL;
    double t0, t1;
    int rc;

    if (!cand || !res)
        return -1;

    memset(res, 0, sizeof(*res));
    res->candidate = cand;
    res->seconds = 0.0;
    set_status_message(res->status, sizeof(res->status), "not_run");

    rc = jent_entropy_init_ex(cand->osr, cand->flags);
    if (rc != 0) {
        snprintf(res->status, sizeof(res->status), "init_failed:%d", rc);
        res->ok = 0;
        return -1;
    }

    ec = jent_entropy_collector_alloc(cand->osr, cand->flags);
    if (!ec) {
        set_status_message(res->status, sizeof(res->status), "alloc_failed");
        res->ok = 0;
        return -1;
    }

    if (jent_status(ec, res->status, sizeof(res->status)) != 0)
        set_status_message(res->status, sizeof(res->status), "status_unavailable");

    t0 = monotonic_seconds();
    rc = generate_bytes(ec, NULL, AUTOJITTER_SMOKE_SIZE);
    t1 = monotonic_seconds();

    res->seconds = t1 - t0;
    res->ok = (rc == 0);

    if (res->ok) {
        if (res->status[0] == '\0' || strcasecmp(res->status, "status_unavailable") == 0)
            set_status_message(res->status, sizeof(res->status), "ok");
    } else {
        set_status_message(res->status, sizeof(res->status), "smoke_failed");
    }

    jent_entropy_collector_free(ec);
    return rc;
}

static int save_profile(const char *path, const struct result *best, const char *random_file)
{
    FILE *fp;

    if (!path || !best || !best->candidate || !random_file) {
        errno = EINVAL;
        return -1;
    }

    fp = fopen(path, "w");
    if (!fp)
        return -1;

    fprintf(fp, "NAME=%s\n", best->candidate->name);
    fprintf(fp, "OSR=%u\n", best->candidate->osr);
    fprintf(fp, "FLAGS=0x%x\n", best->candidate->flags);
    fprintf(fp, "SMOKE_SECONDS=%.9f\n", best->seconds);
    fprintf(fp, "RANDOM_FILE=%s\n", random_file);
    fprintf(fp, "STATUS=%s\n", best->status[0] ? best->status : "ok");

    fclose(fp);
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [--out-dir DIR] [--sample-bytes N]\n"
            "Default output dir: .\n",
            prog ? prog : "autotune");
}

int main(int argc, char **argv)
{
    const char *out_dir = ".";
    size_t sample_bytes = AUTOJITTER_SAMPLE_SIZE;
    char profile_path[PATH_MAX];
    char random_path[PATH_MAX];
    char report_path[PATH_MAX];
    FILE *sample_fp = NULL;
    FILE *report_fp = NULL;
    struct rand_data *ec = NULL;
    struct result best;
    int best_idx = -1;
    size_t i;
    const size_t candidate_count = sizeof(candidates) / sizeof(candidates[0]);

    memset(&best, 0, sizeof(best));

    for (i = 1; i < (size_t)argc; ++i) {
        if (strcmp(argv[i], "--out-dir") == 0 && (i + 1) < (size_t)argc) {
            out_dir = argv[++i];
        } else if (strcmp(argv[i], "--sample-bytes") == 0 && (i + 1) < (size_t)argc) {
            sample_bytes = (size_t)strtoull(argv[++i], NULL, 10);
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (ensure_dir(out_dir) != 0) {
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
    fprintf(report_fp, "sample_bytes=%zu smoke_bytes=%u\n", sample_bytes, AUTOJITTER_SMOKE_SIZE);
    fprintf(report_fp, "candidate_count=%zu\n\n", candidate_count);
    fflush(report_fp);

    print_phase(1, 4, "Searching best parameters");

    for (i = 0; i < candidate_count; ++i) {
        struct result current;
        char label[160];

        snprintf(label, sizeof(label),
                 "candidate %zu/%zu: %s",
                 i + 1, candidate_count, candidates[i].name);

        fprintf(stderr, "\nTrying candidate: %s (osr=%u flags=0x%x)\n",
                candidates[i].name,
                candidates[i].osr,
                candidates[i].flags);
        fflush(stderr);

        show_progress(i, candidate_count, label);

        if (evaluate_candidate(&candidates[i], &current) == 0) {
            fprintf(stderr, "  PASS   %.6f s   status=%s\n",
                    current.seconds,
                    current.status[0] ? current.status : "ok");
        } else {
            fprintf(stderr, "  FAIL            status=%s\n",
                    current.status[0] ? current.status : "error");
        }
        fflush(stderr);

        fprintf(report_fp,
                "candidate=%s osr=%u flags=0x%x ok=%d seconds=%.9f status=%s\n",
                candidates[i].name,
                candidates[i].osr,
                candidates[i].flags,
                current.ok,
                current.seconds,
                current.status[0] ? current.status : "n/a");
        fflush(report_fp);

        if (current.ok) {
            if (best_idx < 0 || current.seconds < best.seconds) {
                best = current;
                best_idx = (int)i;
            }
        }

        show_progress(i + 1, candidate_count, label);
    }

    if (best_idx < 0) {
        fclose(report_fp);
        fprintf(stderr, "\nNo candidate passed the smoke test. See %s\n", report_path);
        return 1;
    }

    fprintf(report_fp,
            "\nselected=%s osr=%u flags=0x%x seconds=%.9f status=%s\n",
            best.candidate->name,
            best.candidate->osr,
            best.candidate->flags,
            best.seconds,
            best.status[0] ? best.status : "ok");
    fflush(report_fp);

    print_phase(2, 4, "Writing selected profile");

    if (save_profile(profile_path, &best, random_path) != 0) {
        fclose(report_fp);
        fprintf(stderr, "Cannot save profile '%s': %s\n", profile_path, strerror(errno));
        return 1;
    }

    print_phase(3, 4, "Generating random output file");

    if (jent_entropy_init_ex(best.candidate->osr, best.candidate->flags) != 0) {
        fclose(report_fp);
        fprintf(stderr, "Re-init failed for winning candidate\n");
        return 1;
    }

    ec = jent_entropy_collector_alloc(best.candidate->osr, best.candidate->flags);
    if (!ec) {
        fclose(report_fp);
        fprintf(stderr, "Collector allocation failed for winning candidate\n");
        return 1;
    }

    sample_fp = fopen(random_path, "wb");
    if (!sample_fp) {
        jent_entropy_collector_free(ec);
        fclose(report_fp);
        fprintf(stderr, "Cannot open random sample '%s': %s\n", random_path, strerror(errno));
        return 1;
    }

    {
        size_t produced = 0;
        const size_t total = sample_bytes;
        unsigned char buf[AUTOJITTER_CHUNK_SIZE];

        while (produced < total) {
            size_t chunk = ((total - produced) > sizeof(buf)) ? sizeof(buf) : (total - produced);
            ssize_t got = jent_read_entropy(ec, (char *)buf, chunk);
            char label[96];

            if (got != (ssize_t)chunk) {
                fclose(sample_fp);
                jent_entropy_collector_free(ec);
                fclose(report_fp);
                fprintf(stderr, "\nFailed to generate validation sample\n");
                return 1;
            }

            if (fwrite(buf, 1, chunk, sample_fp) != chunk) {
                fclose(sample_fp);
                jent_entropy_collector_free(ec);
                fclose(report_fp);
                fprintf(stderr, "\nFailed to write validation sample\n");
                return 1;
            }

            produced += chunk;
            snprintf(label, sizeof(label), "writing %zu bytes", total);
            show_progress(produced, total, label);
        }
    }

    fclose(sample_fp);
    jent_entropy_collector_free(ec);

    print_phase(4, 4, "Writing report");

    fprintf(report_fp, "profile=%s\n", profile_path);
    fprintf(report_fp, "random=%s\n", random_path);
    fprintf(report_fp, "report=%s\n", report_path);
    fclose(report_fp);

    fprintf(stderr, "\nBest candidate: %s\n", best.candidate->name);
    fprintf(stderr, "OSR=%u FLAGS=0x%x\n", best.candidate->osr, best.candidate->flags);
    fprintf(stderr, "Profile: %s\n", profile_path);
    fprintf(stderr, "Sample : %s\n", random_path);
    fprintf(stderr, "Report : %s\n", report_path);

    return 0;
}
