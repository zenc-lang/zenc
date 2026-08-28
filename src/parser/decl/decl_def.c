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

ASTNode *parse_def(ParserContext *ctx, Lexer *l, int is_export)
{
    lexer_next(l); // eat def
    Token n = lexer_next(l);

    char *type_str = NULL;
    Type *type_obj = NULL;

    if (lexer_peek(l).kind == TOK_COLON)
    {
        lexer_next(l);
        // Hybrid Parse
        type_obj = parse_type_formal(ctx, l);
        if (!type_obj)
        {
            return NULL;
        }
        type_str = type_to_string(type_obj);
    }

    char *ns = token_strdup(n);
    if (!type_obj)
    {
        type_obj = type_new(TYPE_UNKNOWN); // Ensure we have an object
    }
    type_obj->is_const = 1;

    // Use is_def flag for manifest constants
    add_symbol_with_token(ctx, ns, type_str ? type_str : "unknown", type_obj, n, is_export);
    ZenSymbol *sym_entry = find_symbol_entry(ctx, ns);
    if (sym_entry)
    {
        sym_entry->kind = SYM_CONSTANT;
        sym_entry->is_def = 1;
        // is_const_value set only if literal
    }

    ASTNode *i = 0;
    if (lexer_peek(l).kind == TOK_OP && is_token(lexer_peek(l), "="))
    {
        lexer_next(l);

        Token tk = lexer_peek(l);
        if (tk.kind == TOK_LPAREN && type_str && strncmp(type_str, "Tuple__", 7) == 0)
        {
            char *code = parse_tuple_literal(ctx, l, type_str);
            i = ast_create(NODE_RAW_STMT);
            i->token = tk;
            i->raw_stmt.content = code;
        }
        else
        {
            i = parse_expression(ctx, l);

            // Try to evaluate constant expression for symbol table
            long long val;
            if (eval_const_int_expr(i, ctx, &val))
            {
                ZenSymbol *s = find_symbol_entry(ctx, ns);
                if (s)
                {
                    s->is_const_value = 1;
                    s->const_int_val = (int)val;
                    s->is_def = 1;

                    // Auto-infer type for def if unknown
                    if (!s->type_name || strcmp(s->type_name, "unknown") == 0)
                    {
                        if (s->type_name)
                        {
                            zfree(s->type_name);
                        }
                        s->type_name = xstrdup("int");
                        if (s->type_info)
                        {
                            zfree(s->type_info);
                        }
                        s->type_info = type_new(TYPE_INT);
                        s->type_info->is_const = 1;
                    }
                }
            }
        }
    }

    else
    {
        zpanic_at(n, "'def' constants must be initialized");
        return NULL;
    }

    if (lexer_peek(l).kind == TOK_SEMICOLON)
    {
        lexer_next(l);
    }

    ASTNode *o = ast_create(NODE_CONST);
    o->token = n;
    o->var_decl.name = ns;
    o->var_decl.type_str = type_str;
    o->var_decl.init_expr = i;
    // Store extra metadata if needed, but NODE_CONST usually suffices

    if (!ctx->current_scope || !ctx->current_scope->parent)
    {
        add_to_global_list(ctx, o);
    }

    return o;
}

ASTNode *parse_type_alias(ParserContext *ctx, Lexer *l, int is_opaque, int is_export)
{
    lexer_next(l); // consume 'type' or 'alias'
    Token n = lexer_next(l);
    if (n.kind != TOK_IDENT)
    {
        zpanic_at(n, "Expected identifier for type alias");
        return NULL;
    }

    lexer_next(l); // consume '='

    Type *t = parse_type_formal(ctx, l);
    if (!t)
    {
        return NULL;
    }
    char *o = type_to_string(t);

    if (lexer_peek(l).kind == TOK_SEMICOLON)
    {
        lexer_next(l);
    }

    ASTNode *node = ast_create(NODE_TYPE_ALIAS);
    node->type_alias.alias = xmalloc(n.len + 1);
    strncpy(node->type_alias.alias, n.start, n.len);
    node->type_alias.alias[n.len] = 0;
    node->type_alias.original_type = o;
    node->type_info = t;
    node->type_alias.is_opaque = is_opaque;
    node->type_alias.defined_in_file =
        ctx->current_filename ? xstrdup(ctx->current_filename) : NULL;

    register_type_alias(ctx, node->type_alias.alias, o, t, is_opaque,
                        node->type_alias.defined_in_file, n, is_export);

    return node;
}
