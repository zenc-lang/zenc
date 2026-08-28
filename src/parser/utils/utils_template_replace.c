// SPDX-License-Identifier: MIT
#include "plugins/plugin_manager.h"
#include "parser.h"
#include "utils/format_expr.h"
#include "utils/colors.h"
#include "utils/utils.h"
#include "constants.h"
#include "ast/primitives.h"
#include <ctype.h>
#include "analysis/const_fold.h"
#include <stdlib.h>
#include <string.h>

int is_unmangle_primitive(const char *base);

static char *replace_in_string(const char *src, const char *old_w, const char *new_w)
{
    if (!src || !old_w || !new_w)
    {
        return src ? xstrdup(src) : NULL;
    }

    // Check for multiple parameters (comma separated)
    if (strchr(old_w, ','))
    {
        char *running_src = xstrdup(src);

        char *p_ptr = (char *)old_w;
        char *c_ptr = (char *)new_w;

        while (*p_ptr && *c_ptr)
        {
            char *p_end = (char *)strchr(p_ptr, ',');
            int p_len = p_end ? (int)(p_end - p_ptr) : (int)strlen(p_ptr);

            char *c_end = (char *)strchr(c_ptr, ',');
            int c_len = c_end ? (int)(c_end - c_ptr) : (int)strlen(c_ptr);

            char *curr_p = xmalloc((size_t)(p_len + 1));
            strncpy(curr_p, p_ptr, (size_t)(p_len));
            curr_p[p_len] = 0;

            char *curr_c = xmalloc((size_t)(c_len + 1));
            strncpy(curr_c, c_ptr, (size_t)(c_len));
            curr_c[c_len] = 0;

            char *next_src = replace_in_string(running_src, curr_p, curr_c);
            zfree(running_src);
            running_src = next_src;

            zfree(curr_p);
            zfree(curr_c);

            if (p_end)
            {
                p_ptr = p_end + 1;
            }
            else
            {
                break;
            }
            if (c_end)
            {
                c_ptr = c_end + 1;
            }
            else
            {
                break;
            }
        }
        return running_src;
    }

    char *result;
    int i, cnt = 0;
    size_t newWlen = strlen(new_w);
    size_t oldWlen = strlen(old_w);

    // Pass 1: Count replacements
    int in_string = 0;
    for (i = 0; src[i] != '\0'; i++)
    {
        if (src[i] == '\"' && (i == 0 || src[i - 1] != '\\'))
        {
            in_string = !in_string;
        }

        if (!in_string && strstr(&src[i], old_w) == &src[i])
        {
            // Check boundaries
            int valid = 1;
            if (i > 0 && is_ident_char(src[i - 1]))
            {
                valid = 0;
            }
            if (valid && (is_ident_char(src[(size_t)(i) + (size_t)(oldWlen)]) ||
                          src[(size_t)(i) + (size_t)(oldWlen)] == '<'))
            {
                valid = 0;
            }

            if (valid)
            {
                cnt++;
                i = i + (int)oldWlen - 1;
            }
        }
    }

    // Allocate result buffer
    result = (char *)xmalloc((size_t)i + (size_t)cnt * (newWlen - oldWlen) + 1);

    // Pass 2: Perform replacement
    int j = 0;
    in_string = 0;

    int src_idx = 0;

    while (src[src_idx] != '\0')
    {
        if (src[src_idx] == '\"' && (src_idx == 0 || src[src_idx - 1] != '\\'))
        {
            in_string = !in_string;
        }

        int replaced = 0;
        if (!in_string && strstr(&src[src_idx], old_w) == &src[src_idx])
        {
            int valid = 1;
            if (src_idx > 0 && is_ident_char(src[src_idx - 1]))
            {
                valid = 0;
            }
            if (valid && (is_ident_char(src[(size_t)(src_idx) + (size_t)(oldWlen)]) ||
                          src[(size_t)(src_idx) + (size_t)(oldWlen)] == '<'))
            {
                valid = 0;
            }

            if (valid)
            {
                strcpy(&result[j], new_w);
                j = j + (int)newWlen;
                src_idx = src_idx + (int)oldWlen;
                replaced = 1;
            }
        }

        if (!replaced)
        {
            result[j++] = src[src_idx++];
        }
    }
    result[j] = '\0';
    return result;
}

Type *replace_type_formal(Type *t, const char *p, const char *c, const char *os, const char *ns);
// Helper to replace generic params in mangled names (e.g. Option_V_None ->
// Option_int_None)
static char *replace_mangled_part(const char *src, const char *param, const char *concrete)
{
    if (!src || !param || !concrete)
    {
        return src ? xstrdup(src) : NULL;
    }

    size_t plen = strlen(param);
    size_t clen = strlen(concrete);
    size_t src_len = strlen(src);

    // Initial estimate for result size
    size_t res_cap = src_len + 512;
    char *result = xmalloc((size_t)(res_cap));
    result[0] = 0;

    const char *curr = src;
    char *out = result;
    size_t current_len = 0;

    while (*curr)
    {
        // Ensure enough space (including the next character or replacement)
        if (current_len + (clen > 1 ? clen : 1) + 1 >= res_cap)
        {
            res_cap = res_cap * 2 + clen;
            char *new_res = xmalloc((size_t)(res_cap));
            memcpy(new_res, result, (size_t)(current_len));
            zfree(result);
            result = new_res;
            out = result + current_len;
        }

        // Check if param matches here
        if (strncmp(curr, param, (size_t)(plen)) == 0)
        {
            int valid = 1;
            int has_underscore_boundary = 0;

            if (curr > src)
            {
                if (*(curr - 1) == '_')
                {
                    has_underscore_boundary = 1;
                }
                else if (is_ident_char(*(curr - 1)))
                {
                    valid = 0;
                }
            }

            if (valid && curr[plen] != 0 && curr[plen] != '_' && is_ident_char(curr[plen]))
            {
                if (strncmp(curr + plen, "Ptr", 3) != 0)
                {
                    valid = 0;
                }
            }
            if (valid && curr[plen] == '_')
            {
                has_underscore_boundary = 1;
            }

            if (valid && !has_underscore_boundary)
            {
                // Also allow <, ,, (, [ as boundaries
                char prev = (curr > src) ? *(curr - 1) : 0;
                if (prev == '<' || prev == ',' || prev == '(' || prev == '[' || prev == ' ')
                {
                    // OK
                }
                else
                {
                    valid = 0;
                }
            }

            if (valid)
            {
                // Ensure double underscore boundary for the replacement
                if (curr > src && *(curr - 1) == '_' && (curr == src + 1 || *(curr - 2) != '_'))
                {
                    *out++ = '_';
                    current_len++;
                }

                memcpy(out, concrete, (size_t)(clen));
                out += clen;
                current_len += clen;

                if (curr[plen] == '_' && curr[plen + 1] != '_')
                {
                    *out++ = '_';
                    current_len++;
                }

                curr += plen;
                continue;
            }
        }
        *out++ = *curr++;
        current_len++;
    }
    *out = 0;
    return result;
}

