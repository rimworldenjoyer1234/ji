#include "qualifier_internal.h"

#include <stdio.h>

static void copy_s(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) {
        return;
    }
    snprintf(dst, dst_sz, "%s", src ? src : "unknown");
}

void qualifier_detect_platform(qualifier_platform_info *out_info) {
    if (!out_info) {
        return;
    }

#if defined(_WIN32)
    copy_s(out_info->os, sizeof(out_info->os), "windows");
#elif defined(__APPLE__)
    copy_s(out_info->os, sizeof(out_info->os), "macos");
#elif defined(__linux__)
    copy_s(out_info->os, sizeof(out_info->os), "linux");
#else
    copy_s(out_info->os, sizeof(out_info->os), "unknown");
#endif

#if defined(__x86_64__) || defined(_M_X64)
    copy_s(out_info->arch, sizeof(out_info->arch), "x86_64");
#elif defined(__aarch64__)
    copy_s(out_info->arch, sizeof(out_info->arch), "aarch64");
#elif defined(__i386__) || defined(_M_IX86)
    copy_s(out_info->arch, sizeof(out_info->arch), "x86");
#else
    copy_s(out_info->arch, sizeof(out_info->arch), "unknown");
#endif

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    copy_s(out_info->cpu_vendor, sizeof(out_info->cpu_vendor), "generic-x86");
#elif defined(__aarch64__)
    copy_s(out_info->cpu_vendor, sizeof(out_info->cpu_vendor), "generic-arm");
#else
    copy_s(out_info->cpu_vendor, sizeof(out_info->cpu_vendor), "generic");
#endif
}
