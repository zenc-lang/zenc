// SPDX-License-Identifier: MIT

#include "../ast/ast.h"
#include "../constants.h"
#include "../parser/parser.h"
#include "../zprep.h"
#include "codegen.h"
#include "compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../platform/misra.h"

static void emit_freestanding_preamble(ParserContext *ctx)
{
    EMIT(ctx, "%s",
         "#include <stddef.h>\n#include <stdint.h>\n#include <stdbool.h>\n#include <stdarg.h>\n");
    EMIT(ctx, "%s",
         "#ifdef __has_builtin\n#if __has_builtin(__builtin_pow)\n#define _zc_pow "
         "__builtin_pow\n#endif\n#endif\n#ifndef _zc_pow\nextern double pow(double, "
         "double);\n#define _zc_pow pow\n#endif\n");
    EMIT(ctx, "%s", ZC_TCC_COMPAT_STR);
    EMIT(ctx, "%s",
         "typedef size_t usize;\ntypedef char* string;\n"
         "#ifndef __CUDACC__\ntypedef intptr_t any;\n#else\ntypedef intptr_t zc_any;\n#define any "
         "zc_any\n#endif\n");
    EMIT(ctx, "%s",
         "#define U0 void\n#define I8 int8_t\n#define U8 uint8_t\n#define I16 int16_t\n#define U16 "
         "uint16_t\n");
    EMIT(ctx, "%s",
         "#define I32 int32_t\n#define U32 uint32_t\n#define I64 int64_t\n#define U64 uint64_t\n");
    EMIT(ctx, "%s", "#define F32 float\n#define F64 double\n");
    EMIT(ctx, "%s",
         "static inline const char* _z_bool_str(_Bool b) { return b ? \"true\" : \"false\"; }\n");
    EMIT(ctx, "%s", "#ifdef __SIZEOF_INT128__\n");
    EMIT(ctx, "%s",
         "static inline const char *_z_u128_str(unsigned __int128 val) {\n    static _Thread_local "
         "char buf[40];\n    if (val == 0) return \"0\";\n    int i = 38;\n    buf[39] = 0;\n    "
         "while (val > 0) { buf[i--] = (char)((val % 10) + '0'); val /= 10; }\n    return &buf[i + "
         "1];\n}\nstatic inline const char *_z_i128_str(__int128 val) {\n    static _Thread_local "
         "char buf[41];\n    if (val == 0) return \"0\";\n    int neg = val < 0;\n    unsigned "
         "__int128 uval = neg ? -val : val;\n    int i = 39;\n    buf[40] = 0;\n    while (uval > "
         "0) { buf[i--] = (char)((uval % 10) + '0'); uval /= 10; }\n    if (neg) buf[i--] = '-';\n "
         "   return &buf[i + 1];\n}\n#define _z_128_map ,__int128: \"%s\", unsigned __int128: "
         "\"%s\"\n#define _z_safe_i128(x) _Generic((x), __int128: (x), default: "
         "(__int128)0)\n#define _z_safe_u128(x) _Generic((x), unsigned __int128: (x), default: "
         "(unsigned __int128)0)\n#define _z_128_arg_map(x) ,__int128: "
         "_z_i128_str(_z_safe_i128(x)), unsigned __int128: _z_u128_str(_z_safe_u128(x))\n");
    EMIT(ctx, "%s", "#else\n");
    EMIT(ctx, "%s", "#define _z_128_map\n");
    EMIT(ctx, "%s", "#define _z_128_arg_map(x)\n");
    EMIT(ctx, "%s", "#endif\n");
    EMIT(ctx, "%s",
         "#define _z_str(x) _Generic((x), _Bool: \"%s\", char: \"%c\", signed char: \"%c\", "
         "unsigned char: \"%u\", short: \"%d\", unsigned short: \"%u\", int: \"%d\", unsigned int: "
         "\"%u\", long: \"%ld\", unsigned long: \"%lu\", long long: \"%lld\", unsigned long long: "
         "\"%llu\", float: \"%f\", double: \"%f\", char*: \"%s\", const char*: \"%s\", void*: "
         "\"%p\" _z_128_map)\n");
    EMIT(ctx, "%s",
         "#define _z_safe_bool(x) _Generic((x), _Bool: (x), default: (_Bool)0)\n#define _z_arg(x) "
         "_Generic((x), _Bool: _z_bool_str(_z_safe_bool(x)) _z_128_arg_map(x), default: (x))\n");
    EMIT(ctx, "%s",
         "typedef struct { void *func; void *ctx; void (*drop)(void*); } z_closure_T;\n");
    EMIT(ctx, "%s", "static void *_z_closure_ctx_stash[256];\n");

    // In true freestanding, explicit definitions of z_malloc/etc are removed.
    // The user must implement them if they use features requiring them.
    // Most primitives (integers, pointers) work without them.
}

