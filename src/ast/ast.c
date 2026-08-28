// SPDX-License-Identifier: MIT
#include "ast.h"
#include "../parser/parser.h"
#include "../constants.h"
#include "../arena.h"
#include "../utils/colors.h"
#include "../utils/utils.h"
#include <stdlib.h>
#include <string.h>

// Local trait registry — avoids dependency on g_parser_ctx.
static TraitReg *registered_traits_local = NULL;

// Set by the language server while indexing a document; the parser uses it to
// resolve tuple-indexing expressions during LSP completion. Defined here (not in
// the LSP) so the compiler core stays self-contained.
int g_is_indexing = 0;

void register_trait(const char *name)
{
    TraitReg *r = xmalloc(sizeof(TraitReg));
    r->name = xstrdup(name);
    r->next = registered_traits_local;
    registered_traits_local = r;
}

void clear_registered_traits(void)
{
    // The TraitReg nodes are arena-allocated, so the arena reset/restore in the
    // fuzz harness and the LSP reclaims them. The list head must be cleared
    // there too, otherwise it dangles into reused arena memory and is_trait()
    // walks a cyclic list forever.
    registered_traits_local = NULL;
}

int is_trait(const char *name)
{
    if (!name)
    {
        return 0;
    }

    // Strip trailing stars for pointer types (e.g., IAnimal*)
    char *base = xstrdup(name);

    // Strip "struct " or "union " if present
    if (strncmp(base, "struct ", 7) == 0)
    {
        char *nb = xstrdup(base + 7);
        zfree(base);
        base = nb;
    }
    else if (strncmp(base, "union ", 6) == 0)
    {
        char *nb = xstrdup(base + 6);
        zfree(base);
        base = nb;
    }

    char *p = (char *)strchr(base, '*');
    if (p)
    {
        *p = '\0';
    }

    TraitReg *r = registered_traits_local;
    while (r)
    {
        if (0 == strcmp(r->name, base))
        {
            zfree(base);
            return 1;
        }
        r = r->next;
    }
    zfree(base);
    return 0;
}

ASTNode *ast_create(NodeType type)
{
    ASTNode *node = xmalloc(sizeof(ASTNode));
    memset(node, 0, sizeof(ASTNode));
    node->kind = type;
    return node;
}

void ast_free(ASTNode *node)
{
    if (node->kind == NODE_AST_COMMENT)
    {
        if (node->comment.content)
        {
            zfree(node->comment.content);
        }
    }
    zfree(node);
}

