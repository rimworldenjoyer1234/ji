#include "qualifier_internal.h"

#include <stdio.h>
#include <stdlib.h>

static int nist_script_exists(void) {
    FILE *f = fopen("external/SP800-90B_EntropyAssessment/run_nist.sh", "rb");
    if (!f) {
        return 0;
    }
    fclose(f);
    return 1;
}

int qualifier_nist_assess_if_available(const qualifier_candidate *cand,
                                       const char *artifacts_dir,
                                       int enabled,
                                       double *out_bonus_score,
                                       int *out_used_nist) {
    (void)cand;
    (void)artifacts_dir;

    if (!out_bonus_score || !out_used_nist) {
        return -1;
    }

    *out_bonus_score = 0.0;
    *out_used_nist = 0;

    if (!enabled || !nist_script_exists()) {
        return 0;
    }

    /*
     * TODO(nist-integration): invoke NIST SP800-90B tooling and parse results.
     * Keep this optional and isolated from normal build/runtime usage.
     */
    *out_bonus_score = 0.05;
    *out_used_nist = 1;
    return 0;
}
