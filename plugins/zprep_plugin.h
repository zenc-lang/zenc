
#ifndef ZPREP_PLUGIN_H
#define ZPREP_PLUGIN_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define ZEN_PLUGIN_API_VERSION 2
#define ZEN_PLUGIN_MAX_NAME 256

/**
 * @brief Host API provided to plugins.
 *
 * Plugins use this structure to interact with the compiler/codegen environment.
 */
typedef struct ZApi
{
    uint32_t api_version; ///< Version of the host API.

    // Context Information (Where are we?).
    const char *filename; ///< Current file name being processed.
    int current_line;     ///< Current line number.
    FILE *out;            ///< Inline output stream (replaces the macro call).
    FILE *hoist_out;      ///< Hoisted output stream (writes to file scope/header).

    // Diagnostic Interface
    void (*error)(const struct ZApi *api, const char *fmt, ...);
    void (*warn)(const struct ZApi *api, const char *fmt, ...);
    void (*note)(const struct ZApi *api, const char *fmt, ...);

    // Host configuration info
    struct
    {
        int is_debug;       ///< Whether the host is in debug mode.
        int verbose;        ///< Whether the host is in verbose mode.
        const char *target; ///< Target operating system name.
        const char *cc;     ///< C compiler being used by the host.
    } config;

    void *user_data; ///< Reserved for future use or host-specific data.
} ZApi;

/**
 * @brief The Plugin Function Signature.
 *
 * Plugins must implement a function with this signature to handle transpilation.
 *
 * @param input_body The raw text content inside the plugin call.
 * @param api Pointer to the host API structure.
 */
typedef void (*ZPluginTranspileFn)(const char *input_body, const ZApi *api);

/**
 * @brief The Plugin LSP Hover Function Signature.
 *
 * @param input_body The raw text content inside the plugin call.
 * @param line The line number inside the plugin block (0-indexed).
 * @param col The column number inside the plugin block (0-indexed).
 * @return const char* Markdown documentation or NULL.
 */
typedef const char *(*ZPluginHoverFn)(const char *input_body, int line, int col);

/**
 * @brief Plugin definition structure.
 */
typedef struct
{
    char name[ZEN_PLUGIN_MAX_NAME]; ///< Name of the plugin.
    ZPluginTranspileFn fn;          ///< Pointer to the transpilation function.
    ZPluginHoverFn hover_fn;        ///< Pointer to the LSP hover function (optional).
} ZPlugin;

/**
 * @brief Signature for the plugin entry point.
 *
 * Dynamic libraries must export a function named `z_plugin_init` matching this signature.
 */
typedef ZPlugin *(*ZPluginInitFn)(void);

#endif
