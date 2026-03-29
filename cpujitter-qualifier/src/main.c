#include "qualifier/qualifier.h"

#include <stdio.h>

#ifndef QUALIFIER_SOURCE_DIR
#define QUALIFIER_SOURCE_DIR "."
#endif

int main(int argc, char **argv) {
    qualifier_options opts;
    qualifier_selected_profile selected;
    qualifier_err err;

    (void)argc;
    (void)argv;

    static char config_path[512];
    static char artifacts_dir[512];
    static char reports_dir[512];
    static char export_path[512];

    snprintf(config_path, sizeof(config_path), "%s/configs/sweep_config.json", QUALIFIER_SOURCE_DIR);
    snprintf(artifacts_dir, sizeof(artifacts_dir), "%s/artifacts", QUALIFIER_SOURCE_DIR);
    snprintf(reports_dir, sizeof(reports_dir), "%s/reports", QUALIFIER_SOURCE_DIR);
    snprintf(export_path, sizeof(export_path), "%s/exported_profiles/selected_profile.json", QUALIFIER_SOURCE_DIR);

    opts.config_path = config_path;
    opts.artifacts_dir = artifacts_dir;
    opts.reports_dir = reports_dir;
    opts.export_path = export_path;
    opts.enable_nist = 0;

    err = qualifier_run(&opts, &selected);
    if (err != QUALIFIER_OK) {
        fprintf(stderr, "qualifier_run failed: %s\n", qualifier_strerror(err));
        return 1;
    }

    printf("Selected profile exported: %s\n", opts.export_path);
    printf("id=%s score=%.6f used_nist=%d\n", selected.id, selected.score, selected.used_nist);
    return 0;
}
