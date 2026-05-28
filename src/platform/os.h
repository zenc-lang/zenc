// SPDX-License-Identifier: MIT
#ifndef ZC_PLATFORM_OS_H
#ifndef ZC_ALLOW_INTERNAL
#error "platform/os.h is internal to Zen C. Include the appropriate public header instead."
#endif

#define ZC_PLATFORM_OS_H

#include "lang.h"
#include "arch.h"
#include "../compat/c23_compat.h"

// OS Detection
#ifdef __COSMOPOLITAN__
#include <cosmo.h>
#define z_is_windows() IsWindows()
#else
#if ZC_OS_WINDOWS
#define z_is_windows() 1
#else
#define z_is_windows() 0
#endif
#endif

// System headers
#if ZC_OS_WINDOWS
#include <windows.h>
#include <direct.h>
#include <io.h>
#ifndef PATH_MAX
#define PATH_MAX 260
#endif
#define realpath(N, R) _fullpath((R), (N), PATH_MAX)
#define F_OK 0
#define access _access
#define getcwd _getcwd
#else
#include <unistd.h>
#include <sys/types.h>
#include <limits.h> /* PATH_MAX */
#endif

// Path helpers
static inline int z_is_abs_path(const char *p)
{
    if (!p)
    {
        return 0;
    }
    if (p[0] == '/')
    {
        return 1;
    }
#if ZC_OS_WINDOWS
    if (p[0] == '\\' || (isalpha(p[0]) && p[1] == ':'))
    {
        return 1;
    }
#endif
    return 0;
}

static inline char *z_path_last_sep(const char *path)
{
    char *last_slash = (char *)strrchr(path, '/');
#if ZC_OS_WINDOWS
    char *last_bs = (char *)strrchr(path, '\\');
    if (last_bs > last_slash)
    {
        return last_bs;
    }
#endif
    return last_slash;
}

static inline const char *z_get_exe_ext(void)
{
#if ZC_OS_WINDOWS
    return ".exe";
#else
    return ".bin";
#endif
}

static inline const char *z_get_null_redirect(void)
{
#if ZC_OS_WINDOWS
    return " > NUL 2>&1";
#else
    return " > /dev/null 2>&1";
#endif
}

static inline const char *z_get_run_prefix(void)
{
#if ZC_OS_WINDOWS
    return "";
#else
    return "./";
#endif
}

static inline const char *z_get_plugin_ext(void)
{
#if ZC_OS_WINDOWS
    return ".dll";
#else
    return ".so";
#endif
}

/**
 * @brief Setup terminal (enable ANSI colors on Windows).
 */
void z_setup_terminal(void);

/**
 * @brief Get wall clock time in seconds.
 */
double z_get_time(void);

/**
 * @brief Get monotonic time in seconds (high precision).
 */
double z_get_monotonic_time(void);

/**
 * @brief Get temporary directory path.
 */
#if !ZC_OS_WINDOWS
ZEN_CONST
#endif
const char *z_get_temp_dir(void);

/**
 * @brief Get current process ID.
 */
int z_get_pid(void);

/**
 * @brief Get the path of the current executable.
 */
void z_get_executable_path(char *buffer, size_t size);

/**
 * @brief Resolve a path to an absolute path.
 * @param path The path to resolve.
 * @param buffer Buffer to store the absolute path.
 * @param size Size of the buffer.
 */
void z_get_absolute_path(const char *path, char *buffer, size_t size);
ZEN_CONST int z_is_zip_path(const char *path);

/**
 * @brief Check if file descriptor refers to a terminal.
 */
int z_isatty(int fd);

// Console / REPL
void repl_enable_raw_mode(void);
void repl_disable_raw_mode(void);
int repl_read_char(char *c);
int repl_get_window_size(int *rows, int *cols);

// Dynamic Library Loading
void *z_dlopen(const char *path);
void *z_dlsym(void *handle, const char *symbol);
void z_dlclose(void *handle);

// OS Helpers
int z_match_os(const char *os_name);
ZEN_CONST const char *z_get_system_name(void);
FILE *z_tmpfile(void);

/**
 * @brief Check if a compiler path/command matches a specific compiler family.
 * @param path The compiler path or command string.
 * @param compiler_name The name to match against (e.g. "gcc", "clang", "tcc", "emcc", "msvc").
 * @return 1 if matched, 0 otherwise.
 */
int z_path_match_compiler(const char *path, const char *compiler_name);

/**
 * @brief Check if a path has a specific extension.
 * @param path The path to check.
 * @param ext The extension to look for (including the dot).
 * @return 1 if matched, 0 otherwise.
 */
int z_path_has_extension(const char *path, const char *ext);

/**
 * @brief Run a command securely without shell interpretation.
 * @param argv NULL-terminated array of arguments.
 * @return Exit code of the process.
 */
int z_run_command(char *const argv[]);

/**
 * @brief Run a command securely and capture its stdout.
 * @param argv NULL-terminated array of arguments.
 * @param buffer Buffer to store output.
 * @param size Size of the buffer.
 * @return Exit code of the process, or -1 on error.
 */
int z_run_command_capture(char *const argv[], char *buffer, size_t size);

#endif // ZC_PLATFORM_OS_H
