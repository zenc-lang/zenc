// SPDX-License-Identifier: MIT
#include <ctype.h>
#include "../constants.h"
#include "cmd.h"
#include "colors.h"
#include "../zprep.h"
#include "../codegen/compat.h"
#include "../zen/zen_facts.h"
#include "../platform/os.h"
#include "../parser/parser.h"

void print_search_paths(CompilerConfig *cfg)
{
    printf("Search paths:\n");
    for (size_t i = 0; i < cfg->include_paths.length; i++)
    {
        printf("  %s\n", cfg->include_paths.data[i]);
    }
    if (cfg->root_path)
    {
        printf("  %s\n", cfg->root_path);
        printf("  %s/std\n", cfg->root_path);
    }
    printf("  ./\n");
    if (cfg->root_path)
    {
        printf("  %s\n", cfg->root_path);
        printf("  %s/std\n", cfg->root_path);
    }
}

void print_version(void)
{
    printf(COLOR_BOLD "zc" COLOR_RESET " %s\n", ZEN_VERSION);
}

static int get_visible_length(const char *str)
{
    int len = 0;
    const char *p = str;
    while (*p)
    {
        if (*p == '\033' && *(p + 1) == '[')
        {
            p += 2;
            while (*p && *p != 'm')
            {
                p++;
            }
            if (*p == 'm')
            {
                p++;
            }
        }
        else
        {
            len++;
            p++;
        }
    }
    return len;
}

static void print_help_item(const char *option, const char *description)
{
    printf("  %s", option);
    int visible_len = get_visible_length(option) + 2; // +2 for the leading spaces
    int target_col = 22;                              // Reduced from 30

    if (visible_len >= target_col - 1)
    {
        printf("\n%-*s", target_col, "");
    }
    else
    {
        for (int i = 0; i < target_col - visible_len; i++)
        {
            putchar(' ');
        }
    }
    printf("%s\n", description);
}

void print_usage(void)
{
    printf(
        "usage: zc [-v | -h | -q | -V] [-I | -L | -l | -D <path/macro>] [--cc <c>] [-O<l>] [-g]\n");
    printf("          [--release] [--json] [--paths] [--zen] <command> [<args>]\n\n");

    printf("common commands:\n");
    print_help_item(COLOR_GREEN "build, run" COLOR_RESET,
                    "Compile program (default / run immediately)");
    print_help_item(COLOR_GREEN "check, transpile" COLOR_RESET,
                    "Type check only / generate C code");
    print_help_item(COLOR_GREEN "repl, lsp, doc" COLOR_RESET,
                    "REPL / Language Server / Documentation");

    printf("\ncommon options:\n");
    print_help_item(COLOR_CYAN "-o <f>, --cc <c>" COLOR_RESET,
                    "Set output name / backend compiler");
    print_help_item(COLOR_CYAN "-I, -L, -l <p>" COLOR_RESET, "Include/Library paths and linking");
    print_help_item(COLOR_CYAN "-D <n>, -O<l>" COLOR_RESET,
                    "Define macro / Set optimization level");
    print_help_item(COLOR_CYAN "-g, -g0, --release" COLOR_RESET,
                    "Debug info (on/off) or Release mode");
    print_help_item(COLOR_CYAN "-v, --verbose" COLOR_RESET, "Show granular compiler phases");
    print_help_item(COLOR_CYAN "-q, --quiet" COLOR_RESET, "Suppress all non-error output");
    print_help_item(COLOR_CYAN "--json" COLOR_RESET, "Emit structured JSON diagnostics");

    printf("\nlanguage & advanced:\n");
    print_help_item(COLOR_CYAN "--check, --free" COLOR_RESET,
                    "Borrow checker / No standard library");
    print_help_item(COLOR_CYAN "--misra" COLOR_RESET, "Generate strictly MISRA C compliant code");
    print_help_item(COLOR_CYAN "--cpp, --cuda" COLOR_RESET, "C++ or CUDA compatibility modes");
    print_help_item(COLOR_CYAN "-c, -S, -E, -shared" COLOR_RESET,
                    "Compile/Asm/Preprocess only / DLL");

    printf("\n'zc -h' for help, 'zc --version' for version. See 'zc help <command>' for info.\n");
}

