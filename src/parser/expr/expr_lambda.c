// SPDX-License-Identifier: MIT
#include "parser.h"
#include "constants.h"
#include "expr_internal.h"
#include "ast/ast.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ASTNode *parse_arrow_lambda_single(ParserContext *ctx, Lexer *l, char *param_name,
                                   int default_capture_mode)
{
    ASTNode *lambda = ast_create(NODE_LAMBDA);
    lambda->lambda.count = 1;
    lambda->lambda.default_capture_mode = default_capture_mode;
    lambda->lambda.param_names = xmalloc(sizeof(char *));
    lambda->lambda.param_names[0] = param_name;
    lambda->lambda.count = 1;

    // Default param type: unknown (to be inferred)
    lambda->lambda.param_types = xmalloc(sizeof(char *));
    lambda->lambda.param_types[0] = NULL;

    // Create Type Info: unknown -> unknown
    Type *t = type_new(TYPE_FUNCTION);
    t->inner = type_new(TYPE_INT);
    t->args = xmalloc(sizeof(Type *));
    t->args[0] = type_new(TYPE_UNKNOWN); // Arg
    t->count = 1;
    lambda->type_info = t;

    // Register parameter in scope for body parsing
    enter_scope(ctx);
    add_symbol(ctx, param_name, NULL, t->args[0], 0);

    // Body parsing...
    ASTNode *body_block = NULL;
    if (lexer_peek(l).kind == TOK_LBRACE)
    {
        body_block = parse_block(ctx, l);
    }
    else
    {
        ASTNode *expr = parse_expression(ctx, l);
        ASTNode *ret = ast_create(NODE_RETURN);
        ret->ret.value = expr;
        body_block = ast_create(NODE_BLOCK);
        body_block->block.statements = ret;
    }
    lambda->lambda.body = body_block;

    // Attempt to infer return type from body if it's a simple return
    if (lambda->lambda.body->block.statements &&
        lambda->lambda.body->block.statements->kind == NODE_RETURN &&
        !lambda->lambda.body->block.statements->next)
    {
        ASTNode *ret_val = lambda->lambda.body->block.statements->ret.value;
        if (ret_val->type_info && ret_val->type_info->kind != TYPE_UNKNOWN)
        {
            if (param_name[0] == 'x')
            {
            }
            // Update return type
            if (t->inner)
            {
                zfree(t->inner);
            }
            t->inner = ret_val->type_info;
        }
    }

    // Update parameter types from symbol table (in case inference happened)
    ZenSymbol *sym = find_symbol_entry(ctx, param_name);
    if (sym && sym->type_info && sym->type_info->kind != TYPE_UNKNOWN)
    {
        zfree(lambda->lambda.param_types[0]);
        lambda->lambda.param_types[0] = type_to_string(sym->type_info);
        t->args[0] = sym->type_info;
    }
    else
    {
        if (lambda->lambda.param_types[0])
        {
            zfree(lambda->lambda.param_types[0]);
        }
        lambda->lambda.param_types[0] = xstrdup("unknown");
        t->args[0] = type_new(TYPE_UNKNOWN);

        if (sym)
        {
            sym->type_name = xstrdup("unknown");
            sym->type_info = t->args[0]; // Bind AST node type implicitly!
        }
    }

    lambda->lambda.return_type = type_to_string(t->inner);
    lambda->lambda.lambda_id = ctx->lambda_counter++;
    lambda->lambda.is_expression = 1;
    if (ctx->known_generics_count == 0)
    {
        register_lambda(ctx, lambda);
    }
    analyze_lambda_captures(ctx, lambda);
    exit_scope(ctx);
    return lambda;
}