Type *type_new(TypeKind kind)
{
    Type *t = xmalloc(sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = kind;
    if (kind == TYPE_FUNCTION)
    {
        t->traits.has_drop = 1;
    }
    t->name = NULL;
    t->link_name = NULL;
    t->inner = NULL;
    t->args = NULL;
    t->count = 0;
    t->is_const = 0;
    t->is_explicit_struct = 0;
    t->is_raw = 0;
    t->array_size = 0;
    return t;
}

Type *type_new_ptr(Type *inner)
{
    Type *t = type_new(TYPE_POINTER);
    t->inner = inner;
    return t;
}

Type *type_new_array(Type *inner, int size)
{
    Type *t = type_new(TYPE_ARRAY);
    t->inner = inner;
    t->array_size = size;
    return t;
}

Type *type_clone(Type *t)
{
    if (!t)
    {
        return NULL;
    }
    Type *clone = xmalloc(sizeof(Type));
    memcpy(clone, t, sizeof(Type));

    // In Zen C, we only clone the top-level Type struct to isolate usage-site metadata
    // like lifetime_depth. However, we SHIELD the recursive structures (inner, args)
    // by sharing their pointers. This ensures that type inference (which may happen
    // at a usage site) correctly propagates back to the canonical Type objects
    // that the codegen and other passes depend on.
    // clone->inner = t->inner; // Already done by memcpy
    // clone->args = t->args;   // Already done by memcpy

    // Note: Strings like name and link_name are shared (shallow copy)
    // but names are usually static or managed by pctx string pool.
    return clone;
}

static int is_char_ptr(Type *t)
{
    if (!t)
    {
        return 0;
    }
    // Handle both primitive char* and legacy struct char*.
    if (TYPE_POINTER == t->kind && t->inner && TYPE_CHAR == t->inner->kind)
    {
        return 1;
    }
    if (TYPE_POINTER == t->kind && t->inner && TYPE_STRUCT == t->inner->kind && t->inner->name &&
        0 == strcmp(t->inner->name, "char"))
    {
        return 1;
    }
    return 0;
}

int is_integer_type(Type *t)
{
    if (!t)
    {
        return 0;
    }

    if (t->kind == TYPE_ALIAS && !t->alias.is_opaque_alias)
    {
        return is_integer_type(t->inner);
    }

    int res =
        (t->kind == TYPE_INT || t->kind == TYPE_CHAR || t->kind == TYPE_BOOL ||
         t->kind == TYPE_I8 || t->kind == TYPE_U8 || t->kind == TYPE_I16 || t->kind == TYPE_U16 ||
         t->kind == TYPE_I32 || t->kind == TYPE_U32 || t->kind == TYPE_I64 || t->kind == TYPE_U64 ||
         t->kind == TYPE_USIZE || t->kind == TYPE_ISIZE || t->kind == TYPE_BYTE ||
         t->kind == TYPE_RUNE || t->kind == TYPE_UINT || t->kind == TYPE_I128 ||
         t->kind == TYPE_U128 || t->kind == TYPE_BITINT || t->kind == TYPE_UBITINT ||
         t->kind == TYPE_C_INT || t->kind == TYPE_C_UINT || t->kind == TYPE_C_LONG ||
         t->kind == TYPE_C_ULONG || t->kind == TYPE_C_LONGLONG || t->kind == TYPE_C_ULONGLONG ||
         t->kind == TYPE_C_SHORT || t->kind == TYPE_C_USHORT || t->kind == TYPE_C_CHAR ||
         t->kind == TYPE_C_UCHAR ||
         (t->kind == TYPE_STRUCT && t->name &&
          (0 == strcmp(t->name, "int8_t") || 0 == strcmp(t->name, "uint8_t") ||
           0 == strcmp(t->name, "int16_t") || 0 == strcmp(t->name, "uint16_t") ||
           0 == strcmp(t->name, "int32_t") || 0 == strcmp(t->name, "uint32_t") ||
           0 == strcmp(t->name, "int64_t") || 0 == strcmp(t->name, "uint64_t") ||
           0 == strcmp(t->name, "size_t") || 0 == strcmp(t->name, "ssize_t") ||
           0 == strcmp(t->name, "ptrdiff_t"))));
    return res;
}

int is_unsigned_type(Type *t)
{
    if (!t)
    {
        return 0;
    }

    if (t->kind == TYPE_ALIAS && !t->alias.is_opaque_alias)
    {
        return is_unsigned_type(t->inner);
    }

    int res =
        (t->kind == TYPE_U8 || t->kind == TYPE_U16 || t->kind == TYPE_U32 || t->kind == TYPE_U64 ||
         t->kind == TYPE_USIZE || t->kind == TYPE_UINT || t->kind == TYPE_U128 ||
         t->kind == TYPE_UBITINT || t->kind == TYPE_C_UINT || t->kind == TYPE_C_ULONG ||
         t->kind == TYPE_C_ULONGLONG || t->kind == TYPE_C_USHORT || t->kind == TYPE_C_UCHAR ||
         (t->kind == TYPE_STRUCT && t->name &&
          (0 == strcmp(t->name, "uint8_t") || 0 == strcmp(t->name, "uint16_t") ||
           0 == strcmp(t->name, "uint32_t") || 0 == strcmp(t->name, "uint64_t") ||
           0 == strcmp(t->name, "size_t"))));
    return res;
}

int is_signed_type(Type *t)
{
    if (!t)
    {
        return 0;
    }

    if (t->kind == TYPE_ALIAS && !t->alias.is_opaque_alias)
    {
        return is_signed_type(t->inner);
    }

    int res =
        (t->kind == TYPE_I8 || t->kind == TYPE_I16 || t->kind == TYPE_I32 || t->kind == TYPE_I64 ||
         t->kind == TYPE_ISIZE || t->kind == TYPE_INT || t->kind == TYPE_I128 ||
         t->kind == TYPE_BITINT || t->kind == TYPE_C_INT || t->kind == TYPE_C_LONG ||
         t->kind == TYPE_C_LONGLONG || t->kind == TYPE_C_SHORT || t->kind == TYPE_C_CHAR ||
         (t->kind == TYPE_STRUCT && t->name &&
          (0 == strcmp(t->name, "int8_t") || 0 == strcmp(t->name, "int16_t") ||
           0 == strcmp(t->name, "int32_t") || 0 == strcmp(t->name, "int64_t") ||
           0 == strcmp(t->name, "ssize_t") || 0 == strcmp(t->name, "ptrdiff_t"))));
    return res;
}

int is_boolean_type(Type *t)
{
    if (!t)
    {
        return 0;
    }

    if (t->kind == TYPE_ALIAS && !t->alias.is_opaque_alias)
    {
        return is_boolean_type(t->inner);
    }

    return (t->kind == TYPE_BOOL);
}

int is_float_type(Type *t)
{
    if (!t)
    {
        return 0;
    }

    if (t->kind == TYPE_ALIAS && !t->alias.is_opaque_alias)
    {
        return is_float_type(t->inner);
    }

    int res = (t->kind == TYPE_FLOAT || t->kind == TYPE_F32 || t->kind == TYPE_F64);
    return res;
}

int is_incomplete_type(struct ParserContext *ctx, Type *t)
{
    if (!t || t->kind != TYPE_STRUCT || !t->name)
    {
        return 0;
    }
    ASTNode *def = find_struct_def(ctx, t->name);
    if (!def)
    {
        return 1;
    }
    return def->strct.is_incomplete;
}

int is_composite_expression(ASTNode *node)
{
    if (!node)
    {
        return 0;
    }

    switch (node->kind)
    {
    case NODE_EXPR_BINARY:
        return 1;
    case NODE_EXPR_UNARY:
        return 1;
    case NODE_TERNARY:
        return 1;
    default:
        return 0;
    }
}

int type_eq(Type *a, Type *b)
{
    if (!a || !b)
    {
        return 0;
    }

    if (a == b)
    {
        return 1;
    }

    if (a->kind == TYPE_UNKNOWN || b->kind == TYPE_UNKNOWN)
    {
        return 1;
    }

    // Lax integer matching (bool == int, char == i8, etc.).
    if (is_integer_type(a) && is_integer_type(b))
    {
        return 1;
    }

    // Lax float matching.
    if (is_float_type(a) && is_float_type(b))
    {
        return 1;
    }

    // String Literal vs char*
    if (a->kind == TYPE_STRING && is_char_ptr(b))
    {
        return 1;
    }

    if (b->kind == TYPE_STRING && is_char_ptr(a))
    {
        return 1;
    }

    if (a->kind != b->kind)
    {
        if (!((a->kind == TYPE_STRUCT && b->kind == TYPE_ENUM) ||
              (a->kind == TYPE_ENUM && b->kind == TYPE_STRUCT)))
        {
            return 0;
        }
    }

    if (a->kind == TYPE_STRUCT || a->kind == TYPE_GENERIC || a->kind == TYPE_ENUM)
    {
        if (!a->name || !b->name)
        {
            return 0;
        }
        return 0 == strcmp(a->name, b->name);
    }
    if (a->kind == TYPE_ALIAS)
    {
        if (a->alias.is_opaque_alias)
        {
            if (b->kind != TYPE_ALIAS || !b->alias.is_opaque_alias)
            {
                return 0;
            }
            return 0 == strcmp(a->name, b->name);
        }
        return type_eq(a->inner, b);
    }
    if (a->kind == TYPE_POINTER)
    {
        return type_eq(a->inner, b->inner);
    }
    if (a->kind == TYPE_FUNCTION)
    {
        if (a->is_raw != b->is_raw)
        {
            return 0;
        }
        if (a->count != b->count)
        {
            return 0;
        }
        if (!type_eq(a->inner, b->inner))
        {
            return 0;
        }
        for (int i = 0; i < a->count; i++)
        {
            if (!type_eq(a->args[i], b->args[i]))
            {
                return 0;
            }
        }
        return 1;
    }
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_VECTOR)
    {
        return a->array_size == b->array_size && type_eq(a->inner, b->inner);
    }

    return 1;
}

