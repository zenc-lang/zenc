// SPDX-License-Identifier: MIT

#include "parser.h"
#include "constants.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ast/ast.h"
#include "utils/format_expr.h"
#include "plugins/plugin_manager.h"
#include "zen/zen_facts.h"
#include "zprep_plugin.h"
#include "analysis/move_check.h"

char *normalize_raw_content(const char *content)
{
    if (!content)
    {
        return NULL;
    }

    size_t len = strlen(content);
    char *normalized = xmalloc((size_t)(len + 1));
    char *d = normalized;
    const char *s = content;

    while (*s)
    {
        if (*s == '\r')
        {
            if (*(s + 1) == '\n')
            {
                *d++ = '\n';
                s += 2;
                continue;
            }
            *d++ = '\n';
            s++;
        }
        else
        {
            *d++ = *s++;
        }
    }
    *d = '\0';
    return normalized;
}

static void append_to_gen(char **gen, size_t *cap, const char *s)
{
    size_t len = strlen(*gen);
    size_t slen = strlen(s);
    if (len + slen + 1 >= *cap)
    {
        *cap = (*cap + slen) * 2;
        *gen = xrealloc(*gen, *cap);
    }
    strcat(*gen, s);
}

static ZEN_FORMAT_PRINTF(3, 4) void append_to_gen_fmt(char **gen, size_t *cap, const char *fmt, ...)
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

    append_to_gen(gen, cap, buf);
    zfree(buf);
}

