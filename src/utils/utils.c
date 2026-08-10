// SPDX-License-Identifier: MIT

#include "parser.h"
#include "constants.h"
#include "zprep.h"
#include "cmd.h"
#include "platform/os.h"
#include <sys/stat.h>

// ** Arena Implementation **
#define ARENA_BLOCK_SIZE (1024 * 1024)

static void *arena_alloc(zarena *a, size_t size)
{
    // We add a size_t header to support xrealloc's size lookup.
    // To maintain alignment, we ensure the total header size is 16 bytes.
    size_t total = size + 16;
    void *ptr = zarena_alloc_align(a, total, 16);
    if (!ptr)
    {
        return NULL;
    }

    *(size_t *)ptr = size;
    return (char *)ptr + 16;
}

// Header stored 16 bytes before the returned pointer, used by xrealloc.
#define XMALLOC_HDR_SIZE 16

// Backward compatibility using global g_compiler.arena
static void *arena_alloc_raw(size_t size)
{
    return arena_alloc(&g_compiler.arena, size);
}

#include <time.h>
#include "platform/arch.h"
#include "platform/os.h"

void *xmalloc(size_t size)
{
    void *ptr = arena_alloc_raw(size + XMALLOC_HDR_SIZE);
    if (!ptr)
    {
        zfatal("xmalloc: out of memory");
        exit(1); // whitelisted
    }
    ((size_t *)ptr)[0] = size;
    ((size_t *)ptr)[1] = 0;
    return (char *)ptr + XMALLOC_HDR_SIZE;
}

void *xcalloc(size_t n, size_t size)
{
    size_t total = n * size;
    void *ptr = arena_alloc_raw(total + XMALLOC_HDR_SIZE);
    if (!ptr)
    {
        zfatal("xcalloc: out of memory");
        exit(1); // whitelisted
    }
    memset(ptr, 0, total + XMALLOC_HDR_SIZE);
    ((size_t *)ptr)[0] = total;
    ((size_t *)ptr)[1] = 0;
    return (char *)ptr + XMALLOC_HDR_SIZE;
}

void *xrealloc(void *ptr, size_t new_size)
{
    if (!ptr)
    {
        return xmalloc((size_t)(new_size));
    }

    // Header is XMALLOC_HDR_SIZE bytes before the returned pointer
    size_t *header = (size_t *)(void *)((char *)ptr - XMALLOC_HDR_SIZE);
    size_t old_size = header[0];

    if (new_size <= old_size)
    {
        return ptr;
    }

    void *new_ptr = xmalloc((size_t)(new_size));
    memcpy(new_ptr, ptr, (size_t)(old_size));
    return new_ptr;
}

char *xstrdup(const char *s)
{
    if (!s)
    {
        zfatal("xstrdup(NULL)");
    }
    size_t len = strlen(s);
    char *d = xmalloc((size_t)(len + 1));
    memcpy(d, s, (size_t)(len));
    d[len] = 0;
    return d;
}

char *merge_underscores(const char *name)
{
    if (!name)
    {
        return NULL;
    }

    size_t len = strlen(name);
    char *res = xmalloc((size_t)(len + 1));
    char *out = res;
    const char *in = name;

    while (*in)
    {
        if (in[0] == '_' && in[1] == '_' && in[2] == '_' && in[3] == '_')
        {
            // Quadruple or more underscores -> collapse to double.
            // A run of exactly three underscores is kept: it encodes the "__"
            // separator plus an identifier that begins with "_" (e.g.
            // `Struct::_method` vs `Struct::method`), so collapsing it would
            // make distinct symbols collide.
            *out++ = '_';
            *out++ = '_';
            in += 4;
            while (*in == '_')
            {
                in++;
            }
        }
        else
        {
            *out++ = *in++;
        }
    }
    *out = '\0';
    return res;
}

char *sanitize_path_for_c_string(const char *path)
{
    if (!path)
    {
        return NULL;
    }
    char *sanitized = xstrdup(path);
    for (int i = 0; sanitized[i]; i++)
    {
        if (sanitized[i] == '\\')
        {
            sanitized[i] = '/';
        }
    }
    return sanitized;
}