void emit_preamble(ParserContext *ctx)
{
    if (ctx->config->misra_mode)
    {
        emit_misra_preamble(ctx->cg.emitter.file);
        return;
    }
    if (ctx->config->is_freestanding)
    {
        emit_freestanding_preamble(ctx);
        return;
    }
    else
    {
        // Standard hosted preamble.
        EMIT(ctx, "%s", "#ifndef _GNU_SOURCE\n#define _GNU_SOURCE\n#endif\n");
        EMIT(ctx, "%s",
             "#include <stdio.h>\n#include <stdlib.h>\n#include <stddef.h>\n#include <string.h>\n");
        EMIT(ctx, "%s", "#include <stdarg.h>\n#include <stdint.h>\n#include <stdbool.h>\n");
        EMIT(ctx, "%s",
             "#ifdef __has_builtin\n#if __has_builtin(__builtin_pow)\n#define _zc_pow "
             "__builtin_pow\n#endif\n#endif\n#ifndef _zc_pow\nextern double pow(double, "
             "double);\n#define _zc_pow pow\n#endif\n");
        EMIT(ctx, "%s", "#include <unistd.h>\n#include <fcntl.h>\n"); // POSIX functions
        EMIT(ctx, "%s", "#define ZC_SIMD(T, N) T __attribute__((vector_size(N * sizeof(T))))\n");

        // Map C11 _Thread_local to C++11 thread_local (used in _z_{u}128_str)
        if (ctx->config->use_cpp ||
            (ctx->config->backend_name && strcmp(ctx->config->backend_name, "cpp") == 0))
        {
            EMIT(ctx, "%s", "#define _Thread_local thread_local\n");
        }

        // C++ compatibility
        if (ctx->config->use_cpp)
        {
            EMIT(ctx, "%s", "#define ZC_AUTO auto\n");
            EMIT(ctx, "%s", "#define ZC_AUTO_INIT(var, init) auto var = (init)\n");
            EMIT(ctx, "%s", "#define ZC_CAST(T, x) static_cast<T>(x)\n");
            EMIT(ctx, "%s", "#define null nullptr\n");
            // C++ _z_str via overloads
            EMIT(ctx, "%s",
                 "inline const char* _z_bool_str(bool b) { return b ? \"true\" : \"false\"; }\n");
            EMIT(ctx, "%s", "inline const char* _z_str(bool)               { return \"%s\"; }\n");
            EMIT(ctx, "%s",
                 "inline const char* _z_arg(bool b)             { return _z_bool_str(b); }\n");
            EMIT(ctx, "%s", "template<typename T> inline T _z_arg(T x)     { return x; }\n");
            EMIT(ctx, "%s", "inline const char* _z_str(char)               { return \"%c\"; }\n");
            EMIT(ctx, "%s", "inline const char* _z_str(signed char)        { return \"%d\"; }\n");
            EMIT(ctx, "%s", "inline const char* _z_str(unsigned char)      { return \"%u\"; }\n");
            EMIT(ctx, "%s", "inline const char* _z_str(short)               { return \"%d\"; }\n");
            EMIT(ctx, "%s", "inline const char* _z_str(unsigned short)      { return \"%u\"; }\n");
            EMIT(ctx, "%s", "inline const char* _z_str(int)                { return \"%d\"; }\n");
            EMIT(ctx, "%s", "inline const char* _z_str(unsigned int)       { return \"%u\"; }\n");
            EMIT(ctx, "%s", "inline const char* _z_str(long)               { return \"%ld\"; }\n");
            EMIT(ctx, "%s", "inline const char* _z_str(unsigned long)      { return \"%lu\"; }\n");
            EMIT(ctx, "%s", "inline const char* _z_str(long long)          { return \"%lld\"; }\n");
            EMIT(ctx, "%s", "inline const char* _z_str(unsigned long long) { return \"%llu\"; }\n");
            EMIT(ctx, "%s", "inline const char* _z_str(float)              { return \"%f\"; }\n");
            EMIT(ctx, "%s", "inline const char* _z_str(double)             { return \"%f\"; }\n");
            EMIT(ctx, "%s", "inline const char* _z_str(char*)              { return \"%s\"; }\n");
            EMIT(ctx, "%s", "inline const char* _z_str(const char*)        { return \"%s\"; }\n");
            EMIT(ctx, "%s", "inline const char* _z_str(void*)              { return \"%p\"; }\n");
        }
        else
        {
            // C mode
            EMIT(ctx, "%s", "#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202300L\n");
            EMIT(ctx, "%s", "#define ZC_AUTO auto\n");
            EMIT(ctx, "%s", "#define ZC_AUTO_INIT(var, init) auto var = (init)\n");
            EMIT(ctx, "%s", "#else\n");
            EMIT(ctx, "%s", "#define ZC_AUTO __auto_type\n");
            EMIT(ctx, "%s", "#define ZC_AUTO_INIT(var, init) __auto_type var = (init)\n");
            EMIT(ctx, "%s", "#endif\n");
            EMIT(ctx, "%s", "#define ZC_CAST(T, x) ((T)(x))\n");
            EMIT(ctx, "%s", ZC_TCC_COMPAT_STR);
            EMIT(ctx, "%s",
                 "static inline const char* _z_bool_str(_Bool b) { return b ? \"true\" : "
                 "\"false\"; }\n");
            EMIT(ctx, "%s", ZC_C_GENERIC_STR);
            EMIT(ctx, "%s", ZC_C_ARG_GENERIC_STR);
        }

        EMIT(ctx, "%s",
             "typedef size_t usize;\ntypedef char* string;\n"
             "#ifndef __CUDACC__\ntypedef intptr_t any;\n#else\ntypedef intptr_t zc_any;\n#define "
             "any zc_any\n#endif\n");
        if (ctx->cg.has_async)
        {
            EMIT(ctx, "%s", "typedef int (*PollFn)(void*);\n");
        }
        EMIT(ctx, "%s",
             "#ifdef ZC_STATIC_PLUGIN\n#define ZC_FUNC static\n#define ZC_GLOBAL "
             "static\n#else\n#define ZC_FUNC\n#define ZC_GLOBAL\n#endif\n");
        EMIT(ctx, "%s",
             "typedef struct { void *func; void *ctx; void (*drop)(void*); } z_closure_T;\n");
        EMIT(ctx, "%s", "static void *_z_closure_ctx_stash[256];\n");
        EMIT(ctx, "%s",
             "typedef void U0;\ntypedef int8_t I8;\ntypedef uint8_t U8;\ntypedef int16_t "
             "I16;\ntypedef uint16_t U16;\n");
        EMIT(ctx, "%s",
             "typedef int32_t I32;\ntypedef uint32_t U32;\ntypedef int64_t I64;\ntypedef uint64_t "
             "U64;\n");
        EMIT(ctx, "%s", "#define F32 float\n#define F64 double\n");

        // Memory Mapping.
        if (ctx->config->use_cpp)
        {
            // C++ needs explicit casts for void* conversions
            EMIT(ctx, "%s", "#define z_malloc(sz) static_cast<char*>(malloc(sz))\n");
            EMIT(ctx, "%s", "#define z_realloc(p, sz) static_cast<char*>(realloc(p, sz))\n");
        }
        else
        {
            EMIT(ctx, "%s", "#define z_malloc malloc\n#define z_realloc realloc\n");
        }
        EMIT(ctx, "%s", "#define z_free free\n#define z_print printf\n");
        EMIT(ctx, "%s",
             "static void __zenc_panic(const char* msg) { fprintf(stderr, \"Panic: %s\\n\", msg); "
             "exit(1); }\n");
        EMIT(ctx, "%s",
             "#if defined(__APPLE__)\n#define _ZC_SEC "
             "__attribute__((used,section(\"__DATA,__zarch\")))\n#elif defined(_WIN32)\n#define "
             "_ZC_SEC __attribute__((used))\n#else\n#define _ZC_SEC "
             "__attribute__((used,section(\".note.zarch\")))\n#endif\n");
        EMIT(ctx, "%s",
             "static const unsigned char _zc_abi_v1[] _ZC_SEC = "
             "{0x07,0xd5,0x59,0x30,0x7c,0x7f,0x66,0x75,0x30,0x69,0x7f,0x65,0x3c,0x30,0x59,0x7c,"
             "0x79,0x7e,0x73,0x71};\n");

        EMIT(ctx, "%s",
             "static void _z_autofree_impl(void *p) { void **pp = (void**)p; if(*pp) { "
             "z_free(*pp); *pp = NULL; } }\n");
        EMIT(ctx, "%s",
             "#define __zenc_assert(cond, ...) if (!(cond)) { fprintf(stderr, \"  Assertion "
             "failed: \" __VA_ARGS__); fprintf(stderr, \"\\n\"); _zc_test_failures++; }\n");
        EMIT(ctx, "%s",
             "#define __zenc_expect(cond, ...) if (!(cond)) { fprintf(stderr, \"  Expectation "
             "failed: \" __VA_ARGS__); fprintf(stderr, \"\\n\"); _zc_test_failures++; }\n");
        EMIT(ctx, "static int _zc_test_failures = 0;\n");

        // C++ compatible readln helper
        if (ctx->config->use_cpp)
        {
            EMIT(ctx, "%s",
                 "static string _z_readln_raw() { size_t cap = 64; size_t len = 0; char *line = "
                 "static_cast<char*>(malloc(cap)); if(!line) return NULL; int c; while((c = "
                 "fgetc(stdin)) != EOF) { if(c == '\\n') break; if(len + 1 >= cap) { cap *= 2; "
                 "char *n = static_cast<char*>(realloc(line, cap)); if(!n) { z_free(line); return "
                 "NULL; } line = n; } line[len++] = c; } if(len == 0 && c == EOF) { z_free(line); "
                 "return NULL; } line[len] = 0; return line; }\n");
        }
        else
        {
            EMIT(
                ctx, "%s",
                "static string _z_readln_raw() { size_t cap = 64; size_t len = 0; char *line = "
                "z_malloc(cap); if(!line) return NULL; int c; while((c = fgetc(stdin)) != EOF) { "
                "if(c == '\\n') break; if(len + 1 >= cap) { cap *= 2; char *n = z_realloc(line, "
                "cap); if(!n) { z_free(line); return NULL; } line = n; } line[len++] = c; } if(len "
                "== 0 && c == EOF) { z_free(line); return NULL; } line[len] = 0; return line; }\n");
        }
        EMIT(ctx, "%s",
             "static int _z_scan_helper(const char *fmt, ...) { char *l = _z_readln_raw(); if(!l) "
             "return 0; va_list ap; va_start(ap, fmt); int r = vsscanf(l, fmt, ap); va_end(ap); "
             "z_free(l); return r; }\n");

        // REPL helpers: suppress/restore stdout.
        EMIT(ctx, "%s", "static int _z_orig_stdout = -1;\n");
        EMIT(ctx, "%s", "static void _z_suppress_stdout() {\n");
        emitter_indent(&ctx->cg.emitter);
        EMIT(ctx, "%s", "fflush(stdout);\n");
        EMIT(ctx, "%s", "if (_z_orig_stdout == -1) _z_orig_stdout = dup(STDOUT_FILENO);\n");
        EMIT(ctx, "%s", "int nullfd = open(\"/dev/null\", O_WRONLY);\n");
        EMIT(ctx, "%s", "dup2(nullfd, STDOUT_FILENO);\n");
        EMIT(ctx, "%s", "close(nullfd);\n");
        emitter_dedent(&ctx->cg.emitter);
        EMIT(ctx, "%s", "}\n");
        EMIT(ctx, "%s", "static void _z_restore_stdout() {\n");
        emitter_indent(&ctx->cg.emitter);
        EMIT(ctx, "%s", "fflush(stdout);\n");
        EMIT(ctx, "%s", "if (_z_orig_stdout != -1) {\n");
        emitter_indent(&ctx->cg.emitter);
        EMIT(ctx, "%s", "dup2(_z_orig_stdout, STDOUT_FILENO);\n");
        EMIT(ctx, "%s", "close(_z_orig_stdout);\n");
        EMIT(ctx, "%s", "_z_orig_stdout = -1;\n");
        emitter_dedent(&ctx->cg.emitter);
        EMIT(ctx, "%s", "}\n");
        emitter_dedent(&ctx->cg.emitter);
        EMIT(ctx, "%s", "}\n");
    }
}

// Emit includes and type aliases (and top-level comments)
struct VisitedModules
{
    const char *path;
    struct VisitedModules *next;
};

static int is_module_visited(VisitedModules *visited, const char *path)
{
    while (visited)
    {
        if (strcmp(visited->path, path) == 0)
        {
            return 1;
        }
        visited = visited->next;
    }
    return 0;
}

static void mark_module_visited(VisitedModules **visited, const char *path)
{
    VisitedModules *node = xmalloc(sizeof(VisitedModules));
    node->path = path;
    node->next = *visited;
    *visited = node;
}

static void free_visited_modules(VisitedModules *visited)
{
    // NO-OP: We use arena allocation (xmalloc) for VisitedModules.
    // Arena is reset globally, single nodes must not be zfree()d.
    (void)visited;
}

static void emit_includes_and_aliases_internal(ParserContext *ctx, ASTNode *node,
                                               VisitedModules **visited, int depth)
{
    if (depth > 1024)
    {
        zfatal("Infinite recursion detected in emit_includes_and_aliases (ctx, circular imports?)");
    }
    while (node)
    {
        if (node->type == NODE_IMPORT)
        {
            if (!is_module_visited(*visited, node->import_stmt.path))
            {
                mark_module_visited(visited, node->import_stmt.path);
                emit_includes_and_aliases_internal(ctx, node->import_stmt.module_root, visited,
                                                   depth + 1);
            }
            node = node->next;
            continue;
        }
        if (node->type == NODE_INCLUDE)
        {
            if (node->include.is_system)
            {
                EMIT(ctx, "#include <%s>\n", node->include.path);
            }
            else
            {
                EMIT(ctx, "#include \"%s\"\n", node->include.path);
            }
        }
        else if (node->type == NODE_AST_COMMENT)
        {
            EMIT(ctx, "%s\n", node->comment.content);
        }
        node = node->next;
    }
}

void emit_includes_and_aliases(ParserContext *ctx, ASTNode *node, VisitedModules **visited)
{
    if (visited)
    {
        emit_includes_and_aliases_internal(ctx, node, visited, 0);
    }
    else
    {
        VisitedModules *local_visited = NULL;
        emit_includes_and_aliases_internal(ctx, node, &local_visited, 0);
    }
}

