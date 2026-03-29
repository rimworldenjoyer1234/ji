#include "qualifier_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CFG_BYTES 16384

static char *read_file(const char *path) {
    FILE *f;
    long sz;
    char *buf;
    size_t n;

    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz <= 0 || sz > MAX_CFG_BYTES) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (char *)malloc((size_t)sz + 1U);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    n = fread(buf, 1U, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    return buf;
}

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static int parse_int(const char **p, int *out) {
    char *endptr;
    long v;
    const char *s = skip_ws(*p);
    v = strtol(s, &endptr, 10);
    if (endptr == s) {
        return -1;
    }
    *out = (int)v;
    *p = endptr;
    return 0;
}

static int parse_int_array(const char *json,
                           const char *key,
                           int *out,
                           size_t cap,
                           size_t *out_count) {
    char needle[64];
    const char *k;
    const char *p;
    size_t count = 0;

    snprintf(needle, sizeof(needle), "\"%s\"", key);
    k = strstr(json, needle);
    if (!k) {
        return -1;
    }
    p = strchr(k, '[');
    if (!p) {
        return -1;
    }
    p++;
    while (*p) {
        int v;
        p = skip_ws(p);
        if (*p == ']') {
            break;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        if (count >= cap || parse_int(&p, &v) != 0) {
            return -1;
        }
        out[count++] = v;
    }
    *out_count = count;
    return count > 0 ? 0 : -1;
}

static int parse_int_field(const char *json, const char *key, int *out) {
    char needle[64];
    const char *k;
    const char *p;
    char *endptr;
    long v;

    snprintf(needle, sizeof(needle), "\"%s\"", key);
    k = strstr(json, needle);
    if (!k) {
        return -1;
    }
    p = strchr(k, ':');
    if (!p) {
        return -1;
    }
    p = skip_ws(p + 1);
    v = strtol(p, &endptr, 10);
    if (endptr == p) {
        return -1;
    }
    *out = (int)v;
    return 0;
}

qualifier_err qualifier_load_sweep_config(const char *path, qualifier_sweep_config *out_cfg) {
    char *json;

    if (!path || !out_cfg) {
        return QUALIFIER_ERR_INVALID_ARG;
    }

    memset(out_cfg, 0, sizeof(*out_cfg));

    json = read_file(path);
    if (!json) {
        return QUALIFIER_ERR_IO;
    }

    if (parse_int_array(json,
                        "osr_values",
                        out_cfg->osr_values,
                        sizeof(out_cfg->osr_values) / sizeof(out_cfg->osr_values[0]),
                        &out_cfg->osr_count) != 0 ||
        parse_int_array(json,
                        "mem_blocks_values",
                        out_cfg->mem_blocks_values,
                        sizeof(out_cfg->mem_blocks_values) / sizeof(out_cfg->mem_blocks_values[0]),
                        &out_cfg->mem_blocks_count) != 0 ||
        parse_int_array(json,
                        "mem_block_size_values",
                        out_cfg->mem_block_size_values,
                        sizeof(out_cfg->mem_block_size_values) / sizeof(out_cfg->mem_block_size_values[0]),
                        &out_cfg->mem_block_size_count) != 0 ||
        parse_int_field(json, "smoke_bytes", &out_cfg->smoke_bytes) != 0 ||
        parse_int_field(json, "samples_per_candidate", &out_cfg->samples_per_candidate) != 0) {
        free(json);
        return QUALIFIER_ERR_PARSE;
    }

    free(json);
    return QUALIFIER_OK;
}
