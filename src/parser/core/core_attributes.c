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

DeclarationAttributes parse_attributes(ParserContext *ctx, Lexer *l)
{
    (void)ctx;
    DeclarationAttributes res;
    memset(&res, 0, sizeof(res));
    res.derived_traits = xmalloc(sizeof(char *) * 32);

    Token t = lexer_peek(l);
    while (t.kind == TOK_AT)
    {
        lexer_next(l);
        Token attr = lexer_next(l);
        if (attr.kind != TOK_IDENT && attr.kind != TOK_COMPTIME && attr.kind != TOK_ALIAS)
        {
            zpanic_at(attr, "Expected attribute name");
            return (DeclarationAttributes){0};
            return (DeclarationAttributes){0};
        }

        if ((0 == strncmp(attr.start, "vector_size", 11) && 11 == attr.len) ||
            (0 == strncmp(attr.start, "vector", 6) && 6 == attr.len))
        {
            if (lexer_peek(l).kind == TOK_LPAREN)
            {
                lexer_next(l);
                Token num = lexer_next(l);
                if (num.kind == TOK_INT)
                {
                    char *tmp = token_strdup(num);
                    res.vector_size = (int)strtol(tmp, NULL, 10);
                    zfree(tmp);
                }
                if (lexer_next(l).kind != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after vector size");
                    return (DeclarationAttributes){0};
                    return (DeclarationAttributes){0};
                }
            }
        }
        else if (0 == strncmp(attr.start, "required", 8) && 8 == attr.len)
        {
            res.is_required = 1;
        }
        else if (0 == strncmp(attr.start, "deprecated", 10) && 10 == attr.len)
        {
            res.is_deprecated = 1;
            if (lexer_peek(l).kind == TOK_LPAREN)
            {
                lexer_next(l);
                Token msg = lexer_next(l);
                if (msg.kind == TOK_STRING)
                {
                    res.deprecated_msg = xmalloc(msg.len - 1);
                    strncpy(res.deprecated_msg, msg.start + 1, msg.len - 2);
                    res.deprecated_msg[msg.len - 2] = 0;
                }
                if (lexer_next(l).kind != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after deprecated message");
                    return (DeclarationAttributes){0};
                    return (DeclarationAttributes){0};
                }
            }
        }
        else if (0 == strncmp(attr.start, "inline", 6) && 6 == attr.len)
        {
            res.is_inline = 1;
        }
        else if (0 == strncmp(attr.start, "noinline", 8) && 8 == attr.len)
        {
            res.is_noinline = 1;
        }
        else if (0 == strncmp(attr.start, "noreturn", 8) && 8 == attr.len)
        {
            res.is_noreturn = 1;
        }
        else if (0 == strncmp(attr.start, "cold", 4) && 4 == attr.len)
        {
            res.is_cold = 1;
        }
        else if (0 == strncmp(attr.start, "hot", 3) && 3 == attr.len)
        {
            res.is_hot = 1;
        }
        else if (0 == strncmp(attr.start, "constructor", 11) && 11 == attr.len)
        {
            res.is_constructor = 1;
        }
        else if (0 == strncmp(attr.start, "destructor", 10) && 10 == attr.len)
        {
            res.is_destructor = 1;
        }
        else if (0 == strncmp(attr.start, "unused", 6) && 6 == attr.len)
        {
            res.is_unused = 1;
        }
        else if (0 == strncmp(attr.start, "weak", 4) && 4 == attr.len)
        {
            res.is_weak = 1;
        }
        else if (0 == strncmp(attr.start, "export", 6) && 6 == attr.len)
        {
            res.is_export = 1;
        }
        else if (0 == strncmp(attr.start, "thread_local", 12) && 12 == attr.len)
        {
            res.is_thread_local = 1;
        }
        else if (0 == strncmp(attr.start, "comptime", 8) && 8 == attr.len)
        {
            res.is_comptime = 1;
        }
        else if (0 == strncmp(attr.start, "section", 7) && 7 == attr.len)
        {
            if (lexer_peek(l).kind == TOK_LPAREN)
            {
                lexer_next(l);
                Token sec = lexer_next(l);
                if (sec.kind == TOK_STRING)
                {
                    res.section = xmalloc(sec.len - 1);
                    strncpy(res.section, sec.start + 1, sec.len - 2);
                    res.section[sec.len - 2] = 0;
                }
                if (lexer_next(l).kind != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after section name");
                    return (DeclarationAttributes){0};
                    return (DeclarationAttributes){0};
                }
            }
            else
            {
                zpanic_at(lexer_peek(l), "@section requires a name: @section(\"name\")");
                return (DeclarationAttributes){0};
                return (DeclarationAttributes){0};
            }
        }
        else if (0 == strncmp(attr.start, "crepr", 5) && 5 == attr.len)
        {
            if (lexer_peek(l).kind == TOK_LPAREN)
            {
                lexer_next(l);
                Token ct = lexer_next(l);
                if (ct.kind == TOK_STRING)
                {
                    res.crepr_c_type = xmalloc(ct.len - 1);
                    strncpy(res.crepr_c_type, ct.start + 1, ct.len - 2);
                    res.crepr_c_type[ct.len - 2] = 0;
                }
                if (lexer_next(l).kind != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after crepr type name");
                    return (DeclarationAttributes){0};
                    return (DeclarationAttributes){0};
                }
            }
            else
            {
                zpanic_at(lexer_peek(l), "@crepr requires a value: @crepr(\"C.kind\")");
                return (DeclarationAttributes){0};
                return (DeclarationAttributes){0};
            }
        }
        else if (0 == strncmp(attr.start, "packed", 6) && 6 == attr.len)
        {
            res.is_packed = 1;
        }
        else if (0 == strncmp(attr.start, "align", 5) && 5 == attr.len)
        {
            if (lexer_peek(l).kind == TOK_LPAREN)
            {
                lexer_next(l);
                Token num = lexer_next(l);
                if (num.kind == TOK_INT)
                {
                    res.align = (int)strtol(num.start, NULL, 10);
                }
                if (lexer_next(l).kind != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after align value");
                    return (DeclarationAttributes){0};
                    return (DeclarationAttributes){0};
                }
            }
            else
            {
                zpanic_at(lexer_peek(l), "@align requires a value: @align(N)");
                return (DeclarationAttributes){0};
                return (DeclarationAttributes){0};
            }
        }
        else if (0 == strncmp(attr.start, "cfg", 3) && 3 == attr.len)
        {
            if (lexer_peek(l).kind == TOK_LPAREN)
            {
                lexer_next(l);
                Token cfg_tok = lexer_next(l);
                if ((cfg_tok.kind == TOK_NOT || (cfg_tok.kind == TOK_IDENT && cfg_tok.len == 3 &&
                                                 strncmp(cfg_tok.start, "not", 3) == 0)))
                {
                    if (lexer_peek(l).kind != TOK_LPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ( after not in @cfg(not(...))");
                        return (DeclarationAttributes){0};
                        return (DeclarationAttributes){0};
                    }
                    lexer_next(l);
                    Token name_tok = lexer_next(l);
                    if (name_tok.kind != TOK_IDENT)
                    {
                        zpanic_at(name_tok, "Expected define name in @cfg(not(NAME))");
                        return (DeclarationAttributes){0};
                        return (DeclarationAttributes){0};
                    }
                    char *cfg_name = token_strdup(name_tok);
                    if (!res.cfg_condition)
                    {
                        res.cfg_condition = xmalloc(strlen(cfg_name) + 32);
                        sprintf(res.cfg_condition, "!defined(ZC_CFG_%s)", cfg_name); /* safe */
                    }
                    else
                    {
                        char *old = res.cfg_condition;
                        res.cfg_condition = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                        sprintf(res.cfg_condition, "%s && !defined(ZC_CFG_%s)", old,
                                cfg_name); /* safe */
                        zfree(old);
                    }
                    zfree(cfg_name);
                    if (lexer_next(l).kind != TOK_RPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ) after name in @cfg(not(NAME))");
                        return (DeclarationAttributes){0};
                        return (DeclarationAttributes){0};
                    }
                }
                else if (cfg_tok.kind == TOK_IDENT && cfg_tok.len == 3 &&
                         strncmp(cfg_tok.start, "any", 3) == 0)
                {
                    if (lexer_peek(l).kind != TOK_LPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ( after any in @cfg(any(...))");
                        return (DeclarationAttributes){0};
                        return (DeclarationAttributes){0};
                    }
                    lexer_next(l);
                    char *any_cond = NULL;
                    while (1)
                    {
                        Token inner_t = lexer_next(l);
                        if (inner_t.kind == TOK_EOF)
                        {
                            break;
                        }
                        if ((inner_t.kind == TOK_NOT ||
                             (inner_t.kind == TOK_IDENT && inner_t.len == 3 &&
                              strncmp(inner_t.start, "not", 3) == 0)))
                        {
                            if (lexer_next(l).kind != TOK_LPAREN)
                            {
                                zpanic_at(lexer_peek(l), "Expected ( after not");
                                return (DeclarationAttributes){0};
                                return (DeclarationAttributes){0};
                            }
                            Token nt = lexer_next(l);
                            if (nt.kind != TOK_IDENT)
                            {
                                zpanic_at(nt, "Expected define name");
                                return (DeclarationAttributes){0};
                                return (DeclarationAttributes){0};
                            }
                            char *cfg_name = token_strdup(nt);
                            if (!any_cond)
                            {
                                any_cond = xmalloc(strlen(cfg_name) + 32);
                                sprintf(any_cond, "!defined(ZC_CFG_%s)", cfg_name); /* safe */
                            }
                            else
                            {
                                char *old = any_cond;
                                any_cond = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                                sprintf(any_cond, "%s || !defined(ZC_CFG_%s)", old,
                                        cfg_name); /* safe */
                                zfree(old);
                            }
                            zfree(cfg_name);
                            if (lexer_next(l).kind != TOK_RPAREN)
                            {
                                zpanic_at(lexer_peek(l), "Expected )");
                                return (DeclarationAttributes){0};
                                return (DeclarationAttributes){0};
                            }
                        }
                        else if (inner_t.kind == TOK_IDENT)
                        {
                            char *cfg_name = token_strdup(inner_t);
                            if (!any_cond)
                            {
                                any_cond = xmalloc(strlen(cfg_name) + 32);
                                sprintf(any_cond, "defined(ZC_CFG_%s)", cfg_name); /* safe */
                            }
                            else
                            {
                                char *old = any_cond;
                                any_cond = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                                sprintf(any_cond, "%s || defined(ZC_CFG_%s)", old,
                                        cfg_name); /* safe */
                                zfree(old);
                            }
                            zfree(cfg_name);
                        }
                        else
                        {
                            zpanic_at(inner_t, "Expected define name in @cfg(any(...))");
                            return (DeclarationAttributes){0};
                            return (DeclarationAttributes){0};
                        }
                        if (lexer_peek(l).kind == TOK_COMMA)
                        {
                            lexer_next(l);
                        }
                        else
                        {
                            break;
                        }
                    }
                    if (lexer_next(l).kind != TOK_RPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ) after any(...)");
                        return (DeclarationAttributes){0};
                        return (DeclarationAttributes){0};
                    }
                    if (any_cond)
                    {
                        if (!res.cfg_condition)
                        {
                            res.cfg_condition = xmalloc(strlen(any_cond) + 32);
                            sprintf(res.cfg_condition, "(%s)", any_cond); /* safe */
                        }
                        else
                        {
                            char *old = res.cfg_condition;
                            res.cfg_condition = xmalloc(strlen(old) + strlen(any_cond) + 32);
                            sprintf(res.cfg_condition, "%s && (%s)", old, any_cond); /* safe */
                            zfree(old);
                        }
                        zfree(any_cond);
                    }
                }
                else if (cfg_tok.kind == TOK_IDENT && cfg_tok.len == 3 &&
                         strncmp(cfg_tok.start, "all", 3) == 0)
                {
                    if (lexer_peek(l).kind != TOK_LPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ( after all in @cfg(all(...))");
                        return (DeclarationAttributes){0};
                        return (DeclarationAttributes){0};
                    }
                    lexer_next(l);
                    char *all_cond = NULL;
                    while (1)
                    {
                        Token inner_t = lexer_next(l);
                        if (inner_t.kind == TOK_EOF)
                        {
                            break;
                        }
                        if ((inner_t.kind == TOK_NOT ||
                             (inner_t.kind == TOK_IDENT && inner_t.len == 3 &&
                              strncmp(inner_t.start, "not", 3) == 0)))
                        {
                            if (lexer_next(l).kind != TOK_LPAREN)
                            {
                                zpanic_at(lexer_peek(l), "Expected ( after not");
                                return (DeclarationAttributes){0};
                                return (DeclarationAttributes){0};
                            }
                            Token nt = lexer_next(l);
                            if (nt.kind != TOK_IDENT)
                            {
                                zpanic_at(nt, "Expected define name");
                                return (DeclarationAttributes){0};
                                return (DeclarationAttributes){0};
                            }
                            char *cfg_name = token_strdup(nt);
                            if (!all_cond)
                            {
                                all_cond = xmalloc(strlen(cfg_name) + 32);
                                sprintf(all_cond, "!defined(ZC_CFG_%s)", cfg_name); /* safe */
                            }
                            else
                            {
                                char *old = all_cond;
                                all_cond = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                                sprintf(all_cond, "%s && !defined(ZC_CFG_%s)", old,
                                        cfg_name); /* safe */
                                zfree(old);
                            }
                            zfree(cfg_name);
                            if (lexer_next(l).kind != TOK_RPAREN)
                            {
                                zpanic_at(lexer_peek(l), "Expected )");
                                return (DeclarationAttributes){0};
                                return (DeclarationAttributes){0};
                            }
                        }
                        else if (inner_t.kind == TOK_IDENT)
                        {
                            char *cfg_name = token_strdup(inner_t);
                            if (!all_cond)
                            {
                                all_cond = xmalloc(strlen(cfg_name) + 32);
                                sprintf(all_cond, "defined(ZC_CFG_%s)", cfg_name); /* safe */
                            }
                            else
                            {
                                char *old = all_cond;
                                all_cond = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                                sprintf(all_cond, "%s && defined(ZC_CFG_%s)", old,
                                        cfg_name); /* safe */
                                zfree(old);
                            }
                            zfree(cfg_name);
                        }
                        else
                        {
                            zpanic_at(inner_t, "Expected define name in @cfg(all(...))");
                            return (DeclarationAttributes){0};
                            return (DeclarationAttributes){0};
                        }
                        if (lexer_peek(l).kind == TOK_COMMA)
                        {
                            lexer_next(l);
                        }
                        else
                        {
                            break;
                        }
                    }
                    if (lexer_next(l).kind != TOK_RPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ) after all(...)");
                        return (DeclarationAttributes){0};
                        return (DeclarationAttributes){0};
                    }
                    if (all_cond)
                    {
                        if (!res.cfg_condition)
                        {
                            res.cfg_condition = xmalloc(strlen(all_cond) + 32);
                            sprintf(res.cfg_condition, "(%s)", all_cond); /* safe */
                        }
                        else
                        {
                            char *old = res.cfg_condition;
                            res.cfg_condition = xmalloc(strlen(old) + strlen(all_cond) + 32);
                            sprintf(res.cfg_condition, "%s && (%s)", old, all_cond); /* safe */
                            zfree(old);
                        }
                        zfree(all_cond);
                    }
                }
                else if (cfg_tok.kind == TOK_IDENT)
                {
                    char *cfg_name = token_strdup(cfg_tok);
                    if (!res.cfg_condition)
                    {
                        res.cfg_condition = xmalloc(strlen(cfg_name) + 32);
                        sprintf(res.cfg_condition, "defined(ZC_CFG_%s)", cfg_name); /* safe */
                    }
                    else
                    {
                        char *old = res.cfg_condition;
                        res.cfg_condition = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                        sprintf(res.cfg_condition, "%s && defined(ZC_CFG_%s)", old,
                                cfg_name); /* safe */
                        zfree(old);
                    }
                    zfree(cfg_name);
                }
                else
                {
                    zpanic_at(cfg_tok, "Expected define name in @cfg(NAME)");
                    return (DeclarationAttributes){0};
                    return (DeclarationAttributes){0};
                }
                if (lexer_next(l).kind != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after @cfg(...)");
                    return (DeclarationAttributes){0};
                    return (DeclarationAttributes){0};
                }
            }
            else
            {
                zpanic_at(lexer_peek(l), "@cfg requires a condition: @cfg(NAME)");
                return (DeclarationAttributes){0};
                return (DeclarationAttributes){0};
            }
        }
        else if (0 == strncmp(attr.start, "link_name", 9) && 9 == attr.len)
        {
            if (lexer_peek(l).kind == TOK_LPAREN)
            {
                lexer_next(l);
                Token name_tok = lexer_next(l);
                if (name_tok.kind == TOK_STRING)
                {
                    res.link_name = xmalloc(name_tok.len - 1);
                    strncpy(res.link_name, name_tok.start + 1, name_tok.len - 2);
                    res.link_name[name_tok.len - 2] = 0;
                }
                else
                {
                    zpanic_at(name_tok, "Expected string literal in @link_name");
                    return (DeclarationAttributes){0};
                    return (DeclarationAttributes){0};
                }
                if (lexer_next(l).kind != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after @link_name value");
                    return (DeclarationAttributes){0};
                    return (DeclarationAttributes){0};
                }
            }
            else
            {
                zpanic_at(lexer_peek(l), "@link_name requires a value: @link_name(\"name\")");
                return (DeclarationAttributes){0};
                return (DeclarationAttributes){0};
            }
        }
        else if (0 == strncmp(attr.start, "link", 4) && 4 == attr.len)
        {
            if (lexer_peek(l).kind == TOK_LPAREN)
            {
                lexer_next(l);
                Token path_tok = lexer_next(l);
                if (path_tok.kind == TOK_STRING)
                {
                    res.link_path = xmalloc(path_tok.len - 1);
                    strncpy(res.link_path, path_tok.start + 1, path_tok.len - 2);
                    res.link_path[path_tok.len - 2] = 0;
                }
                else
                {
                    zpanic_at(path_tok, "Expected string literal in @link");
                    return (DeclarationAttributes){0};
                    return (DeclarationAttributes){0};
                }
                if (lexer_next(l).kind != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after @link path");
                    return (DeclarationAttributes){0};
                    return (DeclarationAttributes){0};
                }
            }
            else
            {
                zpanic_at(lexer_peek(l), "@link requires a path: @link(\"path/to/file.c\")");
                return (DeclarationAttributes){0};
                return (DeclarationAttributes){0};
            }
        }
        else if (0 == strncmp(attr.start, "derive", 6) && 6 == attr.len)
        {
            if (lexer_peek(l).kind == TOK_LPAREN)
            {
                lexer_next(l);
                while (1)
                {
                    Token inner_t = lexer_next(l);
                    if (inner_t.kind != TOK_IDENT)
                    {
                        zpanic_at(inner_t, "Expected trait name in @derive");
                        return (DeclarationAttributes){0};
                        return (DeclarationAttributes){0};
                    }
                    if (res.derived_count < 32)
                    {
                        res.derived_traits[res.derived_count++] = token_strdup(inner_t);
                    }
                    if (lexer_peek(l).kind == TOK_COMMA)
                    {
                        lexer_next(l);
                    }
                    else
                    {
                        break;
                    }
                }
                if (lexer_next(l).kind != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after derive traits");
                    return (DeclarationAttributes){0};
                    return (DeclarationAttributes){0};
                }
            }
            else
            {
                zpanic_at(lexer_peek(l), "@derive requires traits: @derive(Debug, Clone)");
                return (DeclarationAttributes){0};
                return (DeclarationAttributes){0};
            }
        }
        else
        {
            // Checking for CUDA and other attributes...
            if (0 == strncmp(attr.start, "pure", 4) && 4 == attr.len)
            {
                res.is_pure = 1;
            }
            else if (0 == strncmp(attr.start, "global", 6) && 6 == attr.len)
            {
                res.cuda_global = 1;
            }
            else if (0 == strncmp(attr.start, "device", 6) && 6 == attr.len)
            {
                res.cuda_device = 1;
            }
            else if (0 == strncmp(attr.start, "host", 4) && 4 == attr.len)
            {
                res.cuda_host = 1;
            }
            else
            {
                Attribute *new_attr = xmalloc(sizeof(Attribute));
                new_attr->name = token_strdup(attr);
                new_attr->args = NULL;
                new_attr->count = 0;
                new_attr->next = res.custom_attributes; // Prepend
                res.custom_attributes = new_attr;

                if (lexer_peek(l).kind == TOK_LPAREN)
                {
                    lexer_next(l);
                    while (1)
                    {
                        Token inner_t = lexer_next(l);
                        new_attr->args =
                            realloc(new_attr->args, sizeof(char *) * (size_t)(new_attr->count + 1));

                        if (inner_t.kind == TOK_STRING)
                        {
                            new_attr->args[new_attr->count++] = token_strdup(inner_t);
                        }
                        else
                        {
                            new_attr->args[new_attr->count++] = token_strdup(inner_t);
                        }

                        if (lexer_peek(l).kind == TOK_EOF)
                        {
                            zpanic_at(lexer_peek(l), "Unexpected end of file in attribute args");
                            break;
                        }
                        if (lexer_peek(l).kind == TOK_COMMA)
                        {
                            lexer_next(l);
                        }
                        else if (lexer_peek(l).kind == TOK_RPAREN)
                        {
                            break;
                        }
                        else
                        {
                            zpanic_at(lexer_peek(l), "Expected , or ) in attribute args");
                            return (DeclarationAttributes){0};
                            return (DeclarationAttributes){0};
                        }
                    }
                    if (lexer_next(l).kind != TOK_RPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected )");
                        return (DeclarationAttributes){0};
                        return (DeclarationAttributes){0};
                    }
                }
            }
        }

        t = lexer_peek(l);
    }
    return res;
}