static char *z_realpath_arena(const char *path)
{
    char *real = realpath(path, NULL);
    if (real)
    {
        char *res = xstrdup(real);
        libc_free(real);
        return res;
    }
    return xstrdup(path);
}

static int is_std_import(const char *fn)
{
    return strncmp(fn, "std/", 4) == 0 || strncmp(fn, "./std/", 6) == 0;
}

static void maybe_lock_std_root(CompilerConfig *cfg, const char *resolved)
{
    if (cfg->std_locked || !resolved)
    {
        return;
    }
    char *std_pos = (char *)strstr(resolved, "/std/");
    if (!std_pos)
    {
        return;
    }
    size_t root_len = (size_t)(std_pos - resolved);
    if (root_len >= sizeof(cfg->std_root))
    {
        return;
    }
    memcpy(cfg->std_root, resolved, (size_t)(root_len));
    cfg->std_root[root_len] = '\0';
    cfg->std_locked = 1;
}

/* * Helper: try to resolve fn at a given search directory, also trying
 * fn/mod.zc and fn/mod.zenc for directory-as-module resolution.
 * Uses stat() to ensure we only match regular files, not directories. */
static char *try_resolve_at(const char *search_dir, const char *fn, CompilerConfig *cfg)
{
    char path[MAX_PATH_LEN];
    struct stat st;

    // Try the exact path first
    snprintf(path, sizeof(path), "%s/%s", search_dir, fn);
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
    {
        char *resolved = z_realpath_arena(path);
        maybe_lock_std_root(cfg, resolved);
        return resolved;
    }

    // Not found — try as a directory with mod.zc / mod.zenc
    if (strlen(fn) < MAX_PATH_LEN - 20)
    {
        char modpath[MAX_PATH_LEN];

        snprintf(modpath, sizeof(modpath), "%s/%s/mod.zc", search_dir, fn);
        if (stat(modpath, &st) == 0 && S_ISREG(st.st_mode))
        {
            char *resolved = z_realpath_arena(modpath);
            maybe_lock_std_root(cfg, resolved);
            return resolved;
        }

        snprintf(modpath, sizeof(modpath), "%s/%s/mod.zenc", search_dir, fn);
        if (stat(modpath, &st) == 0 && S_ISREG(st.st_mode))
        {
            char *resolved = z_realpath_arena(modpath);
            maybe_lock_std_root(cfg, resolved);
            return resolved;
        }
    }

    return NULL;
}

