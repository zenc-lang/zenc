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

ASTNode *parse_function(ParserContext *ctx, Lexer *l, int is_async, int is_extern,
                        const char *link_name, int is_export)
{
    lexer_next(l);
    Token name_tok = lexer_next(l);
    check_identifier(name_tok);
    char *name = token_strdup(name_tok);

    if (is_async)
    {
        ctx->cg.has_async = 1;
    }

    // Check for C reserved word conflict
    if (is_c_reserved_word(name))
    {
        warn_c_reserved_word(name_tok, name);
    }

    char *gen_param = NULL;
    if (lexer_peek(l).kind == TOK_LANGLE)
    {
        lexer_next(l);

        size_t buf_size = 1024;
        char *buf = xmalloc(buf_size);
        buf[0] = 0;

        while (1)
        {
            Token gt = lexer_next(l);
            if (gt.kind != TOK_IDENT)
            {
                zpanic_at(gt, "Expected generic parameter name");
                return NULL;
            }
            char *s = token_strdup(gt);

            if (strlen(buf) + strlen(s) + 2 >= buf_size)
            {
                buf_size *= 2;
                buf = xrealloc(buf, buf_size);
            }

            if (buf[0])
            {
                strcat(buf, ",");
            }
            strcat(buf, s);

            // Check for shadowing
            if (is_known_generic(ctx, s))
            {
                zpanic_at(gt, "Generic parameter '%s' shadows an existing generic parameter", s);
                return NULL;
            }

            zfree(s);

            if (lexer_peek(l).kind == TOK_COMMA)
            {
                lexer_next(l);
                continue;
            }
            break;
        }

        if (lexer_next(l).kind != TOK_RANGLE)
        {
            zpanic_at(lexer_peek(l), "Expected >");
            return NULL;
        }
        gen_param = xstrdup(buf);
        zfree(buf);
    }

    // Register generic parameters so type parsing recognizes them
    int saved_generic_count = ctx->known_generics_count;
    if (gen_param)
    {
        char *tmp = xstrdup(gen_param);
        char *tok = strtok(tmp, ",");
        while (tok)
        {
            register_generic(ctx, tok);
            tok = strtok(NULL, ",");
        }
        zfree(tmp);
    }

    enter_scope(ctx);
    char **defaults;
    ASTNode **default_values;
    int count;
    Type **arg_types;
    char **param_names;
    char **ctype_overrides;
    int is_varargs = 0;

    char *args = parse_and_convert_args(ctx, l, &defaults, &default_values, &count, &arg_types,
                                        &param_names, &is_varargs, &ctype_overrides);
    if (!args)
    {
        return NULL;
    }

    char *ret = "void";
    Type *ret_type_obj = type_new(TYPE_VOID);

    if (strcmp(name, "main") == 0)
    {
        ret = "int";
        ret_type_obj = type_new(TYPE_C_INT);
    }

    if (lexer_peek(l).kind == TOK_ARROW)
    {
        lexer_next(l);
        ret_type_obj = parse_type_formal(ctx, l);
        if (!ret_type_obj)
        {
            return NULL;
        }
        ret = type_to_string(ret_type_obj);
    }
    else if (lexer_peek(l).kind == TOK_COLON)
    {
        zpanic_at(lexer_peek(l), "Functions use '->' for the return type, not ':'");
        return NULL;
    }

    curr_func_ret = ret;

    // Auto-prefix function name if in module context
    // Don't prefix generic templates or functions inside impl blocks (already
    // mangled)
    if (ctx->imports.current_module_prefix && !gen_param && !ctx->current_impl_struct &&
        !is_extern && !is_extern_symbol(ctx, name))
    {
        char *prefixed_name =
            xmalloc(strlen(ctx->imports.current_module_prefix) + strlen(name) + 3);
        sprintf(prefixed_name, "%s__%s", ctx->imports.current_module_prefix,
                name); /* TODO: check buffer size */
        zfree(name);
        name = prefixed_name;
    }

    // Register if concrete (Global functions only)
    if (!gen_param && !ctx->current_impl_struct)
    {
        register_func(ctx, ctx->current_scope->parent, name, count, defaults, arg_types,
                      ret_type_obj, is_varargs, is_async, 0, link_name, name_tok, is_export);
        // Note: required is set after return by caller (parser_core.c)
    }

    ASTNode *body = NULL;
    Token next_tok = lexer_peek(l);
    if (next_tok.kind == TOK_SEMICOLON)
    {
        lexer_next(l); // consume ;
    }
    else if (next_tok.kind == TOK_LBRACE)
    {
        // Set self context flags for .member shorthand in methods with self
        int prev_in_method = ctx->in_method_with_self;
        int prev_self_ptr = ctx->self_is_pointer;
        if (args && strstr(args, "self"))
        {
            ctx->in_method_with_self = 1;
            ctx->self_is_pointer = (strstr(args, "self*") != NULL);
        }

        body = parse_block(ctx, l);

        // Restore previous state
        ctx->in_method_with_self = prev_in_method;
        ctx->self_is_pointer = prev_self_ptr;
    }
    else
    {
        zpanic_at(next_tok, "Expected '{' or ';' after function signature");
        return NULL;
    }

    // Check for unused parameters
    // The current scope contains arguments (since parse_block creates a new child
    // scope for body) Only check if we parsed a body (not a prototype) function
    if (body && ctx->current_scope)
    {
        ZenSymbol *sym = ctx->current_scope->symbols;
        while (sym)
        {
            // Check if unused and not prefixed with '_' (conventional ignore)
            // also ignore 'self' as it is often mandated by traits
            if (!sym->is_used && !ctx->config->misra_mode && sym->name[0] != '_' &&
                strcmp(sym->name, "self") != 0 && strcmp(name, "main") != 0)
            {
                warn_unused_parameter(sym->decl_token, sym->name, name);
            }
            sym = sym->next;
        }
    }

    exit_scope(ctx);

    // Restore generic count to unregister function-scoped generics
    ctx->known_generics_count = saved_generic_count;

    curr_func_ret = NULL;

    ASTNode *node = ast_create(NODE_FUNCTION);
    node->token = name_tok; // Save definition location
    node->func.name = name;
    node->func.args = args;
    node->func.ret_type = ret;
    node->func.body = body;

    node->func.arg_types = arg_types;
    node->func.param_names = param_names;
    node->func.count = count;
    node->func.defaults = defaults;
    node->func.default_values = default_values;
    node->func.ret_type_info = ret_type_obj;
    node->func.is_varargs = is_varargs;
    node->func.is_async = is_async;
    node->func.is_extern = is_extern;
    node->func.c_type_overrides = ctype_overrides;
    node->link_name = link_name ? xstrdup(link_name) : NULL;

    if (gen_param)
    {
        node->func.generic_params = xstrdup(gen_param);
        if (!ctx->current_impl_struct)
        {
            register_func_template(ctx, name, gen_param, node);
            return NULL;
        }
    }
    if (!ctx->current_impl_struct)
    {
        add_to_func_list(ctx, node);
    }
    return node;
}

char *patch_self_args(const char *args, const char *struct_name)
{
    if (!args)
    {
        return NULL;
    }

    // Sanitize struct name for C usage (Vec<T> -> Vec_T)
    char *safe_name = xmalloc(strlen(struct_name) + 1);
    int j = 0;
    for (int i = 0; struct_name[i]; i++)
    {
        if (struct_name[i] == '<')
        {
            safe_name[j++] = '_';
        }
        else if (struct_name[i] == '>')
        {
            // skip
        }
        else if (struct_name[i] == ' ')
        {
            // skip
        }
        else
        {
            safe_name[j++] = struct_name[i];
        }
    }
    safe_name[j] = 0;

    char *new_args = xmalloc(strlen(args) + strlen(safe_name) + 20);

    // Check if it starts with "const void* self" or "void* self"
    if (strncmp(args, "const void* self", 16) == 0)
    {
        sprintf(new_args, "const %s* self%s", safe_name, args + 16); /* safe */
    }
    else if (strncmp(args, "void* self", 10) == 0)
    {
        sprintf(new_args, "%s* self%s", safe_name, args + 10); /* safe */
    }
    else
    {
        strcpy(new_args, args);
    }
    zfree(safe_name);
    return new_args;
}
