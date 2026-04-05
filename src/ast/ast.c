
#include "ast.h"
#include "../parser/parser.h"
#include "zprep.h"
#include <stdlib.h>
#include <string.h>

typedef struct TraitReg
{
    char *name;
    struct TraitReg *next;
} TraitReg;

static TraitReg *registered_traits = NULL;

void register_trait(const char *name)
{
    TraitReg *r = xmalloc(sizeof(TraitReg));
    r->name = xstrdup(name);
    r->next = registered_traits;
    registered_traits = r;
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
        free(base);
        base = nb;
    }
    else if (strncmp(base, "union ", 6) == 0)
    {
        char *nb = xstrdup(base + 6);
        free(base);
        base = nb;
    }

    char *p = strchr(base, '*');
    if (p)
    {
        *p = '\0';
    }

    TraitReg *r = registered_traits;
    while (r)
    {
        if (0 == strcmp(r->name, base))
        {
            free(base);
            return 1;
        }
        r = r->next;
    }
    free(base);
    return 0;
}

int is_trait_ptr(const char *name)
{
    if (!name)
    {
        return 0;
    }
    const char *p = strchr(name, '*');
    if (!p)
    {
        return 0;
    }
    return is_trait(name);
}

ASTNode *ast_create(NodeType type)
{
    ASTNode *node = xmalloc(sizeof(ASTNode));
    memset(node, 0, sizeof(ASTNode));
    node->type = type;
    return node;
}

