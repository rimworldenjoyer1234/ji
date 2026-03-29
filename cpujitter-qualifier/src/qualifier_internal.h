#ifndef QUALIFIER_INTERNAL_H
#define QUALIFIER_INTERNAL_H

#include <stddef.h>

#include "qualifier/qualifier.h"

typedef struct qualifier_candidate {
    int osr;
    int mem_blocks;
    int mem_block_size;
    int smoke_bytes;
} qualifier_candidate;

typedef struct qualifier_sweep_config {
    int osr_values[16];
    size_t osr_count;
    int mem_blocks_values[16];
    size_t mem_blocks_count;
    int mem_block_size_values[16];
    size_t mem_block_size_count;
    int smoke_bytes;
    int samples_per_candidate;
} qualifier_sweep_config;

void qualifier_detect_platform(qualifier_platform_info *out_info);

qualifier_err qualifier_load_sweep_config(const char *path, qualifier_sweep_config *out_cfg);

qualifier_err qualifier_evaluate_candidates(const qualifier_platform_info *platform,
                                            const qualifier_sweep_config *cfg,
                                            const char *artifacts_dir,
                                            const char *reports_dir,
                                            int enable_nist,
                                            qualifier_selected_profile *out_profile);

qualifier_err qualifier_export_profile(const char *path,
                                       const qualifier_selected_profile *profile);

int qualifier_nist_assess_if_available(const qualifier_candidate *cand,
                                       const char *artifacts_dir,
                                       int enabled,
                                       double *out_bonus_score,
                                       int *out_used_nist);

#endif