void print_command_help(const char *command)
{
    if (strcmp(command, "build") == 0)
    {
        printf("usage: zc build [options] <file>\n\n");
        printf("Compile Zen C source into a standalone executable.\n\n");
        printf("options:\n");
        print_help_item("-o <file>", "Set the name of the output binary");
        print_help_item("-O<level>", "Optimization level (0-3, default 1)");
        print_help_item("-g, -g0", "Enable/disable debug information");
        print_help_item("--release", "Release mode (equivalent to -O3 -g0)");
        print_help_item("-shared", "Build a shared library (.so, .dll)");
        print_help_item("-v, --verbose", "Show all granular compilation phases");
        print_help_item("-q, --quiet", "Suppress non-essential status messages");
    }
    else if (strcmp(command, "run") == 0)
    {
        printf("usage: zc run [options] <file> [<args>]\n\n");
        printf("Compile and execute a Zen C program immediately.\n\n");
        printf("options:\n");
        print_help_item("-o <file>", "Temp binary name (default: a.out)");
        print_help_item("-O<level>", "Backend optimization level");
        print_help_item("-q, --quiet", "Run without compiler status markers");
    }
    else if (strcmp(command, "check") == 0)
    {
        printf("usage: zc check [options] <file>\n\n");
        printf("Verify syntax and type safety without generating code.\n\n");
        printf("options:\n");
        print_help_item("--json", "Output diagnostics in structured JSON");
        print_help_item("--check", "Enable advanced borrow/move checking");
    }
    else if (strcmp(command, "transpile") == 0)
    {
        printf("usage: zc transpile [options] <file>\n\n");
        printf("Convert Zen C source code into human-readable C.\n\n");
        printf("options:\n");
        print_help_item("-o <file>", "Output C file name");
        print_help_item("--emit-c", "Keep the generated C file (implied)");
    }
    else if (strcmp(command, "debug") == 0)
    {
        printf("usage: zc debug <file> [<args>]\n\n");
        printf("Compile and run with full debug information and GDB/LLDB support.\n");
    }
    else if (strcmp(command, "repl") == 0)
    {
        printf("usage: zc repl\n\n");
        printf("Start an interactive Read-Eval-Print Loop session.\n");
    }
    else if (strcmp(command, "lsp") == 0)
    {
        printf("usage: zc lsp\n\n");
        printf("Start the Language Server for IDE integration.\n");
    }
    else if (strcmp(command, "doc") == 0)
    {
        printf("usage: zc doc [options] <file>\n\n");
        printf("Generate Markdown documentation for a Zen C source file and its imports.\n\n");
        printf("options:\n");
        print_help_item("--recursive-doc", "Traverse and document imported modules (default)");
        print_help_item("--no-recursive-doc", "Only document the primary input file");
        print_help_item("--no-check", "Skip semantic analysis to reduce build noise (default)");
        print_help_item("--check", "Enable semantic analysis for full type resolution");
    }
    else
    {
        printf("Unknown command '%s'.\n", command);
        print_usage();
    }
}