static char *type_to_string_impl(Type *t);

char *type_to_string(Type *t)
{
    if (!t)
    {
        return xstrdup("void");
    }
    char *res = type_to_string_impl(t);
    if (t->is_const)
    {
        char *final = xmalloc(strlen(res) + 7);
        sprintf(final, "const %s", res); /* safe */
        zfree(res);
        return final;
    }
    return res;
}

static char *type_to_string_impl(Type *t)
{
    if (!t)
    {
        return xstrdup("void");
    }

    switch (t->kind)
    {
    case TYPE_VOID:
        return xstrdup("void");
    case TYPE_BOOL:
        return xstrdup("bool");
    case TYPE_STRING:
        return xstrdup("string");
    case TYPE_CHAR:
        return xstrdup("char");
    case TYPE_I8:
        return xstrdup("int8_t");
    case TYPE_U8:
        return xstrdup("uint8_t");
    case TYPE_I16:
        return xstrdup("int16_t");
    case TYPE_U16:
        return xstrdup("uint16_t");
    case TYPE_I32:
        return xstrdup("int32_t");
    case TYPE_U32:
        return xstrdup("uint32_t");
    case TYPE_I64:
        return xstrdup("int64_t");
    case TYPE_U64:
        return xstrdup("uint64_t");
    case TYPE_F32:
        return xstrdup("float");
    case TYPE_F64:
        return xstrdup("double");
    case TYPE_USIZE:
        return xstrdup("size_t");
    case TYPE_ISIZE:
        return xstrdup("ptrdiff_t");
    case TYPE_BYTE:
        return xstrdup("uint8_t");
    case TYPE_I128:
        return xstrdup("__int128");
    case TYPE_U128:
        return xstrdup("unsigned __int128");
    case TYPE_RUNE:
        return xstrdup("int32_t");
    case TYPE_UINT:
        return xstrdup("unsigned int");

    // Portable C Types
    case TYPE_C_INT:
        return xstrdup("c_int");
    case TYPE_C_UINT:
        return xstrdup("c_uint");
    case TYPE_C_LONG:
        return xstrdup("c_long");
    case TYPE_C_ULONG:
        return xstrdup("c_ulong");
    case TYPE_C_LONGLONG:
        return xstrdup("c_longlong");
    case TYPE_C_ULONGLONG:
        return xstrdup("c_ulonglong");
    case TYPE_C_SHORT:
        return xstrdup("c_short");
    case TYPE_C_USHORT:
        return xstrdup("c_ushort");
    case TYPE_C_CHAR:
        return xstrdup("c_char");
    case TYPE_C_UCHAR:
        return xstrdup("c_uchar");

    case TYPE_INT:
        return xstrdup("int");
    case TYPE_FLOAT:
        return xstrdup("float");
    case TYPE_BITINT:
    {
        char *res = xmalloc(32);
        sprintf(res, "i%d", t->array_size); /* safe */
        return res;
    }
    case TYPE_UBITINT:
    {
        char *res = xmalloc(32);
        sprintf(res, "u%d", t->array_size); /* safe */
        return res;
    }

    case TYPE_VECTOR:
    {
        if (t->name)
        {
            return xstrdup(t->name);
        }
        char *inner = type_to_string(t->inner);
        char *res = xmalloc(strlen(inner) + 20);
        sprintf(res, "%sx%d", inner, t->array_size); /* safe */
        zfree(inner);
        return res;
    }

    case TYPE_POINTER:
    {
        char *inner = type_to_string(t->inner);
        if (t->is_restrict)
        {
            char *res = xmalloc(strlen(inner) + 16);
            sprintf(res, "%s* __restrict", inner); /* safe */
            return res;
        }
        else
        {
            char *res = xmalloc(strlen(inner) + 2);
            sprintf(res, "%s*", inner); /* safe */
            return res;
        }
    }

    case TYPE_ARRAY:
    {
        if (t->array_size == 0)
        {
            char *inner = type_to_string(t->inner);
            char *res = xmalloc(strlen(inner) + 8);
            sprintf(res, "Slice__%s", inner); /* safe */
            return res;
        }

        Type *base = t;
        int *dims = NULL;
        int dims_cap = 0;
        int dims_count = 0;

        while (base->kind == TYPE_ARRAY && base->array_size > 0)
        {
            if (dims_count == dims_cap)
            {
                dims_cap = dims_cap == 0 ? 4 : dims_cap * 2;
                dims = xrealloc(dims, sizeof(int) * (size_t)(dims_cap));
            }
            dims[dims_count++] = base->array_size;
            base = base->inner;
        }

        char *inner = type_to_string(base);
        size_t total_len = strlen(inner) + 1;
        for (int i = 0; i < dims_count; i++)
        {
            total_len += 20;
        }

        char *res = xmalloc((size_t)(total_len));
        strcpy(res, inner);
        zfree(inner);

        char *p = res + strlen(res);
        for (int i = 0; i < dims_count; i++)
        {
            snprintf(p, 20, "[%d]", dims[i]);
            p += strlen(p);
        }

        if (dims)
        {
            zfree(dims);
        }
        return res;
    }

    case TYPE_FUNCTION:
    {
        if (t->is_raw)
        {
            // fn*(Args)->Ret
            char *ret = type_to_string(t->inner);
            char *res = xmalloc(strlen(ret) + 64);
            snprintf(res, strlen(ret) + 64, "fn*(");

            for (int i = 0; i < t->count; i++)
            {
                if (i > 0)
                {
                    char *tmp = xmalloc(strlen(res) + 3);
                    snprintf(tmp, strlen(res) + 3, "%s, ", res);
                    zfree(res);
                    res = tmp;
                }
                char *arg = type_to_string(t->args[i]);
                char *tmp = xmalloc(strlen(res) + strlen(arg) + 1);
                sprintf(tmp, "%s%s", res, arg); /* safe */
                zfree(res);
                res = tmp;
                zfree(arg);
            }
            if (t->is_varargs)
            {
                char *tmp = xmalloc(strlen(res) + 6);
                sprintf(tmp, "%s, ...", res); /* safe */
                zfree(res);
                res = tmp;
            }
            char *tmp = xmalloc(strlen(res) + strlen(ret) + 6); // ) -> Ret
            sprintf(tmp, "%s) -> %s", res, ret);                /* safe */
            zfree(res);
            res = tmp;
            zfree(ret);
            return res;
        }

        // fn(Args) -> Ret
        char *ret = type_to_string(t->inner);
        char *res = xmalloc(strlen(ret) + 64);
        snprintf(res, strlen(ret) + 64, "fn(");

        for (int i = 0; i < t->count; i++)
        {
            if (i > 0)
            {
                char *tmp = xmalloc(strlen(res) + 3);
                snprintf(tmp, strlen(res) + 3, "%s, ", res);
                zfree(res);
                res = tmp;
            }
            char *arg = type_to_string(t->args[i]);
            char *tmp = xmalloc(strlen(res) + strlen(arg) + 1);
            sprintf(tmp, "%s%s", res, arg); /* safe */
            zfree(res);
            res = tmp;
            zfree(arg);
        }
        char *tmp = xmalloc(strlen(res) + strlen(ret) + 6); // ) -> Ret
        sprintf(tmp, "%s) -> %s", res, ret);                /* safe */
        zfree(res);
        res = tmp;
        zfree(ret);
        return res;
    }

    case TYPE_STRUCT:
    case TYPE_GENERIC:
    {
        if (t->count > 0 && t->name && strstr(t->name, "__") == NULL)
        {
            char *base = t->name;
            size_t base_len = strlen(base);
            char *res = xmalloc((size_t)(base_len + 1));
            strcpy(res, base);

            for (int i = 0; i < t->count; i++)
            {
                char *arg = type_to_string(t->args[i]);
                char *clean_arg = sanitize_mangled_name(arg);

                size_t new_len = strlen(res) + strlen(clean_arg) + 3;
                char *new_res = xmalloc((size_t)(new_len));
                sprintf(new_res, "%s__%s", res, clean_arg); /* safe */

                zfree(res);
                res = new_res;
                zfree(arg);
                zfree(clean_arg);
            }
            return res;
        }
        return xstrdup(t->name);
    }
    case TYPE_ALIAS:
        return xstrdup(t->name);
    case TYPE_ENUM:
        return xstrdup(t->link_name ? t->link_name : t->name);

    default:
        return xstrdup("unknown");
    }
}