char *z_resolve_path(const char *fn, const char *relative_to, CompilerConfig *cfg)
{
    if (!fn)
    {
        return NULL;
    }

    char path[MAX_PATH_LEN];

    // 1. Absolute path
    if (z_is_abs_path(fn))
    {
        struct stat st;
        if (stat(fn, &st) == 0 && S_ISREG(st.st_mode))
        {
            return z_realpath_arena(fn);
        }
        // Try as a directory with mod.zc / mod.zenc
        snprintf(path, sizeof(path), "%s/mod.zc", fn);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        {
            return z_realpath_arena(path);
        }
        snprintf(path, sizeof(path), "%s/mod.zenc", fn);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        {
            return z_realpath_arena(path);
        }
        return NULL;
    }

    // If std library location is locked and this is a std import,
    // only search the locked location — skip other paths entirely.
    if (cfg->std_locked && is_std_import(fn))
    {
        char *resolved = try_resolve_at(cfg->std_root, fn, cfg);
        return resolved; // try_resolve_at already calls maybe_lock_std_root
    }

    // 2. Relative to current file
    if (relative_to)
    {
        char *dir = xstrdup(relative_to);
        char *last_slash = z_path_last_sep(dir);
        if (last_slash)
        {
            *last_slash = 0;
            char *resolved = try_resolve_at(dir, fn, cfg);
            zfree(dir);
            if (resolved)
            {
                return resolved;
            }
        }
        else
        {
            zfree(dir);
        }
    }

    // 3. Current directory
    snprintf(path, sizeof(path), ".");
    char *resolved = try_resolve_at(path, fn, cfg);
    if (resolved)
    {
        return resolved;
    }

    // 4. Include paths (-I)
    for (size_t i = 0; i < cfg->include_paths.length; i++)
    {
        resolved = try_resolve_at(cfg->include_paths.data[i], fn, cfg);
        if (resolved)
        {
            return resolved;
        }
    }

    // 5. Root path (ZC_ROOT)
    if (cfg->root_path)
    {
        // Try with std/ prefix (for stdlib modules like "slice.zc")
        char std_path[MAX_PATH_LEN];
        snprintf(std_path, sizeof(std_path), "%s/std", cfg->root_path);
        resolved = try_resolve_at(std_path, fn, cfg);
        if (resolved)
        {
            return resolved;
        }

        // Try as-is relative to root_path
        resolved = try_resolve_at(cfg->root_path, fn, cfg);
        if (resolved)
        {
            return resolved;
        }
    }

    // 6. System paths — only search if std library isn't locked yet.
    if (!cfg->std_locked)
    {
#ifdef ZEN_SHARE_DIR
        const char *system_paths[] = {ZEN_SHARE_DIR, "/usr/local/share/zenc", "/usr/share/zenc"};
        int sys_count = 3;
#else
        const char *system_paths[] = {"/usr/local/share/zenc", "/usr/share/zenc"};
        int sys_count = 2;
#endif

        for (int i = 0; i < sys_count; i++)
        {
            resolved = try_resolve_at(system_paths[i], fn, cfg);
            if (resolved)
            {
                return resolved;
            }

            // Also try with std/ prefix in system paths
            char sstd[MAX_PATH_LEN];
            snprintf(sstd, sizeof(sstd), "%s/std", system_paths[i]);
            resolved = try_resolve_at(sstd, fn, cfg);
            if (resolved)
            {
                return resolved;
            }
        }
    }

    return NULL;
}

