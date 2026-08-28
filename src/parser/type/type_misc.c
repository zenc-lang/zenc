// SPDX-License-Identifier: MIT
#include "parser.h"
#include "constants.h"
#include "ast/ast.h"
#include "analysis/move_check.h"
#include "plugins/plugin_manager.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "analysis/const_fold.h"
#include "utils/utils.h"
#include "ast/primitives.h"

char *parse_type(ParserContext *ctx, Lexer *l)
{
    Type *t = parse_type_formal(ctx, l);
    if (!t)
    {
        return NULL;
    }

    return type_to_string(t);
}

char *parse_array_literal(ParserContext *ctx, Lexer *l, const char *st)
{
    (void)ctx;
    lexer_next(l);
    size_t cap = 128;
    char *c = xmalloc((size_t)(cap));
    c[0] = 0;
    int n = 0;

    while (1)
    {
        Token t = lexer_peek(l);
        if (t.kind == TOK_RBRACKET)
        {
            lexer_next(l);
            break;
        }
        if (t.kind == TOK_COMMA)
        {
            lexer_next(l);
            continue;
        }
        if (t.kind == TOK_EOF)
        {
            break;
        }

        const char *s = l->src + l->pos;
        int d = 0;
        while (1)
        {
            Token it = lexer_peek(l);
            if (it.kind == TOK_EOF)
            {
                break;
            }
            if (d == 0 && (it.kind == TOK_COMMA || it.kind == TOK_RBRACKET))
            {
                break;
            }
            if (it.kind == TOK_LBRACKET || it.kind == TOK_LPAREN)
            {
                d++;
            }
            if (it.kind == TOK_RBRACKET || it.kind == TOK_RPAREN)
            {
                d--;
            }
            lexer_next(l);
        }

        ptrdiff_t len = (l->src + l->pos) - s;
        if (strlen(c) + (size_t)(len) + 5 > cap)
        {
            cap *= 2;
            c = xrealloc(c, cap);
        }
        if (n > 0)
        {
            strcat(c, ", ");
        }
        strncat(c, s, (size_t)(len));
        n++;
    }

    char rt[64];
    if (st && strncmp(st, "Slice__", 7) == 0)
    {
        strcpy(rt, st + 7);
    }
    else
    {
        strcpy(rt, "int");
    }

    int in_func = (ctx->current_scope != ctx->global_scope);
    size_t st_len = st ? strlen(st) : 0;
    size_t o_sz = strlen(c) + st_len + strlen(rt) + 256;
    char *o = xmalloc((size_t)(o_sz));
    if (st)
    {
        if (ctx->config->use_cpp && in_func)
        {
            snprintf(o, o_sz, "({ static const %s __tmp[] = {%s}; (%s){( %s *)__tmp, %d, %d}; })",
                     rt, c, st, rt, n, n);
        }
        else if (ctx->config->use_cpp)
        {
            snprintf(o, o_sz, "(%s){(%s[]){%s}, %d, %d}", st, rt, c, n, n);
        }
        else
        {
            snprintf(o, o_sz, "(%s){.data=(%s[]){%s}, .len=%d, .cap=%d}", st, rt, c, n, n);
        }
    }
    else
    {
        if (ctx->config->use_cpp && in_func)
        {
            snprintf(o, o_sz,
                     "({ static const int __tmp[] = {%s}; (Slice__int){(int*)__tmp, %d, %d}; })", c,
                     n, n);
        }
        else if (ctx->config->use_cpp)
        {
            snprintf(o, o_sz, "(Slice__int){(int[]){%s}, %d, %d}", c, n, n);
        }
        else
        {
            snprintf(o, o_sz, "(Slice__int){.data=(int[]){%s}, .len=%d, .cap=%d}", c, n, n);
        }
    }
    zfree(c);
    return o;
}
char *parse_tuple_literal(ParserContext *ctx, Lexer *l, const char *tn)
{
    (void)ctx; // suppress unused parameter warning
    lexer_next(l);
    size_t cap = 128;
    char *c = xmalloc((size_t)(cap));
    c[0] = 0;

    while (1)
    {
        Token t = lexer_peek(l);
        if (t.kind == TOK_RPAREN)
        {
            lexer_next(l);
            break;
        }
        if (t.kind == TOK_COMMA)
        {
            lexer_next(l);
            continue;
        }
        if (t.kind == TOK_EOF)
        {
            break;
        }

        const char *s = l->src + l->pos;
        int d = 0;
        while (1)
        {
            Token it = lexer_peek(l);
            if (it.kind == TOK_EOF)
            {
                break;
            }
            if (d == 0 && (it.kind == TOK_COMMA || it.kind == TOK_RPAREN))
            {
                break;
            }
            if (it.kind == TOK_LPAREN)
            {
                d++;
            }
            if (it.kind == TOK_RPAREN)
            {
                d--;
            }
            lexer_next(l);
        }

        ptrdiff_t len = (l->src + l->pos) - s;
        if (strlen(c) + (size_t)(len) + 5 > cap)
        {
            cap *= 2;
            c = xrealloc(c, cap);
        }
        if (strlen(c) > 0)
        {
            strcat(c, ", ");
        }
        strncat(c, s, (size_t)(len));
    }

    size_t o_sz = strlen(c) + strlen(tn) + 128;
    char *o = xmalloc((size_t)(o_sz));
    snprintf(o, o_sz, "(%s){%s}", tn, c);
    zfree(c);
    return o;
}
ASTNode *parse_embed(ParserContext *ctx, Lexer *l)
{
    lexer_next(l);
    Token t = lexer_next(l);
    if (t.kind != TOK_STRING && t.kind != TOK_RAW_STRING)
    {
        zpanic_at(t, "String required");
        return NULL;
    }
    char *content = token_get_string_content(t);
    char fn[MAX_PATH_LEN];
    strncpy(fn, content, MAX_PATH_LEN - 1);
    fn[MAX_PATH_LEN - 1] = 0;
    zfree(content);

    // Check for optional "as Type"
    Type *target_type = NULL;
    if (lexer_peek(l).kind == TOK_IDENT && lexer_peek(l).len == 2 &&
        strncmp(lexer_peek(l).start, "as", 2) == 0)
    {
        lexer_next(l); // consume 'as'
        target_type = parse_type_formal(ctx, l);
    }

    FILE *f = fopen(fn, "rb");
    if (!f)
    {
        zpanic_at(t, "404: %s", fn);
        // In fault-tolerant mode (LSP), zpanic_at returns instead of exiting.
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0)
    {
        fclose(f);
        return NULL;
    }
    if (len < 0)
    {
        fclose(f);
        return NULL;
    }
    rewind(f);
    unsigned char *b = xmalloc((size_t)(len));
    if (fread(b, 1, (size_t)(len), f) != (size_t)(len))
    {
        zfree(b);
        fclose(f);
        return NULL;
    }
    fclose(f);

    size_t oc = (size_t)(len) * 6 + 256;
    char *o = xmalloc((size_t)(oc));

    int in_func = (ctx->current_scope != ctx->global_scope);
    int use_cpp_stmt = (ctx->config->use_cpp && in_func);

    // Default Type if none
    if (!target_type)
    {
        // Default: Slice__char
        register_slice(ctx, "char");

        Type *slice_type = type_new(TYPE_STRUCT);
        slice_type->name = xstrdup("Slice__char");
        target_type = slice_type;

        if (use_cpp_stmt)
        {
            snprintf(o, oc, "({ static const char __tmp[] = {");
        }
        else if (ctx->config->use_cpp)
        {
            snprintf(o, oc, "(Slice__char){(char[]){");
        }
        else
        {
            snprintf(o, oc, "(Slice__char){.data=(char[]){");
        }
    }
    else
    {
        // Handle specific type
        char *ts = type_to_string(target_type);

        if (target_type->kind == TYPE_ARRAY)
        {
            char *inner_ts = type_to_string(target_type->inner);
            if (target_type->array_size > 0)
            {
                Type *ptr_type = type_new_ptr(target_type->inner); // Reuse inner
                target_type = ptr_type;
                if (use_cpp_stmt)
                {
                    snprintf(o, oc, "({ static const %s __tmp[] = {", inner_ts);
                }
                else
                {
                    snprintf(o, oc, "(%s[]){", inner_ts);
                }
            }
            else
            {
                // Slice -> Slice__T struct
                register_slice(ctx, inner_ts);
                char slice_name[MAX_TYPE_NAME_LEN];
                snprintf(slice_name, sizeof(slice_name), "Slice__%s", inner_ts);
                Type *slice_t = type_new(TYPE_STRUCT);
                slice_t->name = xstrdup(slice_name);
                target_type = slice_t;

                if (use_cpp_stmt)
                {
                    snprintf(o, oc, "({ static const %s __tmp[] = {", inner_ts);
                }
                else if (ctx->config->use_cpp)
                {
                    snprintf(o, oc, "(%s){(%s[]){", slice_name, inner_ts);
                }
                else
                {
                    snprintf(o, oc, "(%s){.data=(%s[]){", slice_name, inner_ts);
                }
            }
            zfree(inner_ts);
        }
        else
        {
            if (strcmp(ts, "string") == 0 || strcmp(ts, "char*") == 0)
            {
                snprintf(o, oc, "(char*)\"");
            }
            else
            {
                snprintf(o, oc, "(%s){", ts);
            }
        }
        zfree(ts);
    }

    size_t cur_len = strlen(o);
    char *p = o + cur_len;

    // Check if string mode
    int is_string = (target_type && (strcmp(type_to_string(target_type), "string") == 0 ||
                                     strcmp(type_to_string(target_type), "char*") == 0));

    for (int i = 0; i < len; i++)
    {
        if (cur_len + 16 >= oc)
        {
            break;
        }
        if (is_string)
        {
            // Hex escape for string
            int w = snprintf(p, oc - cur_len, "\\x%02X", b[i]);
            p += w;
            cur_len += (size_t)(w);
        }
        else
        {
            int w = snprintf(p, oc - cur_len, "0x%02X,", b[i]);
            p += w;
            cur_len += (size_t)(w);
        }
    }

    if (cur_len + 16 < oc)
    {
        if (is_string)
        {
            snprintf(p, oc - cur_len, "\"");
        }
        else
        {
            char *actual_ts = type_to_string(target_type);
            int is_slice = (actual_ts && strncmp(actual_ts, "Slice__", 7) == 0);
            zfree(actual_ts);

            if (is_slice)
            {
                if (use_cpp_stmt)
                {
                    char *ts = type_to_string(target_type);
                    if (ctx->config->use_cpp)
                    {
                        snprintf(p, oc - cur_len, "}; (%s){(%s*)__tmp, %ld, %ld}; })", ts,
                                 (strncmp(ts, "Slice__", 7) == 0 ? ts + 7 : "char"), (long)len,
                                 (long)len);
                    }
                    else
                    {
                        snprintf(p, oc - cur_len, "}; (%s){.data=__tmp, .len=%ld, .cap=%ld}; })",
                                 ts, (long)len, (long)len);
                    }
                    zfree(ts);
                }
                else
                {
                    if (ctx->config->use_cpp)
                    {
                        snprintf(p, oc - cur_len, "}, %ld, %ld}", (long)len, (long)len);
                    }
                    else
                    {
                        snprintf(p, oc - cur_len, "},.len=%ld,.cap=%ld}", (long)len, (long)len);
                    }
                }
            }
            else
            {
                if (use_cpp_stmt && target_type && target_type->kind == TYPE_POINTER &&
                    target_type->inner)
                {
                    char *inner_ts = type_to_string(target_type->inner);
                    snprintf(p, oc - cur_len, "}; (%s*)__tmp; })", inner_ts);
                    zfree(inner_ts);
                }
                else
                {
                    snprintf(p, oc - cur_len, "}");
                }
            }
        }
    }

    zfree(b);

    ASTNode *n = ast_create(NODE_RAW_STMT);
    n->token = t;
    n->raw_stmt.content = o;
    n->type_info = target_type;
    return n;
}