// C-compatible type stringifier.
// Strictly uses 'struct T' for explicit structs to support external types.
// Does NOT mangle pointers to 'Ptr'.
static char *type_to_c_string_impl(Type *t);

char *type_to_c_string(Type *t)
{
    if (!t)
    {
        return xstrdup("void");
    }
    char *res = type_to_c_string_impl(t);
    if (t->is_const)
    {
        char *final = xmalloc(strlen(res) + 7);
        sprintf(final, "const %s", res); /* safe */
        zfree(res);
        return final;
    }
    return res;
}

static char *type_to_c_string_impl(Type *t)
{
    if (!t)
    {
        return xstrdup("void");
    }

    switch (t->kind)
    {
    case TYPE_VOID:
        return xstrdup("void");
    case TYPE_STRUCT:
    {
        if (t->link_name)
        {
            return xstrdup(
                t->link_name); // Only prepend 'struct' if explicitly requested (e.g. "struct Foo")
        }
        // otherwise assume it's a typedef/alias (e.g. "Foo").
        if (t->is_explicit_struct)
        {
            const char *final_name = t->link_name ? t->link_name : t->name;
            char *res = xmalloc(strlen(final_name) + 8);
            sprintf(res, "struct %s", final_name); /* safe */
            return res;
        }
        else
        {
            return xstrdup(t->link_name ? t->link_name : t->name);
        }
    }
    case TYPE_BOOL:
        return xstrdup("bool");
    case TYPE_STRING:
        return xstrdup("string");
    case TYPE_CHAR:
        return xstrdup("char");
    case TYPE_I8:
        return xstrdup("int8_t");
    case TYPE_U8:
        return xstrdup("uint8_t");
    case TYPE_I16:
        return xstrdup("int16_t");
    case TYPE_U16:
        return xstrdup("uint16_t");
    case TYPE_I32:
        return xstrdup("int32_t");
    case TYPE_U32:
        return xstrdup("uint32_t");
    case TYPE_I64:
        return xstrdup("int64_t");
    case TYPE_U64:
        return xstrdup("uint64_t");
    case TYPE_F32:
        return xstrdup("float");
    case TYPE_F64:
        return xstrdup("double");
    case TYPE_USIZE:
        return xstrdup("size_t");
    case TYPE_ISIZE:
        return xstrdup("ptrdiff_t");
    case TYPE_BYTE:
        return xstrdup("uint8_t");
    case TYPE_I128:
        return xstrdup("__int128");
    case TYPE_U128:
        return xstrdup("unsigned __int128");
    case TYPE_RUNE:
        return xstrdup("int32_t");
    case TYPE_UINT:
        return xstrdup("unsigned int");

    // Portable C Types (Map directly to C types)
    case TYPE_C_INT:
        return xstrdup(g_compiler.config.misra_mode ? "int32_t" : "int");
    case TYPE_C_UINT:
        return xstrdup(g_compiler.config.misra_mode ? "uint32_t" : "unsigned int");
    case TYPE_C_LONG:
        return xstrdup(g_compiler.config.misra_mode ? "int64_t" : "long");
    case TYPE_C_ULONG:
        return xstrdup(g_compiler.config.misra_mode ? "uint64_t" : "unsigned long");
    case TYPE_C_LONGLONG:
        return xstrdup(g_compiler.config.misra_mode ? "int64_t" : "long long");
    case TYPE_C_ULONGLONG:
        return xstrdup(g_compiler.config.misra_mode ? "uint64_t" : "unsigned long long");
    case TYPE_C_SHORT:
        return xstrdup(g_compiler.config.misra_mode ? "int16_t" : "short");
    case TYPE_C_USHORT:
        return xstrdup(g_compiler.config.misra_mode ? "uint16_t" : "unsigned short");
    case TYPE_C_CHAR:
        return xstrdup(g_compiler.config.misra_mode ? "int8_t" : "char");
    case TYPE_C_UCHAR:
        return xstrdup(g_compiler.config.misra_mode ? "uint8_t" : "unsigned char");

    case TYPE_INT:
        // 'int' in Zen C maps to 'i32' now for portability.
        // FFI should use c_int.
        return xstrdup("int32_t");
    case TYPE_FLOAT:
        return xstrdup("float");
    case TYPE_BITINT:
    {
        char *res = xmalloc(32);
        sprintf(res, "_BitInt(%d)", t->array_size); /* safe */
        return res;
    }
    case TYPE_UBITINT:
    {
        char *res = xmalloc(40);
        sprintf(res, "unsigned _BitInt(%d)", t->array_size); /* safe */
        return res;
    }

    case TYPE_VECTOR:
    {
        if (t->name)
        {
            return xstrdup(t->name);
        }
        char *inner = type_to_c_string(t->inner);
        char *res = xmalloc(strlen(inner) + 32);
        sprintf(res, "ZC_SIMD(%s, %d)", inner, t->array_size); /* safe */
        zfree(inner);
        return res;
    }

    case TYPE_POINTER:
    {
        char *inner = type_to_c_string(t->inner);
        char *ptr_token = (char *)strstr(inner, "(*");
        if (ptr_token)
        {
            long prefix_len = ptr_token - inner + 2; // "void (*"
            char *res = xmalloc(strlen(inner) + 2);
            strncpy(res, inner, (size_t)(prefix_len));
            res[prefix_len] = 0;
            strcat(res, "*");
            strcat(res, ptr_token + 2);
            zfree(inner);
            return res;
        }

        if (t->is_restrict)
        {
            char *res = xmalloc(strlen(inner) + 16);
            sprintf(res, "%s* __restrict", inner); /* safe */
            return res;
        }
        else
        {
            char *res = xmalloc(strlen(inner) + 2);
            sprintf(res, "%s*", inner); /* safe */
            return res;
        }
    }

    case TYPE_ARRAY:
    {
        if (t->array_size == 0)
        {
            char *inner_zens = type_to_string(t->inner);
            char *res = xmalloc(strlen(inner_zens) + 8);
            sprintf(res, "Slice__%s", inner_zens); /* safe */
            zfree(inner_zens);
            return res;
        }

        Type *base = t;
        int *dims = NULL;
        int dims_cap = 0;
        int dims_count = 0;

        while (base->kind == TYPE_ARRAY && base->array_size > 0)
        {
            if (dims_count == dims_cap)
            {
                dims_cap = dims_cap == 0 ? 4 : dims_cap * 2;
                dims = xrealloc(dims, sizeof(int) * (size_t)(dims_cap));
            }
            dims[dims_count++] = base->array_size;
            base = base->inner;
        }

        char *inner = type_to_c_string(base);
        size_t total_len = strlen(inner) + 1;
        for (int i = 0; i < dims_count; i++)
        {
            total_len += 20;
        }

        char *res = xmalloc((size_t)(total_len));
        strcpy(res, inner);
        zfree(inner);

        char *p = res + strlen(res);
        for (int i = 0; i < dims_count; i++)
        {
            snprintf(p, 20, "[%d]", dims[i]);
            p += strlen(p);
        }

        if (dims)
        {
            zfree(dims);
        }
        return res;
    }

    case TYPE_FUNCTION:
        if (t->is_raw)
        {
            char *ret = type_to_c_string(t->inner);
            char *res = xmalloc(strlen(ret) + 64); // heuristic start buffer
            snprintf(res, strlen(ret) + 64, "%s (*)(", ret);

            for (int i = 0; i < t->count; i++)
            {
                if (i > 0)
                {
                    char *tmp = xmalloc(strlen(res) + 3);
                    snprintf(tmp, strlen(res) + 3, "%s, ", res);
                    zfree(res);
                    res = tmp;
                }
                char *arg = type_to_c_string(t->args[i]);
                char *tmp = xmalloc(strlen(res) + strlen(arg) + 1);
                sprintf(tmp, "%s%s", res, arg); /* safe */
                zfree(res);
                res = tmp;
                zfree(arg);
            }
            if (t->is_varargs)
            {
                if (t->count > 0)
                {
                    char *tmp = xmalloc(strlen(res) + 6);
                    sprintf(tmp, "%s, ...", res); /* safe */
                    zfree(res);
                    res = tmp;
                }
                else
                {
                    char *tmp = xmalloc(strlen(res) + 4);
                    sprintf(tmp, "%s...", res); /* safe */
                    zfree(res);
                    res = tmp;
                }
            }
            char *tmp = xmalloc(strlen(res) + 2);
            sprintf(tmp, "%s)", res); /* safe */
            zfree(res);
            res = tmp;
            zfree(ret);
            return res;
        }
        if (t->inner)
        {
            zfree(type_to_c_string(t->inner));
        }
        return xstrdup("z_closure_T");

    case TYPE_GENERIC:
        // Use type_to_string to get the mangled name (e.g. Option_int) instead of raw C string
        // composition This ensures consistency with struct definitions.
        {
            char *s = type_to_string(t);
            return s;
        }

    case TYPE_ALIAS:
        if (t->alias.is_opaque_alias)
        {
            return xstrdup(t->name);
        }
        return type_to_c_string(t->inner);

    case TYPE_ENUM:
        return xstrdup(t->link_name ? t->link_name : t->name);

    case TYPE_UNSAFE_ANY:
        return xstrdup("any");
    default:
        return xstrdup("unknown");
    }
}