char *replace_type_str(const char *src, const char *param, const char *concrete,
                       const char *old_struct, const char *new_struct)
{
    if (!src)
    {
        return NULL;
    }
    if (!param || !concrete)
    {
        return xstrdup(src);
    }

    // 1. Exact match (base case)
    if (strcmp(src, param) == 0)
    {
        return xstrdup(concrete);
    }

    // 2. Handle simple pointer cases recursively (safe as src shrinks)
    size_t slen = strlen(src);
    if (slen > 1 && src[slen - 1] == '*')
    {
        char *base = xmalloc((size_t)(intptr_t)(slen));
        strncpy(base, src, slen - 1);
        base[slen - 1] = 0;
        char *nb = replace_type_str(base, param, concrete, old_struct, new_struct);
        if (!nb)
        {
            zfree(base);
            return NULL;
        }
        char *res = xmalloc(strlen(nb) + 2);
        sprintf(res, "%s*", nb); /* safe */
        zfree(base);
        zfree(nb);
        return res;
    }

    // 3. Structural fallback for complex strings (e.g. "Self", "Option<T>")
    char *res = xstrdup(src);

    // Case 3a: Explicit template replacement (e.g. Vec<T> -> Vec__int32_t)
    if (old_struct && new_struct && param)
    {
        char tpl_w[MAX_TYPE_NAME_LEN];
        snprintf(tpl_w, sizeof(tpl_w), "%s<%s>", old_struct, param);
        if (strstr(res, tpl_w))
        {
            char *tmp = replace_in_string(res, tpl_w, new_struct);
            zfree(res);
            res = tmp;
        }
    }

    // Case 3b: Base struct replacement (e.g. Vec -> Vec__int32_t)
    if (!res)
    {
        return NULL;
    }
    if (old_struct && new_struct && strstr(res, old_struct))
    {
        char *tmp = replace_in_string(res, old_struct, new_struct);
        zfree(res);
        res = tmp;
    }

    // 4. Boundary-safe mangled replacement (e.g. "Option_T" or "Option__T")
    // Split multi-param strings (X, Y, Z) and replace each individually
    char *final_res = xstrdup(res);
    if (param && concrete && strchr(param, ','))
    {
        char *p_ptr = (char *)param;
        char *c_ptr = (char *)concrete;
        while (*p_ptr && *c_ptr)
        {
            char *p_end = (char *)strchr(p_ptr, ',');
            int p_len = p_end ? (int)(p_end - p_ptr) : (int)strlen(p_ptr);
            char *c_end = (char *)strchr(c_ptr, ',');
            int c_len = c_end ? (int)(c_end - c_ptr) : (int)strlen(c_ptr);

            char *p_part = xmalloc((size_t)(p_len + 1));
            strncpy(p_part, p_ptr, (size_t)(p_len));
            p_part[p_len] = 0;

            char *c_part = xmalloc((size_t)(c_len + 1));
            strncpy(c_part, c_ptr, (size_t)(c_len));
            c_part[c_len] = 0;

            char *clean_c = sanitize_mangled_name(c_part);
            char *tmp = replace_mangled_part(final_res, p_part, clean_c);
            zfree(final_res);
            final_res = tmp;

            zfree(p_part);
            zfree(c_part);
            zfree(clean_c);

            if (p_end)
            {
                p_ptr = p_end + 1;
            }
            else
            {
                break;
            }
            if (c_end)
            {
                c_ptr = c_end + 1;
            }
            else
            {
                break;
            }
        }
    }
    else
    {
        char *t1 = replace_in_string(final_res, param, concrete);
        zfree(final_res);
        final_res = t1;

        char *clean_c = sanitize_mangled_name(concrete);
        char *tmp = replace_mangled_part(final_res, param, clean_c);
        zfree(final_res);
        final_res = tmp;
        zfree(clean_c);
    }

    zfree(res);
    return final_res;
}

ASTNode *copy_ast_replacing(ParserContext *ctx, ASTNode *n, const char *p, const char *c,
                            const char *os, const char *ns);

Type *type_from_string_helper(const char *c)
{
    if (!c)
    {
        return NULL;
    }

    // Check for pointer suffix '*'
    size_t len = strlen(c);
    if (len > 0 && c[len - 1] == '*')
    {
        size_t base_len = len - 1;
        char *base = xmalloc((size_t)(base_len + 1));
        strncpy(base, c, (size_t)(base_len));
        base[base_len] = 0;

        Type *inner = type_from_string_helper(base);
        zfree(base);

        return type_new_ptr(inner);
    }

    // Check for 'const ' prefix
    if (strncmp(c, "const ", 6) == 0)
    {
        Type *inner = type_from_string_helper(c + 6);
        if (inner)
        {
            inner->is_const = 1;
        }
        return inner;
    }

    if (strncmp(c, "struct ", 7) == 0)
    {
        Type *n = type_new(TYPE_STRUCT);
        n->name = sanitize_mangled_name(c + 7);
        n->is_explicit_struct = 1;
        return n;
    }

    if (strcmp(c, "int") == 0)
    {
        return type_new(TYPE_INT);
    }
    if (strcmp(c, "float") == 0)
    {
        return type_new(TYPE_FLOAT);
    }
    if (strcmp(c, "void") == 0)
    {
        return type_new(TYPE_VOID);
    }
    if (strcmp(c, "string") == 0)
    {
        return type_new(TYPE_STRING);
    }
    if (strcmp(c, "bool") == 0)
    {
        return type_new(TYPE_BOOL);
    }
    if (strcmp(c, "char") == 0)
    {
        return type_new(TYPE_CHAR);
    }
    if (strcmp(c, "I8") == 0 || strcmp(c, "i8") == 0)
    {
        return type_new(TYPE_I8);
    }
    if (strcmp(c, "U8") == 0 || strcmp(c, "u8") == 0)
    {
        return type_new(TYPE_U8);
    }
    if (strcmp(c, "I16") == 0 || strcmp(c, "i16") == 0)
    {
        return type_new(TYPE_I16);
    }
    if (strcmp(c, "U16") == 0 || strcmp(c, "u16") == 0)
    {
        return type_new(TYPE_U16);
    }
    if (strcmp(c, "I32") == 0 || strcmp(c, "i32") == 0 || strcmp(c, "int32_t") == 0)
    {
        return type_new(TYPE_I32);
    }
    if (strcmp(c, "U32") == 0 || strcmp(c, "u32") == 0 || strcmp(c, "uint32_t") == 0)
    {
        return type_new(TYPE_U32);
    }
    if (strcmp(c, "I64") == 0 || strcmp(c, "i64") == 0 || strcmp(c, "int64_t") == 0)
    {
        return type_new(TYPE_I64);
    }
    if (strcmp(c, "U64") == 0 || strcmp(c, "u64") == 0 || strcmp(c, "uint64_t") == 0)
    {
        return type_new(TYPE_U64);
    }
    if (strcmp(c, "float") == 0 || strcmp(c, "f32") == 0)
    {
        return type_new(TYPE_F32);
    }
    if (strcmp(c, "double") == 0 || strcmp(c, "f64") == 0)
    {
        return type_new(TYPE_F64);
    }
    if (strcmp(c, "I128") == 0 || strcmp(c, "i128") == 0)
    {
        return type_new(TYPE_I128);
    }
    if (strcmp(c, "U128") == 0 || strcmp(c, "u128") == 0)
    {
        return type_new(TYPE_U128);
    }
    if (strcmp(c, "rune") == 0)
    {
        return type_new(TYPE_RUNE);
    }
    if (strcmp(c, "uint") == 0)
    {
        return type_new(TYPE_UINT);
    }

    if (strcmp(c, "byte") == 0)
    {
        return type_new(TYPE_BYTE);
    }
    if (strcmp(c, "usize") == 0)
    {
        return type_new(TYPE_USIZE);
    }
    if (strcmp(c, "isize") == 0)
    {
        return type_new(TYPE_ISIZE);
    }

    Type *n = type_new(TYPE_STRUCT);
    n->name = sanitize_mangled_name(c);
    return n;
}

