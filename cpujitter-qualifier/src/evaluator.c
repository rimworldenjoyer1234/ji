#include "qualifier_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fill_entropy(unsigned char *buf, size_t n) {
    FILE *f = fopen("/dev/urandom", "rb");
    size_t i;

    if (f) {
        if (fread(buf, 1U, n, f) == n) {
            fclose(f);
            return 0;
        }
        fclose(f);
    }

    srand((unsigned int)time(NULL));
    for (i = 0; i < n; i++) {
        buf[i] = (unsigned char)(rand() & 0xFF);
    }
    return 0;
}

static double base_candidate_score(const qualifier_candidate *cand, int samples) {
    unsigned char buf[256];
    int counts[256];
    int i;
    int nonzero = 0;
    double unique_ratio;
    double param_penalty;

    if (samples < 32) {
        samples = 32;
    }
    if (samples > (int)sizeof(buf)) {
        samples = (int)sizeof(buf);
    }

    memset(counts, 0, sizeof(counts));
    fill_entropy(buf, (size_t)samples);
    for (i = 0; i < samples; i++) {
        counts[buf[i]]++;
    }
    for (i = 0; i < 256; i++) {
        if (counts[i] > 0) {
            nonzero++;
        }
    }

    unique_ratio = (double)nonzero / (double)samples;
    param_penalty = 0.001 * (double)(cand->osr + cand->mem_blocks / 16 + cand->mem_block_size / 64);
    return unique_ratio - param_penalty;
}

static void write_report_line(FILE *f,
                              const qualifier_candidate *cand,
                              double score,
                              double nist_bonus,
                              int used_nist) {
    fprintf(f,
            "osr=%d mem_blocks=%d mem_block_size=%d smoke_bytes=%d score=%.6f nist_bonus=%.6f used_nist=%d\n",
            cand->osr,
            cand->mem_blocks,
            cand->mem_block_size,
            cand->smoke_bytes,
            score,
            nist_bonus,
            used_nist);
}

qualifier_err qualifier_evaluate_candidates(const qualifier_platform_info *platform,
                                            const qualifier_sweep_config *cfg,
                                            const char *artifacts_dir,
                                            const char *reports_dir,
                                            int enable_nist,
                                            qualifier_selected_profile *out_profile) {
    size_t i, j, k;
    double best = -1e9;
    qualifier_candidate best_cand;
    int best_used_nist = 0;
    char report_path[512];
    FILE *report;

    if (!platform || !cfg || !reports_dir || !out_profile) {
        return QUALIFIER_ERR_INVALID_ARG;
    }

    snprintf(report_path, sizeof(report_path), "%s/sweep_report.txt", reports_dir);
    report = fopen(report_path, "wb");
    if (!report) {
        return QUALIFIER_ERR_IO;
    }

    memset(&best_cand, 0, sizeof(best_cand));
    for (i = 0; i < cfg->osr_count; i++) {
        for (j = 0; j < cfg->mem_blocks_count; j++) {
            for (k = 0; k < cfg->mem_block_size_count; k++) {
                qualifier_candidate cand;
                double score;
                double nist_bonus = 0.0;
                int used_nist = 0;

                cand.osr = cfg->osr_values[i];
                cand.mem_blocks = cfg->mem_blocks_values[j];
                cand.mem_block_size = cfg->mem_block_size_values[k];
                cand.smoke_bytes = cfg->smoke_bytes;

                score = base_candidate_score(&cand, cfg->samples_per_candidate);
                (void)qualifier_nist_assess_if_available(&cand,
                                                         artifacts_dir,
                                                         enable_nist,
                                                         &nist_bonus,
                                                         &used_nist);
                score += nist_bonus;
                write_report_line(report, &cand, score, nist_bonus, used_nist);

                if (score > best) {
                    best = score;
                    best_cand = cand;
                    best_used_nist = used_nist;
                }
            }
        }
    }

    fclose(report);

    if (best < -1e8) {
        return QUALIFIER_ERR_NO_CANDIDATE;
    }

    memset(out_profile, 0, sizeof(*out_profile));
    snprintf(out_profile->id,
             sizeof(out_profile->id),
             "%.15s-%.15s-%.15s-qualified",
             platform->os,
             platform->arch,
             platform->cpu_vendor);
    snprintf(out_profile->os, sizeof(out_profile->os), "%s", platform->os);
    snprintf(out_profile->arch, sizeof(out_profile->arch), "%s", platform->arch);
    snprintf(out_profile->cpu_vendor, sizeof(out_profile->cpu_vendor), "%s", platform->cpu_vendor);
    out_profile->osr = best_cand.osr;
    out_profile->mem_blocks = best_cand.mem_blocks;
    out_profile->mem_block_size = best_cand.mem_block_size;
    out_profile->smoke_bytes = best_cand.smoke_bytes;
    out_profile->score = best;
    out_profile->used_nist = best_used_nist;

    return QUALIFIER_OK;
}
