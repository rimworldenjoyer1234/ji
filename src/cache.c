#include "cpujitter_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CACHE_BYTES 8192
#define CPUJITTER_CACHE_SCHEMA_VERSION 2

typedef struct cache_fingerprint {
    char os[32], arch[32], cpu_vendor[32], cpu_model[64], virtualization[16];
    int logical_cpu_count;
} cache_fingerprint;

static const char *skip_ws(const char *p){while(*p&&isspace((unsigned char)*p))p++;return p;}
static const char *find_key(const char *obj,const char *key){static char n[128];snprintf(n,sizeof(n),"\"%s\"",key);return strstr(obj,n);} 
static int parse_string_field(const char *obj,const char *key,char *out,size_t out_sz){const char *p=find_key(obj,key),*q;size_t n;if(!p)return -1;p=skip_ws(p+strlen(key)+2);if(*p!=':')return -1;p=skip_ws(p+1);if(*p!='"')return -1;p++;q=strchr(p,'"');if(!q)return -1;n=(size_t)(q-p);if(n+1U>out_sz)return -1;memcpy(out,p,n);out[n]='\0';return 0;}
static int parse_int_field(const char *obj,const char *key,int *out){const char *p=find_key(obj,key);char *e;long v;if(!p)return -1;p=skip_ws(p+strlen(key)+2);if(*p!=':')return -1;p=skip_ws(p+1);v=strtol(p,&e,10);if(e==p)return -1;*out=(int)v;return 0;}
static int parse_bool_field(const char *obj,const char *key,int *out){const char *p=find_key(obj,key);if(!p)return -1;p=strchr(p,':');if(!p)return -1;p=skip_ws(p+1);if(!strncmp(p,"true",4)){*out=1;return 0;}if(!strncmp(p,"false",5)){*out=0;return 0;}return -1;}
static int extract_section(const char *json,const char *key,char *out,size_t out_sz){const char *k=strstr(json,key),*s,*p;int d=0;size_t n;if(!k)return -1;s=strchr(k,'{');if(!s)return -1;p=s;while(*p){if(*p=='{')d++;else if(*p=='}'){d--;if(!d){p++;break;}}p++;}if(d)return -1;n=(size_t)(p-s);if(n+1U>out_sz)return -1;memcpy(out,s,n);out[n]='\0';return 0;}

static cpujitter_err read_cache(const char *path,char *buf,size_t sz){FILE *f=fopen(path,"rb");size_t n;if(!f)return CPUJITTER_ERR_IO;n=fread(buf,1U,sz-1U,f);fclose(f);if(n==0)return CPUJITTER_ERR_PARSE;buf[n]='\0';return CPUJITTER_OK;}
static cpujitter_err parse_fingerprint(const char *json,cache_fingerprint *fp){char sec[MAX_CACHE_BYTES];if(extract_section(json,"\"platform_fingerprint\"",sec,sizeof(sec))!=0)return CPUJITTER_ERR_PARSE;memset(fp,0,sizeof(*fp));if(parse_string_field(sec,"os",fp->os,sizeof(fp->os))||parse_string_field(sec,"arch",fp->arch,sizeof(fp->arch))||parse_string_field(sec,"cpu_vendor",fp->cpu_vendor,sizeof(fp->cpu_vendor))||parse_string_field(sec,"cpu_model",fp->cpu_model,sizeof(fp->cpu_model))||parse_string_field(sec,"virtualization",fp->virtualization,sizeof(fp->virtualization))||parse_int_field(sec,"logical_cpu_count",&fp->logical_cpu_count))return CPUJITTER_ERR_PARSE;return CPUJITTER_OK;}
static int fp_match(const cache_fingerprint *f,const cpujitter_platform_info *p){return !strcmp(f->os,p->os)&&!strcmp(f->arch,p->arch)&&!strcmp(f->cpu_vendor,p->cpu_vendor)&&!strcmp(f->cpu_model,p->cpu_model)&&!strcmp(f->virtualization,p->virtualization)&&f->logical_cpu_count==p->logical_cpu_count;}

cpujitter_err cpujitter_cache_validate_platform(const char *path,const cpujitter_platform_info *platform){char buf[MAX_CACHE_BYTES];cache_fingerprint fp;int sv;cpujitter_err e;if(!path||!platform)return CPUJITTER_ERR_INVALID_ARG;e=read_cache(path,buf,sizeof(buf));if(e!=CPUJITTER_OK)return e;if(parse_int_field(buf,"schema_version",&sv)!=0)return CPUJITTER_ERR_PARSE;if(sv!=CPUJITTER_CACHE_SCHEMA_VERSION)return CPUJITTER_ERR_NO_PROFILE;e=parse_fingerprint(buf,&fp);if(e!=CPUJITTER_OK)return e;return fp_match(&fp,platform)?CPUJITTER_OK:CPUJITTER_ERR_NO_PROFILE;}