void build_compile_arg_list(ArgList *list, const char *outfile, const char *temp_source_file,
                            CompilerConfig *cfg)
{
    // Compiler
    arg_list_add_from_string(list, cfg->cc);

    // GCC Flags
    arg_list_add_from_string(list, cfg->gcc_flags);
    arg_list_add_from_string(list, g_cflags);

    // Suppress warnings triggered by transpiled code
    // nvcc rejects these GCC-specific flags, so skip them for CUDA
    if (!cfg->no_suppress_warnings && !cfg->use_cuda)
    {
        arg_list_add(list, "-Wno-parentheses");
        arg_list_add(list, "-Wno-unused-value");
        arg_list_add(list, "-Wno-unused-variable");
        arg_list_add(list, "-Wno-unused-parameter");
        arg_list_add(list, "-Wno-unused-function");
        arg_list_add(list, "-Wno-unused-but-set-variable");
        arg_list_add(list, "-Wno-sign-compare");
        arg_list_add(list, "-Wno-missing-field-initializers");
    }
    // nvcc needs -x cu when source is .c (non-CUDA extension)
    // When transpiling to .cu, nvcc auto-detects CUDA from the extension

    // Freestanding
    if (cfg->is_freestanding)
    {
        arg_list_add(list, "-ffreestanding");
    }

    // Quiet
    if (cfg->quiet)
    {
        arg_list_add(list, "-w");
    }

    // C++ compatibility flags
    if (cfg->use_cpp && !cfg->use_cuda)
    {
        arg_list_add(list, "-fpermissive");
        arg_list_add(list, "-Wno-write-strings");
    }

    // ObjC mode: tell the compiler to treat input as Objective-C
    if (cfg->use_objc)
    {
        arg_list_add(list, "-x");
        arg_list_add(list, "objective-c");
        arg_list_add(list, "-std=gnu11");
    }

    // Output file
    arg_list_add(list, "-o");
    arg_list_add(list, outfile);

    // Input files
    arg_list_add(list, temp_source_file);
    for (size_t i = 0; i < cfg->c_files.length; i++)
    {
        const char *file = cfg->c_files.data[i];
        size_t len = strlen(file);
        if (cfg->use_cpp && len > 2 && file[len - 2] == '.' && file[len - 1] == 'c')
        {
            // Force C compilation for .c files in C++ mode to match extern "C" linkage
            arg_list_add(list, "-x");
            arg_list_add(list, "c");
        }
        arg_list_add(list, file);
    }

    // Platform flags
    if (z_is_windows() && !cfg->is_freestanding)
    {
        arg_list_add(list, "-static");
    }
    if (!z_is_windows() && !cfg->is_freestanding)
    {
        arg_list_add(list, "-lm");
    }

    // Linker flags
    arg_list_add_from_string(list, g_link_flags);
    if (z_is_windows())
    {
        arg_list_add(list, "-lws2_32");
    }

    // Include paths
    if (cfg->root_path && cfg->root_path[0])
    {
        char abs_root[MAX_PATH_LEN];
        z_get_absolute_path(cfg->root_path, abs_root, sizeof(abs_root));
        if (abs_root[0])
        {
            arg_list_add_fmt(list, "-I%s", abs_root);
        }

        // Installed compilers resolve std via cfg->std_root (discovered from an
        // actual std import); add it as an include path so generated C that
        // does `#include "std/..."` (e.g. the vendored TRE unity build) works.
        if (!cfg->is_freestanding && cfg->std_root[0])
        {
            arg_list_add_fmt(list, "-I%s", cfg->std_root);
        }

        char tre_path[MAX_PATH_LEN + 128];
        snprintf(tre_path, sizeof(tre_path), "%s/std/third-party/tre/include", abs_root);

        int tre_found = 0;
        if (!cfg->is_freestanding && access(tre_path, F_OK) == 0)
        {
            arg_list_add_fmt(list, "-I%s", tre_path);
            tre_found = 1;
        }

        // The executable walk-up (root_path) fails for an installed compiler
        // (std.zc lives under the share dir, not a parent of the binary), so
        // also try the std root resolved from an actual std import.
        if (!tre_found && !cfg->is_freestanding && cfg->std_root[0])
        {
            snprintf(tre_path, sizeof(tre_path), "%s/std/third-party/tre/include", cfg->std_root);
            if (access(tre_path, F_OK) == 0)
            {
                arg_list_add_fmt(list, "-I%s", tre_path);
                tre_found = 1;
            }
        }

        // Robust fallback: if not found via root_path, try relative to the executable's physical
        // location
        if (!tre_found && !cfg->is_freestanding)
        {
            char self_exe[MAX_PATH_SIZE];
            z_get_executable_path(self_exe, sizeof(self_exe));
            // z_get_executable_path gives the directory.
            // If we are in out/bin, the repo root is ../..
            snprintf(tre_path, sizeof(tre_path), "%s/../../std/third-party/tre/include", self_exe);
            if (access(tre_path, F_OK) == 0)
            {
                char abs_tre[MAX_PATH_SIZE];
                z_get_absolute_path(tre_path, abs_tre, sizeof(abs_tre));
                arg_list_add_fmt(list, "-I%s", abs_tre);
                tre_found = 1;
            }
        }

        // Heuristic: try current directory if all else fails
        if (!tre_found && !cfg->is_freestanding)
        {
            if (access("std/third-party/tre/include", F_OK) == 0)
            {
                arg_list_add(list, "-Istd/third-party/tre/include");
                tre_found = 1;
            }
        }

        // Final fallback: Always add the relative path just in case,
        // as the backend compiler is usually run from the same directory as zc.
        if (!tre_found && !cfg->is_freestanding)
        {
            arg_list_add(list, "-Istd/third-party/tre/include");
        }
    }

    // Always add standard library root as an include path for convenience
    if (cfg->root_path)
    {
        arg_list_add_fmt(list, "-I%s", cfg->root_path);
    }

    // User-defined include paths
    for (size_t i = 0; i < cfg->include_paths.length; i++)
    {
        char abs_inc[MAX_PATH_LEN];
        z_get_absolute_path(cfg->include_paths.data[i], abs_inc, sizeof(abs_inc));
        if (abs_inc[0])
        {
            arg_list_add_fmt(list, "-I%s", abs_inc);
        }
    }

    // Input directory (to resolve relative includes in raw blocks)
    if (cfg->input_dir)
    {
        char abs_input_dir[MAX_PATH_LEN];
        z_get_absolute_path(cfg->input_dir, abs_input_dir, sizeof(abs_input_dir));
        if (abs_input_dir[0])
        {
            arg_list_add_fmt(list, "-I%s", abs_input_dir);
        }

        // Only use -iquote for GCC, Clang, and Emscripten (TCC does not support it)
        if (abs_input_dir[0] &&
            (z_path_match_compiler(cfg->cc, "gcc") || z_path_match_compiler(cfg->cc, "g++") ||
             z_path_match_compiler(cfg->cc, "clang") || z_path_match_compiler(cfg->cc, "emcc") ||
             z_path_match_compiler(cfg->cc, "filcc")))
        {
            arg_list_add(list, "-iquote");
            arg_list_add(list, abs_input_dir);
        }
    }
}