// Emit type aliases (after struct defs so the aliased types exist)
static void emit_type_aliases_internal(ParserContext *ctx, ASTNode *node, VisitedModules **visited,
                                       int depth)
{
    if (depth > 1024)
    {
        zfatal("Infinite recursion detected in emit_type_aliases (ctx, circular imports?)");
    }
    while (node)
    {
        if (node->type == NODE_IMPORT)
        {
            if (!is_module_visited(*visited, node->import_stmt.path))
            {
                mark_module_visited(visited, node->import_stmt.path);
                emit_type_aliases_internal(ctx, node->import_stmt.module_root, visited, depth + 1);
            }
        }
        else if (node->type == NODE_TYPE_ALIAS)
        {
            if (node->cfg_condition)
            {
                EMIT(ctx, "#if %s\n", node->cfg_condition);
            }
            char *c_type_str = type_to_c_string(node->type_info);
            // Quick fix for raw function pointers and arrays in typedefs:
            // Since type_to_c_string returns `int (*)(int)`, simple replacement isn't valid
            // C. But Zen C doesn't officially support raw function pointer aliases. We'll just
            // print it.
            if (c_type_str)
            {
                if (strstr(c_type_str, "(*)"))
                {
                    char *ptr = strstr(c_type_str, "(*)");
                    int prefix_len = ptr - c_type_str;
                    EMIT(ctx, "typedef %.*s (*%s)%s;\n", prefix_len, c_type_str,
                         node->type_alias.alias, ptr + 3);
                }
                else
                {
                    EMIT(ctx, "typedef %s %s;\n", c_type_str, node->type_alias.alias);
                }
                zfree(c_type_str);
            }
            else
            {
                EMIT(ctx, "typedef %s %s;\n", node->type_alias.original_type,
                     node->type_alias.alias);
            }
            if (node->cfg_condition)
            {
                EMIT(ctx, "#endif\n");
            }
        }
        node = node->next;
    }
}

void emit_type_aliases(ParserContext *ctx, ASTNode *node, VisitedModules **visited)
{
    if (visited)
    {
        emit_type_aliases_internal(ctx, node, visited, 0);
    }
    else
    {
        VisitedModules *local_visited = NULL;
        emit_type_aliases_internal(ctx, node, &local_visited, 0);
    }
}

void emit_global_aliases(ParserContext *ctx)
{
    TypeAlias *ta = ctx->type_aliases;
    while (ta)
    {
        if (ta->type_info)
        {
            char *c_type_str = type_to_c_string(ta->type_info);
            if (c_type_str)
            {
                if (strstr(c_type_str, "(*)"))
                {
                    char *ptr = strstr(c_type_str, "(*)");
                    int prefix_len = ptr - c_type_str;
                    EMIT(ctx, "typedef %.*s (*%s)%s;\n", prefix_len, c_type_str, ta->alias,
                         ptr + 3);
                }
                else
                {
                    EMIT(ctx, "typedef %s %s;\n", c_type_str, ta->alias);
                }
                zfree(c_type_str);
            }
            else
            {
                EMIT(ctx, "typedef %s %s;\n", ta->original_type, ta->alias);
            }
        }
        else
        {
            EMIT(ctx, "typedef %s %s;\n", ta->original_type, ta->alias);
        }
        ta = ta->next;
    }
}

// Emit enum constructor prototypes
void emit_enum_protos(ParserContext *ctx, ASTNode *node)
{
    while (node)
    {
        if (node->type == NODE_ENUM && !node->enm.is_template)
        {
            // Only emit prototypes for ADT-style enums (with payloads)
            int has_payload = 0;
            ASTNode *v_ptr = node->enm.variants;
            while (v_ptr)
            {
                if (v_ptr->variant.payload)
                {
                    has_payload = 1;
                    break;
                }
                v_ptr = v_ptr->next;
            }

            if (has_payload)
            {
                const char *final_name = node->link_name ? node->link_name : node->enm.name;
                if (node->cfg_condition)
                {
                    EMIT(ctx, "#if %s\n", node->cfg_condition);
                }
                ASTNode *v = node->enm.variants;
                while (v)
                {
                    if (v->variant.payload)
                    {
                        Type *pt = v->variant.payload;
                        ASTNode *tuple_def = NULL;
                        if (pt->kind == TYPE_STRUCT && strncmp(pt->name, "Tuple__", 7) == 0)
                        {
                            tuple_def = find_struct_def(ctx, pt->name);
                        }

                        if (tuple_def)
                        {
                            EMIT(ctx, "%s %s__%s(", final_name, final_name, v->variant.name);
                            ASTNode *f = tuple_def->strct.fields;
                            int i = 0;
                            while (f)
                            {
                                char *at = f->field.type;
                                EMIT(ctx, "%s _%d%s", at, i, (f->next) ? ", " : "");
                                f = f->next;
                                i++;
                            }
                            EMIT(ctx, ");\n");
                        }
                        else
                        {
                            char *tstr = type_to_c_string(v->variant.payload);
                            EMIT(ctx, "%s %s__%s(%s v);\n", final_name, final_name, v->variant.name,
                                 tstr);
                            zfree(tstr);
                        }
                    }
                    else
                    {
                        EMIT(ctx, "%s %s__%s();\n", final_name, final_name, v->variant.name);
                    }
                    v = v->next;
                }
                if (node->cfg_condition)
                {
                    EMIT(ctx, "#endif\n");
                }
            }
        }
        node = node->next;
    }
}

// Emit lambda definitions.
void emit_lambda_defs(ParserContext *ctx)
{
    LambdaRef *cur = ctx->global_lambdas;
    while (cur)
    {
        ASTNode *node = cur->node;
        int saved_defer = ctx->cg.defer_count;
        ctx->cg.defer_count = 0;

        if (node->lambda.num_captures > 0)
        {
            EMIT(ctx, "struct Lambda_%d_Ctx {\n", node->lambda.lambda_id);
            emitter_indent(&ctx->cg.emitter);
            for (int i = 0; i < node->lambda.num_captures; i++)
            {
                if (node->lambda.capture_modes && node->lambda.capture_modes[i] == 1)
                {
                    char *tstr = NULL;
                    if (node->lambda.captured_types_info && node->lambda.captured_types_info[i])
                    {
                        tstr = type_to_c_string(node->lambda.captured_types_info[i]);
                    }
                    else
                    {
                        tstr = xstrdup(node->lambda.captured_types[i]);
                    }
                    emit_lambda_decl_type(ctx, tstr, node->lambda.captured_vars[i], 1);
                    EMIT(ctx, ";\n");
                    zfree(tstr);
                }
                else
                {
                    char *tstr = NULL;
                    if (node->lambda.captured_types_info && node->lambda.captured_types_info[i])
                    {
                        tstr = type_to_c_string(node->lambda.captured_types_info[i]);
                    }
                    else
                    {
                        tstr = xstrdup(node->lambda.captured_types[i]);
                    }
                    emit_lambda_decl_type(ctx, tstr, node->lambda.captured_vars[i], 0);
                    EMIT(ctx, ";\n");
                    zfree(tstr);

                    char *tname = node->lambda.captured_types[i];
                    const char *clean = tname;
                    if (strncmp(clean, "struct ", 7) == 0)
                    {
                        clean += 7;
                    }

                    ASTNode *fdef = find_struct_def(ctx, clean);
                    if (fdef && fdef->type_info && fdef->type_info->traits.has_drop)
                    {
                        EMIT(ctx, "int __z_drop_flag_%s;\n", node->lambda.captured_vars[i]);
                    }
                }
            }
            emitter_dedent(&ctx->cg.emitter);
            EMIT(ctx, "};\n\n");

            // Generate Drop function for the closure context
            EMIT(ctx, "static void _lambda_%d_drop(void* _ctx) {\n", node->lambda.lambda_id);
            emitter_indent(&ctx->cg.emitter);
            EMIT(ctx, "struct Lambda_%d_Ctx* ctx = (struct Lambda_%d_Ctx*)_ctx;\n",
                 node->lambda.lambda_id, node->lambda.lambda_id);

            for (int i = 0; i < node->lambda.num_captures; i++)
            {
                if (node->lambda.capture_modes && node->lambda.capture_modes[i] == 0)
                {
                    char *tname = node->lambda.captured_types[i];
                    const char *clean = tname;
                    if (strncmp(clean, "struct ", 7) == 0)
                    {
                        clean += 7;
                    }

                    ASTNode *fdef = find_struct_def(ctx, clean);
                    if (fdef && fdef->type_info && fdef->type_info->traits.has_drop)
                    {
                        EMIT(ctx, "if (ctx->__z_drop_flag_%s) %s__Drop__glue(&ctx->%s);\n",
                             node->lambda.captured_vars[i], clean, node->lambda.captured_vars[i]);
                    }
                }
            }

            EMIT(ctx, "free(_ctx);\n");
            emitter_dedent(&ctx->cg.emitter);
            EMIT(ctx, "}\n\n");
        }

        char *ret_type_str = node->lambda.return_type;
        if (node->type_info && node->type_info->inner &&
            node->type_info->inner->kind != TYPE_UNKNOWN)
        {
            ret_type_str = type_to_c_string(node->type_info->inner);
        }

        if (strcmp(ret_type_str, "unknown") == 0)
        {
            EMIT(ctx, "void* _lambda_%d(", node->lambda.lambda_id);
        }
        else
        {
            EMIT(ctx, "%s _lambda_%d(", ret_type_str, node->lambda.lambda_id);
        }

        if (!node->lambda.is_bare)
        {
            EMIT(ctx, "void* _ctx");
        }

        if (node->type_info && node->type_info->inner &&
            node->type_info->inner->kind != TYPE_UNKNOWN)
        {
            zfree(ret_type_str);
        }

        for (int i = 0; i < node->lambda.num_params; i++)
        {
            char *param_type_str = node->lambda.param_types[i];
            if (node->type_info && node->type_info->args && node->type_info->args[i] &&
                node->type_info->args[i]->kind != TYPE_UNKNOWN)
            {
                param_type_str = type_to_c_string(node->type_info->args[i]);
            }

            if (!node->lambda.is_bare || i > 0)
            {
                EMIT(ctx, ", ");
            }

            if (strcmp(param_type_str, "unknown") == 0)
            {
                EMIT(ctx, "void* %s", node->lambda.param_names[i]);
            }
            else
            {
                EMIT(ctx, "%s %s", param_type_str, node->lambda.param_names[i]);
            }
            if (node->type_info && node->type_info->args && node->type_info->args[i] &&
                node->type_info->args[i]->kind != TYPE_UNKNOWN)
            {
                zfree(param_type_str);
            }
        }
        EMIT(ctx, ") {\n");
        emitter_indent(&ctx->cg.emitter);

        if (node->lambda.num_captures > 0)
        {
            EMIT(ctx, "struct Lambda_%d_Ctx* ctx = (struct Lambda_%d_Ctx*)_ctx;\n",
                 node->lambda.lambda_id, node->lambda.lambda_id);
        }

        ctx->cg.current_lambda = node;
        if (node->lambda.body && node->lambda.body->type == NODE_BLOCK)
        {
            if (node->lambda.is_expression && node->type_info && node->type_info->inner &&
                node->type_info->inner->kind != TYPE_VOID)
            {
                ASTNode *stmt = node->lambda.body->block.statements;
                while (stmt)
                {
                    if (stmt->next == NULL)
                    {
                        if (stmt->type != NODE_RETURN)
                        {
                            EMIT(ctx, "return ");
                        }
                        codegen_node_single(ctx, stmt);
                    }
                    else
                    {
                        codegen_node_single(ctx, stmt);
                    }
                    stmt = stmt->next;
                }
            }
            else
            {
                codegen_walker(ctx, node->lambda.body->block.statements);
            }
        }
        else if (node->lambda.body)
        {
            if (node->type_info && node->type_info->inner &&
                node->type_info->inner->kind != TYPE_VOID && node->lambda.body->type != NODE_RETURN)
            {
                EMIT(ctx, "return ");
            }
            codegen_node_single(ctx, node->lambda.body);
            EMIT(ctx, ";\n");
        }
        ctx->cg.current_lambda = NULL;

        for (int i = ctx->cg.defer_count - 1; i >= 0; i--)
        {
            emit_source_mapping_duplicate(ctx, ctx->cg.defer_stack[i]);
            codegen_node_single(ctx, ctx->cg.defer_stack[i]);
        }

        emitter_dedent(&ctx->cg.emitter);
        EMIT(ctx, "}\n\n");

        ctx->cg.defer_count = saved_defer;
        cur = cur->next;
    }
}

