// SPDX-License-Identifier: MIT
#include "lsp_config.h"
#include "../utils/cJSON.h"
#include "../utils/utils.h"
#include "../constants.h"
#include "../platform/os.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CONFIG_FILE_NAME "zenc.server.json"

typedef struct
{
    const char *key;
    // Applies the value to `cfg` and returns a short summary of what it loaded ("" if nothing).
    const char *(*apply)(const cJSON *value, const char *root_path, CompilerConfig *cfg,
                         LSPProject *project);
} ConfigKey;

// Set after the first load. An arena rebuild loads the file again, and the client has already
// been told about its problems.
static int g_reported = 0;

// Log a problem to stderr and, on the first load, tell the client about it.
static void config_warn(const char *fmt, ...) ZEN_FORMAT_PRINTF(1, 2);

static void config_warn(const char *fmt, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    fprintf(stderr, "zls: %s\n", msg);
    if (g_reported)
    {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", "window/showMessage");
    cJSON *params = cJSON_AddObjectToObject(root, "params");
    cJSON_AddNumberToObject(params, "type", 2);
    cJSON_AddStringToObject(params, "message", msg);

    char *str = cJSON_PrintUnformatted(root);
    if (str)
    {
        fprintf(stdout, "Content-Length: %zu\r\n\r\n%s", strlen(str), str);
        fflush(stdout);
        zfree(str);
    }
    cJSON_Delete(root);
}

static const char *apply_include_paths(const cJSON *value, const char *root_path,
                                       CompilerConfig *cfg, LSPProject *project)
{
    (void)project;
    if (!cJSON_IsArray(value))
    {
        config_warn(CONFIG_FILE_NAME ": \"include_paths\" must be an array of strings, ignored");
        return "";
    }

    int count = 0;
    int index = 0;
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, value)
    {
        if (!cJSON_IsString(entry) || !entry->valuestring[0])
        {
            config_warn(CONFIG_FILE_NAME ": \"include_paths\"[%d] is not a non-empty string, "
                                         "ignored",
                        index);
            index++;
            continue;
        }
        index++;

        const char *dir = entry->valuestring;
        char path[MAX_PATH_LEN];
        if (z_is_abs_path(dir))
        {
            snprintf(path, sizeof(path), "%s", dir);
        }
        else
        {
            snprintf(path, sizeof(path), "%s/%s", root_path, dir);
        }
        zvec_push_Str(&cfg->include_paths, xstrdup(path));
        count++;
    }

    char *summary = xmalloc(64);
    snprintf(summary, 64, "%d include path(s)", count);
    return summary;
}

static const char *apply_exclude(const cJSON *value, const char *root_path, CompilerConfig *cfg,
                                 LSPProject *project)
{
    (void)root_path;
    (void)cfg;
    if (!cJSON_IsArray(value))
    {
        config_warn(CONFIG_FILE_NAME ": \"exclude\" must be an array of strings, ignored");
        return "";
    }

    int count = 0;
    int index = 0;
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, value)
    {
        if (!cJSON_IsString(entry) || !entry->valuestring[0])
        {
            config_warn(CONFIG_FILE_NAME ": \"exclude\"[%d] is not a non-empty string, ignored",
                        index);
        }
        else
        {
            zvec_push_Str(&project->exclude_patterns, xstrdup(entry->valuestring));
            count++;
        }
        index++;
    }

    char *summary = xmalloc(64);
    snprintf(summary, 64, "%d exclude pattern(s)", count);
    return summary;
}

static const char *apply_use_gitignore(const cJSON *value, const char *root_path,
                                       CompilerConfig *cfg, LSPProject *project)
{
    (void)root_path;
    (void)cfg;
    if (!cJSON_IsBool(value))
    {
        config_warn(CONFIG_FILE_NAME ": \"use_gitignore\" must be true or false, ignored");
        return "";
    }
    project->use_gitignore = cJSON_IsTrue(value);
    return project->use_gitignore ? "" : ".gitignore not used";
}

static const ConfigKey CONFIG_KEYS[] = {
    {"include_paths", apply_include_paths},
    {"exclude", apply_exclude},
    {"use_gitignore", apply_use_gitignore},
};

static int line_of(const char *source, const char *at)
{
    int line = 1;
    for (const char *p = source; p < at && *p; p++)
    {
        if (*p == '\n')
        {
            line++;
        }
    }
    return line;
}

static void load_config(const char *root_path, CompilerConfig *cfg, LSPProject *project)
{
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/" CONFIG_FILE_NAME, root_path);

    char *source = load_file(path, NULL);
    if (!source)
    {
        fprintf(stderr, "zls: no " CONFIG_FILE_NAME "\n");
        return;
    }

    cJSON *json = cJSON_Parse(source);
    if (!json)
    {
        const char *err = cJSON_GetErrorPtr();
        if (err && err >= source && err <= source + strlen(source))
        {
            config_warn(CONFIG_FILE_NAME ": invalid JSON at line %d, file ignored",
                        line_of(source, err));
        }
        else
        {
            config_warn(CONFIG_FILE_NAME ": invalid JSON, file ignored");
        }
        return;
    }

    if (!cJSON_IsObject(json))
    {
        config_warn(CONFIG_FILE_NAME ": top level must be a JSON object, file ignored");
        cJSON_Delete(json);
        return;
    }

    char summary[256] = "";
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, json)
    {
        const ConfigKey *known = NULL;
        for (size_t i = 0; i < sizeof(CONFIG_KEYS) / sizeof(CONFIG_KEYS[0]); i++)
        {
            if (item->string && strcmp(item->string, CONFIG_KEYS[i].key) == 0)
            {
                known = &CONFIG_KEYS[i];
                break;
            }
        }

        if (!known)
        {
            config_warn(CONFIG_FILE_NAME ": unknown key \"%s\", ignored",
                        item->string ? item->string : "");
            continue;
        }

        const char *what = known->apply(item, root_path, cfg, project);
        if (what[0])
        {
            size_t used = strlen(summary);
            snprintf(summary + used, sizeof(summary) - used, "%s%s", used ? ", " : "", what);
        }
    }

    fprintf(stderr, "zls: " CONFIG_FILE_NAME ": %s\n", summary[0] ? summary : "nothing to apply");
    cJSON_Delete(json);
}

void lsp_config_load(const char *root_path, CompilerConfig *cfg, LSPProject *project)
{
    load_config(root_path, cfg, project);
    g_reported = 1;
}
