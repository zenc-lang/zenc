// SPDX-License-Identifier: MIT
#include "zprep.h"
#include "constants.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper to append strings to a global whitelist
static void append_to_whitelist(char ***whitelist_ptr, cJSON *items)
{
    if (!cJSON_IsArray(items))
    {
        return;
    }

    int new_count = cJSON_GetArraySize(items);
    if (new_count == 0)
    {
        return;
    }

    int current_count = 0;
    if (*whitelist_ptr)
    {
        char **ptr = *whitelist_ptr;
        while (*ptr)
        {
            current_count++;
            ptr++;
        }
    }

    size_t new_size = sizeof(char *) * (size_t)(current_count + new_count + 1);
    *whitelist_ptr = xrealloc(*whitelist_ptr, new_size);

    int added = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, items)
    {
        if (cJSON_IsString(item) && item->valuestring)
        {
            (*whitelist_ptr)[current_count + added] = xstrdup(item->valuestring);
            added++;
        }
    }
    (*whitelist_ptr)[current_count + added] = NULL;
}

static int load_config_file(const char *path, CompilerConfig *cfg)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    if (length < 0)
    {
        fclose(f);
        return 0;
    }
    if (length < 0)
    {
        fclose(f);
        return 0;
    }
    fseek(f, 0, SEEK_SET);

    // Use standard malloc/free for temporary file buffer to avoid arena pollution
    char *data = malloc((size_t)(length + 1));
    if (!data)
    {
        fclose(f);
        return 0;
    }

    size_t nread = fread(data, 1, (size_t)(length), f);
    if (nread != (size_t)(length))
    {
        zfree(data);
        fclose(f);
        return 0;
    }
    data[length] = '\0';
    fclose(f);

    cJSON *json = cJSON_Parse(data);
    zfree(data);

    if (json)
    {
        cJSON *c_funcs = cJSON_GetObjectItemCaseSensitive(json, "c_functions");
        if (c_funcs)
        {
            append_to_whitelist(&cfg->c_function_whitelist, c_funcs);
        }
        cJSON *c_types = cJSON_GetObjectItemCaseSensitive(json, "c_types");
        if (c_types)
        {
            append_to_whitelist(&cfg->c_type_whitelist, c_types);
        }
        cJSON_Delete(json);
        return 1;
    }
    return 0;
}

void load_all_configs(CompilerConfig *cfg)
{
    // 1. System-wide config
    int loaded_system = 0;
    char *root = getenv("ZC_ROOT");
    if (root)
    {
        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s/zenc.json", root);
        if (load_config_file(path, cfg))
        {
            loaded_system = 1;
        }
    }

#ifdef ZEN_SHARE_DIR
    if (!loaded_system)
    {
        char system_path[MAX_PATH_LEN];
        snprintf(system_path, sizeof(system_path), "%s/zenc.json", ZEN_SHARE_DIR);
        load_config_file(system_path, cfg);
    }
#endif

    // 2. Hidden project config
    load_config_file(".zenc.json", cfg);

    // 3. Visible project config (legacy/override)
    load_config_file("zenc.json", cfg);
}
