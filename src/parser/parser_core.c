
#include "parser.h"
#include "zprep.h"
#include "analysis/const_fold.h"

static ASTNode *generate_derive_impls(ParserContext *ctx, ASTNode *strct, char **traits, int count);

DeclarationAttributes parse_attributes(ParserContext *ctx, Lexer *l)
{
    (void)ctx;
    DeclarationAttributes res;
    memset(&res, 0, sizeof(res));
    res.derived_traits = xmalloc(sizeof(char *) * 32);

    Token t = lexer_peek(l);
    while (t.type == TOK_AT)
    {
        lexer_next(l);
        Token attr = lexer_next(l);
        if (attr.type != TOK_IDENT)
        {
            zpanic_at(attr, "Expected attribute name");
        }

        if (0 == strncmp(attr.start, "vector_size", 11) && 11 == attr.len)
        {
            if (lexer_peek(l).type == TOK_LPAREN)
            {
                lexer_next(l);
                Token num = lexer_next(l);
                if (num.type == TOK_INT)
                {
                    char *tmp = token_strdup(num);
                    res.vector_size = atoi(tmp);
                    free(tmp);
                }
                if (lexer_next(l).type != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after vector size");
                }
            }
        }
        else if (0 == strncmp(attr.start, "packed", 6) && 6 == attr.len)
        {
            res.is_packed = 1;
        }
        else if (0 == strncmp(attr.start, "align", 5) && 5 == attr.len)
        {
            if (lexer_peek(l).type == TOK_LPAREN)
            {
                lexer_next(l);
                Token num = lexer_next(l);
                if (num.type == TOK_INT)
                {
                    res.align = atoi(num.start);
                }
                if (lexer_next(l).type != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after align value");
                }
            }
            else
            {
                zpanic_at(lexer_peek(l), "@align requires a value: @align(N)");
            }
        }
        else if (0 == strncmp(attr.start, "cfg", 3) && 3 == attr.len)
        {
            if (lexer_peek(l).type == TOK_LPAREN)
            {
                lexer_next(l);
                Token cfg_tok = lexer_next(l);
                if ((cfg_tok.type == TOK_NOT || (cfg_tok.type == TOK_IDENT && cfg_tok.len == 3 &&
                                                 strncmp(cfg_tok.start, "not", 3) == 0)))
                {
                    if (lexer_peek(l).type != TOK_LPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ( after not in @cfg(not(...))");
                    }
                    lexer_next(l);
                    Token name_tok = lexer_next(l);
                    if (name_tok.type != TOK_IDENT)
                    {
                        zpanic_at(name_tok, "Expected define name in @cfg(not(NAME))");
                    }
                    char *cfg_name = token_strdup(name_tok);
                    if (!res.cfg_condition)
                    {
                        res.cfg_condition = xmalloc(strlen(cfg_name) + 32);
                        sprintf(res.cfg_condition, "!defined(ZC_CFG_%s)", cfg_name);
                    }
                    else
                    {
                        char *old = res.cfg_condition;
                        res.cfg_condition = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                        sprintf(res.cfg_condition, "%s && !defined(ZC_CFG_%s)", old, cfg_name);
                        free(old);
                    }
                    free(cfg_name);
                    if (lexer_next(l).type != TOK_RPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ) after name in @cfg(not(NAME))");
                    }
                }
                else if (cfg_tok.type == TOK_IDENT && cfg_tok.len == 3 &&
                         strncmp(cfg_tok.start, "any", 3) == 0)
                {
                    if (lexer_peek(l).type != TOK_LPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ( after any in @cfg(any(...))");
                    }
                    lexer_next(l);
                    char *any_cond = NULL;
                    while (1)
                    {
                        Token inner_t = lexer_next(l);
                        if ((inner_t.type == TOK_NOT ||
                             (inner_t.type == TOK_IDENT && inner_t.len == 3 &&
                              strncmp(inner_t.start, "not", 3) == 0)))
                        {
                            if (lexer_next(l).type != TOK_LPAREN)
                            {
                                zpanic_at(lexer_peek(l), "Expected ( after not");
                            }
                            Token nt = lexer_next(l);
                            if (nt.type != TOK_IDENT)
                            {
                                zpanic_at(nt, "Expected define name");
                            }
                            char *cfg_name = token_strdup(nt);
                            if (!any_cond)
                            {
                                any_cond = xmalloc(strlen(cfg_name) + 32);
                                sprintf(any_cond, "!defined(ZC_CFG_%s)", cfg_name);
                            }
                            else
                            {
                                char *old = any_cond;
                                any_cond = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                                sprintf(any_cond, "%s || !defined(ZC_CFG_%s)", old, cfg_name);
                                free(old);
                            }
                            free(cfg_name);
                            if (lexer_next(l).type != TOK_RPAREN)
                            {
                                zpanic_at(lexer_peek(l), "Expected )");
                            }
                        }
                        else if (inner_t.type == TOK_IDENT)
                        {
                            char *cfg_name = token_strdup(inner_t);
                            if (!any_cond)
                            {
                                any_cond = xmalloc(strlen(cfg_name) + 32);
                                sprintf(any_cond, "defined(ZC_CFG_%s)", cfg_name);
                            }
                            else
                            {
                                char *old = any_cond;
                                any_cond = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                                sprintf(any_cond, "%s || defined(ZC_CFG_%s)", old, cfg_name);
                                free(old);
                            }
                            free(cfg_name);
                        }
                        else
                        {
                            zpanic_at(inner_t, "Expected define name in @cfg(any(...))");
                        }
                        if (lexer_peek(l).type == TOK_COMMA)
                        {
                            lexer_next(l);
                        }
                        else
                        {
                            break;
                        }
                    }
                    if (lexer_next(l).type != TOK_RPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ) after any(...)");
                    }
                    if (any_cond)
                    {
                        if (!res.cfg_condition)
                        {
                            res.cfg_condition = xmalloc(strlen(any_cond) + 32);
                            sprintf(res.cfg_condition, "(%s)", any_cond);
                        }
                        else
                        {
                            char *old = res.cfg_condition;
                            res.cfg_condition = xmalloc(strlen(old) + strlen(any_cond) + 32);
                            sprintf(res.cfg_condition, "%s && (%s)", old, any_cond);
                            free(old);
                        }
                        free(any_cond);
                    }
                }
                else if (cfg_tok.type == TOK_IDENT && cfg_tok.len == 3 &&
                         strncmp(cfg_tok.start, "all", 3) == 0)
                {
                    if (lexer_peek(l).type != TOK_LPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ( after all in @cfg(all(...))");
                    }
                    lexer_next(l);
                    char *all_cond = NULL;
                    while (1)
                    {
                        Token inner_t = lexer_next(l);
                        if ((inner_t.type == TOK_NOT ||
                             (inner_t.type == TOK_IDENT && inner_t.len == 3 &&
                              strncmp(inner_t.start, "not", 3) == 0)))
                        {
                            if (lexer_next(l).type != TOK_LPAREN)
                            {
                                zpanic_at(lexer_peek(l), "Expected ( after not");
                            }
                            Token nt = lexer_next(l);
                            if (nt.type != TOK_IDENT)
                            {
                                zpanic_at(nt, "Expected define name");
                            }
                            char *cfg_name = token_strdup(nt);
                            if (!all_cond)
                            {
                                all_cond = xmalloc(strlen(cfg_name) + 32);
                                sprintf(all_cond, "!defined(ZC_CFG_%s)", cfg_name);
                            }
                            else
                            {
                                char *old = all_cond;
                                all_cond = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                                sprintf(all_cond, "%s && !defined(ZC_CFG_%s)", old, cfg_name);
                                free(old);
                            }
                            free(cfg_name);
                            if (lexer_next(l).type != TOK_RPAREN)
                            {
                                zpanic_at(lexer_peek(l), "Expected )");
                            }
                        }
                        else if (inner_t.type == TOK_IDENT)
                        {
                            char *cfg_name = token_strdup(inner_t);
                            if (!all_cond)
                            {
                                all_cond = xmalloc(strlen(cfg_name) + 32);
                                sprintf(all_cond, "defined(ZC_CFG_%s)", cfg_name);
                            }
                            else
                            {
                                char *old = all_cond;
                                all_cond = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                                sprintf(all_cond, "%s && defined(ZC_CFG_%s)", old, cfg_name);
                                free(old);
                            }
                            free(cfg_name);
                        }
                        else
                        {
                            zpanic_at(inner_t, "Expected define name in @cfg(all(...))");
                        }
                        if (lexer_peek(l).type == TOK_COMMA)
                        {
                            lexer_next(l);
                        }
                        else
                        {
                            break;
                        }
                    }
                    if (lexer_next(l).type != TOK_RPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ) after all(...)");
                    }
                    if (all_cond)
                    {
                        if (!res.cfg_condition)
                        {
                            res.cfg_condition = xmalloc(strlen(all_cond) + 32);
                            sprintf(res.cfg_condition, "(%s)", all_cond);
                        }
                        else
                        {
                            char *old = res.cfg_condition;
                            res.cfg_condition = xmalloc(strlen(old) + strlen(all_cond) + 32);
                            sprintf(res.cfg_condition, "%s && (%s)", old, all_cond);
                            free(old);
                        }
                        free(all_cond);
                    }
                }
                else if (cfg_tok.type == TOK_IDENT)
                {
                    char *cfg_name = token_strdup(cfg_tok);
                    if (!res.cfg_condition)
                    {
                        res.cfg_condition = xmalloc(strlen(cfg_name) + 32);
                        sprintf(res.cfg_condition, "defined(ZC_CFG_%s)", cfg_name);
                    }
                    else
                    {
                        char *old = res.cfg_condition;
                        res.cfg_condition = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                        sprintf(res.cfg_condition, "%s && defined(ZC_CFG_%s)", old, cfg_name);
                        free(old);
                    }
                    free(cfg_name);
                }
                else
                {
                    zpanic_at(cfg_tok, "Expected define name in @cfg(NAME)");
                }
                if (lexer_next(l).type != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after @cfg(...)");
                }
            }
            else
            {
                zpanic_at(lexer_peek(l), "@cfg requires a condition: @cfg(NAME)");
            }
        }
        else if (0 == strncmp(attr.start, "derive", 6) && 6 == attr.len)
        {
            if (lexer_peek(l).type == TOK_LPAREN)
            {
                lexer_next(l);
                while (1)
                {
                    Token inner_t = lexer_next(l);
                    if (inner_t.type != TOK_IDENT)
                    {
                        zpanic_at(inner_t, "Expected trait name in @derive");
                    }
                    if (res.derived_count < 32)
                    {
                        res.derived_traits[res.derived_count++] = token_strdup(inner_t);
                    }
                    if (lexer_peek(l).type == TOK_COMMA)
                    {
                        lexer_next(l);
                    }
                    else
                    {
                        break;
                    }
                }
                if (lexer_next(l).type != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected ) after derive traits");
                }
            }
            else
            {
                zpanic_at(lexer_peek(l), "@derive requires traits: @derive(Debug, Clone)");
            }
        }
        else
        {
            // Checking for CUDA attributes...
            if (0 == strncmp(attr.start, "global", 6) && 6 == attr.len)
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
                new_attr->arg_count = 0;
                new_attr->next = res.custom_attributes; // Prepend
                res.custom_attributes = new_attr;

                if (lexer_peek(l).type == TOK_LPAREN)
                {
                    lexer_next(l); // eat (
                    while (1)
                    {
                        Token inner_t = lexer_next(l);
                        new_attr->args =
                            realloc(new_attr->args, sizeof(char *) * (new_attr->arg_count + 1));

                        if (inner_t.type == TOK_STRING)
                        {
                            new_attr->args[new_attr->arg_count++] = token_strdup(inner_t);
                        }
                        else
                        {
                            new_attr->args[new_attr->arg_count++] = token_strdup(inner_t);
                        }

                        if (lexer_peek(l).type == TOK_COMMA)
                        {
                            lexer_next(l);
                        }
                        else if (lexer_peek(l).type == TOK_RPAREN)
                        {
                            break;
                        }
                        else
                        {
                            zpanic_at(lexer_peek(l), "Expected , or ) in attribute args");
                        }
                    }
                    if (lexer_next(l).type != TOK_RPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected )");
                    }
                }
            }
        }

        t = lexer_peek(l);
    }
    return res;
}

