#include "cpujitter_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILE_BYTES (256 * 1024)

static char *read_text_file(const char *path) {
    FILE *f = fopen(path, "rb");
    char *buf;
    long sz;
    size_t nread;

    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    sz = ftell(f);
    if (sz < 0 || sz > MAX_FILE_BYTES) {
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
    nread = fread(buf, 1U, (size_t)sz, f);
    fclose(f);
    if (nread != (size_t)sz) {
        free(buf);
        return NULL;
    }
    buf[nread] = '\0';
    return buf;
}

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
    p = strchr(p, ':');
    if (!p) {
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
    p = strchr(p, ':');
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

static int parse_string_optional(const char *obj, const char *key, char *out, size_t out_sz) {
    if (parse_string_field(obj, key, out, out_sz) != 0) {
        if (out_sz > 0) {
            out[0] = '\0';
        }
    }
    return 0;
}

static int parse_int_optional(const char *obj, const char *key, int default_v, int *out) {
    if (parse_int_field(obj, key, out) != 0) {
        *out = default_v;
    }
    return 0;
}

static cpujitter_err parse_profile_file(const char *path, profile_entry *out) {
    char *json = read_text_file(path);
    const char *match;
    if (!json || !out) {
        free(json);
        return CPUJITTER_ERR_IO;
    }

    memset(out, 0, sizeof(*out));
    if (parse_string_field(json, "id", out->id, sizeof(out->id)) != 0 ||
        parse_string_field(json, "os", out->os, sizeof(out->os)) != 0 ||
        parse_string_field(json, "arch", out->arch, sizeof(out->arch)) != 0 ||
        parse_string_field(json, "cpu_vendor", out->cpu_vendor, sizeof(out->cpu_vendor)) != 0 ||
        parse_int_field(json, "osr", &out->osr) != 0 ||
        parse_int_field(json, "mem_blocks", &out->mem_blocks) != 0 ||
        parse_int_field(json, "mem_block_size", &out->mem_block_size) != 0 ||
        parse_int_field(json, "smoke_bytes", &out->smoke_bytes) != 0) {
        free(json);
        return CPUJITTER_ERR_PARSE;
    }

    match = find_key(json, "match");
    if (match) {
        parse_string_optional(match, "virtualization", out->virtualization, sizeof(out->virtualization));
        parse_string_optional(match, "cpu_model_exact", out->cpu_model_exact, sizeof(out->cpu_model_exact));
        parse_string_optional(match, "cpu_model_family", out->cpu_model_family, sizeof(out->cpu_model_family));
        parse_int_optional(match, "logical_cpu_min", 0, &out->logical_cpu_min);
        parse_int_optional(match, "logical_cpu_max", 0, &out->logical_cpu_max);
    }

    if (out->virtualization[0] == '\0') {
        snprintf(out->virtualization, sizeof(out->virtualization), "baremetal");
    }

    free(json);
    return CPUJITTER_OK;
}

static void dirname_of(const char *path, char *out, size_t out_sz) {
    const char *slash;
    size_t n;
    if (!path || !out || out_sz == 0) {
        return;
    }
    slash = strrchr(path, '/');
    if (!slash) {
        snprintf(out, out_sz, ".");
        return;
    }
    n = (size_t)(slash - path);
    if (n >= out_sz) {
        n = out_sz - 1U;
    }
    memcpy(out, path, n);
    out[n] = '\0';
}

cpujitter_err cpujitter_profiles_load(const char *index_path,
                                      profile_entry *out_entries,
                                      size_t out_cap,
                                      size_t *out_count) {
    char *json;
    const char *p;
    char base_dir[256];
    size_t count = 0;

    if (!index_path || !out_entries || !out_count) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    json = read_text_file(index_path);
    if (!json) {
        return CPUJITTER_ERR_IO;
    }

    dirname_of(index_path, base_dir, sizeof(base_dir));
    p = json;
    while ((p = strstr(p, "\"path\"")) != NULL) {
        const char *colon = strchr(p, ':');
        const char *q;
        const char *e;
        char rel[192];
        char full[384];
        size_t n;

        if (!colon) {
            free(json);
            return CPUJITTER_ERR_PARSE;
        }
        q = skip_ws(colon + 1);
        if (*q != '"') {
            free(json);
            return CPUJITTER_ERR_PARSE;
        }
        q++;
        e = strchr(q, '"');
        if (!e) {
            free(json);
            return CPUJITTER_ERR_PARSE;
        }
        n = (size_t)(e - q);
        if (n + 1U > sizeof(rel) || count >= out_cap) {
            free(json);
            return CPUJITTER_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(rel, q, n);
        rel[n] = '\0';

        if (snprintf(full, sizeof(full), "%s/%s", base_dir, rel) >= (int)sizeof(full)) {
            free(json);
            return CPUJITTER_ERR_PARSE;
        }
        if (parse_profile_file(full, &out_entries[count]) != CPUJITTER_OK) {
            free(json);
            return CPUJITTER_ERR_PARSE;
        }
        count++;
        p = e + 1;
    }

    free(json);
    *out_count = count;
    return count > 0 ? CPUJITTER_OK : CPUJITTER_ERR_NO_PROFILE;
}

cpujitter_err cpujitter_profiles_find_by_id(const profile_entry *entries,
                                            size_t count,
                                            const char *id,
                                            profile_entry *out_match) {
    size_t i;
    if (!entries || !id || !out_match) {
        return CPUJITTER_ERR_INVALID_ARG;
    }
    for (i = 0; i < count; i++) {
        if (strcmp(entries[i].id, id) == 0) {
            *out_match = entries[i];
            return CPUJITTER_OK;
        }
    }
    return CPUJITTER_ERR_NO_PROFILE;
}

static int model_family_match(const char *pattern, const char *model) {
    size_t n;
    if (!pattern || !pattern[0]) {
        return 0;
    }
    n = strlen(pattern);
    if (pattern[n - 1] == '*') {
        return strncmp(model, pattern, n - 1U) == 0;
    }
    return strcmp(pattern, model) == 0;
}

static int virtualization_rank(const char *profile_virt, const char *platform_virt) {
    int platform_vm = (strcmp(platform_virt, "vm") == 0);
    int profile_vm = (strcmp(profile_virt, "vm") == 0);

    if (platform_vm) {
        return profile_vm ? 40 : -40;
    }
    return profile_vm ? -20 : 20;
}

static int candidate_score(const profile_entry *e,
                           const cpujitter_platform_info *p,
                           char *why,
                           size_t why_len) {
    int score = 0;
    int exact_model = 0;
    int family_model = 0;

    if (strcmp(e->os, p->os) != 0 || strcmp(e->arch, p->arch) != 0 || strcmp(e->cpu_vendor, p->cpu_vendor) != 0) {
        snprintf(why, why_len, "failed core match (os/arch/vendor)");
        return -100000;
    }

    score += 500;
    score += virtualization_rank(e->virtualization, p->virtualization);

    if (e->cpu_model_exact[0]) {
        if (strcmp(e->cpu_model_exact, p->cpu_model) == 0) {
            exact_model = 1;
            score += 400;
        } else {
            snprintf(why, why_len, "failed exact cpu model match");
            return -50000;
        }
    } else if (e->cpu_model_family[0]) {
        if (model_family_match(e->cpu_model_family, p->cpu_model)) {
            family_model = 1;
            score += 250;
        } else {
            snprintf(why, why_len, "failed cpu model family rule");
            return -30000;
        }
    }

    if (e->logical_cpu_min > 0 && p->logical_cpu_count < e->logical_cpu_min) {
        snprintf(why, why_len, "failed logical_cpu_min");
        return -20000;
    }
    if (e->logical_cpu_max > 0 && p->logical_cpu_count > e->logical_cpu_max) {
        snprintf(why, why_len, "failed logical_cpu_max");
        return -20000;
    }
    if (e->logical_cpu_min > 0 || e->logical_cpu_max > 0) {
        score += 30;
    }

    snprintf(why,
             why_len,
             "core-match; model=%s; virtualization=%s; cpu_count=%d",
             exact_model ? "exact" : (family_model ? "family" : "generic"),
             e->virtualization,
             p->logical_cpu_count);
    return score;
}

cpujitter_err cpujitter_profiles_select_best(const profile_entry *entries,
                                             size_t count,
                                             const cpujitter_platform_info *platform,
                                             profile_entry *out_match,
                                             char *out_explanation,
                                             size_t out_explanation_len) {
    size_t i;
    int best_score = -200000;
    size_t best_index = 0;
    char why[160];

    if (!entries || !platform || !out_match || count == 0) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    if (out_explanation && out_explanation_len > 0) {
        out_explanation[0] = '\0';
    }

    for (i = 0; i < count; i++) {
        int score = candidate_score(&entries[i], platform, why, sizeof(why));
        if (score > best_score || (score == best_score && strcmp(entries[i].id, entries[best_index].id) < 0)) {
            best_score = score;
            best_index = i;
            if (out_explanation && out_explanation_len > 0) {
                snprintf(out_explanation,
                         out_explanation_len,
                         "selected=%s score=%d reason=%s",
                         entries[i].id,
                         score,
                         why);
            }
        }
    }

    if (best_score < 0) {
        if (out_explanation && out_explanation_len > 0) {
            snprintf(out_explanation, out_explanation_len, "no profile matched platform");
        }
        return CPUJITTER_ERR_NO_PROFILE;
    }

    *out_match = entries[best_index];
    return CPUJITTER_OK;
}
