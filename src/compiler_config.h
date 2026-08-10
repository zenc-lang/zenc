// SPDX-License-Identifier: MIT
#ifndef COMPILER_CONFIG_H
#define COMPILER_CONFIG_H

/**
 * @file compiler_config.h
 * @brief Compiler configuration structs — no OS/platform dependency.
 *
 * This is the minimal header for CompilerConfig and ZenCompiler.
 * It does NOT include platform/os.h or utils/colors.h.
 * Use compiler.h (which includes this) when you also need globals.
 */

#include <stdint.h>
#include <stddef.h>
#include "utils/zvec.h"
#include "utils/zalloc.h"

ZVEC_GENERATE_IMPL(char *, Str)

#ifndef ZEN_VERSION
#define ZEN_VERSION "0.1.0"
#endif

enum
{
    MAX_FLAGS_SIZE = 1024,
    MAX_PATH_SIZE = 1024,
    MAX_PATTERN_SIZE = 1024
};

typedef struct CompilerConfig
{
    char *input_file;
    zvec_Str extra_files;
    zvec_Str c_files;
    char *output_file;

    int mode_run;
    int mode_debug;
    int mode_check;
    int mode_transpile;
    int emit_c;
    int verbose;
    int quiet;
    int mode_doc;
    int repl_mode;
    int is_freestanding;
    int use_cpp;
    int use_cuda;
    int use_objc;
    int mode_lsp;
    int json_output;
    int use_typecheck;
    int warn_as_errors;
    int no_suppress_warnings;
    int misra_mode;
    uint64_t diag_mask;

    int keep_comments;
    int recursive_doc;

    char gcc_flags[4096];
    char cc[256];

    char **c_function_whitelist;
    char **c_type_whitelist;

    zvec_Str cfg_defines;
    zvec_Str include_paths;

    char *root_path;
    char *input_dir;
    int std_locked;
    char std_root[MAX_PATH_SIZE];
    const char *backend_name;
    zvec_Str backend_opts;
} CompilerConfig;

typedef struct ZenCompiler
{
    CompilerConfig config;
    struct zarena arena;
    int error_count;
    int warning_count;
    char link_flags[1024];
    char cflags[1024];
    double start_time;
} ZenCompiler;

#endif // COMPILER_CONFIG_H