ASTNode *parse_program_nodes(ParserContext *ctx, Lexer *l)
{
    ASTNode *h = 0, *tl = 0;
    while (1)
    {
        if (g_config.keep_comments)
        {
            l->emit_comments = 1;
            Token tk = lexer_peek(l);
            if (tk.type == TOK_COMMENT)
            {
                lexer_next(l);        // consume
                l->emit_comments = 0; // reset
                ASTNode *node = ast_create(NODE_AST_COMMENT);
                node->comment.content = xmalloc(tk.len + 1);
                strncpy(node->comment.content, tk.start, tk.len);
                node->comment.content[tk.len] = 0;

                if (!h)
                {
                    h = node;
                }
                else
                {
                    tl->next = node;
                }
                tl = node;
                continue;
            }
            l->emit_comments = 0;
        }

        skip_comments(l);
        Token t = lexer_peek(l);
        if (t.type == TOK_EOF)
        {
            break;
        }

        if (t.type == TOK_COMPTIME)
        {
            ASTNode *gen = parse_comptime(ctx, l);
            if (gen)
            {
                if (!h)
                {
                    h = gen;
                }
                else
                {
                    tl->next = gen;
                }
                if (!tl)
                {
                    tl = gen;
                }
                while (tl->next)
                {
                    tl = tl->next;
                }
            }
            continue;
        }

        ASTNode *s = 0;

        int attr_required = 0;
        int attr_deprecated = 0;
        int attr_inline = 0;
        int attr_pure = 0;
        int attr_noreturn = 0;
        int attr_cold = 0;
        int attr_hot = 0;
        int attr_packed = 0;
        int attr_align = 0;
        int attr_noinline = 0;
        int attr_constructor = 0;
        int attr_destructor = 0;
        int attr_unused = 0;
        int attr_weak = 0;
        int attr_export = 0;
        int attr_comptime = 0;
        int attr_cuda_global = 0;   // @global -> __global__
        int attr_cuda_device = 0;   // @device -> __device__
        int attr_cuda_host = 0;     // @host -> __host__
        char *cfg_condition = NULL; // @cfg() conditional compilation
        char *deprecated_msg = NULL;
        char *attr_section = NULL;
        int attr_vector_size = 0;

        char *derived_traits[32];
        int derived_count = 0;

        Attribute *current_custom_attributes = NULL;

        while (t.type == TOK_AT)
        {
            lexer_next(l);
            Token attr = lexer_next(l);
            if (attr.type != TOK_IDENT && attr.type != TOK_COMPTIME && attr.type != TOK_ALIAS)
            {
                zpanic_at(attr, "Expected attribute name after @");
            }

            if (0 == strncmp(attr.start, "required", 8) && 8 == attr.len)
            {
                attr_required = 1;
            }
            else if (0 == strncmp(attr.start, "deprecated", 10) && 10 == attr.len)
            {
                attr_deprecated = 1;
                if (lexer_peek(l).type == TOK_LPAREN)
                {
                    lexer_next(l);
                    Token msg = lexer_next(l);
                    if (msg.type == TOK_STRING)
                    {
                        deprecated_msg = xmalloc(msg.len - 1);
                        strncpy(deprecated_msg, msg.start + 1, msg.len - 2);
                        deprecated_msg[msg.len - 2] = 0;
                    }
                    if (lexer_next(l).type != TOK_RPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ) after deprecated message");
                    }
                }
            }
            else if (0 == strncmp(attr.start, "inline", 6) && 6 == attr.len)
            {
                attr_inline = 1;
            }
            else if (0 == strncmp(attr.start, "noinline", 8) && 8 == attr.len)
            {
                attr_noinline = 1;
            }
            else if (0 == strncmp(attr.start, "pure", 4) && 4 == attr.len)
            {
                attr_pure = 1;
            }
            else if (0 == strncmp(attr.start, "noreturn", 8) && 8 == attr.len)
            {
                attr_noreturn = 1;
            }
            else if (0 == strncmp(attr.start, "cold", 4) && 4 == attr.len)
            {
                attr_cold = 1;
            }
            else if (0 == strncmp(attr.start, "hot", 3) && 3 == attr.len)
            {
                attr_hot = 1;
            }
            else if (0 == strncmp(attr.start, "constructor", 11) && 11 == attr.len)
            {
                attr_constructor = 1;
            }
            else if (0 == strncmp(attr.start, "destructor", 10) && 10 == attr.len)
            {
                attr_destructor = 1;
            }
            else if (0 == strncmp(attr.start, "unused", 6) && 6 == attr.len)
            {
                attr_unused = 1;
            }
            else if (0 == strncmp(attr.start, "weak", 4) && 4 == attr.len)
            {
                attr_weak = 1;
            }
            else if (0 == strncmp(attr.start, "export", 6) && 6 == attr.len)
            {
                attr_export = 1;
            }
            else if (0 == strncmp(attr.start, "comptime", 8) && 8 == attr.len)
            {
                attr_comptime = 1;
            }
            else if (0 == strncmp(attr.start, "section", 7) && 7 == attr.len)
            {
                if (lexer_peek(l).type == TOK_LPAREN)
                {
                    lexer_next(l);
                    Token sec = lexer_next(l);
                    if (sec.type == TOK_STRING)
                    {
                        attr_section = xmalloc(sec.len - 1);
                        strncpy(attr_section, sec.start + 1, sec.len - 2);
                        attr_section[sec.len - 2] = 0;
                    }
                    if (lexer_next(l).type != TOK_RPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ) after section name");
                    }
                }
                else
                {
                    zpanic_at(lexer_peek(l), "@section requires a name: @section(\"name\")");
                }
            }
            else if (0 == strncmp(attr.start, "vector", 6) && 6 == attr.len)
            {
                if (lexer_peek(l).type == TOK_LPAREN)
                {
                    lexer_next(l);
                    Token num = lexer_next(l);
                    if (num.type == TOK_INT)
                    {
                        char *tmp = token_strdup(num);
                        attr_vector_size = atoi(tmp);
                        free(tmp);
                    }
                    if (lexer_next(l).type != TOK_RPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ) after vector size");
                    }
                }
            }
            else if (0 == strncmp(attr.start, "packed", 6) && 6 == attr.len)
            {
                attr_packed = 1;
            }
            else if (0 == strncmp(attr.start, "align", 5) && 5 == attr.len)
            {
                if (lexer_peek(l).type == TOK_LPAREN)
                {
                    lexer_next(l);
                    Token num = lexer_next(l);
                    if (num.type == TOK_INT)
                    {
                        attr_align = atoi(num.start);
                    }
                    if (lexer_next(l).type != TOK_RPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ) after align value");
                    }
                }
                else
                {
                    zpanic_at(lexer_peek(l), "@align requires a value: @align(N)");
                }
            }
            else if (0 == strncmp(attr.start, "cfg", 3) && 3 == attr.len)
            {
                if (lexer_peek(l).type == TOK_LPAREN)
                {
                    lexer_next(l);
                    Token cfg_tok = lexer_next(l);
                    if ((cfg_tok.type == TOK_NOT ||
                         (cfg_tok.type == TOK_IDENT && cfg_tok.len == 3 &&
                          strncmp(cfg_tok.start, "not", 3) == 0)))
                    {
                        if (lexer_peek(l).type != TOK_LPAREN)
                        {
                            zpanic_at(lexer_peek(l), "Expected ( after not in @cfg(not(...))");
                        }
                        lexer_next(l);
                        Token name_tok = lexer_next(l);
                        if (name_tok.type != TOK_IDENT)
                        {
                            zpanic_at(name_tok, "Expected define name in @cfg(not(NAME))");
                        }
                        char *cfg_name = token_strdup(name_tok);
                        if (!cfg_condition)
                        {
                            cfg_condition = xmalloc(strlen(cfg_name) + 32);
                            sprintf(cfg_condition, "!defined(ZC_CFG_%s)", cfg_name);
                        }
                        else
                        {
                            char *old = cfg_condition;
                            cfg_condition = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                            sprintf(cfg_condition, "%s && !defined(ZC_CFG_%s)", old, cfg_name);
                            free(old);
                        }
                        free(cfg_name);
                        if (lexer_next(l).type != TOK_RPAREN)
                        {
                            zpanic_at(lexer_peek(l), "Expected ) after name in @cfg(not(NAME))");
                        }
                    }
                    else if (cfg_tok.type == TOK_IDENT && cfg_tok.len == 3 &&
                             strncmp(cfg_tok.start, "any", 3) == 0)
                    {
                        if (lexer_peek(l).type != TOK_LPAREN)
                        {
                            zpanic_at(lexer_peek(l), "Expected ( after any in @cfg(any(...))");
                        }
                        lexer_next(l);
                        char *any_cond = NULL;
                        while (1)
                        {
                            Token inner_t = lexer_next(l);
                            if ((inner_t.type == TOK_NOT ||
                                 (inner_t.type == TOK_IDENT && inner_t.len == 3 &&
                                  strncmp(inner_t.start, "not", 3) == 0)))
                            {
                                if (lexer_next(l).type != TOK_LPAREN)
                                {
                                    zpanic_at(lexer_peek(l), "Expected ( after not");
                                }
                                Token nt = lexer_next(l);
                                if (nt.type != TOK_IDENT)
                                {
                                    zpanic_at(nt, "Expected define name");
                                }
                                char *cfg_name = token_strdup(nt);
                                if (!any_cond)
                                {
                                    any_cond = xmalloc(strlen(cfg_name) + 32);
                                    sprintf(any_cond, "!defined(ZC_CFG_%s)", cfg_name);
                                }
                                else
                                {
                                    char *old = any_cond;
                                    any_cond = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                                    sprintf(any_cond, "%s || !defined(ZC_CFG_%s)", old, cfg_name);
                                    free(old);
                                }
                                free(cfg_name);
                                if (lexer_next(l).type != TOK_RPAREN)
                                {
                                    zpanic_at(lexer_peek(l), "Expected )");
                                }
                            }
                            else if (inner_t.type == TOK_IDENT)
                            {
                                char *cfg_name = token_strdup(inner_t);
                                if (!any_cond)
                                {
                                    any_cond = xmalloc(strlen(cfg_name) + 32);
                                    sprintf(any_cond, "defined(ZC_CFG_%s)", cfg_name);
                                }
                                else
                                {
                                    char *old = any_cond;
                                    any_cond = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                                    sprintf(any_cond, "%s || defined(ZC_CFG_%s)", old, cfg_name);
                                    free(old);
                                }
                                free(cfg_name);
                            }
                            else
                            {
                                zpanic_at(inner_t, "Expected define name in @cfg(any(...))");
                            }
                            if (lexer_peek(l).type == TOK_COMMA)
                            {
                                lexer_next(l);
                            }
                            else
                            {
                                break;
                            }
                        }
                        if (lexer_next(l).type != TOK_RPAREN)
                        {
                            zpanic_at(lexer_peek(l), "Expected ) after any(...)");
                        }
                        if (any_cond)
                        {
                            if (!cfg_condition)
                            {
                                cfg_condition = xmalloc(strlen(any_cond) + 32);
                                sprintf(cfg_condition, "(%s)", any_cond);
                            }
                            else
                            {
                                char *old = cfg_condition;
                                cfg_condition = xmalloc(strlen(old) + strlen(any_cond) + 32);
                                sprintf(cfg_condition, "%s && (%s)", old, any_cond);
                                free(old);
                            }
                            free(any_cond);
                        }
                    }
                    else if (cfg_tok.type == TOK_IDENT && cfg_tok.len == 3 &&
                             strncmp(cfg_tok.start, "all", 3) == 0)
                    {
                        if (lexer_peek(l).type != TOK_LPAREN)
                        {
                            zpanic_at(lexer_peek(l), "Expected ( after all in @cfg(all(...))");
                        }
                        lexer_next(l);
                        char *all_cond = NULL;
                        while (1)
                        {
                            Token inner_t = lexer_next(l);
                            if ((inner_t.type == TOK_NOT ||
                                 (inner_t.type == TOK_IDENT && inner_t.len == 3 &&
                                  strncmp(inner_t.start, "not", 3) == 0)))
                            {
                                if (lexer_next(l).type != TOK_LPAREN)
                                {
                                    zpanic_at(lexer_peek(l), "Expected ( after not");
                                }
                                Token nt = lexer_next(l);
                                if (nt.type != TOK_IDENT)
                                {
                                    zpanic_at(nt, "Expected define name");
                                }
                                char *cfg_name = token_strdup(nt);
                                if (!all_cond)
                                {
                                    all_cond = xmalloc(strlen(cfg_name) + 32);
                                    sprintf(all_cond, "!defined(ZC_CFG_%s)", cfg_name);
                                }
                                else
                                {
                                    char *old = all_cond;
                                    all_cond = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                                    sprintf(all_cond, "%s && !defined(ZC_CFG_%s)", old, cfg_name);
                                    free(old);
                                }
                                free(cfg_name);
                                if (lexer_next(l).type != TOK_RPAREN)
                                {
                                    zpanic_at(lexer_peek(l), "Expected )");
                                }
                            }
                            else if (inner_t.type == TOK_IDENT)
                            {
                                char *cfg_name = token_strdup(inner_t);
                                if (!all_cond)
                                {
                                    all_cond = xmalloc(strlen(cfg_name) + 32);
                                    sprintf(all_cond, "defined(ZC_CFG_%s)", cfg_name);
                                }
                                else
                                {
                                    char *old = all_cond;
                                    all_cond = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                                    sprintf(all_cond, "%s && defined(ZC_CFG_%s)", old, cfg_name);
                                    free(old);
                                }
                                free(cfg_name);
                            }
                            else
                            {
                                zpanic_at(inner_t, "Expected define name in @cfg(all(...))");
                            }
                            if (lexer_peek(l).type == TOK_COMMA)
                            {
                                lexer_next(l);
                            }
                            else
                            {
                                break;
                            }
                        }
                        if (lexer_next(l).type != TOK_RPAREN)
                        {
                            zpanic_at(lexer_peek(l), "Expected ) after all(...)");
                        }
                        if (all_cond)
                        {
                            if (!cfg_condition)
                            {
                                cfg_condition = xmalloc(strlen(all_cond) + 32);
                                sprintf(cfg_condition, "(%s)", all_cond);
                            }
                            else
                            {
                                char *old = cfg_condition;
                                cfg_condition = xmalloc(strlen(old) + strlen(all_cond) + 32);
                                sprintf(cfg_condition, "%s && (%s)", old, all_cond);
                                free(old);
                            }
                            free(all_cond);
                        }
                    }
                    else if (cfg_tok.type == TOK_IDENT)
                    {
                        // @cfg(NAME)
                        char *cfg_name = token_strdup(cfg_tok);
                        if (!cfg_condition)
                        {
                            cfg_condition = xmalloc(strlen(cfg_name) + 32);
                            sprintf(cfg_condition, "defined(ZC_CFG_%s)", cfg_name);
                        }
                        else
                        {
                            char *old = cfg_condition;
                            cfg_condition = xmalloc(strlen(old) + strlen(cfg_name) + 32);
                            sprintf(cfg_condition, "%s && defined(ZC_CFG_%s)", old, cfg_name);
                            free(old);
                        }
                        free(cfg_name);
                    }
                    else
                    {
                        zpanic_at(cfg_tok, "Expected define name in @cfg(NAME)");
                    }
                    if (lexer_next(l).type != TOK_RPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ) after @cfg(...)");
                    }
                }
                else
                {
                    zpanic_at(lexer_peek(l), "@cfg requires a condition: @cfg(NAME)");
                }
            }
            else if (0 == strncmp(attr.start, "derive", 6) && 6 == attr.len)
            {
                if (lexer_peek(l).type == TOK_LPAREN)
                {
                    lexer_next(l);
                    while (1)
                    {
                        Token inner_t = lexer_next(l);
                        if (inner_t.type != TOK_IDENT)
                        {
                            zpanic_at(inner_t, "Expected trait name in @derive");
                        }
                        if (derived_count < 32)
                        {
                            derived_traits[derived_count++] = token_strdup(inner_t);
                        }
                        if (lexer_peek(l).type == TOK_COMMA)
                        {
                            lexer_next(l);
                        }
                        else
                        {
                            break;
                        }
                    }
                    if (lexer_next(l).type != TOK_RPAREN)
                    {
                        zpanic_at(lexer_peek(l), "Expected ) after derive traits");
                    }
                }
                else
                {
                    zpanic_at(lexer_peek(l), "@derive requires traits: @derive(Debug, Clone)");
                }
            }
            else
            {
                // Checking for CUDA attributes...
                if (0 == strncmp(attr.start, "global", 6) && 6 == attr.len)
                {
                    attr_cuda_global = 1;
                }
                else if (0 == strncmp(attr.start, "device", 6) && 6 == attr.len)
                {
                    attr_cuda_device = 1;
                }
                else if (0 == strncmp(attr.start, "host", 4) && 4 == attr.len)
                {
                    attr_cuda_host = 1;
                }
                else
                {
                    Attribute *new_attr = xmalloc(sizeof(Attribute));
                    new_attr->name = token_strdup(attr);
                    new_attr->args = NULL;
                    new_attr->arg_count = 0;
                    new_attr->next = current_custom_attributes; // Prepend
                    current_custom_attributes = new_attr;

                    if (lexer_peek(l).type == TOK_LPAREN)
                    {
                        lexer_next(l); // eat (
                        while (1)
                        {
                            Token inner_t = lexer_next(l);
                            new_attr->args =
                                realloc(new_attr->args, sizeof(char *) * (new_attr->arg_count + 1));

                            if (inner_t.type == TOK_STRING)
                            {
                                new_attr->args[new_attr->arg_count++] = token_strdup(inner_t);
                            }
                            else
                            {
                                new_attr->args[new_attr->arg_count++] = token_strdup(inner_t);
                            }

                            if (lexer_peek(l).type == TOK_COMMA)
                            {
                                lexer_next(l);
                            }
                            else if (lexer_peek(l).type == TOK_RPAREN)
                            {
                                break;
                            }
                            else
                            {
                                zpanic_at(lexer_peek(l), "Expected , or ) in attribute args");
                            }
                        }
                        if (lexer_next(l).type != TOK_RPAREN)
                        {
                            zpanic_at(lexer_peek(l), "Expected )");
                        }
                    }
                }
            }

            t = lexer_peek(l);
        }

        // Removed cfg_skip handling here

        if (t.type == TOK_PREPROC)
        {
            lexer_next(l);
            char *content = xmalloc(t.len + 2);
            strncpy(content, t.start, t.len);
            content[t.len] = '\n';
            content[t.len + 1] = 0;
            s = ast_create(NODE_RAW_STMT);
            s->token = t;
            s->raw_stmt.content = content;

            // Attempt to parse simple integer/constant macros
            try_parse_macro_const(ctx, content);
        }
        else if (t.type == TOK_DEF)
        {
            s = parse_def(ctx, l);
        }
        else if (t.type == TOK_IDENT)
        {
            // Inline function: inline fn name(...) { }
            if (0 == strncmp(t.start, "inline", 6) && 6 == t.len)
            {
                lexer_next(l);
                Token next = lexer_peek(l);
                if (next.type == TOK_IDENT && 2 == next.len && 0 == strncmp(next.start, "fn", 2))
                {
                    s = parse_function(ctx, l, 0);
                    attr_inline = 1;
                }
                else
                {
                    zpanic_at(next, "Expected 'fn' after 'inline'");
                }
            }
            else if (0 == strncmp(t.start, "fn", 2) && 2 == t.len)
            {
                s = parse_function(ctx, l, 0);
            }
            else if (0 == strncmp(t.start, "struct", 6) && 6 == t.len)
            {
                s = parse_struct(ctx, l, 0, 0);
                if (s && s->type == NODE_STRUCT)
                {
                    s->strct.is_packed = attr_packed;
                    s->strct.align = attr_align;
                    s->strct.attributes = current_custom_attributes;

                    if (attr_vector_size > 0)
                    {
                        s->type_info->kind = TYPE_VECTOR;
                        s->type_info->array_size = attr_vector_size;
                    }
                }
            }
            else if (0 == strncmp(t.start, "enum", 4) && 4 == t.len)
            {
                s = parse_enum(ctx, l);
                if (s && s->type == NODE_ENUM)
                {
                    if (derived_count > 0)
                    {
                        ASTNode *impls =
                            generate_derive_impls(ctx, s, derived_traits, derived_count);
                        s->next = impls;
                    }
                }
            }
            else if (t.len == 4 && strncmp(t.start, "impl", 4) == 0)
            {
                s = parse_impl(ctx, l);
            }
            else if (t.len == 5 && strncmp(t.start, "trait", 5) == 0)
            {
                s = parse_trait(ctx, l);
            }
            else if (t.len == 7 && strncmp(t.start, "include", 7) == 0)
            {
                s = parse_include(ctx, l);
            }
            else if (t.len == 6 && strncmp(t.start, "import", 6) == 0)
            {
                s = parse_import(ctx, l);
            }
            else if (t.len == 3 && strncmp(t.start, "let", 3) == 0)
            {
                s = parse_var_decl(ctx, l);
            }
            else if (t.len == 3 && strncmp(t.start, "var", 3) == 0)
            {
                zpanic_at(t, "'var' is deprecated. Use 'let' instead.");
            }
            else if (t.len == 5 && strncmp(t.start, "const", 5) == 0)
            {
                zpanic_at(t, "'const' for declarations is deprecated. Use 'def' for constants or "
                             "'let x: const T' for read-only variables.");
            }
            else if (t.len == 6 && strncmp(t.start, "extern", 6) == 0)
            {
                lexer_next(l);

                Token peek = lexer_peek(l);
                if (peek.type == TOK_IDENT && peek.len == 2 && strncmp(peek.start, "fn", 2) == 0)
                {
                    s = parse_function(ctx, l, 0);
                }
                else if (peek.type == TOK_IDENT && peek.len == 6 &&
                         strncmp(peek.start, "struct", 6) == 0)
                {
                    // extern struct Name; -> opaque struct declaration
                    s = parse_struct(ctx, l, 0, 1);
                    register_extern_symbol(ctx, s->strct.name);
                    s = NULL;
                }
                else if ((peek.type == TOK_IDENT && peek.len == 5 &&
                          strncmp(peek.start, "union", 5) == 0) ||
                         peek.type == TOK_UNION)
                {
                    // extern union Name; -> opaque union declaration
                    s = parse_struct(ctx, l, 1, 1);
                    register_extern_symbol(ctx, s->strct.name);
                    s = NULL;
                }
                else
                {
                    while (1)
                    {
                        Token sym = lexer_next(l);
                        if (sym.type != TOK_IDENT)
                        {
                            break;
                        }

                        char *name = token_strdup(sym);
                        register_extern_symbol(ctx, name);

                        Token next = lexer_peek(l);
                        if (next.type == TOK_COMMA)
                        {
                            lexer_next(l);
                        }
                        else
                        {
                            break;
                        }
                    }

                    if (lexer_peek(l).type == TOK_SEMICOLON)
                    {
                        lexer_next(l);
                    }
                    continue;
                }
            }
            else if (0 == strncmp(t.start, "type", 4) && 4 == t.len)
            {
                s = parse_type_alias(ctx, l, 0);
            }
            else if (0 == strncmp(t.start, "raw", 3) && 3 == t.len)
            {
                lexer_next(l);
                if (lexer_peek(l).type != TOK_LBRACE)
                {
                    zpanic_at(lexer_peek(l), "Expected { after raw");
                }
                lexer_next(l);

                const char *start = l->src + l->pos;

                int depth = 1;
                while (depth > 0)
                {
                    Token inner_t = lexer_next(l);
                    if (inner_t.type == TOK_EOF)
                    {
                        zpanic_at(inner_t, "Unexpected EOF in raw block");
                    }
                    if (inner_t.type == TOK_LBRACE)
                    {
                        depth++;
                    }
                    if (inner_t.type == TOK_RBRACE)
                    {
                        depth--;
                    }
                }

                const char *end = l->src + l->pos - 1;
                size_t len = end - start;

                char *content = xmalloc(len + 1);
                memcpy(content, start, len);
                content[len] = 0;

                s = ast_create(NODE_RAW_STMT);
                s->token = t;
                s->raw_stmt.content = normalize_raw_content(content);
                free(content);
            }
            else
            {
                lexer_next(l);
            }
        }
        else if (t.type == TOK_OPAQUE)
        {
            lexer_next(l); // eat opaque
            Token next = lexer_peek(l);
            if (0 == strncmp(next.start, "struct", 6) && 6 == next.len)
            {
                s = parse_struct(ctx, l, 0, 1);
                if (s && s->type == NODE_STRUCT)
                {
                    s->strct.is_packed = attr_packed;
                    s->strct.align = attr_align;
                }
            }
            else if (next.type == TOK_ALIAS)
            {
                s = parse_type_alias(ctx, l, 1);
            }
            else
            {
                zpanic_at(next, "Expected 'struct' or 'alias' after 'opaque'");
            }
        }
        else if (t.type == TOK_ALIAS)
        {
            s = parse_type_alias(ctx, l, 0);
        }
        else if (t.type == TOK_ASYNC)
        {
            lexer_next(l);
            Token next = lexer_peek(l);
            if (0 == strncmp(next.start, "fn", 2) && 2 == next.len)
            {
                s = parse_function(ctx, l, 1);
            }
            else
            {
                zpanic_at(next, "Expected 'fn' after 'async'");
            }
        }
        else if (t.type == TOK_UNION)
        {
            s = parse_struct(ctx, l, 1, 0);
        }
        else if (t.type == TOK_TRAIT)
        {
            s = parse_trait(ctx, l);
        }
        else if (t.type == TOK_IMPL)
        {
            s = parse_impl(ctx, l);
        }
        else if (t.type == TOK_TEST)
        {
            s = parse_test(ctx, l);
        }
        else
        {
            lexer_next(l);
        }

        if (s && s->type == NODE_FUNCTION)
        {
            s->func.required = attr_required;
            s->func.is_inline = attr_inline || s->func.is_inline;
            s->func.noinline = attr_noinline;
            s->func.constructor = attr_constructor;
            s->func.destructor = attr_destructor;
            s->func.unused = attr_unused;
            s->func.weak = attr_weak;
            s->func.is_export = attr_export;
            s->func.cold = attr_cold;
            s->func.hot = attr_hot;
            s->func.noreturn = attr_noreturn;
            s->func.pure = attr_pure;
            s->func.section = attr_section;
            s->func.is_comptime = attr_comptime;
            s->func.cuda_global = attr_cuda_global;
            s->func.cuda_device = attr_cuda_device;
            s->func.cuda_host = attr_cuda_host;
            s->func.attributes = current_custom_attributes;

            if (attr_deprecated && s->func.name)
            {
                register_deprecated_func(ctx, s->func.name, deprecated_msg);
            }

            if (attr_required && s->func.name)
            {
                FuncSig *sig = find_func(ctx, s->func.name);
                if (sig)
                {
                    sig->required = 1;
                }
            }
        }
        else if (s && s->type == NODE_STRUCT)
        {
            s->strct.is_export = attr_export;
            s->strct.attributes = current_custom_attributes;
            s->strct.is_packed = attr_packed || s->strct.is_packed;
            if (attr_align)
            {
                s->strct.align = attr_align;
            }
            if (attr_deprecated && s->strct.name)
            {
                register_deprecated_func(ctx, s->strct.name, deprecated_msg);
            }
            if (derived_count > 0)
            {
                ASTNode *impls = generate_derive_impls(ctx, s, derived_traits, derived_count);
                s->next = impls;
            }
        }

        if (s)
        {
            s->cfg_condition = cfg_condition;

            if (!h)
            {
                h = s;
            }
            else
            {
                tl->next = s;
            }
            tl = s;
            while (tl->next)
            {
                tl = tl->next;
            }
        }
        else if (cfg_condition)
        {
            free(cfg_condition);
        }
    }
    return h;
}