ASTNode *parse_arrow_lambda_multi(ParserContext *ctx, Lexer *l, char **param_names,
                                  Type **param_types, int count, int default_capture_mode)
{
    ASTNode *lambda = ast_create(NODE_LAMBDA);
    lambda->lambda.param_names = param_names;
    lambda->lambda.count = count;
    lambda->lambda.default_capture_mode = default_capture_mode;

    // Type Info construction
    Type *t = type_new(TYPE_FUNCTION);
    t->inner = type_new(TYPE_INT);
    t->args = xmalloc(sizeof(Type *) * (size_t)(count));
    t->count = count;

    lambda->lambda.param_types = xmalloc(sizeof(char *) * (size_t)(count));
    for (int i = 0; i < count; i++)
    {
        if (param_types && param_types[i])
        {
            lambda->lambda.param_types[i] = type_to_string(param_types[i]);
            t->args[i] = param_types[i];
        }
        else
        {
            lambda->lambda.param_types[i] = xstrdup("unknown");
            t->args[i] = type_new(TYPE_UNKNOWN);
        }
    }
    lambda->type_info = t;

    // Register parameters in scope for body parsing
    enter_scope(ctx);
    for (int i = 0; i < count; i++)
    {
        if (param_types && param_types[i])
        {
            add_symbol(ctx, param_names[i], lambda->lambda.param_types[i], param_types[i], 0);
        }
        else
        {
            add_symbol(ctx, param_names[i], "unknown", t->args[i], 0);
        }
    }

    // Body parsing...
    ASTNode *body_block = NULL;
    if (lexer_peek(l).kind == TOK_LBRACE)
    {
        body_block = parse_block(ctx, l);
    }
    else
    {
        ASTNode *expr = parse_expression(ctx, l);
        ASTNode *ret = ast_create(NODE_RETURN);
        ret->ret.value = expr;
        body_block = ast_create(NODE_BLOCK);
        body_block->block.statements = ret;
    }
    lambda->lambda.body = body_block;
    lambda->lambda.return_type = xstrdup("unknown");
    lambda->lambda.lambda_id = ctx->lambda_counter++;
    lambda->lambda.is_expression = 1;
    if (ctx->known_generics_count == 0)
    {
        register_lambda(ctx, lambda);
    }
    analyze_lambda_captures(ctx, lambda);
    exit_scope(ctx);
    return lambda;
}

ASTNode *parse_tuple_expression(ParserContext *ctx, Lexer *l, const char *type_name,
                                ASTNode *first_elem)
{
    Token tk;
    if (!first_elem)
    {
        tk = lexer_next(l);
    }
    else
    {
        tk = first_elem->token;
    }

    ASTNode *head = first_elem, *prev = first_elem;
    int count = (first_elem ? 1 : 0);

    // If first_elem was provided, we might be at a comma or the closing paren
    if (first_elem && lexer_peek(l).kind == TOK_COMMA)
    {
        lexer_next(l); // eat comma
    }

    while (lexer_peek(l).kind != TOK_RPAREN)
    {
        ASTNode *element = parse_expression(ctx, l);
        if (head == NULL)
        {
            head = element;
        }
        else
        {
            prev->next = element;
        }
        prev = element;
        count++;

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
        zpanic_at(lexer_peek(l), "Expected ) after tuple literal");
        return NULL;
    }

    ASTNode *n = ast_create(NODE_EXPR_TUPLE_LITERAL);
    n->token = tk;
    n->tuple_literal.elements = head;
    n->tuple_literal.count = count;

    extern int g_is_indexing;
    if (g_is_indexing)
    {
        n->resolved_type = xstrdup("Tuple__Indexing");
        return n;
    }

    if (type_name)
    {
        n->resolved_type = xstrdup(type_name);
    }
    else
    {
        char sig[MAX_ERROR_MSG_LEN];
        sig[0] = 0;
        const char *type_names[256];
        int type_count = 0;
        ASTNode *curr = head;
        int i = 0;
        while (curr)
        {
            char *it = infer_type(ctx, curr);
            const char *type_name_to_append = it ? it : "int";

            if (type_count < 256)
            {
                type_names[type_count++] = type_name_to_append;
            }

            if (i > 0)
            {
                if (strlen(sig) + 4 < sizeof(sig))
                {
                    strncat(sig, "__", sizeof(sig) - strlen(sig) - 1);
                }
            }

            if (strlen(sig) + strlen(type_name_to_append) + 1 < sizeof(sig))
            {
                strncat(sig, type_name_to_append, sizeof(sig) - strlen(sig) - 1);
            }

            curr = curr->next;
            i++;
        }
        register_tuple_with_types(ctx, sig, type_names, type_count);
        char tuple_name[1024 + 16];
        char *clean_sig = sanitize_mangled_name(sig);
        snprintf(tuple_name, sizeof(tuple_name), "Tuple__%s", clean_sig);
        zfree(clean_sig);
        n->resolved_type = xstrdup(tuple_name);
    }
    return n;
}
