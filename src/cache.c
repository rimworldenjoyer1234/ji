#include "cpujitter_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CACHE_BYTES 8192
#define CPUJITTER_CACHE_SCHEMA_VERSION 1

typedef struct cache_fingerprint {
    char os[32];
    char arch[32];
    char cpu_vendor[32];
    char cpu_model[64];
    char virtualization[16];
    int logical_cpu_count;
} cache_fingerprint;

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static const char *find_key(const char *obj, const char *key) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    return strstr(obj, needle);
}

static int parse_string_field(const char *obj, const char *key, char *out, size_t out_sz) {
    const char *p = find_key(obj, key);
    const char *q;
    size_t n;
    if (!p) {
        return -1;
    }
    p = skip_ws(p + strlen(key) + 2);
    if (*p != ':') {
        return -1;
    }
    p = skip_ws(p + 1);
    if (*p != '"') {
        return -1;
    }
    p++;
    q = strchr(p, '"');
    if (!q) {
        return -1;
    }
    n = (size_t)(q - p);
    if (n + 1U > out_sz) {
        return -1;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

static int parse_int_field(const char *obj, const char *key, int *out) {
    const char *p = find_key(obj, key);
    char *endptr;
    long v;
    if (!p) {
        return -1;
    }
    p = skip_ws(p + strlen(key) + 2);
    if (*p != ':') {
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

static int extract_section(const char *json, const char *key, char *out, size_t out_sz) {
    const char *k = strstr(json, key);
    const char *s;
    const char *p;
    int depth = 0;
    size_t n;

    if (!k) {
        return -1;
    }
    s = strchr(k, '{');
    if (!s) {
        return -1;
    }

    p = s;
    while (*p) {
        if (*p == '{') {
            depth++;
        } else if (*p == '}') {
            depth--;
            if (depth == 0) {
                p++;
                break;
            }
        }
        p++;
    }
    if (depth != 0) {
        return -1;
    }

    n = (size_t)(p - s);
    if (n + 1U > out_sz) {
        return -1;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    return 0;
}

static cpujitter_err read_cache_text(const char *path, char *out_buf, size_t out_buf_sz) {
    FILE *f;
    size_t n;

    if (!path || !out_buf || out_buf_sz == 0) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    f = fopen(path, "rb");
    if (!f) {
        return CPUJITTER_ERR_IO;
    }

    n = fread(out_buf, 1U, out_buf_sz - 1U, f);
    fclose(f);

    if (n == 0) {
        return CPUJITTER_ERR_PARSE;
    }
    out_buf[n] = '\0';
    return CPUJITTER_OK;
}

static cpujitter_err parse_cache_fingerprint(const char *json, cache_fingerprint *out_fp) {
    char sec[MAX_CACHE_BYTES];

    if (extract_section(json, "\"platform_fingerprint\"", sec, sizeof(sec)) != 0) {
        return CPUJITTER_ERR_PARSE;
    }

    memset(out_fp, 0, sizeof(*out_fp));
    if (parse_string_field(sec, "os", out_fp->os, sizeof(out_fp->os)) != 0 ||
        parse_string_field(sec, "arch", out_fp->arch, sizeof(out_fp->arch)) != 0 ||
        parse_string_field(sec, "cpu_vendor", out_fp->cpu_vendor, sizeof(out_fp->cpu_vendor)) != 0 ||
        parse_string_field(sec, "cpu_model", out_fp->cpu_model, sizeof(out_fp->cpu_model)) != 0 ||
        parse_string_field(sec,
                           "virtualization",
                           out_fp->virtualization,
                           sizeof(out_fp->virtualization)) != 0 ||
        parse_int_field(sec, "logical_cpu_count", &out_fp->logical_cpu_count) != 0) {
        return CPUJITTER_ERR_PARSE;
    }

    return CPUJITTER_OK;
}

static int fingerprint_matches(const cache_fingerprint *fp, const cpujitter_platform_info *platform) {
    return strcmp(fp->os, platform->os) == 0 && strcmp(fp->arch, platform->arch) == 0 &&
           strcmp(fp->cpu_vendor, platform->cpu_vendor) == 0 &&
           strcmp(fp->cpu_model, platform->cpu_model) == 0 &&
           strcmp(fp->virtualization, platform->virtualization) == 0 &&
           fp->logical_cpu_count == platform->logical_cpu_count;
}

cpujitter_err cpujitter_cache_validate_platform(const char *path,
                                                const cpujitter_platform_info *platform) {
    char buf[MAX_CACHE_BYTES];
    cache_fingerprint fp;
    int schema_version;
    cpujitter_err err;

    if (!path || !platform) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    err = read_cache_text(path, buf, sizeof(buf));
    if (err != CPUJITTER_OK) {
        return err;
    }

    if (parse_int_field(buf, "schema_version", &schema_version) != 0) {
        return CPUJITTER_ERR_PARSE;
    }
    if (schema_version != CPUJITTER_CACHE_SCHEMA_VERSION) {
        return CPUJITTER_ERR_NO_PROFILE;
    }

    err = parse_cache_fingerprint(buf, &fp);
    if (err != CPUJITTER_OK) {
        return err;
    }

    return fingerprint_matches(&fp, platform) ? CPUJITTER_OK : CPUJITTER_ERR_NO_PROFILE;
}

cpujitter_err cpujitter_cache_load(const char *path, profile_entry *out_entry) {
    FILE *f;
    char buf[MAX_CACHE_BYTES];
    char prof[MAX_CACHE_BYTES];
    size_t n;

    if (!path || !out_entry) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    f = fopen(path, "rb");
    if (!f) {
        return CPUJITTER_ERR_IO;
    }
    n = fread(buf, 1, sizeof(buf) - 1U, f);
    fclose(f);
    if (n == 0) {
        return CPUJITTER_ERR_PARSE;
    }
    buf[n] = '\0';

    if (extract_section(buf, "\"validated_profile\"", prof, sizeof(prof)) != 0) {
        return CPUJITTER_ERR_PARSE;
    }

    memset(out_entry, 0, sizeof(*out_entry));
    if (parse_string_field(prof, "id", out_entry->id, sizeof(out_entry->id)) != 0 ||
        parse_string_field(prof, "os", out_entry->os, sizeof(out_entry->os)) != 0 ||
        parse_string_field(prof, "arch", out_entry->arch, sizeof(out_entry->arch)) != 0 ||
        parse_string_field(prof,
                           "cpu_vendor",
                           out_entry->cpu_vendor,
                           sizeof(out_entry->cpu_vendor)) != 0 ||
        parse_int_field(prof, "osr", &out_entry->osr) != 0 ||
        parse_int_field(prof, "mem_blocks", &out_entry->mem_blocks) != 0 ||
        parse_int_field(prof, "mem_block_size", &out_entry->mem_block_size) != 0 ||
        parse_int_field(prof, "smoke_bytes", &out_entry->smoke_bytes) != 0) {
        return CPUJITTER_ERR_PARSE;
    }

    (void)parse_string_field(prof, "virtualization", out_entry->virtualization, sizeof(out_entry->virtualization));
    (void)parse_string_field(prof, "cpu_model_exact", out_entry->cpu_model_exact, sizeof(out_entry->cpu_model_exact));
    (void)parse_string_field(prof, "cpu_model_family", out_entry->cpu_model_family, sizeof(out_entry->cpu_model_family));
    (void)parse_int_field(prof, "logical_cpu_min", &out_entry->logical_cpu_min);
    (void)parse_int_field(prof, "logical_cpu_max", &out_entry->logical_cpu_max);

    return CPUJITTER_OK;
}

cpujitter_err cpujitter_cache_load_validated(const char *path,
                                             const cpujitter_platform_info *platform,
                                             profile_entry *out_entry) {
    cpujitter_err err;

    if (!path || !platform || !out_entry) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    err = cpujitter_cache_validate_platform(path, platform);
    if (err != CPUJITTER_OK) {
        return err;
    }

    return cpujitter_cache_load(path, out_entry);
}

cpujitter_err cpujitter_cache_save(const char *path,
                                   const profile_entry *entry,
                                   const cpujitter_platform_info *platform) {
    FILE *f;

    if (!path || !entry || !platform) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    f = fopen(path, "wb");
    if (!f) {
        return CPUJITTER_ERR_IO;
    }

    if (fprintf(f,
                "{\n"
                "  \"schema_version\": %d,\n"
                "  \"platform_fingerprint\": {\n"
                "    \"os\": \"%s\",\n"
                "    \"arch\": \"%s\",\n"
                "    \"cpu_vendor\": \"%s\",\n"
                "    \"cpu_model\": \"%s\",\n"
                "    \"virtualization\": \"%s\",\n"
                "    \"logical_cpu_count\": %d\n"
                "  },\n"
                "  \"validated_profile\": {\n"
                "    \"id\": \"%s\",\n"
                "    \"os\": \"%s\",\n"
                "    \"arch\": \"%s\",\n"
                "    \"cpu_vendor\": \"%s\",\n"
                "    \"virtualization\": \"%s\",\n"
                "    \"cpu_model_exact\": \"%s\",\n"
                "    \"cpu_model_family\": \"%s\",\n"
                "    \"logical_cpu_min\": %d,\n"
                "    \"logical_cpu_max\": %d,\n"
                "    \"osr\": %d,\n"
                "    \"mem_blocks\": %d,\n"
                "    \"mem_block_size\": %d,\n"
                "    \"smoke_bytes\": %d\n"
                "  }\n"
                "}\n",
                CPUJITTER_CACHE_SCHEMA_VERSION,
                platform->os,
                platform->arch,
                platform->cpu_vendor,
                platform->cpu_model,
                platform->virtualization,
                platform->logical_cpu_count,
                entry->id,
                entry->os,
                entry->arch,
                entry->cpu_vendor,
                entry->virtualization,
                entry->cpu_model_exact,
                entry->cpu_model_family,
                entry->logical_cpu_min,
                entry->logical_cpu_max,
                entry->osr,
                entry->mem_blocks,
                entry->mem_block_size,
                entry->smoke_bytes) < 0) {
        fclose(f);
        return CPUJITTER_ERR_IO;
    }

    fclose(f);
    return CPUJITTER_OK;
}
