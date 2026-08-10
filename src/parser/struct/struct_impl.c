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

ASTNode *parse_impl(ParserContext *ctx, Lexer *l)
{

    lexer_next(l); // eat impl

    // Handle impl<T> Struct<T> syntax: generic param declared before struct name
    char *gen_param = NULL;
    if (lexer_peek(l).type == TOK_LANGLE)
    {
        lexer_next(l); // eat <
        Token gt = lexer_next(l);
        gen_param = token_strdup(gt);
        if (lexer_next(l).type != TOK_RANGLE)
        {
            zpanic_at(lexer_peek(l), "Expected >");
            return NULL;
        }
        register_generic(ctx, gen_param);
    }

    Token t1 = lexer_next(l);
    char *name1 = token_strdup(t1);

    // Map primitive types to their C representation for correct mangling
    // Normalize type name (e.g. int -> int32_t)
    const char *normalized = normalize_type_name(name1);
    char *final_name = xstrdup(normalized);
    zfree(name1);
    name1 = final_name;

    // Check for <T> on the struct name
    if (!gen_param && lexer_peek(l).type == TOK_LANGLE)
    {
        // impl Struct<T> syntax: parse and register generic param
        lexer_next(l); // eat <
        Token gt = lexer_next(l);
        gen_param = token_strdup(gt);
        if (lexer_next(l).type != TOK_RANGLE)
        {
            zpanic_at(lexer_peek(l), "Expected >");
            return NULL;
        }
        register_generic(ctx, gen_param);
    }
    else if (gen_param && lexer_peek(l).type == TOK_LANGLE)
    {
        // impl<T> Struct<T> syntax: skip redundant <T> on struct name
        lexer_next(l); // eat <
        lexer_next(l); // eat T
        if (lexer_next(l).type != TOK_RANGLE)
        {
            zpanic_at(lexer_peek(l), "Expected >");
            return NULL;
        }
    }

    // Check for "for" (Trait impl)
    Token pk = lexer_peek(l);
    if (pk.type == TOK_FOR ||
        (pk.type == TOK_IDENT && strncmp(pk.start, "for", 3) == 0 && pk.len == 3))
    {
        if (pk.type != TOK_FOR)
        {
            lexer_next(l);
        }
        else
        {
            lexer_next(l); // eat for
        }
        Token t2 = lexer_next(l);
        char *name2 = token_strdup(t2);

        char *target_gen_param = NULL;
        if (lexer_peek(l).type == TOK_LANGLE)
        {
            lexer_next(l); // eat <
            Token gt = lexer_next(l);
            target_gen_param = token_strdup(gt);
            if (lexer_next(l).type != TOK_RANGLE)
            {
                zpanic_at(lexer_peek(l), "Expected > in impl struct generic");
                return NULL;
            }
            register_generic(ctx, target_gen_param);
        }

        // Check for common error: swapped Struct and Trait
        // impl MyStruct for MyTrait (Wrong) vs impl MyTrait for MyStruct (Correct)
        if (!is_trait(name1) && is_trait(name2))
        {
            zpanic_at(t1,
                      "Incorrect usage of impl. Did you mean 'impl %s for %s'? Syntax is 'impl "
                      "<Trait> for <Struct>'",
                      name2, name1);
            return NULL;
        }

        // Auto-import std/mem.zc if implementing Drop, Copy, or Clone traits
        if (strcmp(name1, "Drop") == 0 || strcmp(name1, "Copy") == 0 || strcmp(name1, "Clone") == 0)
        {
            auto_import_std_mem(ctx);
        }

        register_impl(ctx, name1, name2);

        // RAII: Check for "Drop" trait implementation
        if (strcmp(name1, "Drop") == 0)
        {
            ZenSymbol *s = find_symbol_entry(ctx, name2);
            if (s && s->type_info)
            {
                s->type_info->traits.has_drop = 1;
            }
            else
            {
                // Try finding struct definition
                ASTNode *def = find_struct_def(ctx, name2);
                if (def && def->type_info)
                {
                    def->type_info->traits.has_drop = 1;
                }
            }
        }

        // Iterator: Check for "Iterable" trait implementation
        else if (strcmp(name1, "Iterable") == 0)
        {
            ZenSymbol *s = find_symbol_entry(ctx, name2);
            if (s && s->type_info)
            {
                s->type_info->traits.has_iterable = 1;
            }
            else
            {
                // Try finding struct definition
                ASTNode *def = find_struct_def(ctx, name2);
                if (def && def->type_info)
                {
                    def->type_info->traits.has_iterable = 1;
                }
            }
        }

        ctx->current_impl_struct = name2;

        lexer_next(l);
        ASTNode *h = 0, *tl = 0;

        char *full_target_name = name2;
        if (target_gen_param)
        {
            full_target_name = xmalloc(strlen(name2) + strlen(target_gen_param) + 3);
            snprintf(full_target_name, strlen(name2) + strlen(target_gen_param) + 3, "%s<%s>",
                     name2, target_gen_param);
        }
        else
        {
            full_target_name = xstrdup(name2);
        }

        while (1)
        {
            ctx->current_impl_methods = h;
            skip_comments(l);
            if (lexer_peek(l).type == TOK_RBRACE)
            {
                lexer_next(l);
                break;
            }
            if (lexer_peek(l).type == TOK_EOF)
            {
                zpanic_at(lexer_peek(l), "Unexpected end of file in impl body");
                break;
            }
            DeclarationAttributes attrs = {0};
            if (lexer_peek(l).type == TOK_AT)
            {
                attrs = parse_attributes(ctx, l);
            }

            if (lexer_peek(l).type == TOK_IDENT && strncmp(lexer_peek(l).start, "fn", 2) == 0)
            {
                ASTNode *f = parse_function(ctx, l, 0, 0, attrs.link_name, 0);
                ATTACH_DOC_COMMENT(ctx, f);
                // Mangle: Type_Trait_Method
                {
                    char *tmp = mangle_method_symbol(name2, name1, f->func.name);
                    zfree(f->func.name);
                    f->func.name = tmp;
                }

                patch_and_fix_self(ctx, f, full_target_name);

                if (attrs.cfg_condition)
                {
                    f->cfg_condition = attrs.cfg_condition;
                }
                f->func.pure = attrs.is_pure;
                f->func.unused = attrs.is_unused;
                f->link_name = attrs.link_name;

                // Register function for lookup
                if (f->func.generic_params)
                {
                    register_func_template(ctx, f->func.name, f->func.generic_params, f);
                }
                else
                {
                    register_func(ctx, ctx->current_scope, f->func.name, f->func.arg_count,
                                  f->func.defaults, f->func.arg_types, f->func.ret_type_info,
                                  f->func.is_varargs, f->func.is_async, f->func.pure, f->link_name,
                                  f->token, 0);
                }

                if (!h)
                {
                    h = f;
                }
                else
                {
                    tl->next = f;
                }
                tl = f;
            }
            else if (lexer_peek(l).type == TOK_ASYNC)
            {
                lexer_next(l); // eat async
                if (lexer_peek(l).type == TOK_IDENT && strncmp(lexer_peek(l).start, "fn", 2) == 0)
                {
                    ASTNode *f = parse_function(ctx, l, 1, 0, attrs.link_name, 0);
                    ATTACH_DOC_COMMENT(ctx, f);
                    f->func.is_async = 1;
                    // Mangle: Type_Trait_Method
                    {
                        char *tmp = mangle_method_symbol(name2, name1, f->func.name);
                        zfree(f->func.name);
                        f->func.name = tmp;
                    }

                    patch_and_fix_self(ctx, f, full_target_name);
                    f->link_name = attrs.link_name;
                    f->cfg_condition = attrs.cfg_condition;
                    f->func.pure = attrs.is_pure;
                    f->func.unused = attrs.is_unused;

                    // Register function for lookup
                    if (f->func.generic_params)
                    {
                        register_func_template(ctx, f->func.name, f->func.generic_params, f);
                    }
                    else
                    {
                        register_func(ctx, ctx->current_scope, f->func.name, f->func.arg_count,
                                      f->func.defaults, f->func.arg_types, f->func.ret_type_info,
                                      f->func.is_varargs, f->func.is_async, f->func.pure,
                                      f->link_name, f->token, 0);
                    }

                    if (!h)
                    {
                        h = f;
                    }
                    else
                    {
                        tl->next = f;
                    }
                    tl = f;
                }
                else
                {
                    zpanic_at(lexer_peek(l), "Expected 'fn' after 'async'");
                    return NULL;
                }
            }
            else
            {
                lexer_next(l);
            }
        }

        if (target_gen_param)
        {
            zfree(full_target_name);
        }
        else
        {
            zfree(full_target_name); // It was strdup/ref. Wait, xstrdup needs free.
        }

        ctx->current_impl_struct = NULL; // Restore context
        ASTNode *n = ast_create(NODE_IMPL_TRAIT);
        n->impl_trait.trait_name = name1;
        n->impl_trait.target_type = name2;
        n->impl_trait.methods = h;
        add_to_impl_list(ctx, n);

        // If target struct is generic, register this impl as a template
        ASTNode *def = find_struct_def(ctx, name2);
        if (target_gen_param || (def && ((def->type == NODE_STRUCT && def->strct.is_template) ||
                                         (def->type == NODE_ENUM && def->enm.is_template))))
        {
            const char *gp = "T";
            if (target_gen_param)
            {
                gp = target_gen_param;
            }
            else if (def && def->type == NODE_STRUCT && def->strct.generic_param_count > 0)
            {
                gp = def->strct.generic_params[0];
            }
            else if (def && def->type == NODE_ENUM && def->enm.is_template)
            {
                gp = def->enm.generic_param;
            }
            register_impl_template(ctx, name2, gp, n);
        }

        if (gen_param)
        {
            ctx->known_generics_count--;
        }
        if (target_gen_param)
        {
            ctx->known_generics_count--;
        }
        return n;
    }
    else
    {
        // Regular impl Struct (impl Box or impl Box<T>)

        // Auto-prefix struct name if in module context
        if (ctx->imports.current_module_prefix && !gen_param && !is_extern_symbol(ctx, name1))
        {
            char *prefixed_name =
                xmalloc(strlen(ctx->imports.current_module_prefix) + strlen(name1) + 3);
            snprintf(prefixed_name, strlen(ctx->imports.current_module_prefix) + strlen(name1) + 3,
                     "%s__%s", ctx->imports.current_module_prefix, name1);
            zfree(name1);
            name1 = prefixed_name;
        }

        // Resolve opaque alias (e.g. StringView -> Slice__char)
        TypeAlias *ta = find_type_alias_node(ctx, name1);
        if (ta && !ta->is_opaque)
        {
            const char *alias_resolved = ta->original_type;
            if (alias_resolved)
            {
                zfree(name1);
                name1 = xstrdup(alias_resolved);
            }
        }

        ctx->current_impl_struct = name1; // For patch_self_args inside parse_function

        if (gen_param)
        {
            // GENERIC IMPL TEMPLATE: impl Box<T>
            if (lexer_next(l).type != TOK_LBRACE)
            {
                zpanic_at(lexer_peek(l), "Expected {");
                return NULL;
            }
            char *full_struct_name = xmalloc(strlen(name1) + strlen(gen_param) + 3);
            snprintf(full_struct_name, strlen(name1) + strlen(gen_param) + 3, "%s<%s>", name1,
                     gen_param);

            ASTNode *h = 0, *tl = 0;
            ctx->current_impl_methods = NULL;
            while (1)
            {
                ctx->current_impl_methods = h;
                skip_comments(l);
                if (lexer_peek(l).type == TOK_RBRACE)
                {
                    lexer_next(l);
                    break;
                }
                if (lexer_peek(l).type == TOK_EOF)
                {
                    zpanic_at(lexer_peek(l), "Unexpected end of file in impl body");
                    break;
                }
                DeclarationAttributes attrs = {0};
                if (lexer_peek(l).type == TOK_AT)
                {
                    attrs = parse_attributes(ctx, l);
                }

                if (lexer_peek(l).type == TOK_IDENT && strncmp(lexer_peek(l).start, "fn", 2) == 0)
                {
                    ASTNode *f = parse_function(ctx, l, 0, 0, attrs.link_name, 0);
                    ATTACH_DOC_COMMENT(ctx, f);
                    {
                        char *tmp = mangle_method_symbol(name1, NULL, f->func.name);
                        zfree(f->func.name);
                        f->func.name = tmp;
                    }

                    patch_and_fix_self(ctx, f, full_struct_name);

                    if (attrs.cfg_condition)
                    {
                        f->cfg_condition = attrs.cfg_condition;
                    }
                    f->func.pure = attrs.is_pure;
                    f->func.unused = attrs.is_unused;
                    f->link_name = attrs.link_name;

                    register_func_template(ctx, f->func.name, gen_param, f);

                    // Manual Type construction for self: Foo<T>*
                    if (f->func.arg_count > 0 && f->func.param_names &&
                        strcmp(f->func.param_names[0], "self") == 0)
                    {
                        Type *t_struct = type_new(TYPE_STRUCT);
                        t_struct->name = xstrdup(name1);
                        t_struct->arg_count = 1;
                        t_struct->args = xmalloc(sizeof(Type *));
                        t_struct->args[0] = type_new(TYPE_GENERIC);
                        t_struct->args[0]->name = xstrdup(gen_param);

                        Type *t_ptr = type_new(TYPE_POINTER);
                        t_ptr->inner = t_struct;

                        f->func.arg_types[0] = t_ptr;
                    }

                    if (!h)
                    {
                        h = f;
                    }
                    else
                    {
                        tl->next = f;
                    }
                    tl = f;
                }
                else if (lexer_peek(l).type == TOK_ASYNC)
                {
                    lexer_next(l); // eat async
                    if (lexer_peek(l).type == TOK_IDENT &&
                        strncmp(lexer_peek(l).start, "fn", 2) == 0)
                    {
                        ASTNode *f = parse_function(ctx, l, 1, 0, attrs.link_name, 0);
                        f->func.is_async = 1;
                        {
                            char *tmp = mangle_method_symbol(name1, NULL, f->func.name);
                            zfree(f->func.name);
                            f->func.name = tmp;
                        }

                        patch_and_fix_self(ctx, f, full_struct_name);

                        if (attrs.cfg_condition)
                        {
                            f->cfg_condition = attrs.cfg_condition;
                        }
                        f->func.pure = attrs.is_pure;
                        f->func.unused = attrs.is_unused;
                        f->link_name = attrs.link_name;

                        register_func_template(ctx, f->func.name, gen_param, f);

                        if (f->func.arg_count > 0 && f->func.param_names &&
                            strcmp(f->func.param_names[0], "self") == 0)
                        {
                            Type *t_struct = type_new(TYPE_STRUCT);
                            t_struct->name = xstrdup(name1);
                            t_struct->arg_count = 1;
                            t_struct->args = xmalloc(sizeof(Type *));
                            t_struct->args[0] = type_new(TYPE_GENERIC);
                            t_struct->args[0]->name = xstrdup(gen_param);

                            Type *t_ptr = type_new(TYPE_POINTER);
                            t_ptr->inner = t_struct;

                            f->func.arg_types[0] = t_ptr;
                        }

                        if (!h)
                        {
                            h = f;
                        }
                        else
                        {
                            tl->next = f;
                        }
                        tl = f;
                    }
                    else
                    {
                        zpanic_at(lexer_peek(l), "Expected 'fn' after 'async'");
                        return NULL;
                    }
                }
                else
                {
                    lexer_next(l);
                }
            }
            zfree(full_struct_name);
            // Register Template
            ASTNode *n = ast_create(NODE_IMPL);
            n->token = t1;
            n->impl.struct_name = name1;
            n->impl.methods = h;
            register_impl_template(ctx, name1, gen_param, n);
            ctx->current_impl_struct = NULL;
            if (gen_param)
            {
                ctx->known_generics_count--;
            }
            return NULL; // Do not emit generic template
        }
        else
        {
            // REGULAR IMPL
            lexer_next(l);
            ASTNode *h = 0, *tl = 0;
            while (1)
            {
                ctx->current_impl_methods = h;
                skip_comments(l);
                if (lexer_peek(l).type == TOK_RBRACE)
                {
                    lexer_next(l);
                    break;
                }
                if (lexer_peek(l).type == TOK_EOF)
                {
                    zpanic_at(lexer_peek(l), "Unexpected end of file in impl body");
                    break;
                }

                DeclarationAttributes attrs = {0};
                if (lexer_peek(l).type == TOK_AT)
                {
                    attrs = parse_attributes(ctx, l);
                }

                if (lexer_peek(l).type == TOK_IDENT && strncmp(lexer_peek(l).start, "fn", 2) == 0)
                {
                    ASTNode *f = parse_function(ctx, l, 0, 0, attrs.link_name, 0);
                    if (!f)
                    {
                        return NULL;
                    }

                    {
                        char *tmp = mangle_method_symbol(name1, NULL, f->func.name);
                        zfree(f->func.name);
                        f->func.name = tmp;
                    }

                    patch_and_fix_self(ctx, f, name1);

                    if (attrs.cfg_condition)
                    {
                        f->cfg_condition = attrs.cfg_condition;
                    }
                    f->func.pure = attrs.is_pure;
                    f->func.unused = attrs.is_unused;
                    f->link_name = attrs.link_name;

                    if (f->func.generic_params)
                    {
                        register_func_template(ctx, f->func.name, f->func.generic_params, f);
                    }
                    else
                    {
                        register_func(ctx, ctx->current_scope, f->func.name, f->func.arg_count,
                                      f->func.defaults, f->func.arg_types, f->func.ret_type_info,
                                      f->func.is_varargs, 0, f->func.pure, f->link_name, f->token,
                                      0);
                    }

                    if (!h)
                    {
                        h = f;
                    }
                    else
                    {
                        tl->next = f;
                    }
                    tl = f;
                }
                else if (lexer_peek(l).type == TOK_ASYNC)
                {
                    lexer_next(l);
                    if (lexer_peek(l).type == TOK_IDENT &&
                        strncmp(lexer_peek(l).start, "fn", 2) == 0)
                    {
                        ASTNode *f = parse_function(ctx, l, 1, 0, attrs.link_name, 0);
                        f->func.is_async = 1;
                        {
                            char *tmp = mangle_method_symbol(name1, NULL, f->func.name);
                            zfree(f->func.name);
                            f->func.name = tmp;
                        }
                        patch_and_fix_self(ctx, f, name1);

                        if (attrs.cfg_condition)
                        {
                            f->cfg_condition = attrs.cfg_condition;
                        }
                        f->func.pure = attrs.is_pure;
                        f->func.unused = attrs.is_unused;
                        f->link_name = attrs.link_name;

                        if (f->func.generic_params)
                        {
                            register_func_template(ctx, f->func.name, f->func.generic_params, f);
                        }
                        else
                        {
                            register_func(ctx, ctx->current_scope, f->func.name, f->func.arg_count,
                                          f->func.defaults, f->func.arg_types,
                                          f->func.ret_type_info, f->func.is_varargs, 1,
                                          f->func.pure, f->link_name, f->token, 0);
                        }
                        if (!h)
                        {
                            h = f;
                        }
                        else
                        {
                            tl->next = f;
                        }
                        tl = f;
                    }
                }
                else
                {
                    lexer_next(l);
                }
            }
            ctx->current_impl_struct = NULL;
            ASTNode *n = ast_create(NODE_IMPL);
            n->impl.struct_name = name1;
            n->impl.methods = h;
            add_to_impl_list(ctx, n);

            if (gen_param)
            {
                ctx->known_generics_count--;
            }
            return n;
        }
    }
}
