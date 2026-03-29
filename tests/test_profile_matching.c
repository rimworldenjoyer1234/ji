#include "../src/cpujitter_internal.h"

#include <stdio.h>
#include <string.h>

static profile_entry mk(const char *id,
                        const char *virt,
                        const char *model_exact,
                        const char *model_family) {
    profile_entry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.id, sizeof(e.id), "%s", id);
    snprintf(e.os, sizeof(e.os), "linux");
    snprintf(e.arch, sizeof(e.arch), "x86_64");
    snprintf(e.cpu_vendor, sizeof(e.cpu_vendor), "generic-x86");
    snprintf(e.virtualization, sizeof(e.virtualization), "%s", virt);
    snprintf(e.cpu_model_exact, sizeof(e.cpu_model_exact), "%s", model_exact ? model_exact : "");
    snprintf(e.cpu_model_family, sizeof(e.cpu_model_family), "%s", model_family ? model_family : "");
    e.logical_cpu_min = 1;
    e.logical_cpu_max = 128;
    e.osr = 2;
    e.mem_blocks = 64;
    e.mem_block_size = 64;
    e.smoke_bytes = 32;
    return e;
}

static cpujitter_platform_info platform_baremetal(void) {
    cpujitter_platform_info p;
    memset(&p, 0, sizeof(p));
    snprintf(p.os, sizeof(p.os), "linux");
    snprintf(p.arch, sizeof(p.arch), "x86_64");
    snprintf(p.cpu_vendor, sizeof(p.cpu_vendor), "generic-x86");
    snprintf(p.cpu_model, sizeof(p.cpu_model), "x86-generic");
    snprintf(p.virtualization, sizeof(p.virtualization), "baremetal");
    p.logical_cpu_count = 8;
    return p;
}

static int test_exact_over_family(void) {
    profile_entry entries[2];
    profile_entry out;
    char why[256];
    cpujitter_platform_info p = platform_baremetal();

    entries[0] = mk("family", "baremetal", "", "x86-*");
    entries[1] = mk("exact", "baremetal", "x86-generic", "");

    if (cpujitter_profiles_select_best(entries, 2, &p, &out, why, sizeof(why)) != CPUJITTER_OK) {
        return 1;
    }
    return strcmp(out.id, "exact") == 0 ? 0 : 1;
}

static int test_baremetal_preferred(void) {
    profile_entry entries[2];
    profile_entry out;
    char why[256];
    cpujitter_platform_info p = platform_baremetal();

    entries[0] = mk("vm", "vm", "", "x86-*");
    entries[1] = mk("bm", "baremetal", "", "x86-*");

    if (cpujitter_profiles_select_best(entries, 2, &p, &out, why, sizeof(why)) != CPUJITTER_OK) {
        return 1;
    }
    return strcmp(out.id, "bm") == 0 ? 0 : 1;
}

static int test_vm_preferred_on_vm(void) {
    profile_entry entries[2];
    profile_entry out;
    char why[256];
    cpujitter_platform_info p = platform_baremetal();
    snprintf(p.virtualization, sizeof(p.virtualization), "vm");

    entries[0] = mk("vm", "vm", "", "x86-*");
    entries[1] = mk("bm", "baremetal", "", "x86-*");

    if (cpujitter_profiles_select_best(entries, 2, &p, &out, why, sizeof(why)) != CPUJITTER_OK) {
        return 1;
    }
    return strcmp(out.id, "vm") == 0 ? 0 : 1;
}

static int test_stable_tiebreak(void) {
    profile_entry entries[2];
    profile_entry out;
    char why[256];
    cpujitter_platform_info p = platform_baremetal();

    entries[0] = mk("b-profile", "baremetal", "", "x86-*");
    entries[1] = mk("a-profile", "baremetal", "", "x86-*");

    if (cpujitter_profiles_select_best(entries, 2, &p, &out, why, sizeof(why)) != CPUJITTER_OK) {
        return 1;
    }
    return strcmp(out.id, "a-profile") == 0 ? 0 : 1;
}

int main(void) {
    int failed = 0;
    failed |= test_exact_over_family();
    failed |= test_baremetal_preferred();
    failed |= test_vm_preferred_on_vm();
    failed |= test_stable_tiebreak();

    if (failed) {
        fprintf(stderr, "profile matching tests failed\n");
        return 1;
    }
    printf("profile matching tests passed\n");
    return 0;
}