void ast_free(ASTNode *node)
{
    if (node->type == NODE_AST_COMMENT)
    {
        if (node->comment.content)
        {
            free(node->comment.content);
        }
    }
    free(node);
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
    t->inner = NULL;
    t->args = NULL;
    t->arg_count = 0;
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

Type *type_new_vector(Type *inner, int size)
{
    Type *t = type_new(TYPE_VECTOR);
    t->inner = inner;
    t->array_size = size;
    return t;
}

int is_char_ptr(Type *t)
{
    // Handle both primitive char* and legacy struct char*.
    if (TYPE_POINTER == t->kind && TYPE_CHAR == t->inner->kind)
    {
        return 1;
    }
    if (TYPE_POINTER == t->kind && TYPE_STRUCT == t->inner->kind &&
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
         t->kind == TYPE_C_ULONG || t->kind == TYPE_C_LONG_LONG || t->kind == TYPE_C_ULONG_LONG ||
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
        return 0;
    }

    if (a->kind == TYPE_STRUCT || a->kind == TYPE_GENERIC)
    {
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
        if (a->arg_count != b->arg_count)
        {
            return 0;
        }
        if (!type_eq(a->inner, b->inner))
        {
            return 0;
        }
        for (int i = 0; i < a->arg_count; i++)
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
        sprintf(final, "const %s", res);
        free(res);
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
    case TYPE_C_LONG_LONG:
        return xstrdup("c_long_long");
    case TYPE_C_ULONG_LONG:
        return xstrdup("c_ulong_long");
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
        sprintf(res, "i%d", t->array_size);
        return res;
    }
    case TYPE_UBITINT:
    {
        char *res = xmalloc(32);
        sprintf(res, "u%d", t->array_size);
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
        sprintf(res, "%sx%d", inner, t->array_size);
        free(inner);
        return res;
    }

    case TYPE_POINTER:
    {
        char *inner = type_to_string(t->inner);
        if (t->is_restrict)
        {
            char *res = xmalloc(strlen(inner) + 16);
            sprintf(res, "%s* __restrict", inner);
            return res;
        }
        else
        {
            char *res = xmalloc(strlen(inner) + 2);
            sprintf(res, "%s*", inner);
            return res;
        }
    }

    case TYPE_ARRAY:
    {
        if (t->array_size == 0)
        {
            char *inner = type_to_string(t->inner);
            char *res = xmalloc(strlen(inner) + 8);
            sprintf(res, "Slice__%s", inner);
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
                dims = xrealloc(dims, sizeof(int) * dims_cap);
            }
            dims[dims_count++] = base->array_size;
            base = base->inner;
        }

        char *inner = type_to_string(base);
        int total_len = strlen(inner) + 1;
        for (int i = 0; i < dims_count; i++)
        {
            total_len += 20;
        }

        char *res = xmalloc(total_len);
        strcpy(res, inner);
        free(inner);

        char *p = res + strlen(res);
        for (int i = 0; i < dims_count; i++)
        {
            sprintf(p, "[%d]", dims[i]);
            p += strlen(p);
        }

        if (dims)
        {
            free(dims);
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

            for (int i = 0; i < t->arg_count; i++)
            {
                if (i > 0)
                {
                    char *tmp = xmalloc(strlen(res) + 3);
                    snprintf(tmp, strlen(res) + 3, "%s, ", res);
                    free(res);
                    res = tmp;
                }
                char *arg = type_to_string(t->args[i]);
                char *tmp = xmalloc(strlen(res) + strlen(arg) + 1);
                sprintf(tmp, "%s%s", res, arg);
                free(res);
                res = tmp;
                free(arg);
            }
            char *tmp = xmalloc(strlen(res) + strlen(ret) + 5); // ) -> Ret
            sprintf(tmp, "%s) -> %s", res, ret);
            free(res);
            res = tmp;
            free(ret);
            return res;
        }

        // fn(Args) -> Ret
        char *ret = type_to_string(t->inner);
        char *res = xmalloc(strlen(ret) + 64);
        snprintf(res, strlen(ret) + 64, "fn(");

        for (int i = 0; i < t->arg_count; i++)
        {
            if (i > 0)
            {
                char *tmp = xmalloc(strlen(res) + 3);
                snprintf(tmp, strlen(res) + 3, "%s, ", res);
                free(res);
                res = tmp;
            }
            char *arg = type_to_string(t->args[i]);
            char *tmp = xmalloc(strlen(res) + strlen(arg) + 1);
            sprintf(tmp, "%s%s", res, arg);
            free(res);
            res = tmp;
            free(arg);
        }
        char *tmp = xmalloc(strlen(res) + strlen(ret) + 6); // ) -> Ret
        sprintf(tmp, "%s) -> %s", res, ret);
        free(res);
        res = tmp;
        free(ret);
        return res;
    }

    case TYPE_STRUCT:
    case TYPE_GENERIC:
    {
        if (t->arg_count > 0 && t->name && strstr(t->name, "__") == NULL)
        {
            char *base = t->name;
            size_t base_len = strlen(base);
            char *res = xmalloc(base_len + 1);
            strcpy(res, base);

            for (int i = 0; i < t->arg_count; i++)
            {
                char *arg = type_to_string(t->args[i]);
                char *clean_arg = sanitize_mangled_name(arg);

                size_t new_len = strlen(res) + strlen(clean_arg) + 3;
                char *new_res = xmalloc(new_len);
                sprintf(new_res, "%s__%s", res, clean_arg);

                free(res);
                res = new_res;
                free(arg);
                free(clean_arg);
            }
            return res;
        }
        return xstrdup(t->name);
    }
    case TYPE_ALIAS:
        return xstrdup(t->name);

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
        sprintf(final, "const %s", res);
        free(res);
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
        // Only prepend 'struct' if explicitly requested (e.g. "struct Foo")
        // otherwise assume it's a typedef/alias (e.g. "Foo").
        if (t->is_explicit_struct)
        {
            char *res = xmalloc(strlen(t->name) + 8);
            sprintf(res, "struct %s", t->name);
            return res;
        }
        else
        {
            return xstrdup(t->name);
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
        return xstrdup("int");
    case TYPE_C_UINT:
        return xstrdup("unsigned int");
    case TYPE_C_LONG:
        return xstrdup("long");
    case TYPE_C_ULONG:
        return xstrdup("unsigned long");
    case TYPE_C_LONG_LONG:
        return xstrdup("long long");
    case TYPE_C_ULONG_LONG:
        return xstrdup("unsigned long long");
    case TYPE_C_SHORT:
        return xstrdup("short");
    case TYPE_C_USHORT:
        return xstrdup("unsigned short");
    case TYPE_C_CHAR:
        return xstrdup("char");
    case TYPE_C_UCHAR:
        return xstrdup("unsigned char");

    case TYPE_INT:
        // 'int' in Zen C maps to 'i32' now for portability.
        // FFI should use c_int.
        return xstrdup("int32_t");
    case TYPE_FLOAT:
        return xstrdup("float");
    case TYPE_BITINT:
    {
        char *res = xmalloc(32);
        sprintf(res, "_BitInt(%d)", t->array_size);
        return res;
    }
    case TYPE_UBITINT:
    {
        char *res = xmalloc(40);
        sprintf(res, "unsigned _BitInt(%d)", t->array_size);
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
        sprintf(res, "ZC_SIMD(%s, %d)", inner, t->array_size);
        free(inner);
        return res;
    }

    case TYPE_POINTER:
    {
        char *inner = type_to_c_string(t->inner);
        char *ptr_token = strstr(inner, "(*");
        if (ptr_token)
        {
            long prefix_len = ptr_token - inner + 2; // "void (*"
            char *res = xmalloc(strlen(inner) + 2);
            strncpy(res, inner, prefix_len);
            res[prefix_len] = 0;
            strcat(res, "*");
            strcat(res, ptr_token + 2);
            free(inner);
            return res;
        }

        if (t->is_restrict)
        {
            char *res = xmalloc(strlen(inner) + 16);
            sprintf(res, "%s* __restrict", inner);
            return res;
        }
        else
        {
            char *res = xmalloc(strlen(inner) + 2);
            sprintf(res, "%s*", inner);
            return res;
        }
    }

    case TYPE_ARRAY:
    {
        if (t->array_size == 0)
        {
            char *inner_zens = type_to_string(t->inner);
            char *res = xmalloc(strlen(inner_zens) + 8);
            sprintf(res, "Slice__%s", inner_zens);
            free(inner_zens);
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
                dims = xrealloc(dims, sizeof(int) * dims_cap);
            }
            dims[dims_count++] = base->array_size;
            base = base->inner;
        }

        char *inner = type_to_c_string(base);
        int total_len = strlen(inner) + 1;
        for (int i = 0; i < dims_count; i++)
        {
            total_len += 20;
        }

        char *res = xmalloc(total_len);
        strcpy(res, inner);
        free(inner);

        char *p = res + strlen(res);
        for (int i = 0; i < dims_count; i++)
        {
            sprintf(p, "[%d]", dims[i]);
            p += strlen(p);
        }

        if (dims)
        {
            free(dims);
        }
        return res;
    }

    case TYPE_FUNCTION:
        if (t->is_raw)
        {
            char *ret = type_to_c_string(t->inner);
            char *res = xmalloc(strlen(ret) + 64); // heuristic start buffer
            snprintf(res, strlen(ret) + 64, "%s (*)(", ret);

            for (int i = 0; i < t->arg_count; i++)
            {
                if (i > 0)
                {
                    char *tmp = xmalloc(strlen(res) + 3);
                    snprintf(tmp, strlen(res) + 3, "%s, ", res);
                    free(res);
                    res = tmp;
                }
                char *arg = type_to_c_string(t->args[i]);
                char *tmp = xmalloc(strlen(res) + strlen(arg) + 1);
                sprintf(tmp, "%s%s", res, arg);
                free(res);
                res = tmp;
                free(arg);
            }
            char *tmp = xmalloc(strlen(res) + 2);
            sprintf(tmp, "%s)", res);
            free(res);
            res = tmp;
            free(ret);
            return res;
        }
        if (t->inner)
        {
            free(type_to_c_string(t->inner));
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
        return xstrdup(t->name);

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