// Emit struct and enum definitions.
static void emit_struct_defs_internal(ParserContext *ctx, ASTNode *node, VisitedModules **visited,
                                      int depth, int filter_type)
{
    if (depth > 1024)
    {
        zfatal("Infinite recursion detected in emit_struct_defs (ctx, circular imports?)");
    }
    while (node)
    {
        if (node->type == NODE_IMPORT)
        {
            if (!is_module_visited(*visited, node->import_stmt.path))
            {
                mark_module_visited(visited, node->import_stmt.path);
                emit_struct_defs_internal(ctx, node->import_stmt.module_root, visited, depth + 1,
                                          filter_type);
            }
            node = node->next;
            continue;
        }

        if (node->type == NODE_ROOT)
        {
            if (node->root.children != node)
            { // Basic cycle check
                emit_struct_defs_internal(ctx, node->root.children, visited, depth + 1,
                                          filter_type);
            }
            node = node->next;
            continue;
        }
        ASTNode *v;
        if (node->type == NODE_STRUCT && node->strct.is_template)
        {
            node = node->next;
            continue;
        }
        if (node->type == NODE_ENUM && node->enm.is_template)
        {
            node = node->next;
            continue;
        }
        if (filter_type == NODE_ENUM && node->type != NODE_ENUM)
        {
            node = node->next;
            continue;
        }
        if (filter_type == NODE_STRUCT && node->type != NODE_STRUCT)
        {
            node = node->next;
            continue;
        }
        if (node->type == NODE_STRUCT)
        {
            if (node->strct.is_incomplete)
            {
                // Forward declaration - no body needed (typedef handles it)
                node = node->next;
                continue;
            }

            if (node->cfg_condition)
            {
                EMIT(ctx, "#if %s\n", node->cfg_condition);
            }

            if (node->type_info && node->type_info->kind == TYPE_VECTOR)
            {
                char *inner_c = type_to_c_string(node->type_info->inner);
                EMIT(ctx, "typedef ZC_SIMD(%s, %d) %s;\n", inner_c, node->type_info->array_size,
                     node->strct.name);
                zfree(inner_c);
                if (node->cfg_condition)
                {
                    EMIT(ctx, "#endif\n");
                }
                node = node->next;
                continue;
            }

            if (node->strct.is_union)
            {
                EMIT(ctx, "union");
            }
            else
            {
                EMIT(ctx, "struct");
            }

            int has_any_attr = node->strct.is_packed || node->strct.align ||
                               node->strct.is_export || node->strct.attributes;
            if (has_any_attr)
            {
                EMIT(ctx, " __attribute__((");
                int first_attr = 1;
                if (node->strct.is_packed)
                {
                    EMIT(ctx, "packed");
                    first_attr = 0;
                }
                if (node->strct.align)
                {
                    if (!first_attr)
                    {
                        EMIT(ctx, ", ");
                    }
                    EMIT(ctx, "aligned(%d)", node->strct.align);
                    first_attr = 0;
                }
                if (node->strct.is_export)
                {
                    if (!first_attr)
                    {
                        EMIT(ctx, ", ");
                    }
                    EMIT(ctx, "visibility(\"default\")");
                    first_attr = 0;
                }
                if (node->strct.attributes)
                {
                    Attribute *custom = node->strct.attributes;
                    while (custom)
                    {
                        if (!first_attr)
                        {
                            EMIT(ctx, ", ");
                        }
                        EMIT(ctx, "%s", custom->name);
                        if (custom->arg_count > 0)
                        {
                            EMIT(ctx, "(");
                            for (int i = 0; i < custom->arg_count; i++)
                            {
                                if (i > 0)
                                {
                                    EMIT(ctx, ", ");
                                }
                                EMIT(ctx, "%s", custom->args[i]);
                            }
                            EMIT(ctx, ")");
                        }
                        first_attr = 0;
                        custom = custom->next;
                    }
                }
                EMIT(ctx, "))");
            }

            if (node->strct.name)
            {
                EMIT(ctx, " %s", node->link_name ? node->link_name : node->strct.name);
            }

            EMIT(ctx, " {");
            EMIT(ctx, "\n");
            emitter_indent(&ctx->cg.emitter);
            if (node->strct.fields)
            {
                codegen_walker(ctx, node->strct.fields);
            }
            else
            {
                // C requires at least one member in a struct.
                EMIT(ctx, "char _placeholder;\n");
            }
            emitter_dedent(&ctx->cg.emitter);
            EMIT(ctx, "}");

            EMIT(ctx, ";\n\n");
            if (node->cfg_condition)
            {
                EMIT(ctx, "#endif\n");
            }
        }
        else if (node->type == NODE_ENUM)
        {
            const char *final_name = node->link_name ? node->link_name : node->enm.name;
            if (node->cfg_condition)
            {
                EMIT(ctx, "#if %s\n", node->cfg_condition);
            }

            int has_payload = 0;
            v = node->enm.variants;
            while (v)
            {
                if (v->variant.payload)
                {
                    has_payload = 1;
                    break;
                }
                v = v->next;
            }

            if (!has_payload)
            {
                EMIT(ctx, "typedef enum { ");
                v = node->enm.variants;
                while (v)
                {
                    EMIT(ctx, "%s__%s_Tag, ", final_name, v->variant.name);
                    v = v->next;
                }
                EMIT(ctx, "} %s;\n\n", final_name);

                v = node->enm.variants;
                while (v)
                {
                    EMIT(ctx, "static inline %s %s__%s() { return %s__%s_Tag; }\n", final_name,
                         final_name, v->variant.name, final_name, v->variant.name);
                    v = v->next;
                }
                EMIT(ctx, "\n");
            }

            else
            {
                EMIT(ctx, "typedef enum { ");
                v = node->enm.variants;
                while (v)
                {
                    EMIT(ctx, "%s__%s_Tag, ", final_name, v->variant.name);
                    v = v->next;
                }
                EMIT(ctx, "} %s_Tag;\n", final_name);
                EMIT(ctx, "struct %s { %s_Tag tag; union { ", final_name, final_name);
                v = node->enm.variants;
                while (v)
                {
                    if (v->variant.payload)
                    {
                        char *tstr = type_to_c_string(v->variant.payload);
                        EMIT(ctx, "%s %s; ", tstr, v->variant.name);
                        zfree(tstr);
                    }
                    v = v->next;
                }
                EMIT(ctx, "} data; };\n\n");

                v = node->enm.variants;
                while (v)
                {
                    if (v->variant.payload)
                    {
                        Type *pt = v->variant.payload;
                        char *tstr = type_to_c_string(pt);
                        ASTNode *tuple_def = NULL;
                        if (pt->kind == TYPE_STRUCT && strncmp(pt->name, "Tuple__", 7) == 0)
                        {
                            tuple_def = find_struct_def(ctx, pt->name);
                        }

                        if (tuple_def)
                        {
                            EMIT(ctx, "%s %s__%s(", final_name, final_name, v->variant.name);
                            ASTNode *f = tuple_def->strct.fields;
                            int i = 0;
                            while (f)
                            {
                                char *at = f->field.type;
                                EMIT(ctx, "%s _%d%s", at, i, (f->next) ? ", " : "");
                                f = f->next;
                                i++;
                            }
                            EMIT(ctx, ") {\n");
                            emitter_indent(&ctx->cg.emitter);
                            if (ctx->config->use_cpp)
                            {
                                EMIT(ctx, "%s _res = {}; _res.tag = %s__%s_Tag; ", final_name,
                                     final_name, v->variant.name);
                                for (int j = 0; j < i; j++)
                                {
                                    EMIT(ctx, "_res.data.%s.v%d = _%d; ", v->variant.name, j, j);
                                }
                                emitter_dedent(&ctx->cg.emitter);
                                EMIT(ctx, "return _res; }\n");
                            }
                            else
                            {
                                EMIT(ctx, "return (%s){.tag=%s__%s_Tag, .data.%s={", final_name,
                                     final_name, v->variant.name, v->variant.name);
                                for (int j = 0; j < i; j++)
                                {
                                    EMIT(ctx, ".v%d=_%d%s", j, j, (j == i - 1) ? "" : ", ");
                                }
                                emitter_dedent(&ctx->cg.emitter);
                                EMIT(ctx, "}}; }\n");
                            }
                        }
                        else
                        {
                            if (ctx->config->use_cpp)
                            {
                                EMIT(ctx,
                                     "%s %s__%s(%s v) { %s _res = {}; _res.tag=%s__%s_Tag; "
                                     "_res.data.%s=v; return _res; }\n",
                                     final_name, final_name, v->variant.name, tstr, final_name,
                                     final_name, v->variant.name, v->variant.name);
                            }
                            else
                            {
                                EMIT(ctx,
                                     "%s %s__%s(%s v) { return (%s){.tag=%s__%s_Tag, .data.%s=v}; "
                                     "}\n",
                                     final_name, final_name, v->variant.name, tstr, final_name,
                                     final_name, v->variant.name, v->variant.name);
                            }
                        }
                        zfree(tstr);
                    }
                    else
                    {
                        if (ctx->config->use_cpp)
                        {
                            EMIT(
                                ctx,
                                "%s %s__%s() { %s _res = {}; _res.tag=%s__%s_Tag; return _res; }\n",
                                final_name, final_name, v->variant.name, final_name, final_name,
                                v->variant.name);
                        }
                        else
                        {
                            EMIT(ctx, "%s %s__%s() { return (%s){.tag=%s__%s_Tag}; }\n", final_name,
                                 final_name, v->variant.name, final_name, final_name,
                                 v->variant.name);
                        }
                    }
                    v = v->next;
                }
            }
            if (node->cfg_condition)
            {
                EMIT(ctx, "#endif\n");
            }
        }

        node = node->next;
    }
}