Type *replace_type_formal(Type *t, const char *p, const char *c, const char *os, const char *ns)
{
    if (!t || (uintptr_t)t < 0x10000)
    {
        return NULL;
    }

    // Defensive check: Ensure kind is valid
    if ((int)t->kind < 0 || (int)t->kind > 100) // 100 is a safe upper bound for TypeKind
    {
        return NULL;
    }

    // Exact Match Logic (with multi-param splitting)
    if ((t->kind == TYPE_STRUCT || t->kind == TYPE_GENERIC) && t->name)
    {

        if (p && c && strchr(p, ','))
        {
            char *p_ptr = (char *)p;
            char *c_ptr = (char *)c;
            while (*p_ptr && *c_ptr)
            {
                char *p_end = (char *)strchr(p_ptr, ',');
                int p_len = p_end ? (int)(p_end - p_ptr) : (int)strlen(p_ptr);
                char *c_end = (char *)strchr(c_ptr, ',');
                int c_len = c_end ? (int)(c_end - c_ptr) : (int)strlen(c_ptr);

                if ((int)strlen(t->name) == p_len && strncmp(t->name, p_ptr, (size_t)(p_len)) == 0)
                {
                    char *c_part = xmalloc((size_t)(c_len + 1));
                    strncpy(c_part, c_ptr, (size_t)(c_len));
                    c_part[c_len] = 0;

                    Type *res = type_from_string_helper(c_part);
                    zfree(c_part);
                    return res;
                }
                if (p_end)
                {
                    p_ptr = p_end + 1;
                }
                else
                {
                    break;
                }
                if (c_end)
                {
                    c_ptr = c_end + 1;
                }
                else
                {
                    break;
                }
            }
        }
        else if (p && strcmp(t->name, p) == 0)
        {
            return type_from_string_helper(c);
        }
    }

    Type *n = xmalloc(sizeof(Type));
    *n = *t;

    if (t->name)
    {
        if (os && ns && strcmp(t->name, os) == 0)
        {
            n->name = xstrdup(ns);
            n->kind = TYPE_STRUCT;
            n->count = 0;
            n->args = NULL;
        }
        else if (p && c)
        {
            // Suffix Match Logic (with multi-param splitting)
            char p_suffix[4096];
            p_suffix[0] = 0;

            const char *p_ptr = p;
            while (p_ptr && *p_ptr)
            {
                const char *p_next = (char *)strchr(p_ptr, ',');
                int sub_len = p_next ? (int)(p_next - p_ptr) : (int)strlen(p_ptr);
                char *sub = xmalloc((size_t)(sub_len + 1));
                strncpy(sub, p_ptr, (size_t)(sub_len));
                sub[sub_len] = 0;

                char *clean_sub = sanitize_mangled_name(sub);
                strncat(p_suffix, "__", sizeof(p_suffix) - strlen(p_suffix) - 1);
                strncat(p_suffix, clean_sub, sizeof(p_suffix) - strlen(p_suffix) - 1);
                zfree(clean_sub);
                zfree(sub);

                if (p_next)
                {
                    p_ptr = p_next + 1;
                }
                else
                {
                    break;
                }
            }

            size_t nlen = strlen(t->name);
            size_t slen = strlen(p_suffix);

            int match = 0;
            size_t found_slen = 0;
            size_t num_ptr_suffixes = 0;
            if (nlen >= slen && strcmp(t->name + nlen - slen, p_suffix) == 0)
            {
                match = 1;
                found_slen = slen;
            }
            else if (nlen > slen)
            {
                // Try matching with Ptr suffix
                const char *p_match = (char *)strstr(t->name, p_suffix);
                while (p_match)
                {
                    const char *after = p_match + slen;
                    int is_all_ptr = 1;
                    if (*after == '\0')
                    {
                        is_all_ptr = 0; // Handled by exact match above
                    }
                    while (*after)
                    {
                        if (strncmp(after, "Ptr", 3) == 0)
                        {
                            after += 3;
                        }
                        else
                        {
                            is_all_ptr = 0;
                            break;
                        }
                    }
                    if (is_all_ptr)
                    {
                        match = 1;
                        found_slen = nlen - (size_t)(p_match - t->name);
                        num_ptr_suffixes = (nlen - (size_t)(p_match - t->name) - slen) / 3u;
                        break;
                    }
                    p_match = strstr(p_match + 1, p_suffix);
                }
            }

            if (match)
            {
                slen = found_slen;
                char c_suffix[MAX_ERROR_MSG_LEN];
                c_suffix[0] = 0;
                const char *c_ptr = c;
                while (c_ptr && *c_ptr)
                {
                    const char *c_next = (char *)strchr(c_ptr, ',');
                    int sub_len = c_next ? (int)(c_next - c_ptr) : (int)strlen(c_ptr);

                    char *sub = xmalloc((size_t)(sub_len + 1));
                    strncpy(sub, c_ptr, (size_t)(sub_len));
                    sub[sub_len] = 0;

                    char *clean = sanitize_mangled_name(sub);
                    // Standardize: always use __ for mangled part
                    strncat(c_suffix, "__", sizeof(c_suffix) - strlen(c_suffix) - 1);
                    strncat(c_suffix, clean, sizeof(c_suffix) - strlen(c_suffix) - 1);
                    zfree(clean);
                    zfree(sub);

                    if (c_next)
                    {
                        c_ptr = c_next + 1;
                    }
                    else
                    {
                        break;
                    }
                }

                // Calculate required size more accurately
                size_t c_suffix_len = strlen(c_suffix);
                size_t total_needed =
                    (nlen > slen ? nlen - slen : 0) + c_suffix_len + (num_ptr_suffixes * 3) + 1;
                char *new_name = xmalloc((size_t)(total_needed));
                if (nlen > slen)
                {
                    strncpy(new_name, t->name, nlen - slen);
                    new_name[nlen - slen] = 0;
                }
                else
                {
                    new_name[0] = 0;
                }

                // Handle underscore merging: ensure exactly two underscores
                char *p_end = new_name + strlen(new_name);
                while (p_end > new_name && *(p_end - 1) == '_')
                {
                    *(--p_end) = '\0';
                }
                strcat(new_name, c_suffix);

                // Restore Ptr suffixes
                for (size_t k = 0; k < num_ptr_suffixes; k++)
                {
                    strcat(new_name, "Ptr");
                }
                n->name = new_name;
                n->kind = TYPE_STRUCT;
                n->count = 0;
                n->args = NULL;
            }
            else
            {
                n->name = xstrdup(t->name);
            }
        }
        else
        {
            n->name = xstrdup(t->name);
        }
    }

    if (t->kind == TYPE_POINTER || t->kind == TYPE_ARRAY || t->kind == TYPE_FUNCTION ||
        t->kind == TYPE_VECTOR)
    {
        n->inner = replace_type_formal(t->inner, p, c, os, ns);
    }

    if (t->count > 0 && t->args)
    {
        n->args = xmalloc(sizeof(Type *) * (size_t)(t->count));
        for (int i = 0; i < t->count; i++)
        {
            n->args[i] = replace_type_formal(t->args[i], p, c, os, ns);
        }
    }

    return n;
}

