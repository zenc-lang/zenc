// SPDX-License-Identifier: MIT
#include "plugin_manager.h"

#include "../compiler.h"
#include "../compat/c23_compat.h"
#include "../utils/colors.h"
#include "../constants.h"
#include "../diagnostics/diagnostics.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ZC_NO_PLUGINS

// Linked list node for plugins.
typedef struct PluginNode
{
    ZPlugin *plugin;
    void *handle; // dlopen handle (NULL for built-ins).
    struct PluginNode *next;
} PluginNode;

static PluginNode *head = NULL;

void zptr_plugin_mgr_init(void)
{
    head = NULL;
}

// Diagnostic wrappers for plugins
static ZEN_FORMAT_PRINTF(2, 3) void plugin_error(const ZApi *api, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char msg[MAX_ERROR_MSG_LEN];
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    Token t = {0};
    t.file = api->filename;
    t.line = api->current_line;
    t.col = 1;
    t.col = 1;
    zerror_at(t, "%s", msg);
}

static ZEN_FORMAT_PRINTF(2, 3) void plugin_warn(const ZApi *api, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char msg[MAX_ERROR_MSG_LEN];
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    Token t = {0};
    t.file = api->filename;
    t.line = api->current_line;
    t.col = 1;
    t.col = 1;
    zwarn_at(t, "%s", msg);
}

static ZEN_FORMAT_PRINTF(2, 3) void plugin_note(const ZApi *api, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char msg[MAX_ERROR_MSG_LEN];
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    // Using printf for nnote if we don't have a znote_at
    printf(COLOR_BOLD COLOR_CYAN "  note: " COLOR_RESET "%s:%d: %s\n", api->filename,
           api->current_line, msg);
}

void zptr_init_api(ZApi *api, const char *filename, int line, CompilerConfig *cfg)
{
    if (!api)
    {
        return;
    }

    api->api_version = ZEN_PLUGIN_API_VERSION;
    api->filename = filename ? filename : "input.zc";
    api->current_line = line;

    api->error = plugin_error;
    api->warn = plugin_warn;
    api->note = plugin_note;

    api->config.is_debug = cfg->mode_debug;
    api->config.verbose = cfg->verbose;
    api->config.target = ZC_OS_NAME;
    api->config.cc = cfg->cc;
}

void zptr_register_plugin(ZPlugin *plugin)
{
    if (!plugin)
    {
        return;
    }

    if (zptr_find_plugin(plugin->name))
    {
        return;
    }

    PluginNode *node = libc_malloc(sizeof(PluginNode));
    if (node)
    {
        node->plugin = plugin;
        node->handle = NULL;
        node->next = head;
        head = node;
    }
}

ZPlugin *zptr_load_plugin(const char *path)
{
    void *handle = z_dlopen(path);
    if (!handle)
    {
#ifdef ZC_STATIC_PLUGINS
        // Try to resolve path to a static plugin name
        // Path might be "plugins/name", "plugins/name.so", "plugins/name.zc" etc.
        const char *name_start = z_path_last_sep(path);
        if (name_start)
        {
            name_start++;
        }
        else
        {
            name_start = path;
        }

        char name[256];
        strncpy(name, name_start, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        char *dot = (char *)strchr(name, '.');
        if (dot)
        {
            *dot = '\0';
        }

        ZPlugin *sp = zptr_find_plugin(name);
        if (sp)
        {
            return sp;
        }
#endif
        return NULL;
    }

    ZPluginInitFn init_fn = (ZPluginInitFn)z_dlsym(handle, "z_plugin_init");
    if (!init_fn)
    {
        z_dlclose(handle);
        return NULL;
    }

    ZPlugin *plugin = init_fn();
    if (!plugin)
    {
        z_dlclose(handle);
        return NULL;
    }

    /* Hot-reload: Unload existing plugin with the same name if it exists */
    zptr_unload_plugin(plugin->name);

    // Register
    PluginNode *node = libc_malloc(sizeof(PluginNode));

    if (node)
    {
        node->plugin = plugin;
        node->handle = handle;
        node->next = head;
        head = node;
    }
    else
    {
        // Out of memory
        z_dlclose(handle);
        return NULL;
    }

    return plugin;
}

ZPlugin *zptr_find_plugin(const char *name)
{
    PluginNode *curr = head;
    while (curr)
    {
        if (strcmp(curr->plugin->name, name) == 0)
        {
            return curr->plugin;
        }
        curr = curr->next;
    }

    // Try finding in static built-ins if enabled
#ifdef ZC_STATIC_PLUGINS
    ZPlugin *p = zptr_get_static_plugin(name);
    if (p)
    {
        return p;
    }
#endif

    return NULL;
}

void zptr_plugin_mgr_cleanup(void)
{
    PluginNode *curr = head;
    while (curr)
    {
        PluginNode *next = curr->next;
        if (curr->handle)
        {
            z_dlclose(curr->handle);
        }
        libc_free(curr);
        curr = next;
    }
    head = NULL;
}

int zptr_unload_plugin(const char *name)
{
    PluginNode *curr = head;
    PluginNode *prev = NULL;

    while (curr)
    {
        if (strcmp(curr->plugin->name, name) == 0)
        {
            if (prev)
            {
                prev->next = curr->next;
            }
            else
            {
                head = curr->next;
            }

            if (curr->handle)
            {
                z_dlclose(curr->handle);
            }
            libc_free(curr);
            return 1;
        }
        prev = curr;
        curr = curr->next;
    }
    return 0;
}

#else

void zptr_plugin_mgr_init(void)
{
}
void zptr_register_plugin(ZPlugin *plugin)
{
    (void)plugin;
}
ZPlugin *zptr_load_plugin(const char *path)
{
    (void)path;
    return NULL;
}
ZPlugin *zptr_find_plugin(const char *name)
{
    (void)name;
    return NULL;
}
void zptr_init_api(ZApi *api, const char *filename, int line, CompilerConfig *cfg)
{
    (void)api;
    (void)filename;
    (void)line;
    (void)cfg;
}
void zptr_plugin_mgr_cleanup(void)
{
}
int zptr_unload_plugin(const char *name)
{
    (void)name;
    return 0;
}
ZEN_CONST ZPlugin *zptr_get_static_plugin(const char *name)
{
    (void)name;
    return NULL;
}

#endif