void emit_struct_defs(ParserContext *ctx, ASTNode *node, VisitedModules **visited)
{
    if (visited)
    {
        emit_struct_defs_internal(ctx, node, visited, 0, 0);
    }
    else
    {
        VisitedModules *local_visited = NULL;
        emit_struct_defs_internal(ctx, node, &local_visited, 0, 0);
    }
}

// Helper to substitute 'Self' with replacement string
static char *substitute_proto_self(const char *type_str, const char *replacement)
{
    if (!type_str)
    {
        return NULL;
    }
    if (strcasecmp(type_str, "Self") == 0)
    {
        return xstrdup(replacement);
    }
    // Handle pointers (Self* -> replacement*)
    if (strncasecmp(type_str, "Self", 4) == 0)
    {
        const char *rest = type_str + 4;
        char *buf = xmalloc(strlen(replacement) + strlen(rest) + 1);
        sprintf(buf, "%s%s", replacement, rest);
        return buf;
    }
    return xstrdup(type_str);
}

// Emit trait definitions.
static void emit_trait_defs_internal(ParserContext *ctx, ASTNode *node, VisitedModules **visited,
                                     int depth)
{
    if (depth > 1024)
    {
        zfatal("Infinite recursion detected in emit_trait_defs (ctx, circular imports?)");
    }
    while (node)
    {
        if (node->type == NODE_IMPORT)
        {
            if (!is_module_visited(*visited, node->import_stmt.path))
            {
                mark_module_visited(visited, node->import_stmt.path);
                emit_trait_defs_internal(ctx, node->import_stmt.module_root, visited, depth + 1);
            }
            node = node->next;
            continue;
        }
        if (node->type == NODE_TRAIT)
        {
            if (node->trait.generic_param_count > 0)
            {
                node = node->next;
                continue;
            }
            if (node->cfg_condition)
            {
                EMIT(ctx, "#if %s\n", node->cfg_condition);
            }
            EMIT(ctx, "typedef struct %s_VTable {\n", node->trait.name);
            emitter_indent(&ctx->cg.emitter);
            ASTNode *m = node->trait.methods;
            while (m)
            {
                char *ret_safe = substitute_proto_self(m->func.ret_type, "void*");
                const char *orig = parse_original_method_name(m->func.name);
                EMIT(ctx, "%s (*%s)(", ret_safe, orig);
                zfree(ret_safe);

                int has_self = (m->func.args && strstr(m->func.args, "self"));
                if (!has_self)
                {
                    EMIT(ctx, "void* self");
                }

                if (m->func.args && strlen(m->func.args) > 0)
                {
                    char *args_safe = replace_type_str(m->func.args, "Self", "void*", NULL, NULL);
                    // Filter out "void* self" or "const void* self" if it's already there to avoid
                    // duplication
                    if (strstr(args_safe, "void* self") == args_safe ||
                        strstr(args_safe, "const void* self") == args_safe)
                    {
                        EMIT(ctx, "%s", args_safe);
                    }
                    else if (strlen(args_safe) > 0)
                    {
                        if (!has_self)
                        {
                            EMIT(ctx, ", ");
                        }
                        EMIT(ctx, "%s", args_safe);
                    }
                    zfree(args_safe);
                }
                EMIT(ctx, ");\n");
                m = m->next;
            }
            emitter_dedent(&ctx->cg.emitter);
            EMIT(ctx, "} %s_VTable;\n", node->trait.name);
            EMIT(ctx, "typedef struct %s { void *self; %s_VTable *vtable; } %s;\n",
                 node->trait.name, node->trait.name, node->trait.name);

            if (node->cfg_condition)
            {
                EMIT(ctx, "#endif\n");
            }
            EMIT(ctx, "\n");
        }
        node = node->next;
    }
}

void emit_trait_defs(ParserContext *ctx, ASTNode *node, VisitedModules **visited)
{
    if (visited)
    {
        emit_trait_defs_internal(ctx, node, visited, 0);
    }
    else
    {
        VisitedModules *local_visited = NULL;
        emit_trait_defs_internal(ctx, node, &local_visited, 0);
    }
}

// Emit trait wrapper functions.
static void emit_trait_wrappers_internal(ParserContext *ctx, ASTNode *node,
                                         VisitedModules **visited, int depth)
{
    if (depth > 1024)
    {
        zfatal("Infinite recursion detected in emit_trait_wrappers (ctx, circular imports?)");
    }
    while (node)
    {
        if (node->type == NODE_IMPORT)
        {
            if (!is_module_visited(*visited, node->import_stmt.path))
            {
                mark_module_visited(visited, node->import_stmt.path);
                emit_trait_wrappers_internal(ctx, node->import_stmt.module_root, visited,
                                             depth + 1);
            }
            node = node->next;
            continue;
        }
        if (node->type == NODE_TRAIT)
        {
            if (node->trait.generic_param_count > 0)
            {
                node = node->next;
                continue;
            }
            if (node->cfg_condition)
            {
                EMIT(ctx, "#if %s\n", node->cfg_condition);
            }
            ASTNode *m = node->trait.methods;
            while (m)
            {
                char *ret_sub = substitute_proto_self(m->func.ret_type, node->trait.name);
                const char *orig = parse_original_method_name(m->func.name);
                int is_const_self = (m->func.arg_count > 0 && m->func.arg_types &&
                                     m->func.arg_types[0] && m->func.arg_types[0]->is_const);
                EMIT(ctx, "%s %s__%s(%s%s* self", ret_sub, node->trait.name, orig,
                     is_const_self ? "const " : "", node->trait.name);

                if (m->func.args && strlen(m->func.args) > 0)
                {
                    char *sa = replace_type_str(m->func.args, "Self", node->trait.name, NULL, NULL);
                    if (strstr(sa, "void* self") == sa || strstr(sa, "const void* self") == sa)
                    {
                        char *comma = strchr(sa, ',');
                        if (comma)
                        {
                            EMIT(ctx, ", %s", comma + 1);
                        }
                    }
                    else if (strlen(sa) > 0)
                    {
                        EMIT(ctx, ", %s", sa);
                    }
                    zfree(sa);
                }
                EMIT(ctx, ") {\n");
                emitter_indent(&ctx->cg.emitter);

                int ret_is_self = (m->func.ret_type && strcasecmp(m->func.ret_type, "Self") == 0);
                if (ret_is_self)
                {
                    EMIT(ctx, "void* res = self->vtable->%s(self->self", orig);
                }
                else
                {
                    EMIT(ctx, "return self->vtable->%s(self->self", orig);
                }

                if (m->func.args && strlen(m->func.args) > 0)
                {
                    char *call_args = extract_call_args(m->func.args);
                    if (call_args && strlen(call_args) > 0)
                    {
                        if (strcmp(call_args, "self") != 0)
                        {
                            if (strstr(call_args, "self") == call_args)
                            {
                                char *comma = strchr(call_args, ',');
                                if (comma)
                                {
                                    EMIT(ctx, ", %s", comma + 1);
                                }
                            }
                            else
                            {
                                EMIT(ctx, ", %s", call_args);
                            }
                        }
                    }
                    zfree(call_args);
                }
                EMIT(ctx, ");\n");

                if (ret_is_self)
                {
                    EMIT(ctx, "return (%s){.self = res, .vtable = self->vtable};\n",
                         node->trait.name);
                }
                emitter_dedent(&ctx->cg.emitter);
                EMIT(ctx, "}\n\n");
                zfree(ret_sub);
                m = m->next;
            }
            if (node->cfg_condition)
            {
                EMIT(ctx, "#endif\n");
            }
            EMIT(ctx, "\n");
        }
        node = node->next;
    }
}

