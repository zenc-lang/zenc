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

ASTNode *parse_trait(ParserContext *ctx, Lexer *l)
{
    lexer_next(l); // eat trait
    Token n = lexer_next(l);
    check_identifier(n);
    if (n.kind != TOK_IDENT)
    {
        zpanic_at(n, "Expected trait name");
        return NULL;
    }
    char *name = xmalloc(n.len + 1);
    strncpy(name, n.start, n.len);
    name[n.len] = 0;

    // Generics <T>
    char **generic_params = NULL;
    int generic_count = 0;
    if (lexer_peek(l).kind == TOK_LANGLE)
    {
        lexer_next(l);                                // eat <
        generic_params = xmalloc(sizeof(char *) * 8); // simplified
        while (1)
        {
            Token p = lexer_next(l);
            check_identifier(p);
            if (p.kind != TOK_IDENT)
            {
                zpanic_at(p, "Expected generic parameter name");
                return NULL;
            }
            generic_params[generic_count] = xmalloc(p.len + 1);
            strncpy(generic_params[generic_count], p.start, p.len);
            generic_params[generic_count][p.len] = 0;
            generic_count++;

            Token sep = lexer_peek(l);
            if (sep.kind == TOK_EOF)
            {
                zpanic_at(sep, "Expected '>' in generic parameter list");
                break;
            }
            if (sep.kind == TOK_COMMA)
            {
                lexer_next(l);
                continue;
            }
            else if (sep.kind == TOK_RANGLE)
            {
                lexer_next(l);
                break;
            }
            else
            {
                zpanic_at(sep, "Expected , or > in generic params");
                return NULL;
            }
        }
    }

    if (generic_count > 0)
    {
        for (int i = 0; i < generic_count; i++)
        {
            register_generic(ctx, generic_params[i]);
        }
    }

    lexer_next(l);

    ASTNode *methods = NULL, *tail = NULL;
    while (1)
    {
        skip_comments(l);
        if (lexer_peek(l).kind == TOK_RBRACE)
        {
            lexer_next(l);
            break;
        }
        if (lexer_peek(l).kind == TOK_EOF)
        {
            zpanic_at(lexer_peek(l), "Unexpected end of file in trait body");
            break;
        }

        DeclarationAttributes attrs = {0};
        if (lexer_peek(l).kind == TOK_AT)
        {
            attrs = parse_attributes(ctx, l);
        }

        // Parse method signature: fn name(args...) -> ret;
        Token ft = lexer_next(l);
        if (ft.kind != TOK_IDENT || strncmp(ft.start, "fn", 2) != 0)
        {
            zpanic_at(ft, "Expected fn in trait");
            return NULL;
        }

        Token mn = lexer_next(l);
        check_identifier(mn);
        char *mname = xmalloc(mn.len + 1);
        strncpy(mname, mn.start, mn.len);
        mname[mn.len] = 0;

        char **defaults = NULL;
        int count = 0;
        Type **arg_types = NULL;
        ASTNode **default_values = NULL;
        char **param_names = NULL;
        int is_varargs = 0;
        char *args = parse_and_convert_args(ctx, l, &defaults, &default_values, &count, &arg_types,
                                            &param_names, &is_varargs, NULL);

        char *ret = xstrdup("void");
        Type *ret_type_obj = type_new(TYPE_VOID);
        if (lexer_peek(l).kind == TOK_ARROW)
        {
            lexer_next(l);
            ret_type_obj = parse_type_formal(ctx, l);
            if (!ret_type_obj)
            {
                return NULL;
            }
            zfree(ret);
            ret = type_to_string(ret_type_obj);
        }

        if (lexer_peek(l).kind == TOK_SEMICOLON)
        {
            lexer_next(l);
            ASTNode *m = ast_create(NODE_FUNCTION);
            ATTACH_DOC_COMMENT(ctx, m);
            m->token = ft;
            m->func.param_names = param_names;
            m->func.name = mname;
            m->func.args = args;
            m->func.defaults = defaults;
            m->func.default_values = default_values;
            m->func.count = count;
            m->func.arg_types = arg_types;
            m->func.ret_type = ret;
            m->func.ret_type_info = ret_type_obj;
            m->func.body = NULL;
            m->link_name = attrs.link_name;
            m->cfg_condition = attrs.cfg_condition;
            m->func.pure = attrs.is_pure;
            if (!methods)
            {
                methods = m;
            }
            else
            {
                tail->next = m;
            }
            tail = m;
        }
        else
        {
            // Default implementation? Not supported yet.
            zpanic_at(lexer_peek(l), "Trait methods must end with ; for now");
            return NULL;
        }
    }

    ASTNode *n_node = ast_create(NODE_TRAIT);
    n_node->trait.name = name;
    n_node->trait.methods = methods;
    n_node->trait.generic_params = generic_params;
    n_node->trait.generic_param_count = generic_count;

    if (generic_count > 0)
    {
        ctx->known_generics_count -= generic_count;
    }

    register_trait(name);
    add_to_global_list(ctx, n_node); // Track for codegen (VTable emission)
    return n_node;
}