char *load_file(const char *fn, const char *relative_to)
{
    char *resolved = z_resolve_path(fn, relative_to, &g_compiler.config);
    if (!resolved)
    {
        return NULL;
    }

    FILE *f = fopen(resolved, "rb");
    if (!f)
    {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long l = ftell(f);
    if (l < 0)
    {
        fclose(f);
        return NULL;
    }
    if (l < 0)
    {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *b = xmalloc((size_t)(l + 1));
    if (fread(b, 1, (size_t)(l), f) != (size_t)(l))
    {
        zfree(b);
        fclose(f);
        return NULL;
    }
    b[l] = 0;
    fclose(f);
    return b;
}

// ** Global Compiler State **
ZenCompiler g_compiler = {0};

void append_flag(char *dest, size_t max_size, const char *prefix, const char *val)
{
    size_t current_len = strlen(dest);

    if (current_len > 0 && dest[current_len - 1] != ' ')
    {
        strncat(dest, " ", max_size - current_len - 1);
        current_len++;
    }

    if (prefix)
    {
        strncat(dest, prefix, max_size - current_len - 1);
        current_len = strlen(dest);
    }

    if (val)
    {
        strncat(dest, val, max_size - current_len - 1);
    }
}

// Helper for environment expansion
static void expand_env_vars(char *dest, size_t dest_size, const char *src)
{
    char *d = dest;
    const char *s = src;
    size_t remaining = dest_size - 1;

    while (*s && remaining > 0)
    {
        if (*s == '$' && *(s + 1) == '{')
        {
            const char *end = (char *)strchr(s + 2, '}');
            if (end)
            {
                char var_name[MAX_VAR_NAME_LEN];
                ptrdiff_t len = end - (s + 2);
                if (len < MAX_VAR_NAME_LEN - 1)
                {
                    strncpy(var_name, s + 2, (size_t)(len));
                    var_name[len] = 0;
                    char *val = getenv(var_name);
                    if (val)
                    {
                        size_t val_len = strlen(val);
                        if (val_len < remaining)
                        {
                            strncpy(d, val, (size_t)(remaining));
                            d += val_len;
                            remaining -= val_len;
                            s = end + 1;
                            continue;
                        }
                    }
                }
            }
        }
        *d++ = *s++;
        remaining--;
    }
    *d = 0;
}

// Helper to determine active OS
static int is_os_active(const char *os_name)
{
    if (0 == strcmp(os_name, "linux"))
    {
#if ZC_OS_LINUX
        return 1;
#else
        return 0;
#endif
    }
    else if (0 == strcmp(os_name, "windows"))
    {
#if ZC_OS_WINDOWS
        return 1;
#else
        return 0;
#endif
    }
    else if (0 == strcmp(os_name, "macos") || 0 == strcmp(os_name, "darwin"))
    {
#if ZC_OS_MACOS
        return 1;
#else
        return 0;
#endif
    }
    return 0;
}

void scan_build_directives(ParserContext *ctx, const char *src)
{
    (void)ctx;
    const char *p = src;
    while (*p)
    {
        if (p[0] == '/' && p[1] == '/' && p[2] == '>')
        {
            p += 3;
            while (*p && isspace((unsigned char)*p) && *p != '\n')
            {
                p++;
            }

            const char *start = p;
            int len = 0;
            while (p[len] && p[len] != '\n')
            {
                len++;
            }

            char raw_line[2048];
            if (len >= 2047)
            {
                len = 2047;
            }
            strncpy(raw_line, start, (size_t)(len));
            raw_line[len] = 0;

            // Strip trailing \r (Windows CRLF)
            size_t rlen = strlen(raw_line);
            while (rlen > 0 && (raw_line[rlen - 1] == '\r' || raw_line[rlen - 1] == '\n' ||
                                isspace((unsigned char)raw_line[rlen - 1])))
            {
                raw_line[--rlen] = 0;
            }

            char line[2048];
            expand_env_vars(line, sizeof(line), raw_line);

            char *directive = line;
            char *colon = (char *)strchr(line, ':');
            if (colon)
            {
                *colon = 0; // split the string temporarily
                if (0 == strcmp(line, "linux") || 0 == strcmp(line, "windows") ||
                    0 == strcmp(line, "macos") || 0 == strcmp(line, "darwin"))
                {
                    if (is_os_active(line))
                    {
                        directive = colon + 1;
                        while (*directive && isspace((unsigned char)*directive))
                        {
                            directive++;
                        }
                    }
                    else
                    {
                        // OS specified but not active, skip this directive completely
                        goto next_line;
                    }
                }
                else
                {
                    // Not an OS prefix, restore the colon
                    *colon = ':';
                    directive = line;
                }
            }

            char *directive_val = NULL;
            // Process Directive
            if (0 == strncmp(directive, "link:", 5))
            {
                directive_val = directive + 5;
                while (*directive_val && isspace((unsigned char)*directive_val))
                {
                    directive_val++;
                }
                append_flag(g_link_flags, sizeof(g_link_flags), directive_val, NULL);
            }
            else if (0 == strncmp(directive, "cflags:", 7))
            {
                directive_val = directive + 7;
                while (*directive_val && isspace((unsigned char)*directive_val))
                {
                    directive_val++;
                }
                append_flag(g_cflags, sizeof(g_cflags), directive_val, NULL);
            }
            else if (0 == strncmp(directive, "include:", 8))
            {
                directive_val = directive + 8;
                while (*directive_val && isspace((unsigned char)*directive_val))
                {
                    directive_val++;
                }

                char *dp = directive_val;
                while (*dp)
                {
                    while (*dp && isspace((unsigned char)*dp))
                    {
                        dp++;
                    }
                    if (!*dp)
                    {
                        break;
                    }

                    char path[MAX_PATH_LEN];
                    char *d = path;
                    while (*dp && !isspace((unsigned char)*dp))
                    {
                        if (d - path < 1023)
                        {
                            *d++ = *dp++;
                        }
                        else
                        {
                            dp++;
                        }
                    }
                    *d = '\0';

                    char flags[MAX_PATH_LEN + 32];
                    snprintf(flags, sizeof(flags), "-I%s", path);
                    append_flag(g_cflags, sizeof(g_cflags), flags, NULL);
                }
            }
            else if (strncmp(directive, "lib:", 4) == 0)
            {
                directive_val = directive + 4;
                while (*directive_val && isspace((unsigned char)*directive_val))
                {
                    directive_val++;
                }

                char *dp = directive_val;
                while (*dp)
                {
                    while (*dp && isspace((unsigned char)*dp))
                    {
                        dp++;
                    }
                    if (!*dp)
                    {
                        break;
                    }

                    char path[MAX_PATH_LEN];
                    char *d = path;
                    while (*dp && !isspace((unsigned char)*dp))
                    {
                        if (d - path < 1023)
                        {
                            *d++ = *dp++;
                        }
                        else
                        {
                            dp++;
                        }
                    }
                    *d = '\0';

                    char flags[MAX_PATH_LEN + 32];
                    snprintf(flags, sizeof(flags), "-L%s", path);
                    append_flag(g_link_flags, sizeof(g_link_flags), flags, NULL);
                }
            }
            else if (strncmp(directive, "framework:", 10) == 0)
            {
                directive_val = directive + 10;
                while (*directive_val && isspace((unsigned char)*directive_val))
                {
                    directive_val++;
                }

                char *dp = directive_val;
                while (*dp)
                {
                    while (*dp && isspace((unsigned char)*dp))
                    {
                        dp++;
                    }
                    if (!*dp)
                    {
                        break;
                    }

                    char name[MAX_VAR_NAME_LEN];
                    char *d = name;
                    while (*dp && !isspace((unsigned char)*dp))
                    {
                        if (d - name < 255)
                        {
                            *d++ = *dp++;
                        }
                        else
                        {
                            dp++;
                        }
                    }
                    *d = '\0';

                    char flags[MAX_VAR_NAME_LEN + 32];
                    snprintf(flags, sizeof(flags), "-framework %s", name);
                    append_flag(g_link_flags, sizeof(g_link_flags), flags, NULL);
                }
            }
            else if (strncmp(directive, "define:", 7) == 0)
            {
                directive_val = directive + 7;
                while (*directive_val && isspace((unsigned char)*directive_val))
                {
                    directive_val++;
                }

                char *dp = directive_val;
                while (*dp)
                {
                    while (*dp && isspace((unsigned char)*dp))
                    {
                        dp++;
                    }
                    if (!*dp)
                    {
                        break;
                    }

                    char def_val[MAX_ERROR_MSG_LEN];
                    char *d = def_val;
                    while (*dp && !isspace((unsigned char)*dp))
                    {
                        if (d - def_val < 1023)
                        {
                            *d++ = *dp++;
                        }
                        else
                        {
                            dp++;
                        }
                    }
                    *d = '\0';

                    append_flag(g_cflags, sizeof(g_cflags), "-D", def_val);

                    char *name = xstrdup(def_val);
                    char *eq = (char *)strchr(name, '=');
                    if (eq)
                    {
                        *eq = '\0';
                    }
                    zvec_push_Str(&ctx->config->cfg_defines, name);
                }
            }
            else if (0 == strncmp(directive, "shell:", 6))
            {
                directive_val = directive + 6;
                while (*directive_val && isspace((unsigned char)*directive_val))
                {
                    directive_val++;
                }
                zwarn("Security Alert: Execution of 'shell:' directive (%s) was BLOCKED by default "
                      "to prevent Remote Code Execution.",
                      directive_val);
                // Intentionally ignored system() call for security reasons
            }
            else if (strncmp(directive, "get:", 4) == 0)
            {
                char *url = directive + 4;
                while (*url && isspace((unsigned char)*url))
                {
                    url++;
                }
                zwarn("Security Alert: Execution of 'get:' directive (%s) was BLOCKED. Please "
                      "download external dependencies manually.",
                      url);
                // Intentionally ignored external network hit for security reasons
            }
            else if (strncmp(directive, "pkg-config:", 11) == 0)
            {
                char *libs = directive + 11;

                // Security check for malicious pkg-config commands containing shell injections.
                // We only allow a strict whitelist of characters: alphanumeric, spaces, and safe
                // non-alphanumeric. This prevents characters like ;, &, |, $, `, (, ), <, >, etc.
                int is_safe = 1;
                if (!libs || !*libs)
                {
                    is_safe = 0;
                }
                else
                {
                    for (int i = 0; libs[i]; i++)
                    {
                        if (!isalnum((unsigned char)libs[i]) && libs[i] != '-' && libs[i] != '_' &&
                            libs[i] != ' ' && libs[i] != '.' && libs[i] != '+')
                        {
                            is_safe = 0;
                            break;
                        }
                    }
                }

                if (!is_safe)
                {
                    zwarn("Security Alert: Execution of 'pkg-config:' directive with invalid chars "
                          "(%s) was BLOCKED.",
                          libs);
                }
                else
                {
                    ArgList args;
                    arg_list_init(&args);
                    arg_list_add(&args, "pkg-config");
                    arg_list_add(&args, "--cflags");
                    arg_list_add_from_string(&args, libs);

                    char buf[MAX_ERROR_MSG_LEN];
                    if (z_run_command_capture(args.args, buf, sizeof(buf)) == 0)
                    {
                        size_t l = strlen(buf);
                        if (l > 0 && buf[l - 1] == '\n')
                        {
                            buf[l - 1] = 0;
                        }
                        append_flag(g_cflags, sizeof(g_cflags), buf, NULL);
                    }
                    arg_list_free(&args);

                    arg_list_init(&args);
                    arg_list_add(&args, "pkg-config");
                    arg_list_add(&args, "--libs");
                    arg_list_add_from_string(&args, libs);

                    if (z_run_command_capture(args.args, buf, sizeof(buf)) == 0)
                    {
                        size_t l = strlen(buf);
                        if (l > 0 && buf[l - 1] == '\n')
                        {
                            buf[l - 1] = 0;
                        }
                        append_flag(g_link_flags, sizeof(g_link_flags), buf, NULL);
                    }
                    arg_list_free(&args);
                }
            }
            else
            {
                zwarn("Unknown build directive: '%s'", directive);
            }

            p += len;
        }
    next_line:
        while (*p && *p != '\n')
        {
            p++;
        }
        if (*p == '\n')
        {
            p++;
        }
    }
}

// Levenshtein distance for "did you mean?" suggestions.
int levenshtein(const char *s1, const char *s2)
{
    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);

    // Quick optimization.
    if (len1 > len2 ? len1 - len2 > 3 : len2 - len1 > 3)
    {
        return 999;
    }

    // Use a single-dimensional array to avoid VLA stack overflow
    int *matrix = malloc((size_t)(len1 + 1) * (size_t)(len2 + 1) * sizeof(int));
    if (!matrix)
    {
        return 999;
    }

#define MATRIX(i, j) matrix[(i) * (len2 + 1) + (j)]

    for (size_t i = 0; i <= len1; i++)
    {
        MATRIX(i, 0) = (int)i;
    }
    for (size_t j = 0; j <= len2; j++)
    {
        MATRIX(0, j) = (int)j;
    }

    for (size_t i = 1; i <= len1; i++)
    {
        for (size_t j = 1; j <= len2; j++)
        {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            int del = MATRIX(i - 1, j) + 1;
            int ins = MATRIX(i, j - 1) + 1;
            int sub = MATRIX(i - 1, j - 1) + cost;

            MATRIX(i, j) = (del < ins) ? del : ins;
            if (sub < MATRIX(i, j))
            {
                MATRIX(i, j) = sub;
            }
        }
    }

    int result = MATRIX(len1, len2);
    zfree(matrix);
    return result;
}

char *z_basename(const char *path)
{
    if (!path)
    {
        return NULL;
    }
    const char *last_slash = (char *)strrchr(path, '/');
    const char *last_bslash = (char *)strrchr(path, '\\');
    const char *last_sep = last_slash > last_bslash ? last_slash : last_bslash;
    if (last_sep)
    {
        return xstrdup(last_sep + 1);
    }
    return xstrdup(path);
}

char *z_strip_ext(const char *filename)
{
    if (!filename)
    {
        return NULL;
    }
    char *res = xstrdup(filename);
    if (!res)
    {
        return NULL;
    }
    char *last_dot = (char *)strrchr(res, '.');
    if (last_dot)
    {
        *last_dot = '\0';
    }
    return res;
}