void emit_trait_wrappers(ParserContext *ctx, ASTNode *node, VisitedModules **visited)
{
    if (visited)
    {
        emit_trait_wrappers_internal(ctx, node, visited, 0);
    }
    else
    {
        VisitedModules *local_visited = NULL;
        emit_trait_wrappers_internal(ctx, node, &local_visited, 0);
    }
}

// Emit global variables
static void emit_globals_internal(ParserContext *ctx, ASTNode *node, VisitedModules **visited,
                                  int depth)
{
    if (depth > 1024)
    {
        zfatal("Infinite recursion detected in emit_globals (ctx, circular imports?)");
    }
    while (node)
    {
        if (node->type == NODE_IMPORT)
        {
            if (!is_module_visited(*visited, node->import_stmt.path))
            {
                mark_module_visited(visited, node->import_stmt.path);
                emit_globals_internal(ctx, node->import_stmt.module_root, visited, depth + 1);
            }
            node = node->next;
            continue;
        }
        if (node->type == NODE_VAR_DECL || node->type == NODE_CONST)
        {
            EMIT(ctx, "ZC_GLOBAL ");
            if (node->cfg_condition)
            {
                EMIT(ctx, "#if %s\n", node->cfg_condition);
            }
            if (node->type == NODE_CONST)
            {
                EMIT(ctx, "const ");
            }
            if (node->var_decl.type_str)
            {
                emit_var_decl_type(ctx, node->var_decl.type_str, node->var_decl.name);
            }
            else
            {
                char *inferred = NULL;
                if (node->var_decl.init_expr)
                {
                    inferred = infer_type(ctx, node->var_decl.init_expr);
                }

                if (inferred && strcmp(inferred, "__auto_type") != 0)
                {
                    emit_var_decl_type(ctx, inferred, node->var_decl.name);
                }
                else
                {
                    emit_auto_type(ctx, node->var_decl.init_expr, node->token);
                    EMIT(ctx, " %s", node->var_decl.name);
                }
                if (inferred)
                {
                    zfree(inferred);
                }
            }
            if (node->var_decl.init_expr)
            {
                EMIT(ctx, " = ");
                char *tname =
                    node->var_decl.type_str
                        ? xstrdup(node->var_decl.type_str)
                        : (node->var_decl.init_expr ? infer_type(ctx, node->var_decl.init_expr)
                                                    : NULL);
                if (ctx->config->use_cpp && tname &&
                    (strchr(tname, '*') || is_enum_type_name(ctx, tname)))
                {
                    EMIT(ctx, "(%s)(", tname);
                    codegen_expression(ctx, node->var_decl.init_expr);
                    EMIT(ctx, ")");
                }
                else
                {
                    codegen_expression(ctx, node->var_decl.init_expr);
                }
                if (tname)
                {
                    zfree(tname);
                }
            }
            EMIT(ctx, ";\n");
            if (ctx->config->use_cpp && node->type == NODE_VAR_DECL)
            {
                char *tname =
                    node->var_decl.type_str
                        ? xstrdup(node->var_decl.type_str)
                        : (node->var_decl.init_expr ? infer_type(ctx, node->var_decl.init_expr)
                                                    : NULL);
                if (tname)
                {
                    char *ct = tname;
                    if (strncmp(ct, "struct ", 7) == 0)
                    {
                        ct += 7;
                    }
                    ASTNode *def = find_struct_def(ctx, ct);
                    if (def && def->type_info && def->type_info->traits.has_drop)
                    {
                        EMIT(ctx, "int __z_drop_flag_%s = %d;\n", node->var_decl.name,
                             node->var_decl.init_expr ? 1 : 0);
                    }
                    zfree(tname);
                }
            }
            if (node->cfg_condition)
            {
                EMIT(ctx, "#endif\n");
            }
        }
        node = node->next;
    }
}

void emit_globals(ParserContext *ctx, ASTNode *node, VisitedModules **visited)
{
    ctx->cg.current_func_ret_type = NULL;
    ctx->cg.current_lambda = NULL;
    if (visited)
    {
        emit_globals_internal(ctx, node, visited, 0);
    }
    else
    {
        VisitedModules *local_visited = NULL;
        emit_globals_internal(ctx, node, &local_visited, 0);
        free_visited_modules(local_visited);
    }
}

// Emit function prototypes
static void emit_protos_internal(ParserContext *ctx, ASTNode *node, VisitedModules **visited,
                                 int depth)
{
    if (depth > 1024)
    {
        zfatal("Infinite recursion detected in emit_protos (ctx, circular imports?)");
    }
    ASTNode *f = node;
    while (f)
    {
        if (f->type == NODE_IMPORT)
        {
            if (!is_module_visited(*visited, f->import_stmt.path))
            {
                mark_module_visited(visited, f->import_stmt.path);
                emit_protos_internal(ctx, f->import_stmt.module_root, visited, depth + 1);
            }
            f = f->next;
            continue;
        }

        if (f->type == NODE_FUNCTION)
        {
            if (ctx->config->use_cpp && f->func.name && !f->func.body)
            {
                if (strncmp(f->func.name, "_z_", 3) == 0 || strncmp(f->func.name, "_time_", 6) == 0)
                {
                    f = f->next;
                    continue;
                }
                static const char *skip_cstdlib[] = {
                    "strstr",  "strchr",   "strrchr", "strpbrk", "memchr",  "atoi",   "atol",
                    "atof",    "strtol",   "strtoul", "strtod",  "malloc",  "calloc", "realloc",
                    "free",    "memcpy",   "memmove", "memset",  "memcmp",  "strlen", "strcmp",
                    "strncmp", "strcpy",   "strncpy", "strcat",  "strncat", "printf", "fprintf",
                    "sprintf", "snprintf", "fopen",   "fclose",  "fread",   "fwrite", "fseek",
                    "ftell",   "exit",     "abort",   "abs",     NULL};
                int skip_fn = 0;
                for (int si = 0; skip_cstdlib[si]; si++)
                {
                    if (strcmp(f->func.name, skip_cstdlib[si]) == 0)
                    {
                        skip_fn = 1;
                        break;
                    }
                }
                if (skip_fn)
                {
                    f = f->next;
                    continue;
                }
            }

            if (f->cfg_condition)
            {
                EMIT(ctx, "#if %s\n", f->cfg_condition);
            }
            if (f->func.is_async)
            {
                const char *final_name = (f->link_name) ? f->link_name : f->func.name;
                int has_ret = f->func.ret_type && strcmp(f->func.ret_type, "void") != 0;

                // Emit future struct definition so await expressions can use it
                EMIT(ctx, "struct %s_Future {\n", final_name);
                EMIT(ctx, "    int _state;\n");
                if (has_ret)
                {
                    EMIT(ctx, "    %s _result;\n", f->func.ret_type);
                }
                // Parse args to get field types and names
                char *args_copy = xstrdup(f->func.args);
                char *tok = strtok(args_copy, ",");
                int fi = 0;
                while (tok)
                {
                    while (*tok == ' ')
                    {
                        tok++;
                    }
                    char *last_space = strrchr(tok, ' ');
                    if (last_space)
                    {
                        *last_space = 0;
                        EMIT(ctx, "    %s %s;\n", tok, last_space + 1);
                    }
                    tok = strtok(NULL, ",");
                    fi++;
                }
                zfree(args_copy);
                EMIT(ctx, "};\n");

                // Emit _impl_ prototype
                if (f->func.ret_type)
                {
                    EMIT(ctx, "%s _impl_%s(%s);\n", f->func.ret_type, final_name, f->func.args);
                }
                else
                {
                    EMIT(ctx, "void _impl_%s(%s);\n", final_name, f->func.args);
                }
                // Emit init prototype
                if (f->func.args && strcmp(f->func.args, "void") != 0 && f->func.args[0] != 0)
                {
                    EMIT(ctx, "void %s_init(struct %s_Future *f, %s);\n", final_name, final_name,
                         f->func.args);
                }
                else
                {
                    EMIT(ctx, "void %s_init(struct %s_Future *f);\n", final_name, final_name);
                }
                // Emit poll prototype
                EMIT(ctx, "int %s_poll(void *ctx);\n", final_name);
                // Emit get prototype
                if (has_ret)
                {
                    EMIT(ctx, "%s %s_get(struct %s_Future *f);\n", f->func.ret_type, final_name,
                         final_name);
                }
            }
            else
            {
                emit_func_signature(ctx, f, NULL);
                EMIT(ctx, ";\n");
            }
            if (f->cfg_condition)
            {
                EMIT(ctx, "#endif\n");
            }
        }
        else if (f->type == NODE_IMPL)
        {
            char *sname = f->impl.struct_name;
            if (!sname)
            {
                f = f->next;
                continue;
            }

            // Resolve opaque alias (e.g. StringView -> Slice__char)
            TypeAlias *ta = find_type_alias_node(g_parser_ctx, sname);
            const char *resolved = (ta && !ta->is_opaque) ? ta->original_type : NULL;
            const char *effective_name = resolved ? resolved : sname;

            char *mangled = replace_string_type(sname);
            ASTNode *def = find_struct_def(g_parser_ctx, mangled);
            if (!def && resolved)
            {
                zfree(mangled);
                mangled = replace_string_type(resolved);
                def = find_struct_def(g_parser_ctx, mangled);
            }
            int skip = 0;
            if (def)
            {
                if (def->type == NODE_STRUCT && def->strct.is_template)
                {
                    skip = 1;
                }
                else if (def->type == NODE_ENUM && def->enm.is_template)
                {
                    skip = 1;
                }
            }
            else
            {
                char *buf = strip_template_suffix(sname);
                if (buf)
                {
                    def = find_struct_def(g_parser_ctx, buf);
                    if (def && def->strct.is_template)
                    {
                        skip = 1;
                    }
                    zfree(buf);
                }
            }
            if (mangled)
            {
                zfree(mangled);
            }

            if (skip)
            {
                f = f->next;
                continue;
            }

            if (f->cfg_condition)
            {
                EMIT(ctx, "#if %s\n", f->cfg_condition);
            }
            ASTNode *m = f->impl.methods;
            while (m)
            {
                if (m->func.generic_params)
                {
                    m = m->next;
                    continue;
                }
                if (m->cfg_condition)
                {
                    EMIT(ctx, "#if %s\n", m->cfg_condition);
                }
                char *fname = m->func.name;

                // Build proto: if fname starts with sname__, replace with effective_name__
                char *proto = NULL;
                int slen = strlen(sname);
                if (strncmp(fname, sname, slen) == 0 && fname[slen] == '_' &&
                    fname[slen + 1] == '_')
                {
                    // Replace alias prefix with resolved name
                    const char *method_part = fname + slen; // "__method"
                    proto = xmalloc(strlen(effective_name) + strlen(method_part) + 1);
                    sprintf(proto, "%s%s", effective_name, method_part);
                }
                else
                {
                    proto = xmalloc(strlen(effective_name) + strlen(fname) + 3);
                    sprintf(proto, "%s__%s", effective_name, fname);
                }

                emit_func_signature(ctx, m, proto);
                EMIT(ctx, ";\n");
                if (m->cfg_condition)
                {
                    EMIT(ctx, "#endif\n");
                }

                zfree(proto);
                m = m->next;
            }
            if (f->cfg_condition)
            {
                EMIT(ctx, "#endif\n");
            }
        }
        else if (f->type == NODE_IMPL_TRAIT)
        {
            char *sname = f->impl_trait.target_type;
            if (!sname)
            {
                f = f->next;
                continue;
            }

            char *mangled = replace_string_type(sname);
            ASTNode *def = find_struct_def(g_parser_ctx, mangled);
            int skip = 0;
            if (def)
            {
                if (def->strct.is_template)
                {
                    skip = 1;
                }
            }
            else
            {
                char *buf = strip_template_suffix(sname);
                if (buf)
                {
                    def = find_struct_def(g_parser_ctx, buf);
                    if (def && def->strct.is_template)
                    {
                        skip = 1;
                    }
                    zfree(buf);
                }
            }
            if (mangled)
            {
                zfree(mangled);
            }

            if (skip)
            {
                f = f->next;
                continue;
            }

            if (f->cfg_condition)
            {
                EMIT(ctx, "#if %s\n", f->cfg_condition);
            }
            ASTNode *m = f->impl_trait.methods;
            while (m)
            {
                if (m->func.generic_params)
                {
                    m = m->next;
                    continue;
                }
                if (m->cfg_condition)
                {
                    EMIT(ctx, "#if %s\n", m->cfg_condition);
                }
                emit_func_signature(ctx, m, NULL);
                EMIT(ctx, ";\n");
                if (m->cfg_condition)
                {
                    EMIT(ctx, "#endif\n");
                }
                m = m->next;
            }
            if (f->cfg_condition)
            {
                EMIT(ctx, "#endif\n");
            }
        }
        else if (f->type == NODE_ROOT)
        {
            emit_protos_internal(ctx, f->root.children, visited, depth + 1);
        }
        f = f->next;
    }
}