char *process_printf_sugar(ParserContext *ctx, Token srctoken, const char *content, int newline,
                           const char *target, char ***used_syms, int *count, int check_symbols,
                           int is_raw, int is_expr)
{
    int saved_silent = ctx->silent_warnings;
    ctx->silent_warnings = !check_symbols;

    static int fs_id_gen = 0;
    int fs_id = fs_id_gen++;

    size_t gen_cap = 1024 * 32;
    char *gen = xmalloc((size_t)(gen_cap));
    gen[0] = 0;
    append_to_gen(&gen, &gen_cap, "({ ");

    if (is_expr)
    {
        append_to_gen_fmt(
            &gen, &gen_cap,
            "static char _fs_buf_%d[8192]; _fs_buf_%d[0]=0; char _fs_t_%d[2048]; (void)_fs_t_%d; ",
            fs_id, fs_id, fs_id, fs_id);
    }

    char *s = xstrdup(content);
    char *cur = s;

    while (*cur)
    {
        char *brace = cur;
        while (*brace)
        {
            if (*brace == '{' && !is_raw)

            {
                if (brace[1] == '{')
                {
                    brace += 2;
                    continue;
                }
                break;
            }
            if (*brace == '}' && brace[1] == '}')
            {
                brace += 2;
                continue;
            }
            brace++;
        }

        if (brace > cur)
        {
            if (is_expr)
            {
                append_to_gen_fmt(&gen, &gen_cap, "strcat(_fs_buf_%d, \"", fs_id);
            }
            else
            {
                append_to_gen_fmt(&gen, &gen_cap, "fprintf(%s, \"%%s\", \"", target);
            }

            ptrdiff_t seg_len = brace - cur;
            char *txt = xmalloc((size_t)seg_len + 1);
            int write_idx = 0;
            for (ptrdiff_t i = 0; i < seg_len; i++)
            {
                if (cur[i] == '{' && cur[i + 1] == '{')
                {
                    txt[write_idx++] = '{';
                    i++;
                }
                else if (cur[i] == '}' && cur[i + 1] == '}')
                {
                    txt[write_idx++] = '}';
                    i++;
                }
                else
                {
                    txt[write_idx++] = cur[i];
                }
            }
            txt[write_idx] = 0;

            char *escaped = escape_c_string(txt);
            append_to_gen(&gen, &gen_cap, escaped);
            append_to_gen(&gen, &gen_cap, "\"); ");

            zfree(escaped);
            zfree(txt);
        }

        if (*brace == 0)
        {
            break;
        }

        char *p = brace + 1;
        char *colon = NULL;
        int depth = 1;
        int paren_depth = 0;
        while (*p && depth > 0)
        {
            if (*p == '{')
            {
                depth++;
            }
            if (*p == '}')
            {
                depth--;
            }
            if (*p == '(')
            {
                paren_depth++;
            }
            if (*p == ')')
            {
                if (paren_depth > 0)
                {
                    paren_depth--;
                }
            }
            if (depth == 1 && paren_depth == 0 && *p == ':' && !colon)
            {
                if (*(p + 1) == ':')
                {
                    p++;
                }
                else
                {
                    colon = p;
                }
            }
            if (depth == 0)
            {
                break;
            }
            p++;
        }

        if (*p == 0)
        {
            zpanic_at(srctoken, "Unclosed interpolation brace in printf-sugar");
            return NULL;
        }
        *p = 0;
        char *expr = brace + 1;

        char *read = expr;
        char *write = expr;
        while (*read)
        {
            if (*read == '\\' && *(read + 1) == '"')
            {
                *write = '"';
                read += 2;
                write++;
            }
            else
            {
                *write = *read;
                read++;
                write++;
            }
        }
        *write = 0;
        char *fmt = NULL;
        if (colon)
        {
            *colon = 0;
            fmt = colon + 1;
        }

        char *clean_expr = expr;
        while (*clean_expr == ' ')
        {
            clean_expr++;
        }

        char *final_expr = xstrdup(clean_expr);

        if (check_symbols)
        {
            Lexer lex;
            lexer_init(&lex, clean_expr, ctx->config, ctx->current_filename);
            lex.line = srctoken.line;
            lex.col = srctoken.col;

            Token t;
            Token prev = {0};
            while ((t = lexer_next(&lex)).kind != TOK_EOF)
            {
                if (t.kind == TOK_IDENT)
                {
                    if (prev.kind == TOK_OP && prev.len == 1 && prev.start[0] == '.')
                    {
                        prev = t;
                        continue;
                    }

                    char *name = token_strdup(t);
                    ZenSymbol *sym = find_symbol_entry(ctx, name);
                    if (sym)
                    {
                        sym->is_used = 1;
                    }

                    if (used_syms && count)
                    {
                        *used_syms = xrealloc(*used_syms, sizeof(char *) * (size_t)(*count + 1));
                        (*used_syms)[*count] = name;
                        (*count)++;
                    }
                    else
                    {
                        zfree(name);
                    }
                }
                prev = t;
            }
        }

        expr = final_expr;
        clean_expr = final_expr;

        Lexer lex;
        lexer_init(&lex, clean_expr, ctx->config, ctx->current_filename);
        lex.line = srctoken.line;
        lex.col = srctoken.col;

        ASTNode *expr_node = parse_expression(ctx, &lex);
        if (expr_node)
        {
            infer_type(ctx, expr_node);
        }
        else
        {
            zpanic_at(srctoken, "Could not parse expression in interpolation");
            return NULL;
        }

        char *rw_expr = NULL;
        char *mangled_to_string = NULL;
        int to_string_is_ptr = 0;
        char *to_string_struct_name = NULL;

        int is_temporary = 1;
        int is_simple_type = 0;
        if (expr_node)
        {
            if (expr_node->kind == NODE_EXPR_VAR || expr_node->kind == NODE_EXPR_MEMBER ||
                expr_node->kind == NODE_EXPR_INDEX)
            {
                is_temporary = 0;
            }

            Type *t = expr_node->type_info;
            if (t && t->kind != TYPE_STRUCT && t->kind != TYPE_ENUM && t->kind != TYPE_UNKNOWN &&
                t->kind != TYPE_ALIAS)
            {
                is_simple_type = 1;
            }
        }
        else if (clean_expr[0] == '"' || clean_expr[0] == '\'' || isdigit(clean_expr[0]))
        {
            is_simple_type = 1;
        }

        int force_simple = is_simple_type || ctx->is_comptime || !is_temporary;

        if (expr_node)
        {
            if (expr_node->type_info)
            {
                Type *t = expr_node->type_info;
                char *struct_name = NULL;
                int is_ptr = 0;

                if (t->kind == TYPE_STRUCT)
                {
                    struct_name = t->name;
                }
                else if (t->kind == TYPE_POINTER && t->inner && t->inner->kind == TYPE_STRUCT)
                {
                    struct_name = t->inner->name;
                    is_ptr = 1;
                }

                if (struct_name)
                {
                    size_t mangled_sz = strlen(struct_name) + sizeof("__to_string");
                    char *mangled = xmalloc((size_t)(mangled_sz));
                    snprintf(mangled, mangled_sz, "%s__to_string", struct_name);
                    if (find_func(ctx, mangled))
                    {
                        mangled_to_string = xstrdup(mangled);
                        to_string_is_ptr = is_ptr;
                        to_string_struct_name = xstrdup(struct_name);
                    }
                    zfree(mangled);
                }
            }
        }

        if (expr_node)
        {
            char *expr_buf = format_expression_as_c(ctx, expr_node);
            if (expr_buf)
            {
                rw_expr = expr_buf;
            }
        }

        if (!rw_expr)
        {
            rw_expr = xstrdup(expr);
        }

        if (fmt)
        {
            if (force_simple)
            {
                if (is_expr)
                {
                    append_to_gen_fmt(&gen, &gen_cap,
                                      "snprintf(_fs_t_%d, 2048, \"%%%s\", %s); strncat(_fs_buf_%d, "
                                      "_fs_t_%d, 8192 - strlen(_fs_buf_%d) - 1); ",
                                      fs_id, fmt, rw_expr, fs_id, fs_id, fs_id);
                }
                else
                {
                    append_to_gen_fmt(&gen, &gen_cap, "fprintf(%s, \"%%%s\", %s); ", target, fmt,
                                      rw_expr);
                }
            }
            else
            {
                if (is_expr)
                {
                    append_to_gen_fmt(
                        &gen, &gen_cap,
                        "({ ZC_AUTO_INIT(_z_interp_val, %s); snprintf(_fs_t_%d, 2048, \"%%%s\", "
                        "_z_interp_val); strncat(_fs_buf_%d, _fs_t_%d, 8192 - strlen(_fs_buf_%d) - "
                        "1); _z_drop(_z_interp_val); }); ",
                        rw_expr, fs_id, fmt, fs_id, fs_id, fs_id);
                }
                else
                {
                    append_to_gen_fmt(&gen, &gen_cap,
                                      "({ ZC_AUTO_INIT(_z_interp_val, %s); fprintf(%s, \"%%%s\", "
                                      "_z_interp_val); _z_drop(_z_interp_val); }); ",
                                      rw_expr, target, fmt);
                }
            }
        }
        else
        {
            const char *format_spec = NULL;
            Type *t = expr_node ? expr_node->type_info : NULL;
            char *inferred_type = t ? type_to_string(t) : find_symbol_type(ctx, clean_expr);

            int is_bool = 0;

            if (t && !mangled_to_string)
            {
                Type *base = t;
                int is_p = 0;
                if (base->kind == TYPE_POINTER)
                {
                    base = base->inner;
                    is_p = 1;
                }

                while (base)
                {
                    if (base->kind == TYPE_ALIAS)
                    {
                        base = base->inner;
                    }
                    else if (base->kind == TYPE_STRUCT && base->name)
                    {
                        TypeAlias *ta = find_type_alias_node(ctx, base->name);
                        if (ta && ta->type_info)
                        {
                            base = ta->type_info;
                        }
                        else
                        {
                            break;
                        }
                    }
                    else
                    {
                        break;
                    }
                }

                if (base && base->kind == TYPE_ARRAY && base->array_size == 0)
                {
                    char *inner_name = type_to_string(base->inner);
                    char slice_name[MAX_TYPE_NAME_LEN];
                    snprintf(slice_name, sizeof(slice_name), "Slice__%s", inner_name);
                    zfree(inner_name);

                    ASTNode *def = find_struct_def(ctx, slice_name);
                    if (def && def->kind == NODE_STRUCT)
                    {
                        int has_data = 0;
                        int has_len = 0;
                        char *data_type = NULL;

                        ASTNode *curr = def->strct.fields;
                        while (curr)
                        {
                            if (strcmp(curr->field.name, "data") == 0)
                            {
                                has_data = 1;
                                data_type = curr->field.type;
                            }
                            else if (strcmp(curr->field.name, "len") == 0)
                            {
                                has_len = 1;
                            }
                            curr = curr->next;
                        }

                        if (has_data && has_len && data_type &&
                            (strstr(data_type, "char") || strstr(data_type, "u8") ||
                             strstr(data_type, "byte")))
                        {
                            const char *acc = is_p ? "->" : ".";
                            if (force_simple)
                            {
                                if (is_expr)
                                {
                                    append_to_gen_fmt(
                                        &gen, &gen_cap,
                                        "snprintf(_fs_t_%d, 2048, \"%%.*s\", (int)(%s)%slen, "
                                        "(%s)%sdata); strncat(_fs_buf_%d, _fs_t_%d, 8192 - "
                                        "strlen(_fs_buf_%d) - 1); ",
                                        fs_id, rw_expr, acc, rw_expr, acc, fs_id, fs_id, fs_id);
                                }
                                else
                                {
                                    append_to_gen_fmt(
                                        &gen, &gen_cap,
                                        "fprintf(%s, \"%%.*s\", (int)(%s)%slen, (%s)%sdata); ",
                                        target, rw_expr, acc, rw_expr, acc);
                                }
                            }
                            else
                            {
                                if (is_expr)
                                {
                                    append_to_gen_fmt(
                                        &gen, &gen_cap,
                                        "({ ZC_AUTO_INIT(_z_interp_val, %s); snprintf(_fs_t_%d, "
                                        "2048, \"%%.*s\", (int)(_z_interp_val)%slen, "
                                        "(_z_interp_val)%sdata); strncat(_fs_buf_%d, _fs_t_%d, "
                                        "8192 - strlen(_fs_buf_%d) - 1); _z_drop(_z_interp_val); "
                                        "}); ",
                                        rw_expr, fs_id, acc, acc, fs_id, fs_id, fs_id);
                                }
                                else
                                {
                                    append_to_gen_fmt(
                                        &gen, &gen_cap,
                                        "({ ZC_AUTO_INIT(_z_interp_val, %s); fprintf(%s, "
                                        "\"%%.*s\", (int)(_z_interp_val)%slen, "
                                        "(_z_interp_val)%sdata); _z_drop(_z_interp_val); }); ",
                                        rw_expr, target, acc, acc);
                                }
                            }
                            goto next_segment;
                        }
                    }
                }
                else if (base && base->kind == TYPE_STRUCT && base->name)
                {
                    ASTNode *def = find_struct_def(ctx, base->name);
                    if (def && def->kind == NODE_STRUCT)
                    {
                        int has_data = 0;
                        int has_len = 0;
                        char *data_type = NULL;

                        ASTNode *curr = def->strct.fields;
                        while (curr)
                        {
                            if (strcmp(curr->field.name, "data") == 0)
                            {
                                has_data = 1;
                                data_type = curr->field.type;
                            }
                            else if (strcmp(curr->field.name, "len") == 0)
                            {
                                has_len = 1;
                            }
                            curr = curr->next;
                        }

                        if (has_data && has_len && data_type &&
                            (strstr(data_type, "char") || strstr(data_type, "u8") ||
                             strstr(data_type, "byte")))
                        {
                            const char *acc = is_p ? "->" : ".";
                            if (force_simple)
                            {
                                if (is_expr)
                                {
                                    append_to_gen_fmt(&gen, &gen_cap,
                                                      "sprintf(_fs_t_%d, \"%%.*s\", "
                                                      "(int)(%s)%slen, (%s)%sdata); " /* TODO: check
                                                                                         buffer size
                                                                                       */
                                                      "strcat(_fs_buf_%d, _fs_t_%d); ",
                                                      fs_id, rw_expr, acc, rw_expr, acc, fs_id,
                                                      fs_id);
                                }
                                else
                                {
                                    append_to_gen_fmt(
                                        &gen, &gen_cap,
                                        "fprintf(%s, \"%%.*s\", (int)(%s)%slen, (%s)%sdata); ",
                                        target, rw_expr, acc, rw_expr, acc);
                                }
                            }
                            else
                            {
                                if (is_expr)
                                {
                                    append_to_gen_fmt(&gen, &gen_cap,
                                                      "({ ZC_AUTO_INIT(_z_interp_val, %s); "
                                                      "sprintf(_fs_t_%d, " /* TODO: check buffer
                                                                              size */
                                                      "\"%%.*s\", (int)(_z_interp_val)%slen, "
                                                      "(_z_interp_val)%sdata); strcat(_fs_buf_%d, "
                                                      "_fs_t_%d); "
                                                      "_z_drop(_z_interp_val); }); ",
                                                      rw_expr, fs_id, acc, acc, fs_id, fs_id);
                                }
                                else
                                {
                                    append_to_gen_fmt(
                                        &gen, &gen_cap,
                                        "({ ZC_AUTO_INIT(_z_interp_val, %s); fprintf(%s, "
                                        "\"%%.*s\", (int)(_z_interp_val)%slen, "
                                        "(_z_interp_val)%sdata); _z_drop(_z_interp_val); }); ",
                                        rw_expr, target, acc, acc);
                                }
                            }
                            goto next_segment;
                        }
                    }
                }
            }

            if (t && t->kind == TYPE_RUNE)
            {
                format_spec = "%s";
                char *orig = rw_expr;
                rw_expr = xmalloc(strlen(orig) + 32);
                sprintf(rw_expr, "_z_str_rune(%s)", orig); /* safe */
            }
            else if (inferred_type)
            {
                if (strstr(inferred_type, "*") && !strstr(inferred_type, "char") &&
                    strcmp(inferred_type, "string") != 0 && strcmp(inferred_type, "str") != 0)
                {
                    format_spec = "%p";
                }

                if (t)
                {
                    zfree(inferred_type);
                }
            }

            if (!format_spec)
            {
            }

            if (format_spec)
            {
                if (is_bool)
                {
                    if (force_simple)
                    {
                        if (is_expr)
                        {
                            append_to_gen_fmt(
                                &gen, &gen_cap,
                                "sprintf(_fs_t_%d, \"%%%s\", _z_bool_str(%s)); " /* TODO: check
                                                                                    buffer size */
                                "strcat(_fs_buf_%d, _fs_t_%d); ",
                                fs_id, format_spec + 1, rw_expr, fs_id, fs_id);
                        }
                        else
                        {
                            append_to_gen_fmt(&gen, &gen_cap,
                                              "fprintf(%s, \"%%%s\", _z_bool_str(%s)); ", target,
                                              format_spec + 1, rw_expr);
                        }
                    }
                    else
                    {
                        if (is_expr)
                        {
                            append_to_gen_fmt(&gen, &gen_cap,
                                              "({ ZC_AUTO_INIT(_z_interp_val, %s); "
                                              "sprintf(_fs_t_%d, \"%%%s\", " /* TODO: check buffer
                                                                                size */
                                              "_z_bool_str(_z_interp_val)); strcat(_fs_buf_%d, "
                                              "_fs_t_%d); "
                                              "_z_drop(_z_interp_val); }); ",
                                              rw_expr, fs_id, format_spec + 1, fs_id, fs_id);
                        }
                        else
                        {
                            append_to_gen_fmt(
                                &gen, &gen_cap,
                                "({ ZC_AUTO_INIT(_z_interp_val, %s); fprintf(%s, \"%%%s\", "
                                "_z_bool_str(_z_interp_val)); _z_drop(_z_interp_val); }); ",
                                rw_expr, target, format_spec + 1);
                        }
                    }
                }
                else
                {
                    if (force_simple)
                    {
                        if (is_expr)
                        {
                            append_to_gen_fmt(&gen, &gen_cap,
                                              "sprintf(_fs_t_%d, \"%%%s\", %s); strcat(_fs_buf_%d, "
                                              "_fs_t_%d); ", /* TODO: check buffer size */
                                              fs_id, format_spec + 1, rw_expr, fs_id, fs_id);
                        }
                        else
                        {
                            append_to_gen_fmt(&gen, &gen_cap, "fprintf(%s, \"%%%s\", %s); ", target,
                                              format_spec + 1, rw_expr);
                        }
                    }
                    else
                    {
                        if (is_expr)
                        {
                            append_to_gen_fmt(&gen, &gen_cap,
                                              "({ ZC_AUTO_INIT(_z_interp_val, %s); "
                                              "sprintf(_fs_t_%d, \"%%%s\", " /* TODO: check buffer
                                                                                size */
                                              "_z_interp_val); strcat(_fs_buf_%d, _fs_t_%d); "
                                              "_z_drop(_z_interp_val); }); ",
                                              rw_expr, fs_id, format_spec + 1, fs_id, fs_id);
                        }
                        else
                        {
                            append_to_gen_fmt(
                                &gen, &gen_cap,
                                "({ ZC_AUTO_INIT(_z_interp_val, %s); fprintf(%s, \"%%%s\", "
                                "_z_interp_val); _z_drop(_z_interp_val); }); ",
                                rw_expr, target, format_spec + 1);
                        }
                    }
                }
            }
            else
            {
                if (mangled_to_string)
                {
                    if (force_simple && !is_temporary)
                    {
                        if (is_expr)
                        {
                            append_to_gen_fmt(
                                &gen, &gen_cap,
                                "sprintf(_fs_t_%d, \"%%s\", (char*)%s(%s%s)); " /* TODO: check
                                                                                   buffer size */
                                "strcat(_fs_buf_%d, _fs_t_%d); ",
                                fs_id, mangled_to_string, to_string_is_ptr ? "" : "&", rw_expr,
                                fs_id, fs_id);
                        }
                        else
                        {
                            append_to_gen_fmt(
                                &gen, &gen_cap, "fprintf(%s, \"%%s\", (char*)%s(%s%s)); ", target,
                                mangled_to_string, to_string_is_ptr ? "" : "&", rw_expr);
                        }
                    }
                    else
                    {
                        if (is_expr)
                        {
                            append_to_gen_fmt(&gen, &gen_cap,
                                              "({ ZC_AUTO_INIT(_z_interp_val, %s); "
                                              "sprintf(_fs_t_%d, \"%%s\", " /* TODO: check buffer
                                                                               size */
                                              "(char*)%s(%s_z_interp_val)); strcat(_fs_buf_%d, "
                                              "_fs_t_%d); "
                                              "_z_drop(_z_interp_val); }); ",
                                              rw_expr, fs_id, mangled_to_string,
                                              to_string_is_ptr ? "" : "&", fs_id, fs_id);
                        }
                        else
                        {
                            append_to_gen_fmt(
                                &gen, &gen_cap,
                                "({ ZC_AUTO_INIT(_z_interp_val, %s); fprintf(%s, \"%%s\", "
                                "(char*)%s(%s_z_interp_val)); _z_drop(_z_interp_val); }); ",
                                rw_expr, target, mangled_to_string, to_string_is_ptr ? "" : "&");
                        }
                    }
                }
                else
                {
                    if (force_simple)
                    {
                        if (is_expr)
                        {
                            append_to_gen_fmt(
                                &gen, &gen_cap,
                                "sprintf(_fs_t_%d, _z_str(%s), _z_arg(%s)); " /* TODO: check buffer
                                                                                 size */
                                "strcat(_fs_buf_%d, _fs_t_%d); ",
                                fs_id, rw_expr, rw_expr, fs_id, fs_id);
                        }
                        else
                        {
                            append_to_gen_fmt(&gen, &gen_cap,
                                              "fprintf(%s, _z_str(%s), _z_arg(%s)); ", target,
                                              rw_expr, rw_expr);
                        }
                    }
                    else
                    {
                        if (is_expr)
                        {
                            append_to_gen_fmt(
                                &gen, &gen_cap,
                                "({ ZC_AUTO_INIT(_z_interp_val, %s); sprintf(_fs_t_%d, " /* TODO:
                                                                                            check
                                                                                            buffer
                                                                                            size */
                                "_z_str(_z_interp_val), _z_arg(_z_interp_val)); strcat(_fs_buf_%d, "
                                "_fs_t_%d); _z_drop(_z_interp_val); }); ",
                                rw_expr, fs_id, fs_id, fs_id);
                        }
                        else
                        {
                            append_to_gen_fmt(&gen, &gen_cap,
                                              "({ ZC_AUTO_INIT(_z_interp_val, %s); fprintf(%s, "
                                              "_z_str(_z_interp_val), _z_arg(_z_interp_val)); "
                                              "_z_drop(_z_interp_val); }); ",
                                              rw_expr, target);
                        }
                    }
                }
            }
        }

    next_segment:
        if (mangled_to_string)
        {
            zfree(mangled_to_string);
        }
        if (to_string_struct_name)
        {
            zfree(to_string_struct_name);
        }

        if (rw_expr)
        {
            zfree(rw_expr);
        }

        cur = p + 1;
    }

    if (newline)
    {
        if (is_expr)
        {
            append_to_gen_fmt(&gen, &gen_cap, "strcat(_fs_buf_%d, \"\\n\"); ", fs_id);
        }
        else
        {
            append_to_gen_fmt(&gen, &gen_cap, "fprintf(%s, \"\\n\"); ", target);
        }
    }
    else if (!is_expr)
    {
        append_to_gen(&gen, &gen_cap, "fflush(stdout); ");
    }

    if (is_expr)
    {
        append_to_gen_fmt(&gen, &gen_cap, "_fs_buf_%d; })", fs_id);
    }
    else
    {
        append_to_gen(&gen, &gen_cap, "0; })");
    }

    zfree(s);
    ctx->silent_warnings = saved_silent;
    return gen;
}

