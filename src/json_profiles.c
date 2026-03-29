#include "cpujitter_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILE_BYTES (256 * 1024)

static char *read_text_file(const char *path, size_t *out_len) {
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
    if (out_len) {
        *out_len = nread;
    }
    return buf;
}

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static const char *find_key(const char *obj_start, const char *obj_end, const char *key) {
    char needle[128];
    const char *p;

    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = obj_start;
    while (p && p < obj_end) {
        const char *k = strstr(p, needle);
        if (!k || k >= obj_end) {
            return NULL;
        }
        return k + strlen(needle);
    }
    return NULL;
}

static int parse_json_string_field(const char *obj_start,
                                   const char *obj_end,
                                   const char *key,
                                   char *out,
                                   size_t out_sz) {
    const char *p = find_key(obj_start, obj_end, key);
    const char *q;
    size_t n;

    if (!p) {
        return -1;
    }
    p = skip_ws(p);
    if (*p != ':') {
        return -1;
    }
    p = skip_ws(p + 1);
    if (*p != '"') {
        return -1;
    }
    p++;
    q = p;
    while (*q && *q != '"' && q < obj_end) {
        q++;
    }
    if (*q != '"') {
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

static int parse_json_int_field(const char *obj_start,
                                const char *obj_end,
                                const char *key,
                                int *out) {
    const char *p = find_key(obj_start, obj_end, key);
    char *endptr;
    long v;

    if (!p) {
        return -1;
    }
    p = skip_ws(p);
    if (*p != ':') {
        return -1;
    }
    p = skip_ws(p + 1);
    v = strtol(p, &endptr, 10);
    if (endptr == p) {
        return -1;
    }
    if (v < -2147483647L || v > 2147483647L) {
        return -1;
    }
    *out = (int)v;
    return 0;
}

static cpujitter_err parse_profile_object(const char *obj_start,
                                          const char *obj_end,
                                          profile_entry *entry) {
    if (parse_json_string_field(obj_start, obj_end, "id", entry->id, sizeof(entry->id)) != 0 ||
        parse_json_string_field(obj_start, obj_end, "os", entry->os, sizeof(entry->os)) != 0 ||
        parse_json_string_field(obj_start, obj_end, "arch", entry->arch, sizeof(entry->arch)) != 0 ||
        parse_json_string_field(obj_start,
                                obj_end,
                                "cpu_vendor",
                                entry->cpu_vendor,
                                sizeof(entry->cpu_vendor)) != 0 ||
        parse_json_int_field(obj_start, obj_end, "osr", &entry->osr) != 0 ||
        parse_json_int_field(obj_start, obj_end, "mem_blocks", &entry->mem_blocks) != 0 ||
        parse_json_int_field(obj_start, obj_end, "mem_block_size", &entry->mem_block_size) != 0 ||
        parse_json_int_field(obj_start, obj_end, "smoke_bytes", &entry->smoke_bytes) != 0) {
        return CPUJITTER_ERR_PARSE;
    }
    return CPUJITTER_OK;
}

cpujitter_err cpujitter_profiles_load(const char *path,
                                      profile_entry *out_entries,
                                      size_t out_cap,
                                      size_t *out_count) {
    char *json;
    const char *profiles;
    const char *p;
    size_t count = 0;

    if (!path || !out_entries || !out_count) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    json = read_text_file(path, NULL);
    if (!json) {
        return CPUJITTER_ERR_IO;
    }

    profiles = strstr(json, "\"profiles\"");
    if (!profiles) {
        free(json);
        return CPUJITTER_ERR_PARSE;
    }
    p = strchr(profiles, '[');
    if (!p) {
        free(json);
        return CPUJITTER_ERR_PARSE;
    }

    p++;
    while (*p) {
        const char *obj_start;
        const char *obj_end;
        int depth = 0;

        p = skip_ws(p);
        if (*p == ']') {
            break;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '{') {
            free(json);
            return CPUJITTER_ERR_PARSE;
        }

        obj_start = p;
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
            free(json);
            return CPUJITTER_ERR_PARSE;
        }
        obj_end = p;

        if (count >= out_cap) {
            free(json);
            return CPUJITTER_ERR_BUFFER_TOO_SMALL;
        }
        if (parse_profile_object(obj_start, obj_end, &out_entries[count]) != CPUJITTER_OK) {
            free(json);
            return CPUJITTER_ERR_PARSE;
        }
        count++;
    }

    *out_count = count;
    free(json);
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

static int match_score(const profile_entry *e, const cpujitter_platform_info *p) {
    int score = 0;
    if (strcmp(e->os, p->os) == 0) {
        score += 100;
    }
    if (strcmp(e->arch, p->arch) == 0) {
        score += 50;
    }
    if (strcmp(e->cpu_vendor, p->cpu_vendor) == 0) {
        score += 25;
    }
    return score;
}

cpujitter_err cpujitter_profiles_select_best(const profile_entry *entries,
                                             size_t count,
                                             const cpujitter_platform_info *platform,
                                             profile_entry *out_match) {
    size_t i;
    int best_score = -1;
    size_t best_index = 0;

    if (!entries || !platform || !out_match || count == 0) {
        return CPUJITTER_ERR_INVALID_ARG;
    }

    for (i = 0; i < count; i++) {
        int score = match_score(&entries[i], platform);
        if (score > best_score) {
            best_score = score;
            best_index = i;
        } else if (score == best_score && score >= 0 && strcmp(entries[i].id, entries[best_index].id) < 0) {
            best_index = i;
        }
    }

    if (best_score <= 0) {
        return CPUJITTER_ERR_NO_PROFILE;
    }

    *out_match = entries[best_index];
    return CPUJITTER_OK;
}
