#include "qualifier_internal.h"

#include <stdio.h>
#include <string.h>

qualifier_err qualifier_run(const qualifier_options *opts, qualifier_selected_profile *out_profile) {
    qualifier_platform_info platform;
    qualifier_sweep_config cfg;
    qualifier_err err;

    if (!opts || !out_profile || !opts->config_path || !opts->reports_dir || !opts->export_path) {
        return QUALIFIER_ERR_INVALID_ARG;
    }

    qualifier_detect_platform(&platform);

    err = qualifier_load_sweep_config(opts->config_path, &cfg);
    if (err != QUALIFIER_OK) {
        return err;
    }

    err = qualifier_evaluate_candidates(&platform,
                                        &cfg,
                                        opts->artifacts_dir ? opts->artifacts_dir : "artifacts",
                                        opts->reports_dir,
                                        opts->enable_nist,
                                        out_profile);
    if (err != QUALIFIER_OK) {
        return err;
    }

    err = qualifier_export_profile(opts->export_path, out_profile);
    if (err != QUALIFIER_OK) {
        return err;
    }

    return QUALIFIER_OK;
}

const char *qualifier_strerror(qualifier_err err) {
    switch (err) {
    case QUALIFIER_OK:
        return "ok";
    case QUALIFIER_ERR_INVALID_ARG:
        return "invalid argument";
    case QUALIFIER_ERR_IO:
        return "i/o error";
    case QUALIFIER_ERR_PARSE:
        return "parse error";
    case QUALIFIER_ERR_NO_CANDIDATE:
        return "no candidate";
    case QUALIFIER_ERR_EXPORT:
        return "export failed";
    case QUALIFIER_ERR_INTERNAL:
        return "internal error";
    default:
        return "unknown";
    }
}