ASTNode *parse_macro_call(ParserContext *ctx, Lexer *l, char *macro_name)
{
    Token start_tok = lexer_peek(l);
    if (lexer_peek(l).kind != TOK_OP || lexer_peek(l).start[0] != '!')
    {
        return NULL;
    }
    lexer_next(l);

    if (lexer_peek(l).kind != TOK_LBRACE)
    {
        zpanic_at(lexer_peek(l), "Expected { after macro invocation");
        return NULL;
    }
    lexer_next(l);
    int start_line = l->line;
    int start_col = l->col;
    const char *body_start = l->src + l->pos;

    int depth = 1;
    while (depth > 0)
    {
        Token t = lexer_peek(l);
        if (t.kind == TOK_EOF)
        {
            zpanic_at(t, "Unexpected EOF in macro block");
            return NULL;
        }

        if (t.kind == TOK_LBRACE)
        {
            depth++;
        }
        if (t.kind == TOK_RBRACE)
        {
            depth--;
        }

        lexer_next(l);
    }
    int end_line = l->line;
    int end_col = l->col;

    const char *body_end = l->src + l->pos - 1;
    size_t body_len = (size_t)(body_end - body_start);
    char *body = xmalloc((size_t)(body_len + 1));
    memcpy(body, body_start, (size_t)(body_len));
    body[body_len] = '\0';

    const char *plugin_name = NULL;
#if ZC_HAS_PLUGINS
    plugin_name = resolve_plugin(ctx, macro_name);
#endif
    if (!plugin_name)
    {
        char err[MAX_SHORT_MSG_LEN];
        snprintf(err, sizeof(err), "Unknown plugin: %s (did you forget 'import plugin \"%s\"'?)",
                 macro_name, macro_name);
        zpanic_at(start_tok, "%s", err);
        return NULL;

        if (ctx->config->mode_lsp)
        {
            zfree(body);
            ASTNode *n = ast_create(NODE_PLUGIN);
            n->plugin_stmt.plugin_name = xstrdup(macro_name);
            n->plugin_stmt.body = xstrdup("");
            n->plugin_stmt.start_line = start_line;
            n->plugin_stmt.start_col = start_col;
            n->plugin_stmt.end_line = end_line;
            n->plugin_stmt.end_col = end_col;
            return n;
        }
    }

    ZPlugin *found = ctx->hook_find_plugin ? (ZPlugin *)ctx->hook_find_plugin(plugin_name) : NULL;

    if (!found)
    {
        char err[MAX_SHORT_MSG_LEN];
        snprintf(err, sizeof(err), "Plugin implementation not found: %s", plugin_name);
        zpanic_at(start_tok, "%s", err);
        return NULL;

        return NULL;
    }

    FILE *capture = tmpfile();
    if (!capture)
    {
        zpanic_at(start_tok, "Failed to create capture buffer for plugin expansion");
        return NULL;
    }

    ZApi api;
    if (ctx->hook_plugin_init_api)
    {
        ctx->hook_plugin_init_api(&api, ctx->current_filename, start_tok.line, ctx->config);
    }
    api.out = capture;
    api.hoist_out = ctx->cg.hoist_out;

    found->fn(body, &api);

    long len = ftell(capture);
    if (len < 0)
    {
        fclose(capture);
        return NULL;
    }
    if (len < 0)
    {
        fclose(capture);
        return NULL;
    }
    rewind(capture);
    char *expanded_code = xmalloc((size_t)(len + 1));
    size_t nread = fread(expanded_code, 1, (size_t)(len), capture);
    if (nread != (size_t)(len))
    {
        zfree(expanded_code);
        fclose(capture);
        return NULL;
    }
    expanded_code[len] = 0;
    fclose(capture);
    if (ctx->config->mode_lsp)
    {
        ASTNode *n = ast_create(NODE_PLUGIN);
        n->plugin_stmt.plugin_name = xstrdup(plugin_name);
        n->plugin_stmt.body = body;
        n->plugin_stmt.start_line = start_line;
        n->plugin_stmt.start_col = start_col;
        n->plugin_stmt.end_line = end_line;
        n->plugin_stmt.end_col = end_col;
        n->line = start_tok.line;
        n->token = start_tok;
        return n;
    }

    zfree(body);

    ASTNode *n = ast_create(NODE_RAW_STMT);
    n->token = start_tok;
    n->line = start_tok.line;
    n->raw_stmt.content = expanded_code;

    return n;
}

