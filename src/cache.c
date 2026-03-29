#include "cpujitter_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CACHE_BYTES 8192

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

static int extract_validated_profile_section(const char *json, char *out, size_t out_sz) {
    const char *k = strstr(json, "\"validated_profile\"");
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

    if (extract_validated_profile_section(buf, prof, sizeof(prof)) != 0) {
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

    return CPUJITTER_OK;
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
                "  \"schema_version\": 1,\n"
                "  \"platform\": {\"os\": \"%s\", \"arch\": \"%s\", \"cpu_vendor\": \"%s\"},\n"
                "  \"validated_profile\": {\n"
                "    \"id\": \"%s\",\n"
                "    \"os\": \"%s\",\n"
                "    \"arch\": \"%s\",\n"
                "    \"cpu_vendor\": \"%s\",\n"
                "    \"osr\": %d,\n"
                "    \"mem_blocks\": %d,\n"
                "    \"mem_block_size\": %d,\n"
                "    \"smoke_bytes\": %d\n"
                "  }\n"
                "}\n",
                platform->os,
                platform->arch,
                platform->cpu_vendor,
                entry->id,
                entry->os,
                entry->arch,
                entry->cpu_vendor,
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