ASTNode *parse_program(ParserContext *ctx, Lexer *l)
{
    g_parser_ctx = ctx;
    enter_scope(ctx);
    register_builtins(ctx);

    ASTNode *r = ast_create(NODE_ROOT);
    r->root.children = parse_program_nodes(ctx, l);
    return r;
}

static ASTNode *generate_derive_impls(ParserContext *ctx, ASTNode *strct, char **traits, int count)
{
    ASTNode *head = NULL, *tail = NULL;
    char *name = strct->strct.name;

    for (int i = 0; i < count; i++)
    {
        char *trait = traits[i];
        char *code = NULL;

        if (0 == strcmp(trait, "Clone"))
        {
            code = xmalloc(1024);
            sprintf(code, "impl %s { fn clone(self) -> %s { return *self; } }", name, name);
        }
        else if (0 == strcmp(trait, "Eq"))
        {
            char body[4096];
            body[0] = 0;

            if (strct->type == NODE_ENUM)
            {
                // Simple Enum equality (tag comparison)
                // Generate Eq impl for Enum

                sprintf(body, "return self.tag == other.tag;");
            }
            else
            {
                ASTNode *f = strct->strct.fields;
                int first = 1;
                strcat(body, "return ");
                while (f)
                {
                    if (f->type == NODE_FIELD)
                    {
                        char *fn = f->field.name;
                        char *ft = f->field.type;
                        if (!first)
                        {
                            strcat(body, " && ");
                        }
                        char cmp[256];

                        // Detect pointer using type_info OR string check (fallback)
                        int is_ptr = 0;
                        if (f->type_info && f->type_info->kind == TYPE_POINTER)
                        {
                            is_ptr = 1;
                        }
                        // Fallback: check if type string ends with '*'
                        if (!is_ptr && ft && strchr(ft, '*'))
                        {
                            is_ptr = 1;
                        }

                        // Only look up struct def for non-pointer types
                        ASTNode *fdef = is_ptr ? NULL : find_struct_def(ctx, ft);

                        if (!is_ptr && fdef && fdef->type == NODE_ENUM)
                        {
                            // Enum field: compare tags
                            sprintf(cmp, "self.%s.tag == other.%s.tag", fn, fn);
                        }
                        else if (!is_ptr && fdef && fdef->type == NODE_STRUCT)
                        {
                            // Struct field: use __eq function
                            sprintf(cmp, "%s__eq(&self.%s, &other.%s)", ft, fn, fn);
                        }
                        else
                        {
                            // Primitive, POINTER, or unknown: use ==
                            sprintf(cmp, "self.%s == other.%s", fn, fn);
                        }
                        strcat(body, cmp);
                        first = 0;
                    }
                    f = f->next;
                }
                if (first)
                {
                    strcat(body, "true");
                }
                strcat(body, ";");
            }
            code = xmalloc(4096 + 1024);
            // Updated signature: other is a pointer T*
            sprintf(code, "impl %s { fn eq(self, other: %s*) -> bool { %s } }", name, name, body);
        }
        else if (0 == strcmp(trait, "Debug"))
        {
            // Simplistic Debug for now, I know.
            code = xmalloc(1024);
            sprintf(code, "impl %s { fn to_string(self) -> char* { return \"%s {{ ... }}\"; } }",
                    name, name);
        }
        else if (0 == strcmp(trait, "Copy"))
        {
            // Marker trait for Copy/Move semantics
            code = xmalloc(1024);
            sprintf(code, "impl Copy for %s {}", name);
        }
        else if (0 == strcmp(trait, "FromJson"))
        {
            // Generate from_json(j: JsonValue*) -> Result<StructName>
            // Only works for structs (not enums)
            if (strct->type != NODE_STRUCT)
            {
                zwarn_at(strct->token, "@derive(FromJson) only works on structs");
                continue;
            }

            char body[8192];
            body[0] = 0;

            // Track Vec<String> fields for forget calls
            char *vec_fields[32];
            int vec_field_count = 0;

            // Build field assignments
            ASTNode *f = strct->strct.fields;
            while (f)
            {
                if (f->type == NODE_FIELD)
                {
                    char *fn = f->field.name;
                    char *ft = f->field.type;
                    char assign[2048];

                    if (!fn || !ft)
                    {
                        f = f->next;
                        continue;
                    }

                    // Map types to appropriate get_* calls
                    int is_int_type = strcmp(ft, "int") == 0 || strcmp(ft, "int32_t") == 0 ||
                                      strcmp(ft, "i32") == 0 || strcmp(ft, "i64") == 0 ||
                                      strcmp(ft, "int64_t") == 0 || strcmp(ft, "u32") == 0 ||
                                      strcmp(ft, "uint32_t") == 0 || strcmp(ft, "u64") == 0 ||
                                      strcmp(ft, "uint64_t") == 0 || strcmp(ft, "usize") == 0 ||
                                      strcmp(ft, "size_t") == 0;
                    if (is_int_type)
                    {
                        sprintf(assign, "let _f_%s = (*j).get_int(\"%s\").unwrap_or(0);\n", fn, fn);
                    }
                    else if (strcmp(ft, "double") == 0)
                    {
                        sprintf(assign, "let _f_%s = (*j).get_float(\"%s\").unwrap_or(0.0);\n", fn,
                                fn);
                    }
                    else if (strcmp(ft, "bool") == 0)
                    {
                        sprintf(assign, "let _f_%s = (*j).get_bool(\"%s\").unwrap_or(false);\n", fn,
                                fn);
                    }
                    else if (strcmp(ft, "char*") == 0)
                    {
                        sprintf(assign, "let _f_%s = (*j).get_string(\"%s\").unwrap_or(\"\");\n",
                                fn, fn);
                    }
                    else if (strcmp(ft, "String") == 0)
                    {
                        sprintf(
                            assign,
                            "let _f_%s = String::new((*j).get_string(\"%s\").unwrap_or(\"\"));\n",
                            fn, fn);
                    }
                    else if (ft && strstr(ft, "Vec") && strstr(ft, "String"))
                    {
                        // Track this field for forget() call later
                        if (vec_field_count < 32)
                        {
                            vec_fields[vec_field_count++] = fn;
                        }
                        sprintf(
                            assign,
                            "let _f_%s = Vec<String>::new();\n"
                            "let _arr_%s = (*j).get_array(\"%s\");\n"
                            "if _arr_%s.is_some() {\n"
                            "  let _a_%s = _arr_%s.unwrap();\n"
                            "  for let _i_%s: usize = 0; _i_%s < _a_%s.len(); _i_%s = _i_%s + 1 {\n"
                            "    let _item_%s = _a_%s.at(_i_%s);\n"
                            "    if _item_%s.is_some() {\n"
                            "      let _str_%s = (*_item_%s.unwrap()).as_string();\n"
                            "      if _str_%s.is_some() {\n"
                            "        let _s_%s = String::new(_str_%s.unwrap());\n"
                            "        _f_%s.push(_s_%s); _s_%s.forget();\n"
                            "      }\n"
                            "    }\n"
                            "  }\n"
                            "}\n",
                            fn, fn, fn, fn, fn, fn, fn, fn, fn, fn, fn, fn, fn, fn, fn, fn, fn, fn,
                            fn, fn, fn, fn, fn);
                    }
                    else
                    {
                        // Nested struct: call NestedType::from_json recursively
                        sprintf(assign,
                                "let _opt_%s = (*j).get(\"%s\");\n"
                                "let _f_%s: %s;\n"
                                "if _opt_%s.is_some() { _f_%s = "
                                "%s::from_json(_opt_%s.unwrap()).unwrap(); }\n",
                                fn, fn, fn, ft, fn, fn, ft, fn);
                    }
                    strcat(body, assign);
                }
                f = f->next;
            }

            // Build struct initialization
            strcat(body, "return Result<");
            strcat(body, name);
            strcat(body, ">::Ok(");
            strcat(body, name);
            strcat(body, " { ");

            f = strct->strct.fields;
            int first = 1;
            while (f)
            {
                if (f->type == NODE_FIELD)
                {
                    if (!first)
                    {
                        strcat(body, ", ");
                    }
                    char init[128];
                    // Check if this is a Vec<String> field - clone it to avoid double-free
                    int is_vec_field = 0;
                    for (int vi = 0; vi < vec_field_count; vi++)
                    {
                        if (strcmp(vec_fields[vi], f->field.name) == 0)
                        {
                            is_vec_field = 1;
                            break;
                        }
                    }
                    if (is_vec_field)
                    {
                        sprintf(init, "%s: _f_%s.clone()", f->field.name, f->field.name);
                    }
                    else
                    {
                        sprintf(init, "%s: _f_%s", f->field.name, f->field.name);
                    }
                    strcat(body, init);
                    first = 0;
                }
                f = f->next;
            }
            strcat(body, " }); ");

            code = xmalloc(8192 + 1024);
            sprintf(code, "impl %s { fn from_json(j: JsonValue*) -> Result<%s> { %s } }", name,
                    name, body);
        }
        else if (0 == strcmp(trait, "ToJson"))
        {
            // Generate to_json(self) -> JsonValue
            // Only works for structs (not enums)
            if (strct->type != NODE_STRUCT)
            {
                zwarn_at(strct->token, "@derive(ToJson) only works on structs");
                continue;
            }

            char body[8192];
            strcpy(body, "let _obj = JsonValue::object();\n");

            ASTNode *f = strct->strct.fields;
            while (f)
            {
                if (f->type == NODE_FIELD)
                {
                    char *fn = f->field.name;
                    char *ft = f->field.type;
                    char set_call[2048];

                    if (!fn || !ft)
                    {
                        f = f->next;
                        continue;
                    }

                    int is_int_type = strcmp(ft, "int") == 0 || strcmp(ft, "int32_t") == 0 ||
                                      strcmp(ft, "i32") == 0 || strcmp(ft, "i64") == 0 ||
                                      strcmp(ft, "int64_t") == 0 || strcmp(ft, "u32") == 0 ||
                                      strcmp(ft, "uint32_t") == 0 || strcmp(ft, "u64") == 0 ||
                                      strcmp(ft, "uint64_t") == 0 || strcmp(ft, "usize") == 0 ||
                                      strcmp(ft, "size_t") == 0;
                    if (is_int_type)
                    {
                        sprintf(set_call, "_obj.set(\"%s\", JsonValue::number((double)self.%s));\n",
                                fn, fn);
                    }
                    else if (strcmp(ft, "double") == 0)
                    {
                        sprintf(set_call, "_obj.set(\"%s\", JsonValue::number(self.%s));\n", fn,
                                fn);
                    }
                    else if (strcmp(ft, "bool") == 0)
                    {
                        sprintf(set_call, "_obj.set(\"%s\", JsonValue::bool(self.%s));\n", fn, fn);
                    }
                    else if (strcmp(ft, "char*") == 0)
                    {
                        sprintf(set_call, "_obj.set(\"%s\", JsonValue::string(self.%s));\n", fn,
                                fn);
                    }
                    else if (strcmp(ft, "String") == 0)
                    {
                        sprintf(set_call, "_obj.set(\"%s\", JsonValue::string(self.%s.c_str()));\n",
                                fn, fn);
                    }
                    else if (ft && strstr(ft, "Vec") && strstr(ft, "String"))
                    {
                        sprintf(set_call,
                                "let _arr_%s = JsonValue::array();\n"
                                "for let _i_%s: usize = 0; _i_%s < self.%s.length(); _i_%s = _i_%s "
                                "+ 1 {\n"
                                "  _arr_%s.push(JsonValue::string(self.%s.get(_i_%s).c_str()));\n"
                                "}\n"
                                "_obj.set(\"%s\", _arr_%s);\n",
                                fn, fn, fn, fn, fn, fn, fn, fn, fn, fn, fn);
                    }
                    else
                    {
                        // Nested struct: call to_json recursively
                        sprintf(set_call, "_obj.set(\"%s\", self.%s.to_json());\n", fn, fn);
                    }
                    strcat(body, set_call);
                }
                f = f->next;
            }

            strcat(body, "return _obj;");

            code = xmalloc(8192 + 1024);
            sprintf(code, "impl %s { fn to_json(self) -> JsonValue { %s } }", name, body);
        }

        if (code)
        {
            Lexer tmp;
            lexer_init(&tmp, code);
            ASTNode *impl = parse_impl(ctx, &tmp);
            if (impl)
            {
                if (!head)
                {
                    head = impl;
                }
                else
                {
                    tail->next = impl;
                }
                tail = impl;
            }
        }
    }
    return head;
}