void arg_list_init(ArgList *list)
{
    list->cap = 32;
    list->count = 0;
    list->args = xmalloc(list->cap * sizeof(char *));
}

void arg_list_add(ArgList *list, const char *arg)
{
    if (!arg || !arg[0])
    {
        return;
    }
    if (list->count + 1 >= list->cap)
    {
        list->cap *= 2;
        list->args = xrealloc(list->args, list->cap * sizeof(char *));
    }
    list->args[list->count++] = xstrdup(arg);
    list->args[list->count] = NULL;
}

void arg_list_add_fmt(ArgList *list, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (!fmt)
    {
        va_end(args);
        return;
    }
    int size = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (size < 0)
    {
        return;
    }

    char *buf = xmalloc((size_t)(size + 1));
    va_start(args, fmt);
    vsnprintf(buf, (size_t)(size + 1), fmt, args);
    va_end(args);

    arg_list_add(list, buf);
    zfree(buf);
}

void arg_list_free(ArgList *list)
{
    for (size_t i = 0; i < list->count; i++)
    {
        zfree(list->args[i]);
    }
    zfree(list->args);
    list->args = NULL;
    list->count = 0;
    list->cap = 0;
}

int arg_run(ArgList *list)
{
    return z_run_command(list->args);
}

void arg_list_add_from_string(ArgList *list, const char *str)
{
    if (!str || !str[0])
    {
        return;
    }

    const char *p = str;
    while (*p)
    {
        while (*p && isspace((unsigned char)*p))
        {
            p++;
        }
        if (!*p)
        {
            break;
        }

        char arg[MAX_PATH_LEN];
        char *d = arg;
        int in_quote = 0;

        while (*p && (in_quote || (!isspace((unsigned char)*p) && *p != '\r')))
        {
            if (*p == '\\' && *(p + 1) == '\"')
            {
                if (d - arg < 4095)
                {
                    *d++ = '\"';
                }
                p += 2;
            }
            else if (*p == '\"')
            {
                in_quote = !in_quote;
                p++;
            }
            else
            {
                if (d - arg < 4095)
                {
                    *d++ = *p++;
                }
                else
                {
                    p++;
                }
            }
        }
        *d = '\0';
        arg_list_add(list, arg);
    }
}
