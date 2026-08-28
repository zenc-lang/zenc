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

void parser_audit_preprocessor(ParserContext *ctx, Token tok)
{
    CompilerConfig *cfg = &ctx->compiler->config;
    const char *p = tok.start;
    while (isspace(*p) || *p == '#')
    {
        p++;
    }

    if (strncmp(p, "define", 6) == 0)
    {
        if (cfg->misra_mode)
        {
            zerror_at(tok,
                      "MISRA Violation: '#' directives are prohibited (MISRA Rule Zen 1.4). Use "
                      "'def' instead.");

            const char *id_start = p + 6;
            while (isspace(*id_start))
            {
                id_start++;
            }
            const char *id_end = id_start;
            while (isalnum(*id_end) || *id_end == '_')
            {
                id_end++;
            }
            ptrdiff_t id_len = id_end - id_start;
            if (id_len > 0)
            {
                char id[128];
                if (id_len >= 128)
                {
                    id_len = 127;
                }
                strncpy(id, id_start, (size_t)(id_len));
                id[id_len] = 0;

                if (strcmp(id, "errno") == 0 || strcmp(id, "assert") == 0 ||
                    strcmp(id, "NULL") == 0 || strcmp(id, "static_assert") == 0 ||
                    strcmp(id, "bool") == 0 || strcmp(id, "restrict") == 0 ||
                    strcmp(id, "inline") == 0)
                {
                    zerror_at(
                        tok, "MISRA Rule 21.1: #define shall not be used on a reserved macro name");
                }
            }
        }
        else
        {
            zwarn_at_diag(0, tok,
                          "Preprocessor directive '#define' is deprecated. Use 'def' instead.");
        }
        char *content = xmalloc(tok.len + 1);
        strncpy(content, tok.start, tok.len);
        content[tok.len] = 0;
        try_parse_macro_const(ctx, content);
        zfree(content);
    }
    else if (strncmp(p, "include", 7) == 0)
    {
        if (cfg->misra_mode)
        {
            zerror_at(
                tok,
                "MISRA Violation: '#include' is prohibited (Rule Zen 1.4). Use 'import' instead.");
        }
        else
        {
            zwarn_at_diag(0, tok,
                          "Preprocessor directive '#include' is deprecated. Use 'import' instead.");
        }
    }
    else if (strncmp(p, "if", 2) == 0 || strncmp(p, "elif", 4) == 0 ||
             strncmp(p, "ifdef", 5) == 0 || strncmp(p, "ifndef", 6) == 0)
    {
        int is_elif = (strncmp(p, "elif", 4) == 0);
        int is_ifdef = (strncmp(p, "ifdef", 5) == 0);
        int is_ifndef = (strncmp(p, "ifndef", 6) == 0);

        if (cfg->misra_mode)
        {
            zerror_at(tok, "MISRA Violation: '#' preprocessor conditions are prohibited (MISRA "
                           "Rule Zen 1.4). Use "
                           "'@cfg(...)' instead.");

            if (is_ifdef || is_ifndef)
            {
                const char *expr_start = p + (is_ifdef ? 5 : 6);
                while (isspace(*expr_start))
                {
                    expr_start++;
                }
            }
            else
            {
                const char *expr_start = p + (is_elif ? 4 : 2);
                while (isspace(*expr_start))
                {
                    expr_start++;
                }

                ptrdiff_t expr_len = (tok.start + tok.len) - expr_start;
                if (expr_len > 0)
                {
                    char *expr_buf = xmalloc((size_t)(expr_len + 1));
                    strncpy(expr_buf, expr_start, (size_t)(expr_len));
                    expr_buf[expr_len] = 0;

                    char *comment = (char *)strstr(expr_buf, "//");
                    if (comment)
                    {
                        *comment = 0;
                    }
                    char *nl = (char *)strchr(expr_buf, '\n');
                    if (nl)
                    {
                        *nl = 0;
                    }
                    char *cr = (char *)strchr(expr_buf, '\r');
                    if (cr)
                    {
                        *cr = 0;
                    }

                    if (ctx->hook_check_preprocessor_expr)
                    {
                        ctx->hook_check_preprocessor_expr(ctx, tok, expr_buf);
                    }
                    zfree(expr_buf);
                }
            }
        }
        else
        {
            zwarn_at_diag(0, tok,
                          "Preprocessor directive '#' conditions are deprecated. Use "
                          "'@cfg(...)' instead.");
        }
    }
    else if (strncmp(p, "undef", 5) == 0 || strncmp(p, "error", 5) == 0 ||
             strncmp(p, "warning", 7) == 0 || strncmp(p, "pragma", 6) == 0)
    {
        int is_undef = (strncmp(p, "undef", 5) == 0);
        if (cfg->misra_mode)
        {
            zerror_at(tok, "MISRA Violation: '#' directives are prohibited (MISRA Rule Zen 1.4).");

            if (is_undef)
            {
                const char *id_start = p + 5;
                while (isspace(*id_start))
                {
                    id_start++;
                }
                const char *id_end = id_start;
                while (isalnum(*id_end) || *id_end == '_')
                {
                    id_end++;
                }
                ptrdiff_t id_len = id_end - id_start;
                if (id_len > 0)
                {
                    char id[128];
                    if ((size_t)id_len >= sizeof(id))
                    {
                        id_len = 127;
                    }
                    strncpy(id, id_start, (size_t)(id_len));
                    id[id_len] = 0;

                    if (strcmp(id, "errno") == 0 || strcmp(id, "assert") == 0 ||
                        strcmp(id, "NULL") == 0 || strcmp(id, "static_assert") == 0)
                    {
                        zerror_at(
                            tok,
                            "MISRA Rule 21.1: #undef shall not be used on a reserved macro name");
                    }
                }
            }
        }
        else
        {
            zwarn_at_diag(0, tok, "Preprocessor directive '#' is deprecated.");
        }
    }
}

