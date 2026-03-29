#include "qualifier_internal.h"

#include <stdio.h>

qualifier_err qualifier_export_profile(const char *path,
                                       const qualifier_selected_profile *profile) {
    FILE *f;

    if (!path || !profile) {
        return QUALIFIER_ERR_INVALID_ARG;
    }

    f = fopen(path, "wb");
    if (!f) {
        return QUALIFIER_ERR_EXPORT;
    }

    if (fprintf(f,
                "{\n"
                "  \"id\": \"%s\",\n"
                "  \"os\": \"%s\",\n"
                "  \"arch\": \"%s\",\n"
                "  \"cpu_vendor\": \"%s\",\n"
                "  \"osr\": %d,\n"
                "  \"mem_blocks\": %d,\n"
                "  \"mem_block_size\": %d,\n"
                "  \"smoke_bytes\": %d\n"
                "}\n",
                profile->id,
                profile->os,
                profile->arch,
                profile->cpu_vendor,
                profile->osr,
                profile->mem_blocks,
                profile->mem_block_size,
                profile->smoke_bytes) < 0) {
        fclose(f);
        return QUALIFIER_ERR_EXPORT;
    }

    fclose(f);
    return QUALIFIER_OK;
}