Type *get_inner_type(Type *t)
{
    while (t && t->kind == TYPE_ALIAS && !t->alias.is_opaque_alias)
    {
        t = t->inner;
    }
    return t;
}

// Type inference — resolve an expression node to its C type string.
// (Moved from codegen_utils.c this is part of core, not codegen)

char *infer_type(ParserContext *ctx, ASTNode *node)
{
    if (!node || !ctx)
    {
        return NULL;
    }

    if (node->type_info && node->type_info->kind != TYPE_UNKNOWN)
    {
        char *t = type_to_c_string(node->type_info);
        return t;
    }

    if (node->resolved_type && strcmp(node->resolved_type, "unknown") != 0)
    {
        if (strcmp(node->resolved_type, "c_int") == 0)
        {
            return "int";
        }
        if (strcmp(node->resolved_type, "c_uint") == 0)
        {
            return "unsigned int";
        }
        if (strcmp(node->resolved_type, "c_long") == 0)
        {
            return "long";
        }
        if (strcmp(node->resolved_type, "c_ulong") == 0)
        {
            return "unsigned long";
        }
        if (strcmp(node->resolved_type, "c_longlong") == 0)
        {
            return "long long";
        }
        if (strcmp(node->resolved_type, "c_ulonglong") == 0)
        {
            return "unsigned long long";
        }
        if (strcmp(node->resolved_type, "c_short") == 0)
        {
            return "short";
        }
        if (strcmp(node->resolved_type, "c_ushort") == 0)
        {
            return "unsigned short";
        }
        if (strcmp(node->resolved_type, "c_char") == 0)
        {
            return "char";
        }
        if (strcmp(node->resolved_type, "c_uchar") == 0)
        {
            return "unsigned char";
        }

        return node->resolved_type;
    }

    if (node->kind == NODE_EXPR_LITERAL)
    {
        if (node->type_info)
        {
            return type_to_c_string(node->type_info);
        }
        return NULL;
    }

    if (node->kind == NODE_EXPR_VAR)
    {
        ZenSymbol *sym = find_symbol_entry(ctx, node->var_ref.name);
        if (sym)
        {
            if (sym->type_name)
            {
                return sym->type_name;
            }
            if (sym->type_info)
            {
                return type_to_c_string(sym->type_info);
            }
        }
    }

    if (node->kind == NODE_EXPR_CALL)
    {
        if (node->call.callee->kind == NODE_EXPR_VAR)
        {
            FuncSig *sig = find_func(ctx, node->call.callee->var_ref.name);
            if (sig)
            {
                if (sig->is_async)
                {
                    if (sig->ret_type)
                    {
                        return type_to_c_string(sig->ret_type);
                    }
                    return "void";
                }
                if (sig->ret_type)
                {
                    return type_to_c_string(sig->ret_type);
                }
            }

            // Fallback for known stdlib memory/file functions.
            if (strcmp(node->call.callee->var_ref.name, "malloc") == 0 ||
                strcmp(node->call.callee->var_ref.name, "calloc") == 0 ||
                strcmp(node->call.callee->var_ref.name, "realloc") == 0 ||
                strcmp(node->call.callee->var_ref.name, "fopen") == 0 ||
                strcmp(node->call.callee->var_ref.name, "popen") == 0 ||
                strcmp(node->call.callee->var_ref.name, "fdopen") == 0)
            {
                return "void*";
            }
            ASTNode *sdef = find_struct_def(ctx, node->call.callee->var_ref.name);
            if (sdef)
            {
                return node->call.callee->var_ref.name;
            }

            // Check for enum variants (constructors)
            EnumVariantReg *ev = find_enum_variant(ctx, node->call.callee->var_ref.name);
            if (ev)
            {
                return ev->enum_name;
            }
        }
        // Method call: target.method() - look up Type_method signature.
        if (node->call.callee->kind == NODE_EXPR_MEMBER)
        {
            char *target_type = infer_type(ctx, node->call.callee->member.target);
            if (target_type)
            {
                char clean_type[MAX_TYPE_NAME_LEN];
                snprintf(clean_type, sizeof(clean_type), "%s", target_type);

                // Robustly strip all pointer levels for method lookup
                char *ptr = (char *)strchr(clean_type, '*');
                if (ptr)
                {
                    *ptr = 0;
                }

                char *base = clean_type;
                if (strncmp(base, "struct ", 7) == 0)
                {
                    base += 7;
                }

                char func_base[MAX_MANGLED_NAME_LEN];
                snprintf(func_base, sizeof(func_base), "%s__%s", base,
                         node->call.callee->member.field);
                char *func_name = merge_underscores(func_base);

                FuncSig *sig = find_func(ctx, func_name);

                if (sig && sig->ret_type)
                {
                    char *ret = type_to_c_string(sig->ret_type);
                    zfree(func_name);
                    return ret;
                }
                zfree(func_name);
            }
        }

        if (node->call.callee->kind == NODE_EXPR_VAR)
        {
            ZenSymbol *sym = find_symbol_entry(ctx, node->call.callee->var_ref.name);
            if (sym && sym->type_info && sym->type_info->kind == TYPE_FUNCTION &&
                sym->type_info->inner)
            {
                return type_to_c_string(sym->type_info->inner);
            }
        }
    }

    if (node->kind == NODE_TRY)
    {
        char *inner_type = infer_type(ctx, node->try_stmt.expr);
        if (inner_type)
        {
            // Extract T from Result<T> or Option<T>
            char *start = (char *)strchr(inner_type, '<');
            if (start)
            {
                start++; // Skip <
                char *end = (char *)strrchr(inner_type, '>');
                if (end && end > start)
                {
                    ptrdiff_t len = end - start;
                    char *extracted = xmalloc((size_t)(len + 1));
                    strncpy(extracted, start, (size_t)(len));
                    extracted[len] = 0;
                    return extracted;
                }
            }

            // Find the struct/enum definition and look for "Ok" or "val"
            char *search_name = inner_type;
            if (strncmp(search_name, "struct ", 7) == 0)
            {
                search_name += 7;
            }

            ASTNode *def = find_struct_def(ctx, search_name);
            if (!def)
            {
                // check enums list explicitly if not found in instantiated list
                StructRef *er = ctx->parsed_enums_list;
                while (er)
                {
                    if (er->node && er->node->kind == NODE_ENUM &&
                        strcmp(er->node->enm.name, search_name) == 0)
                    {
                        def = er->node;
                        break;
                    }
                    er = er->next;
                }
            }

            if (def)
            {
                if (def->kind == NODE_ENUM)
                {
                    // Look for "Ok" variant
                    ASTNode *var = def->enm.variants;
                    while (var)
                    {
                        if (var->variant.name && strcmp(var->variant.name, "Ok") == 0)
                        {
                            if (var->variant.payload)
                            {
                                return type_to_c_string(var->variant.payload);
                            }
                            // Ok with no payload? Then it's void/u0.
                            return "void";
                        }
                        var = var->next;
                    }
                }
                else if (def->kind == NODE_STRUCT)
                {
                    // Look for "val" field
                    ASTNode *field = def->strct.fields;
                    while (field)
                    {
                        if (field->field.name && strcmp(field->field.name, "val") == 0)
                        {
                            return xstrdup(field->field.type);
                        }
                        field = field->next;
                    }
                }
            }
        }
    }

    if (node->kind == NODE_EXPR_MEMBER)
    {
        char *parent_type = infer_type(ctx, node->member.target);
        if (!parent_type)
        {
            return NULL;
        }

        char clean_name[MAX_TYPE_NAME_LEN];
        snprintf(clean_name, sizeof(clean_name), "%s", parent_type);
        char *ptr = (char *)strchr(clean_name, '*');
        if (ptr)
        {
            *ptr = 0;
        }

        return get_field_type_str(ctx, clean_name, node->member.field);
    }

    if (node->kind == NODE_EXPR_BINARY)
    {
        if (strcmp(node->binary.op, "??") == 0)
        {
            return infer_type(ctx, node->binary.left);
        }

        const char *op = node->binary.op;
        char *left_type = infer_type(ctx, node->binary.left);
        char *right_type = infer_type(ctx, node->binary.right);

        int is_logical = (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0 || strcmp(op, "==") == 0 ||
                          strcmp(op, "!=") == 0 || strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
                          strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0);

        if (is_logical)
        {
            return xstrdup("int");
        }

        if (left_type && strcmp(left_type, "usize") == 0)
        {
            return "usize";
        }
        if (right_type && strcmp(right_type, "usize") == 0)
        {
            return "usize";
        }
        if (left_type && strcmp(left_type, "double") == 0)
        {
            return "double";
        }

        return left_type ? left_type : right_type;
    }

    if (node->kind == NODE_MATCH)
    {
        ASTNode *case_node = node->match_stmt.cases;
        while (case_node)
        {
            char *type = infer_type(ctx, case_node->match_case.body);
            if (type && strcmp(type, "void") != 0 && strcmp(type, "unknown") != 0)
            {
                return type;
            }
            case_node = case_node->next;
        }
        return NULL;
    }

    if (node->kind == NODE_EXPR_INDEX)
    {
        char *array_type = infer_type(ctx, node->index.array);
        if (array_type)
        {
            // If T*, returns T. If T[], returns T.
            char *ptr = (char *)strrchr(array_type, '*');
            if (ptr)
            {
                ptrdiff_t len = ptr - array_type;
                char *buf = xmalloc((size_t)(len + 1));
                strncpy(buf, array_type, (size_t)(len));
                buf[len] = 0;
                return buf;
            }

            if (strncmp(array_type, "Slice__", 7) == 0)
            {
                return xstrdup(array_type + 7);
            }

            char *search_name = array_type;
            if (strncmp(search_name, "struct ", 7) == 0)
            {
                search_name += 7;
            }

            ASTNode *def = find_struct_def(ctx, search_name);
            if (def && def->type_info && def->type_info->kind == TYPE_VECTOR &&
                def->type_info->inner)
            {
                return type_to_c_string(def->type_info->inner);
            }
        }
        return "int";
    }

    if (node->kind == NODE_EXPR_UNARY)
    {
        if (strcmp(node->unary.op, "&") == 0)
        {
            char *inner = infer_type(ctx, node->unary.operand);
            if (inner)
            {
                char *buf = xmalloc(strlen(inner) + 2);
                sprintf(buf, "%s*", inner); /* safe */
                return buf;
            }
        }
        if (strcmp(node->unary.op, "*") == 0)
        {
            char *inner = infer_type(ctx, node->unary.operand);
            if (inner)
            {
                if (strcmp(inner, "string") == 0)
                {
                    return xstrdup("char");
                }
                char *ptr = (char *)strchr(inner, '*');
                if (ptr)
                {
                    // Return base type (naive)
                    ptrdiff_t len = ptr - inner;
                    char *dup = xmalloc((size_t)(len + 1));
                    strncpy(dup, inner, (size_t)(len));
                    dup[len] = 0;
                    return dup;
                }
            }
        }
        return infer_type(ctx, node->unary.operand);
    }

    if (node->kind == NODE_AWAIT)
    {
        // Infer underlying type T from await Async<T>
        // Check operand type for Generics <T>
        char *op_type = infer_type(ctx, node->unary.operand);
        if (op_type)
        {
            char *start = (char *)strchr(op_type, '<');
            if (start)
            {
                start++; // Skip <
                char *end = (char *)strrchr(op_type, '>');
                if (end && end > start)
                {
                    ptrdiff_t len = end - start;
                    char *extracted = xmalloc((size_t)(len + 1));
                    strncpy(extracted, start, (size_t)(len));
                    extracted[len] = 0;
                    return extracted;
                }
            }
        }

        // Fallback: If it's a direct call await foo(), we can lookup signature even if generic
        // syntax wasn't used
        if (node->unary.operand->kind == NODE_EXPR_CALL &&
            node->unary.operand->call.callee->kind == NODE_EXPR_VAR)
        {
            FuncSig *sig = find_func(ctx, node->unary.operand->call.callee->var_ref.name);
            if (sig && sig->ret_type)
            {
                return type_to_c_string(sig->ret_type);
            }
        }

        return "void*";
    }

    if (node->kind == NODE_EXPR_CAST)
    {
        return node->cast.target_type;
    }

    if (node->kind == NODE_EXPR_STRUCT_INIT)
    {
        return node->struct_init.struct_name;
    }

    if (node->kind == NODE_EXPR_ARRAY_LITERAL)
    {
        if (node->type_info)
        {
            return type_to_c_string(node->type_info);
        }
        return NULL;
    }

    if (node->kind == NODE_EXPR_LITERAL)
    {
        if (node->literal.kind == LITERAL_STRING)
        {
            return xstrdup("string");
        }
        if (node->literal.kind == LITERAL_CHAR)
        {
            return xstrdup("char");
        }
        if (node->literal.kind == LITERAL_FLOAT)
        {
            return "double";
        }
        return "int";
    }

    return NULL;
}

// Extract variable names from argument string.

// Parse original method name from mangled name.

// Replace string type in arguments.

// Helper to emit auto type or fallback.

// Field type lookup — get the type string of a struct field.
// (Moved from codegen_utils.c)

char *get_field_type_str(ParserContext *ctx, const char *struct_name, const char *field_name)
{
    char clean_name[MAX_TYPE_NAME_LEN];
    strncpy(clean_name, struct_name, sizeof(clean_name) - 1);
    clean_name[sizeof(clean_name) - 1] = 0;

    char *ptr = (char *)strchr(clean_name, '<');
    if (ptr)
    {
        *ptr = 0;
    }

    ASTNode *def = find_struct_def(ctx, clean_name);
    if (!def)
    {
        return NULL;
    }

    ASTNode *f = def->strct.fields;
    while (f)
    {
        if (strcmp(f->field.name, field_name) == 0)
        {
            return f->field.type;
        }
        f = f->next;
    }
    return NULL;
}

// Type inference.
