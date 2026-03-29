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
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    sz = ftell(f);
    if (sz < 0 || sz > MAX_FILE_BYTES) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    buf = (char *)malloc((size_t)sz + 1U);
    if (!buf) { fclose(f); return NULL; }
    nread = fread(buf, 1U, (size_t)sz, f);
    fclose(f);
    if (nread != (size_t)sz) { free(buf); return NULL; }
    buf[nread] = '\0';
    return buf;
}

static const char *skip_ws(const char *p) { while (*p && isspace((unsigned char)*p)) p++; return p; }
static const char *find_key(const char *obj, const char *key) {
    static char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    return strstr(obj, needle);
}

static int parse_string_field(const char *obj, const char *key, char *out, size_t out_sz) {
    const char *p = find_key(obj, key); const char *q; size_t n;
    if (!p) return -1; p = strchr(p, ':'); if (!p) return -1; p = skip_ws(p + 1); if (*p != '"') return -1;
    p++; q = strchr(p, '"'); if (!q) return -1; n = (size_t)(q - p); if (n + 1U > out_sz) return -1;
    memcpy(out, p, n); out[n] = '\0'; return 0;
}
static int parse_int_field(const char *obj, const char *key, int *out) {
    const char *p = find_key(obj, key); char *endptr; long v;
    if (!p) return -1; p = strchr(p, ':'); if (!p) return -1; p = skip_ws(p + 1);
    v = strtol(p, &endptr, 10); if (endptr == p) return -1; *out = (int)v; return 0;
}
static int parse_bool_field(const char *obj, const char *key, int *out) {
    const char *p = find_key(obj, key); if (!p) return -1; p = strchr(p, ':'); if (!p) return -1; p = skip_ws(p + 1);
    if (strncmp(p, "true", 4) == 0) { *out = 1; return 0; }
    if (strncmp(p, "false", 5) == 0) { *out = 0; return 0; }
    return -1;
}

static int parse_bool_optional(const char *obj, const char *key, int *out) { if (parse_bool_field(obj, key, out) != 0) *out = 0; return 0; }
static int parse_int_optional(const char *obj, const char *key, int def, int *out) { if (parse_int_field(obj, key, out) != 0) *out = def; return 0; }
static int parse_string_optional(const char *obj, const char *key, char *out, size_t out_sz) { if (parse_string_field(obj,key,out,out_sz)!=0) out[0]='\0'; return 0; }

static void compute_flags(profile_entry *e) {
    e->flags = 0U;
    if (e->disable_memory_access) e->flags |= 0x0001U;
    if (e->force_internal_timer) e->flags |= 0x0002U;
    if (e->disable_internal_timer) e->flags |= 0x0004U;
    if (e->force_fips) e->flags |= 0x0008U;
    if (e->ntg1) e->flags |= 0x0010U;
    if (e->cache_all) e->flags |= 0x0020U;
}

static cpujitter_err parse_profile_file(const char *path, profile_entry *out) {
    char *json = read_text_file(path);
    const char *match;
    const char *cfg;
    if (!json || !out) { free(json); return CPUJITTER_ERR_IO; }
    memset(out, 0, sizeof(*out));

    if (parse_string_field(json, "id", out->id, sizeof(out->id)) != 0 ||
        parse_string_field(json, "os", out->os, sizeof(out->os)) != 0 ||
        parse_string_field(json, "arch", out->arch, sizeof(out->arch)) != 0 ||
        parse_string_field(json, "cpu_vendor", out->cpu_vendor, sizeof(out->cpu_vendor)) != 0 ||
        parse_int_field(json, "osr", &out->osr) != 0 ||
        parse_int_field(json, "smoke_bytes", &out->smoke_bytes) != 0) {
        free(json); return CPUJITTER_ERR_PARSE;
    }

    cfg = find_key(json, "runtime");
    if (cfg) {
        parse_bool_optional(cfg, "disable_memory_access", &out->disable_memory_access);
        parse_bool_optional(cfg, "force_internal_timer", &out->force_internal_timer);
        parse_bool_optional(cfg, "disable_internal_timer", &out->disable_internal_timer);
        parse_bool_optional(cfg, "force_fips", &out->force_fips);
        parse_bool_optional(cfg, "ntg1", &out->ntg1);
        parse_bool_optional(cfg, "cache_all", &out->cache_all);
        parse_int_optional(cfg, "max_memsize", 256, &out->max_memsize_kb);
        parse_int_optional(cfg, "hashloop", 1, &out->hashloop);
    } else {
        out->max_memsize_kb = 256;
        out->hashloop = 1;
    }

    match = find_key(json, "match");
    if (match) {
        parse_string_optional(match, "virtualization", out->virtualization, sizeof(out->virtualization));
        parse_string_optional(match, "cpu_model_exact", out->cpu_model_exact, sizeof(out->cpu_model_exact));
        parse_string_optional(match, "cpu_model_family", out->cpu_model_family, sizeof(out->cpu_model_family));
        parse_int_optional(match, "logical_cpu_min", 0, &out->logical_cpu_min);
        parse_int_optional(match, "logical_cpu_max", 0, &out->logical_cpu_max);
    }
    if (out->virtualization[0] == '\0') snprintf(out->virtualization, sizeof(out->virtualization), "baremetal");

    compute_flags(out);
    free(json);
    return CPUJITTER_OK;
}