ASTNode *copy_ast_replacing(ParserContext *ctx, ASTNode *n, const char *p, const char *c,
                            const char *os, const char *ns)
{
    if (!n)
    {
        return NULL;
    }

    ASTNode *new_node = ast_create(n->kind);
    ASTNode *old_next =
        new_node->next; // Preserve next if ast_create did something (it doesn't currently)
    *new_node = *n;
    new_node->next = old_next; // Restore next before recursion

    if (n->resolved_type)
    {
        new_node->resolved_type = replace_type_str(n->resolved_type, p, c, os, ns);
    }
    new_node->type_info = replace_type_formal(n->type_info, p, c, os, ns);

    new_node->next = copy_ast_replacing(ctx, n->next, p, c, os, ns);

    switch (n->kind)
    {
    case NODE_FUNCTION:
        new_node->func.name = n->func.name ? xstrdup(n->func.name) : NULL;
        new_node->func.ret_type = replace_type_str(n->func.ret_type, p, c, os, ns);

        char *tmp_args = n->func.args ? xstrdup(n->func.args) : NULL;
        if (p && c && strchr(p, ','))
        {
            char *p_ptr = (char *)p;
            char *c_ptr = (char *)c;
            while (*p_ptr && *c_ptr)
            {
                char *p_end = (char *)strchr(p_ptr, ',');
                int p_len = p_end ? (int)(p_end - p_ptr) : (int)strlen(p_ptr);
                char *c_end = (char *)strchr(c_ptr, ',');
                int c_len = c_end ? (int)(c_end - c_ptr) : (int)strlen(c_ptr);

                char *p_part = xmalloc((size_t)(p_len + 1));
                strncpy(p_part, p_ptr, (size_t)(p_len));
                p_part[p_len] = 0;

                char *c_part = xmalloc((size_t)(c_len + 1));
                strncpy(c_part, c_ptr, (size_t)(c_len));
                c_part[c_len] = 0;

                char *t1 = replace_in_string(tmp_args, p_part, c_part);
                zfree(tmp_args);
                tmp_args = t1;

                char *clean_c = sanitize_mangled_name(c_part);
                char *t2 = replace_mangled_part(tmp_args, p_part, clean_c);
                zfree(tmp_args);
                tmp_args = t2;

                zfree(p_part);
                zfree(c_part);
                zfree(clean_c);

                if (p_end)
                {
                    p_ptr = p_end + 1;
                }
                else
                {
                    break;
                }
                if (c_end)
                {
                    c_ptr = c_end + 1;
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            char *t1 = replace_in_string(tmp_args, p, c);
            zfree(tmp_args);
            tmp_args = t1;

            if (p && c)
            {
                char *clean_c = sanitize_mangled_name(c);
                char *t2 = replace_mangled_part(tmp_args, p, clean_c);
                zfree(tmp_args);
                tmp_args = t2;
                zfree(clean_c);
            }
        }

        if (os && ns)
        {
            char *tmp2 = replace_in_string(tmp_args, os, ns);
            zfree(tmp_args);
            tmp_args = tmp2;
        }
        new_node->func.count = n->func.count;
        if (n->func.count > 0 && n->func.arg_types)
        {
            new_node->func.arg_types = xmalloc(sizeof(Type *) * (size_t)(n->func.count));
            for (int i = 0; i < n->func.count; i++)
            {
                new_node->func.arg_types[i] =
                    replace_type_formal(n->func.arg_types[i], p, c, os, ns);
            }
        }
        else
        {
            new_node->func.arg_types = NULL;
        }
        new_node->func.args = tmp_args;

        new_node->func.ret_type_info = replace_type_formal(n->func.ret_type_info, p, c, os, ns);

        // Deep copy default values AST if present
        if (n->func.default_values && n->func.count > 0)
        {
            new_node->func.default_values = xmalloc(sizeof(ASTNode *) * (size_t)(n->func.count));
            // We also need to regenerate the string defaults array based on the substituted ASTs
            // This ensures potential generic params in default values (T{}) are updated (i32{})
            // in the string representation used by codegen.
            char **new_defaults_strs = xmalloc(sizeof(char *) * (size_t)(n->func.count));

            for (int i = 0; i < n->func.count; i++)
            {
                if (n->func.default_values[i])
                {
                    new_node->func.default_values[i] =
                        copy_ast_replacing(ctx, n->func.default_values[i], p, c, os, ns);
                    new_defaults_strs[i] = ast_to_string(new_node->func.default_values[i]);
                }
                else
                {
                    new_node->func.default_values[i] = NULL;
                    new_defaults_strs[i] = NULL;
                }
            }
            // Replace the old string-based defaults with our regenerated ones
            // Note: We leak the old 'tmp_args' calculated above, but that's just a single string
            // for valid args The 'defaults' array in func struct is what matters for function
            // definition. Wait, NODE_FUNCTION has char *args (legacy) AND char **defaults (array).
            // parse_and_convert_args populated both.
            // We need to update new_node->func.defaults.
            new_node->func.defaults = new_defaults_strs;
        }

        new_node->func.body = copy_ast_replacing(ctx, n->func.body, p, c, os, ns);
        break;
    case NODE_BLOCK:
        new_node->block.statements = copy_ast_replacing(ctx, n->block.statements, p, c, os, ns);
        break;
    case NODE_RAW_STMT:
    {
        char *s1 = xstrdup(n->raw_stmt.content);
        if (p && c && strchr(p, ','))
        {
            char *p_ptr = (char *)p;
            char *c_ptr = (char *)c;
            while (*p_ptr && *c_ptr)
            {
                char *p_end = (char *)strchr(p_ptr, ',');
                int p_len = p_end ? (int)(p_end - p_ptr) : (int)strlen(p_ptr);
                char *c_end = (char *)strchr(c_ptr, ',');
                int c_len = c_end ? (int)(c_end - c_ptr) : (int)strlen(c_ptr);

                char *p_part = xmalloc((size_t)(p_len + 1));
                strncpy(p_part, p_ptr, (size_t)(p_len));
                p_part[p_len] = 0;

                char *c_part = xmalloc((size_t)(c_len + 1));
                strncpy(c_part, c_ptr, (size_t)(c_len));
                c_part[c_len] = 0;

                char *t1 = replace_in_string(s1, p_part, c_part);
                zfree(s1);
                s1 = t1;

                char *clean_c = sanitize_mangled_name(c_part);
                char *t2 = replace_mangled_part(s1, p_part, clean_c);
                zfree(s1);
                s1 = t2;

                zfree(p_part);
                zfree(c_part);
                zfree(clean_c);

                if (p_end)
                {
                    p_ptr = p_end + 1;
                }
                else
                {
                    break;
                }
                if (c_end)
                {
                    c_ptr = c_end + 1;
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            char *t1 = replace_in_string(s1, p, c);
            zfree(s1);
            s1 = t1;

            if (p && c)
            {
                char *clean_c = sanitize_mangled_name(c);
                char *t2 = replace_mangled_part(s1, p, clean_c);
                zfree(s1);
                s1 = t2;
                zfree(clean_c);
            }
        }

        if (os && ns)
        {
            char *s2 = replace_in_string(s1, os, ns);
            zfree(s1);
            s1 = s2;
        }

        new_node->raw_stmt.content = s1;
    }
    break;
    case NODE_VAR_DECL:
        new_node->var_decl.name = n->var_decl.name ? xstrdup(n->var_decl.name) : NULL;
        new_node->var_decl.type_str = replace_type_str(n->var_decl.type_str, p, c, os, ns);
        new_node->var_decl.type_info = replace_type_formal(n->var_decl.type_info, p, c, os, ns);
        new_node->var_decl.init_expr = copy_ast_replacing(ctx, n->var_decl.init_expr, p, c, os, ns);
        break;
    case NODE_RETURN:
        new_node->ret.value = copy_ast_replacing(ctx, n->ret.value, p, c, os, ns);
        break;
    case NODE_EXPR_BINARY:
        new_node->binary.left = copy_ast_replacing(ctx, n->binary.left, p, c, os, ns);
        new_node->binary.right = copy_ast_replacing(ctx, n->binary.right, p, c, os, ns);
        new_node->binary.op = n->binary.op ? xstrdup(n->binary.op) : NULL;
        break;
    case NODE_EXPR_UNARY:
        new_node->unary.op = n->unary.op ? xstrdup(n->unary.op) : NULL;
        new_node->unary.operand = copy_ast_replacing(ctx, n->unary.operand, p, c, os, ns);
        break;
    case NODE_EXPR_CALL:
        new_node->call.callee = copy_ast_replacing(ctx, n->call.callee, p, c, os, ns);
        new_node->call.args = copy_ast_replacing(ctx, n->call.args, p, c, os, ns);
        new_node->call.arg_names = n->call.arg_names; // Share pointer (shallow copy)
        new_node->call.count = n->call.count;
        break;
    case NODE_EXPR_VAR:
    {
        char *n1 = n->var_ref.name ? xstrdup(n->var_ref.name) : NULL;
        if (p && c && strchr(p, ','))
        {
            char *p_ptr = (char *)p;
            char *c_ptr = (char *)c;
            while (*p_ptr && *c_ptr)
            {
                char *p_end = (char *)strchr(p_ptr, ',');
                int p_len = p_end ? (int)(p_end - p_ptr) : (int)strlen(p_ptr);
                char *c_end = (char *)strchr(c_ptr, ',');
                int c_len = c_end ? (int)(c_end - c_ptr) : (int)strlen(c_ptr);

                char *p_part = xmalloc((size_t)(p_len + 1));
                strncpy(p_part, p_ptr, (size_t)(p_len));
                p_part[p_len] = 0;

                char *c_part = xmalloc((size_t)(c_len + 1));
                strncpy(c_part, c_ptr, (size_t)(c_len));
                c_part[c_len] = 0;

                char *t1 = replace_in_string(n1, p_part, c_part);
                zfree(n1);
                n1 = t1;

                char *clean_c = sanitize_mangled_name(c_part);
                char *t2 = replace_mangled_part(n1, p_part, clean_c);
                zfree(n1);
                n1 = t2;

                zfree(p_part);
                zfree(c_part);
                zfree(clean_c);

                if (p_end)
                {
                    p_ptr = p_end + 1;
                }
                else
                {
                    break;
                }
                if (c_end)
                {
                    c_ptr = c_end + 1;
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            if (p && c)
            {
                char *t1 = replace_in_string(n1, p, c);
                zfree(n1);
                n1 = t1;

                char *clean_c = sanitize_mangled_name(c);
                char *n2 = replace_mangled_part(n1, p, clean_c);
                zfree(clean_c);
                zfree(n1);
                n1 = n2;
            }
        }

        if (os && ns)
        {
            size_t os_len = strlen(os);
            size_t ns_len = strlen(ns);
            // Only replace if it starts with os__ and DOES NOT already start with ns__
            if (strncmp(n1, os, (size_t)(os_len)) == 0 && n1[os_len] == '_' &&
                n1[os_len + 1] == '_' && strncmp(n1, ns, (size_t)(ns_len)) != 0)
            {
                char *suffix = n1 + os_len;
                char buf[MAX_ERROR_MSG_LEN];
                snprintf(buf, sizeof(buf), "%s%s", ns, suffix);
                char *n3 = merge_underscores(buf);
                zfree(n1);
                n1 = n3;
            }
        }
        new_node->var_ref.name = n1;
    }
    break;
    case NODE_FIELD:
        new_node->field.name = n->field.name ? xstrdup(n->field.name) : NULL;
        new_node->field.type = replace_type_str(n->field.type, p, c, os, ns);
        break;
    case NODE_EXPR_LITERAL:
        if (n->literal.kind == LITERAL_STRING)
        {
            new_node->literal.string_val =
                n->literal.string_val ? xstrdup(n->literal.string_val) : NULL;
        }
        break;
    case NODE_EXPR_MEMBER:
        new_node->member.target = copy_ast_replacing(ctx, n->member.target, p, c, os, ns);
        new_node->member.field = n->member.field ? xstrdup(n->member.field) : NULL;
        break;
    case NODE_EXPR_INDEX:
        new_node->index.array = copy_ast_replacing(ctx, n->index.array, p, c, os, ns);
        new_node->index.index = copy_ast_replacing(ctx, n->index.index, p, c, os, ns);
        break;
    case NODE_EXPR_CAST:
        new_node->cast.target_type = replace_type_str(n->cast.target_type, p, c, os, ns);
        new_node->cast.expr = copy_ast_replacing(ctx, n->cast.expr, p, c, os, ns);
        break;
    case NODE_EXPR_STRUCT_INIT:
    {
        char *new_name = replace_type_str(n->struct_init.struct_name, p, c, os, ns);

        int is_ptr = 0;
        size_t len = strlen(new_name);
        if (len > 0 && new_name[len - 1] == '*')
        {
            is_ptr = 1;
        }

        int is_primitive = is_primitive_type_name(new_name);

        if ((is_ptr || is_primitive) && !n->struct_init.fields)
        {
            new_node->kind = NODE_EXPR_LITERAL;
            new_node->literal.kind = LITERAL_INT;
            new_node->literal.int_val = 0;
            zfree(new_name);
        }
        else
        {
            new_node->struct_init.struct_name = new_name;
            ASTNode *h = NULL, *t = NULL, *curr = n->struct_init.fields;
            while (curr)
            {
                ASTNode *cp = copy_ast_replacing(ctx, curr, p, c, os, ns);
                cp->next = NULL;
                if (!h)
                {
                    h = cp;
                }
                else
                {
                    t->next = cp;
                }
                t = cp;
                curr = curr->next;
            }
            new_node->struct_init.fields = h;
        }
        break;
    }
    case NODE_IF:
        new_node->if_stmt.condition = copy_ast_replacing(ctx, n->if_stmt.condition, p, c, os, ns);
        new_node->if_stmt.then_body = copy_ast_replacing(ctx, n->if_stmt.then_body, p, c, os, ns);
        new_node->if_stmt.else_body = copy_ast_replacing(ctx, n->if_stmt.else_body, p, c, os, ns);
        break;
    case NODE_WHILE:
        new_node->while_stmt.condition =
            copy_ast_replacing(ctx, n->while_stmt.condition, p, c, os, ns);
        new_node->while_stmt.body = copy_ast_replacing(ctx, n->while_stmt.body, p, c, os, ns);
        break;
    case NODE_FOR:
        new_node->for_stmt.init = copy_ast_replacing(ctx, n->for_stmt.init, p, c, os, ns);
        new_node->for_stmt.condition = copy_ast_replacing(ctx, n->for_stmt.condition, p, c, os, ns);
        new_node->for_stmt.step = copy_ast_replacing(ctx, n->for_stmt.step, p, c, os, ns);
        new_node->for_stmt.body = copy_ast_replacing(ctx, n->for_stmt.body, p, c, os, ns);
        break;
    case NODE_FOR_RANGE:
        new_node->for_range.start = copy_ast_replacing(ctx, n->for_range.start, p, c, os, ns);
        new_node->for_range.end = copy_ast_replacing(ctx, n->for_range.end, p, c, os, ns);
        new_node->for_range.body = copy_ast_replacing(ctx, n->for_range.body, p, c, os, ns);
        break;

    case NODE_MATCH_CASE:
        if (n->match_case.pattern)
        {
            char *s1 = xstrdup(n->match_case.pattern);
            if (p && c && strchr(p, ','))
            {
                char *p_ptr = (char *)p;
                char *c_ptr = (char *)c;
                while (*p_ptr && *c_ptr)
                {
                    char *p_end = (char *)strchr(p_ptr, ',');
                    int p_len = p_end ? (int)(p_end - p_ptr) : (int)strlen(p_ptr);
                    char *c_end = (char *)strchr(c_ptr, ',');
                    int c_len = c_end ? (int)(c_end - c_ptr) : (int)strlen(c_ptr);

                    char *p_part = xmalloc((size_t)(p_len + 1));
                    strncpy(p_part, p_ptr, (size_t)(p_len));
                    p_part[p_len] = 0;

                    char *c_part = xmalloc((size_t)(c_len + 1));
                    strncpy(c_part, c_ptr, (size_t)(c_len));
                    c_part[c_len] = 0;

                    char *t1 = replace_mangled_part(s1, p_part, c_part);
                    zfree(s1);
                    s1 = t1;

                    zfree(p_part);
                    zfree(c_part);

                    if (p_end)
                    {
                        p_ptr = p_end + 1;
                    }
                    else
                    {
                        break;
                    }
                    if (c_end)
                    {
                        c_ptr = c_end + 1;
                    }
                    else
                    {
                        break;
                    }
                }
            }
            else
            {
                char *t1 = replace_in_string(s1, p, c);
                zfree(s1);
                s1 = t1;
                char *t2 = replace_mangled_part(s1, p, c);
                zfree(s1);
                s1 = t2;
            }

            if (os && ns)
            {
                char *s2 = replace_in_string(s1, os, ns);
                zfree(s1);
                s1 = s2;
                char *colons = (char *)strstr(s1, "::");
                if (colons)
                {
                    colons[0] = '_';
                    memmove(colons + 1, colons + 2, strlen(colons + 2) + 1);
                }
            }
            new_node->match_case.pattern = s1;
        }
        new_node->match_case.binding_count = n->match_case.binding_count;
        if (n->match_case.binding_names)
        {
            new_node->match_case.binding_names =
                xmalloc(sizeof(char *) * (size_t)(n->match_case.binding_count));
            for (int i = 0; i < n->match_case.binding_count; i++)
            {
                if (n->match_case.binding_names[i])
                {
                    new_node->match_case.binding_names[i] = xstrdup(n->match_case.binding_names[i]);
                }
                else
                {
                    new_node->match_case.binding_names[i] = NULL;
                }
            }
        }
        if (n->match_case.binding_refs)
        {
            new_node->match_case.binding_refs =
                xmalloc(sizeof(int) * (size_t)(n->match_case.binding_count));
            memcpy(new_node->match_case.binding_refs, n->match_case.binding_refs,
                   sizeof(int) * (size_t)(n->match_case.binding_count));
        }
        new_node->match_case.is_default = n->match_case.is_default;
        new_node->match_case.is_destructuring = n->match_case.is_destructuring;

        new_node->match_case.body = copy_ast_replacing(ctx, n->match_case.body, p, c, os, ns);
        if (n->match_case.guard)
        {
            new_node->match_case.guard = copy_ast_replacing(ctx, n->match_case.guard, p, c, os, ns);
        }
        break;

    case NODE_IMPL:
        new_node->impl.struct_name = replace_type_str(n->impl.struct_name, p, c, os, ns);
        new_node->impl.methods = copy_ast_replacing(ctx, n->impl.methods, p, c, os, ns);
        break;
    case NODE_IMPL_TRAIT:
        new_node->impl_trait.trait_name =
            n->impl_trait.trait_name ? xstrdup(n->impl_trait.trait_name) : NULL;
        new_node->impl_trait.target_type =
            replace_type_str(n->impl_trait.target_type, p, c, os, ns);
        new_node->impl_trait.methods = copy_ast_replacing(ctx, n->impl_trait.methods, p, c, os, ns);
        break;
    case NODE_TYPEOF:
    case NODE_EXPR_SIZEOF:
        new_node->size_of.target_type = replace_type_str(n->size_of.target_type, p, c, os, ns);
        new_node->size_of.expr = copy_ast_replacing(ctx, n->size_of.expr, p, c, os, ns);
        new_node->size_of.is_type = n->size_of.is_type;
        if (n->size_of.target_type_info)
        {
            new_node->size_of.target_type_info =
                replace_type_formal(n->size_of.target_type_info, p, c, os, ns);
        }
        break;
    case NODE_LAMBDA:
        // Use a new lambda ID for each instantiation to ensure unique C function names
        new_node->lambda.lambda_id = ctx->lambda_counter++;
        new_node->lambda.count = n->lambda.count;
        if (n->lambda.count > 0)
        {
            new_node->lambda.param_names = xmalloc(sizeof(char *) * (size_t)(n->lambda.count));
            new_node->lambda.param_types = xmalloc(sizeof(char *) * (size_t)(n->lambda.count));
            for (int i = 0; i < n->lambda.count; i++)
            {
                new_node->lambda.param_names[i] = xstrdup(n->lambda.param_names[i]);
                new_node->lambda.param_types[i] =
                    replace_type_str(n->lambda.param_types[i], p, c, os, ns);
            }
        }
        new_node->lambda.return_type = replace_type_str(n->lambda.return_type, p, c, os, ns);
        new_node->lambda.capture_count = n->lambda.capture_count;
        if (n->lambda.capture_count > 0)
        {
            new_node->lambda.captured_vars =
                xmalloc(sizeof(char *) * (size_t)(n->lambda.capture_count));
            new_node->lambda.captured_types =
                xmalloc(sizeof(char *) * (size_t)(n->lambda.capture_count));
            new_node->lambda.captured_types_info =
                xmalloc(sizeof(Type *) * (size_t)(n->lambda.capture_count));
            if (n->lambda.capture_modes)
            {
                new_node->lambda.capture_modes =
                    xmalloc(sizeof(int) * (size_t)(n->lambda.capture_count));
            }

            for (int i = 0; i < n->lambda.capture_count; i++)
            {
                new_node->lambda.captured_vars[i] = xstrdup(n->lambda.captured_vars[i]);
                new_node->lambda.captured_types[i] =
                    replace_type_str(n->lambda.captured_types[i], p, c, os, ns);
                new_node->lambda.captured_types_info[i] =
                    replace_type_formal(n->lambda.captured_types_info[i], p, c, os, ns);
                if (n->lambda.capture_modes)
                {
                    new_node->lambda.capture_modes[i] = n->lambda.capture_modes[i];
                }
            }
        }
        new_node->lambda.body = copy_ast_replacing(ctx, n->lambda.body, p, c, os, ns);
        new_node->lambda.is_bare = n->lambda.is_bare;
        register_lambda(ctx, new_node);
        break;
    case NODE_DESTRUCT_VAR:
        if (n->destruct.count > 0)
        {
            new_node->destruct.names = xmalloc(sizeof(char *) * (size_t)(n->destruct.count));
            new_node->destruct.types = xmalloc(sizeof(char *) * (size_t)(n->destruct.count));
            new_node->destruct.type_infos = xmalloc(sizeof(Type *) * (size_t)(n->destruct.count));
            if (n->destruct.field_names)
            {
                new_node->destruct.field_names =
                    xmalloc(sizeof(char *) * (size_t)(n->destruct.count));
            }

            for (int i = 0; i < n->destruct.count; i++)
            {
                new_node->destruct.names[i] = xstrdup(n->destruct.names[i]);
                new_node->destruct.types[i] = replace_type_str(n->destruct.types[i], p, c, os, ns);
                new_node->destruct.type_infos[i] =
                    replace_type_formal(n->destruct.type_infos[i], p, c, os, ns);
                if (n->destruct.field_names && n->destruct.field_names[i])
                {
                    new_node->destruct.field_names[i] = xstrdup(n->destruct.field_names[i]);
                }
                else if (n->destruct.field_names)
                {
                    new_node->destruct.field_names[i] = NULL;
                }
            }
        }
        new_node->destruct.init_expr = copy_ast_replacing(ctx, n->destruct.init_expr, p, c, os, ns);
        new_node->destruct.struct_name = replace_type_str(n->destruct.struct_name, p, c, os, ns);
        new_node->destruct.else_block =
            copy_ast_replacing(ctx, n->destruct.else_block, p, c, os, ns);
        break;
    case NODE_MATCH:
        new_node->match_stmt.expr = copy_ast_replacing(ctx, n->match_stmt.expr, p, c, os, ns);
        new_node->match_stmt.cases = copy_ast_replacing(ctx, n->match_stmt.cases, p, c, os, ns);
        break;
    case NODE_LOOP:
        new_node->loop_stmt.body = copy_ast_replacing(ctx, n->loop_stmt.body, p, c, os, ns);
        break;
    case NODE_REPEAT:
        new_node->repeat_stmt.count = n->repeat_stmt.count ? xstrdup(n->repeat_stmt.count) : NULL;
        new_node->repeat_stmt.body = copy_ast_replacing(ctx, n->repeat_stmt.body, p, c, os, ns);
        break;
    case NODE_UNLESS:
        new_node->unless_stmt.condition =
            copy_ast_replacing(ctx, n->unless_stmt.condition, p, c, os, ns);
        new_node->unless_stmt.body = copy_ast_replacing(ctx, n->unless_stmt.body, p, c, os, ns);
        break;
    case NODE_GUARD:
        new_node->guard_stmt.condition =
            copy_ast_replacing(ctx, n->guard_stmt.condition, p, c, os, ns);
        new_node->guard_stmt.body = copy_ast_replacing(ctx, n->guard_stmt.body, p, c, os, ns);
        break;
    case NODE_BREAK:
    case NODE_CONTINUE:
        // No members to copy besides next (handled at end)
        break;
    case NODE_EXPR_ARRAY_LITERAL:
        new_node->array_literal.elements =
            copy_ast_replacing(ctx, n->array_literal.elements, p, c, os, ns);
        new_node->array_literal.count = n->array_literal.count;
        break;
    case NODE_EXPR_TUPLE_LITERAL:
        new_node->tuple_literal.elements =
            copy_ast_replacing(ctx, n->tuple_literal.elements, p, c, os, ns);
        new_node->tuple_literal.count = n->tuple_literal.count;
        break;
    case NODE_EXPR_SLICE:
        new_node->slice.array = copy_ast_replacing(ctx, n->slice.array, p, c, os, ns);
        new_node->slice.start = copy_ast_replacing(ctx, n->slice.start, p, c, os, ns);
        new_node->slice.end = copy_ast_replacing(ctx, n->slice.end, p, c, os, ns);
        break;
    case NODE_EXPECT:
    case NODE_ASSERT:
        new_node->assert_stmt.condition =
            copy_ast_replacing(ctx, n->assert_stmt.condition, p, c, os, ns);
        new_node->assert_stmt.message =
            n->assert_stmt.message ? xstrdup(n->assert_stmt.message) : NULL;
        break;
    case NODE_DEFER:
        new_node->defer_stmt.stmt = copy_ast_replacing(ctx, n->defer_stmt.stmt, p, c, os, ns);
        break;
    case NODE_TERNARY:
        new_node->ternary.cond = copy_ast_replacing(ctx, n->ternary.cond, p, c, os, ns);
        new_node->ternary.true_expr = copy_ast_replacing(ctx, n->ternary.true_expr, p, c, os, ns);
        new_node->ternary.false_expr = copy_ast_replacing(ctx, n->ternary.false_expr, p, c, os, ns);
        break;
    case NODE_ASM:
        new_node->asm_stmt.code = n->asm_stmt.code ? xstrdup(n->asm_stmt.code) : NULL;
        new_node->asm_stmt.is_volatile = n->asm_stmt.is_volatile;
        new_node->asm_stmt.output_count = n->asm_stmt.output_count;
        new_node->asm_stmt.input_count = n->asm_stmt.input_count;
        new_node->asm_stmt.clobber_count = n->asm_stmt.clobber_count;
        // ASM usually doesn't contain generic parameters in constraints, but we could harden here
        // if needed
        break;
    case NODE_GOTO:
        new_node->goto_stmt.label_name =
            n->goto_stmt.label_name ? xstrdup(n->goto_stmt.label_name) : NULL;
        break;
    case NODE_LABEL:
        new_node->label_stmt.label_name =
            n->label_stmt.label_name ? xstrdup(n->label_stmt.label_name) : NULL;
        break;
    case NODE_DO_WHILE:
        new_node->while_stmt.condition =
            copy_ast_replacing(ctx, n->while_stmt.condition, p, c, os, ns);
        new_node->while_stmt.body = copy_ast_replacing(ctx, n->while_stmt.body, p, c, os, ns);
        break;
    case NODE_TRY:
        new_node->try_stmt.expr = copy_ast_replacing(ctx, n->try_stmt.expr, p, c, os, ns);
        break;
    case NODE_REFLECTION:
        new_node->reflection.kind = n->reflection.kind;
        new_node->reflection.target_type =
            replace_type_formal(n->reflection.target_type, p, c, os, ns);
        break;
    case NODE_REPL_PRINT:
        new_node->repl_print.expr = copy_ast_replacing(ctx, n->repl_print.expr, p, c, os, ns);
        break;
    case NODE_CUDA_LAUNCH:
        new_node->cuda_launch.call = copy_ast_replacing(ctx, n->cuda_launch.call, p, c, os, ns);
        new_node->cuda_launch.grid = copy_ast_replacing(ctx, n->cuda_launch.grid, p, c, os, ns);
        new_node->cuda_launch.block = copy_ast_replacing(ctx, n->cuda_launch.block, p, c, os, ns);
        new_node->cuda_launch.shared_mem =
            copy_ast_replacing(ctx, n->cuda_launch.shared_mem, p, c, os, ns);
        new_node->cuda_launch.stream = copy_ast_replacing(ctx, n->cuda_launch.stream, p, c, os, ns);
        break;
    case NODE_VA_START:
        new_node->va_start_args.ap = copy_ast_replacing(ctx, n->va_start_args.ap, p, c, os, ns);
        new_node->va_start_args.last_arg =
            copy_ast_replacing(ctx, n->va_start_args.last_arg, p, c, os, ns);
        break;
    case NODE_VA_END:
        new_node->va_end_args.ap = copy_ast_replacing(ctx, n->va_end_args.ap, p, c, os, ns);
        break;
    case NODE_VA_COPY:
        new_node->va_copy_args.dest = copy_ast_replacing(ctx, n->va_copy_args.dest, p, c, os, ns);
        new_node->va_copy_args.src = copy_ast_replacing(ctx, n->va_copy_args.src, p, c, os, ns);
        break;
    case NODE_VA_ARG:
        new_node->va_arg_val.ap = copy_ast_replacing(ctx, n->va_arg_val.ap, p, c, os, ns);
        new_node->va_arg_val.type_info = replace_type_formal(n->va_arg_val.type_info, p, c, os, ns);
        break;
    default:
        break;
    }
    return new_node;
}

// Helper to sanitize type names for mangling (e.g. "int*" -> "intPtr")
char *sanitize_mangled_name(const char *s)
{
    char *buf = xmalloc(strlen(s) * 4 + 1);

    // Skip "struct " prefix if present to avoid "struct_" in mangled names
    if (strncmp(s, "struct ", 7) == 0)
    {
        s += 7;
    }

    char *p = buf;
    while (*s)
    {
        if (*s == '*')
        {
            strcpy(p, "Ptr");
            p += 3;
        }
        else if (*s == '<' || *s == ',' || *s == ' ')
        {
            *p++ = '_';
            *p++ = '_';
        }
        else if (*s == '>' || *s == '&')
        {
            // Skip > and & (often used in references) to keep names clean
        }
        else if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
                 *s == '_')
        {
            *p++ = *s;
        }
        else
        {
            *p++ = '_';
        }
        s++;
    }
    *p = 0;
    return buf;
}

char *unmangle_ptr_suffix(const char *s);

// Helper to unmangle Ptr suffix back to pointer type ("intPtr" -> "int*")
char *unmangle_ptr_suffix(const char *s)
{
    if (!s)
    {
        return NULL;
    }

    size_t len = strlen(s);
    if (len <= 3 || strcmp(s + len - 3, "Ptr") != 0)
    {
        return xstrdup(s); // No Ptr suffix, return as-is
    }

    // Extract base type (everything before "Ptr")
    char *base = xmalloc(len - 2);
    strncpy(base, s, len - 3);
    base[len - 3] = '\0';

    char *result = xmalloc(strlen(base) + 16);

    // Check if base is a primitive type
    if (is_primitive_type_name(base))
    {
        sprintf(result, "%s*", base); /* safe */
    }
    else
    {
        // Don't unmangle non-primitives ending in Ptr (like Vec_intPtr)
        strcpy(result, s);
    }

    zfree(base);
    return result;
}

// Helper function to recursively scan AST for sizeof types AND generic calls to trigger
int is_unmangle_primitive(const char *base)
{
    return (strcmp(base, "int") == 0 || strcmp(base, "uint") == 0 || strcmp(base, "char") == 0 ||
            strcmp(base, "bool") == 0 || strcmp(base, "void") == 0 || strcmp(base, "byte") == 0 ||
            strcmp(base, "rune") == 0 || strcmp(base, "float") == 0 ||
            strcmp(base, "double") == 0 || strcmp(base, "f32") == 0 || strcmp(base, "f64") == 0 ||
            strcmp(base, "size_t") == 0 || strcmp(base, "usize") == 0 ||
            strcmp(base, "isize") == 0 || strcmp(base, "ptrdiff_t") == 0 ||
            strncmp(base, "i8", 2) == 0 || strncmp(base, "u8", 2) == 0 ||
            strncmp(base, "int8", 4) == 0 || strncmp(base, "int16", 5) == 0 ||
            strncmp(base, "int32", 5) == 0 || strncmp(base, "int64", 5) == 0 ||
            strncmp(base, "uint8", 5) == 0 || strncmp(base, "uint16", 6) == 0 ||
            strncmp(base, "uint32", 6) == 0 || strncmp(base, "uint64", 6) == 0);
}
