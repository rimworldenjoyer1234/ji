#include "cpujitter_internal.h"

#include <stdio.h>
#include <string.h>

static void safe_copy(char *dst, size_t dst_sz, const char *src) {
    if (dst_sz == 0) {
        return;
    }
    snprintf(dst, dst_sz, "%s", src ? src : "unknown");
}

void cpujitter_detect_platform(cpujitter_platform_info *out_info) {
    if (!out_info) {
        return;
    }

#if defined(_WIN32)
    safe_copy(out_info->os, sizeof(out_info->os), "windows");
#elif defined(__APPLE__)
    safe_copy(out_info->os, sizeof(out_info->os), "macos");
#elif defined(__linux__)
    safe_copy(out_info->os, sizeof(out_info->os), "linux");
#else
    safe_copy(out_info->os, sizeof(out_info->os), "unknown");
#endif

#if defined(__x86_64__) || defined(_M_X64)
    safe_copy(out_info->arch, sizeof(out_info->arch), "x86_64");
#elif defined(__aarch64__)
    safe_copy(out_info->arch, sizeof(out_info->arch), "aarch64");
#elif defined(__i386__) || defined(_M_IX86)
    safe_copy(out_info->arch, sizeof(out_info->arch), "x86");
#else
    safe_copy(out_info->arch, sizeof(out_info->arch), "unknown");
#endif

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    safe_copy(out_info->cpu_vendor, sizeof(out_info->cpu_vendor), "generic-x86");
#elif defined(__aarch64__)
    safe_copy(out_info->cpu_vendor, sizeof(out_info->cpu_vendor), "generic-arm");
#else
    safe_copy(out_info->cpu_vendor, sizeof(out_info->cpu_vendor), "generic");
#endif
}
