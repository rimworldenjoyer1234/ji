#ifndef QUALIFIER_QUALIFIER_H
#define QUALIFIER_QUALIFIER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum qualifier_err {
    QUALIFIER_OK = 0,
    QUALIFIER_ERR_INVALID_ARG = 1,
    QUALIFIER_ERR_IO = 2,
    QUALIFIER_ERR_PARSE = 3,
    QUALIFIER_ERR_NO_CANDIDATE = 4,
    QUALIFIER_ERR_EXPORT = 5,
    QUALIFIER_ERR_INTERNAL = 6
} qualifier_err;

typedef struct qualifier_platform_info {
    char os[32];
    char arch[32];
    char cpu_vendor[32];
} qualifier_platform_info;

typedef struct qualifier_selected_profile {
    char id[64];
    char os[32];
    char arch[32];
    char cpu_vendor[32];
    int osr;
    int mem_blocks;
    int mem_block_size;
    int smoke_bytes;
    double score;
    int used_nist;
} qualifier_selected_profile;

typedef struct qualifier_options {
    const char *config_path;
    const char *artifacts_dir;
    const char *reports_dir;
    const char *export_path;
    int enable_nist;
} qualifier_options;

qualifier_err qualifier_run(const qualifier_options *opts, qualifier_selected_profile *out_profile);

const char *qualifier_strerror(qualifier_err err);

#ifdef __cplusplus
}
#endif

#endif