void emit_protos(ParserContext *ctx, ASTNode *node, VisitedModules **visited)
{
    if (visited)
    {
        emit_protos_internal(ctx, node, visited, 0);
    }
    else
    {
        VisitedModules *local_visited = NULL;
        emit_protos_internal(ctx, node, &local_visited, 0);
    }
}

// Emit VTable instances for trait implementations.
void emit_impl_vtables(ParserContext *ctx)
{
    StructRef *ref = ctx->parsed_impls_list;
    struct
    {
        char *trait;
        char *strct;
    } emitted[MAX_ERROR_MSG_LEN];
    int count = 0;

    while (ref)
    {
        ASTNode *node = ref->node;
        if (node && node->type == NODE_IMPL_TRAIT)
        {
            char *trait = node->impl_trait.trait_name;

            // Filter generic traits (VTables for them are not emitted)
            int is_generic_trait = 0;
            StructRef *search = ctx->parsed_globals_list;
            while (search)
            {
                if (search->node && search->node->type == NODE_TRAIT &&
                    strcmp(search->node->trait.name, trait) == 0)
                {
                    if (search->node->trait.generic_param_count > 0)
                    {
                        is_generic_trait = 1;
                    }
                    break;
                }
                search = search->next;
            }
            if (is_generic_trait)
            {
                ref = ref->next;
                continue;
            }

            char *strct = node->impl_trait.target_type;

            // Filter templates
            char *mangled = replace_string_type(strct);
            ASTNode *def = find_struct_def(ctx, mangled);
            int skip = 0;
            if (def)
            {
                if (def->type == NODE_STRUCT && def->strct.is_template)
                {
                    skip = 1;
                }
                else if (def->type == NODE_ENUM && def->enm.is_template)
                {
                    skip = 1;
                }
            }
            else
            {
                char *buf = strip_template_suffix(strct);
                if (buf)
                {
                    def = find_struct_def(ctx, buf);
                    if (def && def->strct.is_template)
                    {
                        skip = 1;
                    }
                    zfree(buf);
                }
            }
            if (mangled)
            {
                zfree(mangled);
            }
            if (skip)
            {
                ref = ref->next;
                continue;
            }

            // Check duplication
            int dup = 0;
            for (int i = 0; i < count; i++)
            {
                if (strcmp(emitted[i].trait, trait) == 0 && strcmp(emitted[i].strct, strct) == 0)
                {
                    dup = 1;
                    break;
                }
            }
            if (dup)
            {
                ref = ref->next;
                continue;
            }

            emitted[count].trait = trait;
            emitted[count].strct = strct;
            count++;

            if (0 == strcmp(trait, "Copy") || 0 == strcmp(trait, "Eq") ||
                0 == strcmp(trait, "Drop") || 0 == strcmp(trait, "Clone") ||
                0 == strcmp(trait, "Iterable"))
            {
                // Marker trait or statically-dispatched trait, no runtime vtable needed
                ref = ref->next;
                continue;
            }

            EMIT(ctx, "%s_VTable %s__%s__VTable = {", trait, strct, trait);

            ASTNode *m = node->impl_trait.methods;
            while (m)
            {
                // Calculate expected prefix: Struct__Trait__
                size_t pre_sz = strlen(strct) + strlen(trait) + 6;
                char *prefix = xmalloc(pre_sz);
                snprintf(prefix, pre_sz, "%s__%s__", strct, trait);

                const char *orig_name = m->func.name;
                if (strncmp(orig_name, prefix, strlen(prefix)) == 0)
                {
                    orig_name += strlen(prefix);
                }
                else
                {
                    orig_name = parse_original_method_name(m->func.name);
                }

                EMIT(ctx, ".%s = (__typeof__(((%s_VTable*)0)->%s))%s", orig_name, trait, orig_name,
                     m->func.name);
                zfree(prefix);
                if (m->next)
                {
                    EMIT(ctx, ", ");
                }
                m = m->next;
            }
            EMIT(ctx, "};\n");
        }
        ref = ref->next;
    }
}

// Emit test functions and runner. Returns number of tests emitted.
int emit_tests_and_runner(ParserContext *ctx, ASTNode *node)
{
    ASTNode *cur = node;
    int test_count = 0;
    while (cur)
    {
        if (cur->type == NODE_TEST)
        {
            if (cur->cfg_condition)
            {
                EMIT(ctx, "#if %s\n", cur->cfg_condition);
            }
            EMIT(ctx, "static int _z_test_%d() {\n", test_count);
            EMIT(ctx, "fprintf(stderr, \"  TEST: %s ... \");\n", cur->test_stmt.name);
            EMIT(ctx, "int _zc_before = _zc_test_failures;\n");
            int saved = ctx->cg.defer_count;
            char *saved_ret = ctx->cg.current_func_ret_type;
            ctx->cg.current_func_ret_type = "void";
            codegen_walker(ctx, cur->test_stmt.body);
            ctx->cg.current_func_ret_type = saved_ret;
            // Run defers
            for (int i = ctx->cg.defer_count - 1; i >= saved; i--)
            {
                emit_source_mapping_duplicate(ctx, ctx->cg.defer_stack[i]);
                codegen_node_single(ctx, ctx->cg.defer_stack[i]);
            }
            ctx->cg.defer_count = saved;
            EMIT(ctx, "if (_zc_before == _zc_test_failures) { fprintf(stderr, \"OK\\n\"); } "
                      "else { fprintf(stderr, \"FAIL\\n\"); }\n");
            EMIT(ctx, "return _zc_test_failures - _zc_before;\n");
            EMIT(ctx, "}\n");
            if (cur->cfg_condition)
            {
                EMIT(ctx, "#endif\n");
            }
            test_count++;
        }
        cur = cur->next;
    }
    if (test_count > 0)
    {
        EMIT(ctx, "\nint _z_run_tests() {\n");
        emitter_indent(&ctx->cg.emitter);
        EMIT(ctx, "int _zc_total = 0;\n");
        cur = node;
        int i = 0;
        while (cur)
        {
            if (cur->type == NODE_TEST)
            {
                if (cur->cfg_condition)
                {
                    EMIT(ctx, "#if %s\n", cur->cfg_condition);
                }
                EMIT(ctx, "_zc_total += _z_test_%d();\n", i);
                if (cur->cfg_condition)
                {
                    EMIT(ctx, "#endif\n");
                }
                i++;
            }
            cur = cur->next;
        }
        EMIT(ctx, "fprintf(stderr, \"\\n%%d test(s) failed\\n\", _zc_total);\n");
        EMIT(ctx, "return _zc_total;\n");
        emitter_dedent(&ctx->cg.emitter);
        EMIT(ctx, "}\n\n");
    }
    return test_count;
}

