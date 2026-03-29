#include "qualifier/qualifier.h"

#include <stdio.h>
#include <string.h>

#ifndef QUALIFIER_SOURCE_DIR
#define QUALIFIER_SOURCE_DIR "."
#endif

int main(void) {
    qualifier_options opts;
    qualifier_selected_profile selected;
    qualifier_err err;

    char config_path[512];
    char artifacts_dir[512];
    char reports_dir[512];
    char export_path[512];

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

    if (selected.id[0] == '\0' || selected.osr <= 0 || selected.mem_blocks <= 0 || selected.mem_block_size <= 0) {
        fprintf(stderr, "selected profile invalid\n");
        return 1;
    }

    printf("smoke ok: %s\n", selected.id);
    return 0;
}