cpujitter_err cpujitter_cache_load(const char *path, profile_entry *out_entry){char buf[MAX_CACHE_BYTES],sec[MAX_CACHE_BYTES];cpujitter_err e;if(!path||!out_entry)return CPUJITTER_ERR_INVALID_ARG;e=read_cache(path,buf,sizeof(buf));if(e!=CPUJITTER_OK)return e;if(extract_section(buf,"\"validated_profile\"",sec,sizeof(sec))!=0)return CPUJITTER_ERR_PARSE;memset(out_entry,0,sizeof(*out_entry));
    if(parse_string_field(sec,"id",out_entry->id,sizeof(out_entry->id))||parse_string_field(sec,"os",out_entry->os,sizeof(out_entry->os))||parse_string_field(sec,"arch",out_entry->arch,sizeof(out_entry->arch))||parse_string_field(sec,"cpu_vendor",out_entry->cpu_vendor,sizeof(out_entry->cpu_vendor))||parse_int_field(sec,"osr",&out_entry->osr)||parse_int_field(sec,"smoke_bytes",&out_entry->smoke_bytes)||parse_int_field(sec,"max_memsize",&out_entry->max_memsize_kb)||parse_int_field(sec,"hashloop",&out_entry->hashloop)) return CPUJITTER_ERR_PARSE;
    (void)parse_string_field(sec,"virtualization",out_entry->virtualization,sizeof(out_entry->virtualization));
    (void)parse_string_field(sec,"cpu_model_exact",out_entry->cpu_model_exact,sizeof(out_entry->cpu_model_exact));
    (void)parse_string_field(sec,"cpu_model_family",out_entry->cpu_model_family,sizeof(out_entry->cpu_model_family));
    (void)parse_int_field(sec,"logical_cpu_min",&out_entry->logical_cpu_min);
    (void)parse_int_field(sec,"logical_cpu_max",&out_entry->logical_cpu_max);
    (void)parse_bool_field(sec,"disable_memory_access",&out_entry->disable_memory_access);
    (void)parse_bool_field(sec,"force_internal_timer",&out_entry->force_internal_timer);
    (void)parse_bool_field(sec,"disable_internal_timer",&out_entry->disable_internal_timer);
    (void)parse_bool_field(sec,"force_fips",&out_entry->force_fips);
    (void)parse_bool_field(sec,"ntg1",&out_entry->ntg1);
    (void)parse_bool_field(sec,"cache_all",&out_entry->cache_all);
    out_entry->flags=0; if(out_entry->disable_memory_access)out_entry->flags|=0x0001U; if(out_entry->force_internal_timer)out_entry->flags|=0x0002U; if(out_entry->disable_internal_timer)out_entry->flags|=0x0004U; if(out_entry->force_fips)out_entry->flags|=0x0008U; if(out_entry->ntg1)out_entry->flags|=0x0010U; if(out_entry->cache_all)out_entry->flags|=0x0020U;
    return CPUJITTER_OK;
}

cpujitter_err cpujitter_cache_load_validated(const char *path,const cpujitter_platform_info *platform,profile_entry *out_entry){cpujitter_err e=cpujitter_cache_validate_platform(path,platform);if(e!=CPUJITTER_OK)return e;return cpujitter_cache_load(path,out_entry);} 

cpujitter_err cpujitter_cache_save(const char *path,const profile_entry *e,const cpujitter_platform_info *p){FILE *f;if(!path||!e||!p)return CPUJITTER_ERR_INVALID_ARG;f=fopen(path,"wb");if(!f)return CPUJITTER_ERR_IO;
    if(fprintf(f,"{\n  \"schema_version\": %d,\n  \"platform_fingerprint\": {\n    \"os\": \"%s\",\n    \"arch\": \"%s\",\n    \"cpu_vendor\": \"%s\",\n    \"cpu_model\": \"%s\",\n    \"virtualization\": \"%s\",\n    \"logical_cpu_count\": %d\n  },\n  \"validated_profile\": {\n    \"id\": \"%s\",\n    \"os\": \"%s\",\n    \"arch\": \"%s\",\n    \"cpu_vendor\": \"%s\",\n    \"virtualization\": \"%s\",\n    \"cpu_model_exact\": \"%s\",\n    \"cpu_model_family\": \"%s\",\n    \"logical_cpu_min\": %d,\n    \"logical_cpu_max\": %d,\n    \"osr\": %d,\n    \"disable_memory_access\": %s,\n    \"force_internal_timer\": %s,\n    \"disable_internal_timer\": %s,\n    \"force_fips\": %s,\n    \"ntg1\": %s,\n    \"cache_all\": %s,\n    \"max_memsize\": %d,\n    \"hashloop\": %d,\n    \"smoke_bytes\": %d\n  }\n}\n",CPUJITTER_CACHE_SCHEMA_VERSION,p->os,p->arch,p->cpu_vendor,p->cpu_model,p->virtualization,p->logical_cpu_count,e->id,e->os,e->arch,e->cpu_vendor,e->virtualization,e->cpu_model_exact,e->cpu_model_family,e->logical_cpu_min,e->logical_cpu_max,e->osr,e->disable_memory_access?"true":"false",e->force_internal_timer?"true":"false",e->disable_internal_timer?"true":"false",e->force_fips?"true":"false",e->ntg1?"true":"false",e->cache_all?"true":"false",e->max_memsize_kb,e->hashloop,e->smoke_bytes)<0){fclose(f);return CPUJITTER_ERR_IO;} fclose(f); return CPUJITTER_OK; }