// Helper to emit typedefs for mangled pointer types (e.g., StringPtr for String*)
// used as generic parameters. This resolves "unknown type name StringPtr" errors.
static void emit_mangled_pointer_typedefs(ParserContext *ctx)
{
    char *emitted[2048];
    int count = 0;

    Instantiation *inst = ctx->instantiations;
    while (inst)
    {
        if (inst->concrete_arg && inst->unmangled_arg && strstr(inst->concrete_arg, "Ptr") &&
            strchr(inst->unmangled_arg, '*'))
        {
            // Check if already emitted
            int found = 0;
            for (int i = 0; i < count; i++)
            {
                if (strcmp(emitted[i], inst->concrete_arg) == 0)
                {
                    found = 1;
                    break;
                }
            }

            if (!found && count < 2048)
            {
                // In C, structs are usually typedef'd, so "typedef String* StringPtr;" is valid.
                EMIT(ctx, "typedef %s %s;\n", inst->unmangled_arg, inst->concrete_arg);
                emitted[count++] = inst->concrete_arg;
            }
        }
        inst = inst->next;
    }

    // Also scan instantiated functions which might have unique pointer arguments
    ASTNode *ifn = ctx->instantiated_funcs;
    while (ifn)
    {
        if (ifn->type == NODE_FUNCTION && ifn->func.name && strstr(ifn->func.name, "__"))
        {
            char *mangled_part = strstr(ifn->func.name, "__") + 2;
            if (strstr(mangled_part, "Ptr"))
            {
                // This is more complex because we need the original type.
                // For now, struct instantiations cover 99% of cases via collections.
            }
        }
        ifn = ifn->next;
    }
}

// Emit type definitions-
void print_type_defs(ParserContext *ctx, ASTNode *nodes)
{
    emit_mangled_pointer_typedefs(ctx);

    if (!ctx->config->is_freestanding && !ctx->config->misra_mode)
    {
        EMIT(ctx, "typedef char* string;\n");

        EMIT(ctx, "typedef struct { void **data; int len; int cap; } Vec;\n");
        EMIT(ctx, "#define Vec_new() (Vec){.data=0, .len=0, .cap=0}\n");

        if (ctx->config->use_cpp)
        {
            EMIT(ctx, "static void _z_vec_push(Vec *v, void *item) { if(v->len >= v->cap) { v->cap "
                      "= v->cap?v->cap*2:8; v->data = static_cast<void**>(realloc(v->data, v->cap "
                      "* sizeof(void*))); } v->data[v->len++] = item; }\n");
            EMIT(ctx,
                 "static inline Vec _z_make_vec(int count, ...) { Vec v = {0}; v.cap = count > 8 ? "
                 "count : 8; v.data = static_cast<void**>(malloc(v.cap * sizeof(void*))); v.len = "
                 "0; va_list args; va_start(args, count); for(int i=0; i<count; i++) { "
                 "v.data[v.len++] = va_arg(args, void*); } va_end(args); return v; }\n");
        }
        else
        {
            EMIT(ctx, "static void _z_vec_push(Vec *v, void *item) { if(v->len >= v->cap) { v->cap "
                      "= v->cap?v->cap*2:8; v->data = z_realloc(v->data, v->cap * sizeof(void*)); "
                      "} v->data[v->len++] = item; }\n");
            EMIT(ctx, "static inline Vec _z_make_vec(int count, ...) { Vec v = {0}; v.cap = count "
                      "> 8 ? count : 8; v.data = z_malloc(v.cap * sizeof(void*)); v.len = 0; "
                      "va_list args; va_start(args, count); for(int i=0; i<count; i++) { "
                      "v.data[v.len++] = va_arg(args, void*); } va_end(args); return v; }\n");
        }
        EMIT(ctx, "#define Vec_push(v, i) _z_vec_push(&(v), (void*)(uintptr_t)(i))\n");
        EMIT(ctx, "static inline long _z_check_bounds(long index, long limit) { if(index < 0 || "
                  "index >= limit) { fprintf(stderr, \"Index out of bounds: %%ld (limit "
                  "%%ld)\\n\", index, limit); exit(1); } return index; }\n");
    }
    else
    {
        EMIT(ctx, "static inline long _z_check_bounds(long index, long limit) { if((index < 0) || "
                  "(index >= limit)) { __builtin_trap(); } return index; }\n");
    }

    ASTNode *local = nodes;
    while (local)
    {
        if (local->type == NODE_STRUCT && !local->strct.is_template)
        {
            if (local->type_info && local->type_info->kind == TYPE_VECTOR)
            {
                // For vectors, we emit a custom typedef in emit_struct_defs.
                // Standard 'typedef struct Name Name' would conflict.
            }
            else if (local->strct.name)
            {
                const char *final_name = local->link_name ? local->link_name : local->strct.name;
                const char *keyword = local->strct.is_union ? "union" : "struct";
                EMIT(ctx, "typedef %s %s %s;\n", keyword, final_name, final_name);
            }
        }
        if (local->type == NODE_ENUM && !local->enm.is_template && local->enm.name)
        {
            const char *final_name = local->link_name ? local->link_name : local->enm.name;

            // Only forward-declare as struct if it's an ADT-style enum (has payloads)
            int has_payload = 0;
            ASTNode *v = local->enm.variants;
            while (v)
            {
                if (v->variant.payload)
                {
                    has_payload = 1;
                    break;
                }
                v = v->next;
            }

            if (has_payload)
            {
                EMIT(ctx, "typedef struct %s %s;\n", final_name, final_name);
            }
            // Simple enums will be emitted as 'typedef enum' later in emit_struct_defs
        }

        local = local->next;
    }
    EMIT(ctx, "\n");

    SliceType *rev = NULL;
    SliceType *c = ctx->used_slices;
    while (c)
    {
        SliceType *next = c->next;
        c->next = rev;
        rev = c;
        c = next;
    }
    ctx->used_slices = rev;

    c = ctx->used_slices;
    while (c)
    {
        EMIT(ctx,
             "typedef struct Slice__%s Slice__%s;\nstruct Slice__%s { %s *data; int len; int cap; "
             "};\n",
             c->name, c->name, c->name, c->name);
        c = c->next;
    }

    // Emit full tuple struct definitions (typedef + body).
    // Must come before enum protos and user struct/enum bodies,
    // which may reference tuple types by value.
    // used_tuples is LIFO; reverse so inner (declared first) tuples come first.
    int tup_count = 0;
    TupleType *tup = ctx->used_tuples;
    while (tup)
    {
        tup_count++;
        tup = tup->next;
    }
    if (tup_count > 0)
    {
        TupleType **tup_arr = xmalloc(sizeof(TupleType *) * (size_t)tup_count);
        tup = ctx->used_tuples;
        for (int ti = tup_count - 1; ti >= 0; ti--)
        {
            tup_arr[ti] = tup;
            tup = tup->next;
        }
        for (int ti = 0; ti < tup_count; ti++)
        {
            char *clean_sig = sanitize_mangled_name(tup_arr[ti]->sig);
            EMIT(ctx, "typedef struct Tuple__%s Tuple__%s;\nstruct Tuple__%s { ", clean_sig,
                 clean_sig, clean_sig);
            zfree(clean_sig);
            for (int i = 0; i < tup_arr[ti]->count; i++)
            {
                EMIT(ctx, "%s v%d; ", tup_arr[ti]->types[i], i);
            }
            EMIT(ctx, "};\n");
        }
        zfree(tup_arr);
    }

    // End of type definitions
}

static int last_source_mapping_line = -1;
static NodeType last_source_mapping_type = NODE_ROOT;
static int allow_duplicate_source_mapping = 0;

int should_emit_source_mapping(ASTNode *node)
{
    return node && node->type < NODE_REPL_PRINT && node->type != NODE_BLOCK &&
           node->type != NODE_EXPR_UNARY && node->type != NODE_FIELD;
}

void emit_source_mapping_duplicate(ParserContext *ctx, ASTNode *node)
{
    allow_duplicate_source_mapping++;
    emit_source_mapping(ctx, node);
    allow_duplicate_source_mapping--;
}

void emit_source_mapping(ParserContext *ctx, ASTNode *node)
{
    if (!ctx->config->mode_debug)
    {
        return;
    }

    if (!should_emit_source_mapping(node))
    {
        return;
    }

    if (allow_duplicate_source_mapping <= 0)
    {
        if (node->token.line == last_source_mapping_line && node->type == last_source_mapping_type)
        {
            return;
        }
    }

    if (!node->token.start || !node->token.file)
    {
        zwarn_at(node->token,
                 "Encountered source mapping issue for node type %i, please report this issue.",
                 node->type);
        return;
    }

    last_source_mapping_line = node->token.line;
    last_source_mapping_type = node->type;

    if (!ctx->config->misra_mode)
    {
        char *safe_file = sanitize_path_for_c_string(node->token.file);
        EMIT(ctx, "\n#line %i \"%s\"\n", node->token.line, safe_file);
    }
}