ASTNode *parse_comptime_body(ParserContext *ctx, Lexer *l)
{
    z_parse_expect(l, TOK_COMPTIME, "comptime");
    z_parse_expect(l, TOK_LBRACE, "expected { after comptime");

    const char *start = l->src + l->pos;
    int depth = 1;
    while (depth > 0)
    {
        Token t = lexer_next(l);
        if (t.kind == TOK_EOF)
        {
            zpanic_at(t, "Unexpected EOF in comptime block");
            return NULL;
        }
        if (t.kind == TOK_STRING || t.kind == TOK_FSTRING || t.kind == TOK_RAW_STRING)
        {
            continue;
        }
        if (t.kind == TOK_LBRACE)
        {
            depth++;
        }
        if (t.kind == TOK_RBRACE)
        {
            depth--;
        }
    }
    ptrdiff_t len = (l->src + l->pos - 1) - start;
    char *code = xmalloc((size_t)(len + 1));
    strncpy(code, start, (size_t)(len));
    code[len] = 0;

    size_t wrapped_len = (size_t)len + 4;
    char *wrapped = xmalloc((size_t)(wrapped_len + 1));
    sprintf(wrapped, "{ %s }", code); /* safe */
    zfree(code);

    Lexer cl;
    lexer_init(&cl, wrapped, ctx->config, ctx->current_filename);
    ASTNode *block = parse_block(ctx, &cl);
    zfree(wrapped);
    if (!block)
    {
        return NULL;
    }

    ASTNode *stmts = block->block.statements;
    block->block.statements = NULL;
    ast_free(block);
    return stmts;
}