static void dirname_of(const char *path, char *out, size_t out_sz) {
    const char *slash = strrchr(path, '/'); size_t n;
    if (!slash) { snprintf(out, out_sz, "."); return; }
    n = (size_t)(slash - path); if (n >= out_sz) n = out_sz - 1U; memcpy(out, path, n); out[n] = '\0';
}

cpujitter_err cpujitter_profiles_load(const char *index_path, profile_entry *out_entries, size_t out_cap, size_t *out_count) {
    char *json; const char *p; char base[256]; size_t count = 0;
    if (!index_path || !out_entries || !out_count) return CPUJITTER_ERR_INVALID_ARG;
    json = read_text_file(index_path); if (!json) return CPUJITTER_ERR_IO;
    dirname_of(index_path, base, sizeof(base));
    p = json;
    while ((p = strstr(p, "\"path\"")) != NULL) {
        const char *q = strchr(p, ':'); const char *e; char rel[192]; char full[384]; size_t n;
        if (!q) { free(json); return CPUJITTER_ERR_PARSE; }
        q = skip_ws(q + 1); if (*q != '"') { free(json); return CPUJITTER_ERR_PARSE; }
        q++; e = strchr(q, '"'); if (!e) { free(json); return CPUJITTER_ERR_PARSE; }
        n = (size_t)(e - q); if (n + 1U > sizeof(rel) || count >= out_cap) { free(json); return CPUJITTER_ERR_BUFFER_TOO_SMALL; }
        memcpy(rel, q, n); rel[n] = '\0';
        if (snprintf(full, sizeof(full), "%s/%s", base, rel) >= (int)sizeof(full)) { free(json); return CPUJITTER_ERR_PARSE; }
        if (parse_profile_file(full, &out_entries[count]) != CPUJITTER_OK) { free(json); return CPUJITTER_ERR_PARSE; }
        count++; p = e + 1;
    }
    free(json); *out_count = count; return count > 0 ? CPUJITTER_OK : CPUJITTER_ERR_NO_PROFILE;
}

cpujitter_err cpujitter_profiles_find_by_id(const profile_entry *entries, size_t count, const char *id, profile_entry *out_match) {
    size_t i; if (!entries || !id || !out_match) return CPUJITTER_ERR_INVALID_ARG;
    for (i = 0; i < count; i++) if (strcmp(entries[i].id, id) == 0) { *out_match = entries[i]; return CPUJITTER_OK; }
    return CPUJITTER_ERR_NO_PROFILE;
}

static int model_family_match(const char *pattern, const char *model) {
    size_t n; if (!pattern || !pattern[0]) return 0; n = strlen(pattern);
    if (pattern[n - 1] == '*') return strncmp(model, pattern, n - 1U) == 0;
    return strcmp(pattern, model) == 0;
}
static int virtualization_rank(const char *pv, const char *lv) { int lvm = strcmp(lv,"vm")==0; int pvm=strcmp(pv,"vm")==0; if (lvm) return pvm?40:-40; return pvm?-20:20; }
static int candidate_score(const profile_entry *e, const cpujitter_platform_info *p, char *why, size_t wl) {
    int score = 0; int exact = 0; int family = 0;
    if (strcmp(e->os,p->os)!=0||strcmp(e->arch,p->arch)!=0||strcmp(e->cpu_vendor,p->cpu_vendor)!=0){snprintf(why,wl,"failed core match");return -100000;}
    score += 500; score += virtualization_rank(e->virtualization,p->virtualization);
    if (e->cpu_model_exact[0]) { if (strcmp(e->cpu_model_exact,p->cpu_model)==0){exact=1;score+=400;} else {snprintf(why,wl,"failed exact model"); return -50000;} }
    else if (e->cpu_model_family[0]) { if (model_family_match(e->cpu_model_family,p->cpu_model)){family=1;score+=250;} else {snprintf(why,wl,"failed family model"); return -30000;} }
    if (e->logical_cpu_min>0 && p->logical_cpu_count<e->logical_cpu_min) return -20000;
    if (e->logical_cpu_max>0 && p->logical_cpu_count>e->logical_cpu_max) return -20000;
    snprintf(why, wl, "model=%s virt=%s", exact?"exact":(family?"family":"generic"), e->virtualization);
    return score;
}

cpujitter_err cpujitter_profiles_select_best(const profile_entry *entries,size_t count,const cpujitter_platform_info *platform,profile_entry *out_match,char *out_explanation,size_t out_explanation_len){
    size_t i,best_i=0; int best=-200000; char why[128];
    if(!entries||!platform||!out_match||count==0) return CPUJITTER_ERR_INVALID_ARG;
    if(out_explanation&&out_explanation_len) out_explanation[0]='\0';
    for(i=0;i<count;i++){int s=candidate_score(&entries[i],platform,why,sizeof(why)); if(s>best||(s==best&&strcmp(entries[i].id,entries[best_i].id)<0)){best=s;best_i=i; if(out_explanation&&out_explanation_len) snprintf(out_explanation,out_explanation_len,"selected=%s score=%d reason=%s",entries[i].id,s,why);}}
    if(best<0){ if(out_explanation&&out_explanation_len) snprintf(out_explanation,out_explanation_len,"no profile matched platform"); return CPUJITTER_ERR_NO_PROFILE; }
    *out_match=entries[best_i]; return CPUJITTER_OK;
}