void try_parse_macro_const(ParserContext *ctx, const char *content)
{
    CompilerConfig *cfg = &ctx->compiler->config;
    Lexer l;
    lexer_init(&l, content, ctx->config, ctx->current_filename);
    l.emit_comments = 0;

    lexer_next(&l);

    const char *p = content;
    while (isspace(*p))
    {
        p++;
    }
    if (*p == '#')
    {
        p++;
    }

    lexer_init(&l, p, ctx->config, ctx->current_filename);

    Token def = lexer_next(&l);
    if (def.kind != TOK_IDENT || strncmp(def.start, "define", 6) != 0)
    {
        return;
    }

    Token name = lexer_next(&l);
    if (name.kind != TOK_IDENT)
    {
        return;
    }

    const char *p_scan = name.start + name.len;
    while (*p_scan && *p_scan != '\n')
    {
        if (*p_scan == '#')
        {
            if (*(p_scan + 1) == '#')
            {
                if (cfg->misra_mode)
                {
                    zerror_at(name, "MISRA Rule 20.10: '##' operator used in macro");
                }
                p_scan++;
            }
            else
            {
                if (cfg->misra_mode)
                {
                    zerror_at(name, "MISRA Rule 20.10: '#' operator used in macro");
                }
            }
        }
        p_scan++;
    }

    if (*(name.start + name.len) == '(')
    {
        if (cfg->misra_mode)
        {
            const char *pm = name.start + name.len;
            while (isspace(*pm))
            {
                pm++;
            }
            if (*pm == '(')
            {
                pm++;
                char *params[32];
                int param_count = 0;
                while (*pm && *pm != ')' && param_count < 32)
                {
                    while (isspace(*pm) || *pm == ',')
                    {
                        pm++;
                    }
                    if (!isalpha(*pm) && *pm != '_')
                    {
                        break;
                    }
                    const char *p_start = pm;
                    while (is_ident_char(*pm))
                    {
                        pm++;
                    }
                    ptrdiff_t len = pm - p_start;
                    params[param_count] = xmalloc((size_t)(len + 1));
                    strncpy(params[param_count], p_start, (size_t)(len));
                    params[param_count][len] = 0;
                    param_count++;
                }
                while (*pm && *pm != ')')
                {
                    pm++;
                }
                if (*pm == ')')
                {
                    pm++;
                }

                unsigned int used_norm = 0;
                unsigned int used_op = 0;

                const char *pb = pm;
                while (*pb && *pb != '\n')
                {
                    while (isspace(*pb))
                    {
                        pb++;
                    }
                    if (!*pb || *pb == '\n')
                    {
                        break;
                    }

                    if (*pb == '#')
                    {
                        int is_concat = (pb[1] == '#');
                        pb += (is_concat ? 2 : 1);
                        while (isspace(*pb))
                        {
                            pb++;
                        }
                        if (isalpha(*pb) || *pb == '_')
                        {
                            const char *id_start = pb;
                            while (is_ident_char(*pb))
                            {
                                pb++;
                            }
                            ptrdiff_t id_len = pb - id_start;
                            for (int i = 0; i < param_count; i++)
                            {
                                if (id_len == (int)strlen(params[i]) &&
                                    strncmp(id_start, params[i], (size_t)(id_len)) == 0)
                                {
                                    used_op |= (1u << i);
                                    if (!is_concat)
                                    {
                                        const char *pafter = pb;
                                        while (isspace(*pafter))
                                        {
                                            pafter++;
                                        }
                                        if (pafter[0] == '#' && pafter[1] == '#')
                                        {
                                            zerror_at(
                                                name,
                                                "MISRA Rule 20.11: # parameter followed by ##");
                                        }
                                    }
                                }
                            }
                        }
                    }
                    else if (isalpha(*pb) || *pb == '_')
                    {
                        const char *id_start = pb;
                        while (is_ident_char(*pb))
                        {
                            pb++;
                        }
                        ptrdiff_t id_len = pb - id_start;
                        const char *pafter = pb;
                        while (isspace(*pafter))
                        {
                            pafter++;
                        }
                        int follows_concat = (pafter[0] == '#' && pafter[1] == '#');

                        for (int i = 0; i < param_count; i++)
                        {
                            if (id_len == (int)strlen(params[i]) &&
                                strncmp(id_start, params[i], (size_t)(id_len)) == 0)
                            {
                                if (follows_concat)
                                {
                                    used_op |= (1u << i);
                                }
                                else
                                {
                                    used_norm |= (1u << i);
                                }
                            }
                        }
                    }
                    else
                    {
                        pb++;
                    }
                }

                for (int i = 0; i < param_count; i++)
                {
                    if ((used_op & (1 << i)) && (used_norm & (1 << i)))
                    {
                        char msg[128];
                        snprintf(msg, sizeof(msg),
                                 "MISRA Rule 20.12: parameter '%s' used as both operand to #/## "
                                 "and normal token",
                                 params[i]);
                        zerror_at(name, "%s", msg);
                    }
                    zfree(params[i]);
                }
            }
        }
        return;
    }

    Lexer check_l = l;
    int balance = 0;
    while (1)
    {
        Token ct = lexer_next(&check_l);
        if (ct.kind == TOK_EOF)
        {
            break;
        }
        if (ct.kind == TOK_LPAREN)
        {
            balance++;
        }
        else if (ct.kind == TOK_RPAREN)
        {
            balance--;
        }
        else if (ct.kind == TOK_LBRACE || ct.kind == TOK_RBRACE || ct.kind == TOK_SEMICOLON)
        {
            return;
        }

        if (cfg->misra_mode && ct.kind == TOK_OP)
        {
            if (ct.len == 1 && *ct.start == '#')
            {
                zerror_at(ct, "MISRA Rule 20.10: '#' operator used in macro");
            }
            else if (ct.len == 2 && strncmp(ct.start, "##", 2) == 0)
            {
                zerror_at(ct, "MISRA Rule 20.10: '##' operator used in macro");
            }
        }

        if (ct.kind == TOK_IDENT)
        {
            char *tok_str = token_strdup(ct);
            int is_prim = is_primitive_type_name(tok_str);

            if (!is_prim)
            {
                if (is_token(ct, "signed") || is_token(ct, "unsigned") || is_token(ct, "struct") ||
                    is_token(ct, "union") || is_token(ct, "enum") || is_token(ct, "const") ||
                    is_token(ct, "volatile") || is_token(ct, "extern") || is_token(ct, "static") ||
                    is_token(ct, "register") || is_token(ct, "auto") || is_token(ct, "typedef"))
                {
                    is_prim = 1;
                }
            }

            zfree(tok_str);

            if (is_prim)
            {
                return;
            }
        }
    }
    if (balance != 0)
    {
        return;
    }

    if (lexer_peek(&l).kind == TOK_EOF)
    {
        return;
    }

    ASTNode *expr = parse_expression(ctx, &l);
    if (!expr)
    {
        return;
    }

    long long val;
    if (eval_const_int_expr(expr, ctx, &val))
    {
        char *n = token_strdup(name);

        ZenSymbol *existing = find_symbol_entry(ctx, n);
        if (!existing)
        {
            add_symbol_with_token(ctx, n, "int", type_new(TYPE_INT), name, 0);
            ZenSymbol *sym = find_symbol_entry(ctx, n);
            if (sym)
            {
                sym->is_const_value = 1;
                sym->const_int_val = (int)val;
                sym->is_def = 1;
            }
        }
        else
        {
            zfree(n);
        }
    }
}