ASTNode *parse_plugin(ParserContext *ctx, Lexer *l, Token tok)
{
    (void)ctx;

    Token tk = lexer_next(l);
    if (tk.kind != TOK_IDENT)
    {
        zpanic_at(tk, "Expected plugin name after 'plugin' keyword");
        return NULL;
    }

    char *plugin_name = xmalloc(tk.len + 1);
    strncpy(plugin_name, tk.start, tk.len);
    plugin_name[tk.len] = '\0';

    char *body = xmalloc(8192);
    body[0] = '\0';
    int body_len = 0;

    while (1)
    {
        Token t = lexer_peek(l);
        if (t.kind == TOK_EOF)
        {
            zpanic_at(t, "Unexpected EOF in plugin block, expected 'end'");
            break;
        }

        if (t.kind == TOK_IDENT && t.len == 3 && strncmp(t.start, "end", 3) == 0)
        {
            lexer_next(l);
            break;
        }

        if ((size_t)((size_t)(body_len) + t.len) + 2 < 8192)
        {
            strncat(body, t.start, t.len);
            body[(size_t)((size_t)(body_len) + t.len)] = ' ';
            body[(size_t)((size_t)(body_len) + t.len) + 1] = '\0';
            body_len += (int)(t.len) + 1;
        }

        lexer_next(l);
    }

    ASTNode *n = ast_create(NODE_PLUGIN);
    n->token = tok;
    n->plugin_stmt.plugin_name = plugin_name;
    n->plugin_stmt.body = body;

    if (lexer_peek(l).kind == TOK_SEMICOLON)
    {
        lexer_next(l);
    }
    return n;
}
