
#include "parser.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../ast/ast.h"
#include "../plugins/plugin_manager.h"
#include "../zen/zen_facts.h"
#include "zprep_plugin.h"
#include "../codegen/codegen.h"

extern char *g_current_filename;

/**
 * @brief Auto-imports std/mem.zc if not already imported.
 *
 * This is called when the Drop trait is used (impl Drop for X).
 */
static void auto_import_std_mem(ParserContext *ctx)
{
    // Check if Drop trait is already registered (means mem.zc was imported)
    if (check_impl(ctx, "Drop", "__trait_marker__"))
    {
        // Check_impl returns 0 if not found, but we need a different check
        // Let's check if we can find any indicator that mem.zc was loaded
    }

    // Resolve path to std/mem.zc
    char *resolved = z_resolve_path("std/mem.zc", g_current_filename);
    if (!resolved)
    {
        return; // Could not find mem.zc
    }

    // Check if already imported by path
    if (is_file_imported(ctx, resolved))
    {
        free(resolved);
        return;
    }
    mark_file_imported(ctx, resolved);

    // Load and parse the file
    char *src = load_file(resolved);
    if (!src)
    {
        free(resolved);
        return;
    }

    Lexer i;
    lexer_init(&i, src);

    // Save and restore filename context
    char *saved_fn = g_current_filename;
    g_current_filename = resolved;

    // Parse the mem module contents
    parse_program_nodes(ctx, &i);

    g_current_filename = saved_fn;
    free(resolved);
}

// Trait Parsing
ASTNode *parse_trait(ParserContext *ctx, Lexer *l)
{
    lexer_next(l); // eat trait
    Token n = lexer_next(l);
    check_identifier(ctx, n);
    if (n.type != TOK_IDENT)
    {
        zpanic_at(n, "Expected trait name");
    }
    char *name = xmalloc(n.len + 1);
    strncpy(name, n.start, n.len);
    name[n.len] = 0;

    // Generics <T>
    char **generic_params = NULL;
    int generic_count = 0;
    if (lexer_peek(l).type == TOK_LANGLE)
    {
        lexer_next(l);                                // eat <
        generic_params = xmalloc(sizeof(char *) * 8); // simplified
        while (1)
        {
            Token p = lexer_next(l);
            check_identifier(ctx, p);
            if (p.type != TOK_IDENT)
            {
                zpanic_at(p, "Expected generic parameter name");
            }
            generic_params[generic_count] = xmalloc(p.len + 1);
            strncpy(generic_params[generic_count], p.start, p.len);
            generic_params[generic_count][p.len] = 0;
            generic_count++;

            Token sep = lexer_peek(l);
            if (sep.type == TOK_COMMA)
            {
                lexer_next(l);
                continue;
            }
            else if (sep.type == TOK_RANGLE)
            {
                lexer_next(l);
                break;
            }
            else
            {
                zpanic_at(sep, "Expected , or > in generic params");
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

    lexer_next(l); // eat {

    ASTNode *methods = NULL, *tail = NULL;
    while (1)
    {
        skip_comments(l);
        if (lexer_peek(l).type == TOK_RBRACE)
        {
            lexer_next(l);
            break;
        }

        // Parse method signature: fn name(args...) -> ret;
        Token ft = lexer_next(l);
        if (ft.type != TOK_IDENT || strncmp(ft.start, "fn", 2) != 0)
        {
            zpanic_at(ft, "Expected fn in trait");
        }

        Token mn = lexer_next(l);
        check_identifier(ctx, mn);
        char *mname = xmalloc(mn.len + 1);
        strncpy(mname, mn.start, mn.len);
        mname[mn.len] = 0;

        char **defaults = NULL;
        int arg_count = 0;
        Type **arg_types = NULL;
        ASTNode **default_values = NULL;
        char **param_names = NULL;
        int is_varargs = 0;
        char *args = parse_and_convert_args(ctx, l, &defaults, &default_values, &arg_count,
                                            &arg_types, &param_names, &is_varargs, NULL);

        char *ret = xstrdup("void");
        if (lexer_peek(l).type == TOK_ARROW)
        {
            lexer_next(l);
            char *rt = parse_type(ctx, l);
            free(ret);
            ret = rt;
        }

        if (lexer_peek(l).type == TOK_SEMICOLON)
        {
            lexer_next(l);
            ASTNode *m = ast_create(NODE_FUNCTION);
            m->token = ft;
            m->func.param_names = param_names;
            m->func.name = mname;
            m->func.args = args;
            m->func.defaults = defaults;
            m->func.default_values = default_values;
            m->func.arg_count = arg_count;
            m->func.arg_types = arg_types;
            m->func.ret_type = ret;
            m->func.body = NULL;
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

ASTNode *parse_impl(ParserContext *ctx, Lexer *l)
{

    lexer_next(l); // eat impl
    Token t1 = lexer_next(l);
    char *name1 = token_strdup(t1);

    // Map primitive types to their C representation for correct mangling
    // Normalize type name (e.g. int -> int32_t)
    const char *normalized = normalize_type_name(name1);
    char *final_name = xstrdup(normalized);
    free(name1);
    name1 = final_name;

    char *gen_param = NULL;
    // Check for <T> on the struct name
    if (lexer_peek(l).type == TOK_LANGLE)
    {
        lexer_next(l); // eat <
        Token gt = lexer_next(l);
        gen_param = token_strdup(gt);
        if (lexer_next(l).type != TOK_RANGLE)
        {
            zpanic_at(lexer_peek(l), "Expected >");
        }
    }

    if (gen_param)
    {
        register_generic(ctx, gen_param);
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

        ctx->current_impl_struct = name2; // Set context to prevent duplicate emission and prefixing

        lexer_next(l); // eat {
        ASTNode *h = 0, *tl = 0;

        char *full_target_name = name2;
        if (target_gen_param)
        {
            full_target_name = xmalloc(strlen(name2) + strlen(target_gen_param) + 3);
            sprintf(full_target_name, "%s<%s>", name2, target_gen_param);
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
            DeclarationAttributes attrs = {0};
            if (lexer_peek(l).type == TOK_AT)
            {
                attrs = parse_attributes(ctx, l);
            }

            if (lexer_peek(l).type == TOK_IDENT && strncmp(lexer_peek(l).start, "fn", 2) == 0)
            {
                ASTNode *f = parse_function(ctx, l, 0);
                // Mangle: Type_Trait_Method
                char *mangled = xmalloc(strlen(name2) + strlen(name1) + strlen(f->func.name) + 4);
                sprintf(mangled, "%s__%s_%s", name2, name1, f->func.name);
                free(f->func.name);
                f->func.name = mangled;

                // Use full_target_name (Vec<T>) for self patching
                char *na = patch_self_args(f->func.args, full_target_name);
                free(f->func.args);
                f->func.args = na;

                if (attrs.cfg_condition)
                {
                    f->cfg_condition = attrs.cfg_condition;
                }
                f->func.pure = attrs.is_pure;

                // Register function for lookup
                if (f->func.generic_params)
                {
                    register_func_template(ctx, mangled, f->func.generic_params, f);
                }
                else
                {
                    register_func(ctx, mangled, f->func.arg_count, f->func.defaults,
                                  f->func.arg_types, f->func.ret_type_info, f->func.is_varargs,
                                  f->func.is_async, f->func.pure, f->token);
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
                    ASTNode *f = parse_function(ctx, l, 1);
                    f->func.is_async = 1;
                    // Mangle: Type_Trait_Method
                    char *mangled =
                        xmalloc(strlen(name2) + strlen(name1) + strlen(f->func.name) + 5);
                    sprintf(mangled, "%s__%s_%s", name2, name1, f->func.name);
                    free(f->func.name);
                    f->func.name = mangled;

                    char *na = patch_self_args(f->func.args, full_target_name);
                    free(f->func.args);
                    f->func.args = na;

                    // Register function for lookup
                    if (f->func.generic_params)
                    {
                        register_func_template(ctx, mangled, f->func.generic_params, f);
                    }
                    else
                    {
                        register_func(ctx, mangled, f->func.arg_count, f->func.defaults,
                                      f->func.arg_types, f->func.ret_type_info, f->func.is_varargs,
                                      f->func.is_async, f->func.pure, f->token);
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
                }
            }
            else
            {
                lexer_next(l);
            }
        }

        if (target_gen_param)
        {
            free(full_target_name);
        }
        else
        {
            free(full_target_name); // It was strdup/ref. Wait, xstrdup needs free.
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
        if (ctx->current_module_prefix && !gen_param)
        {
            char *prefixed_name = xmalloc(strlen(ctx->current_module_prefix) + strlen(name1) + 2);
            sprintf(prefixed_name, "%s_%s", ctx->current_module_prefix, name1);
            free(name1);
            name1 = prefixed_name;
        }

        // Resolve opaque alias (e.g. StringView -> Slice_char)
        TypeAlias *ta = find_type_alias_node(ctx, name1);
        if (ta && !ta->is_opaque)
        {
            const char *alias_resolved = ta->original_type;
            if (alias_resolved)
            {
                free(name1);
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
            }
            char *full_struct_name = xmalloc(strlen(name1) + strlen(gen_param) + 3);
            sprintf(full_struct_name, "%s<%s>", name1, gen_param);

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
                if (lexer_peek(l).type == TOK_IDENT && strncmp(lexer_peek(l).start, "fn", 2) == 0)
                {
                    ASTNode *f = parse_function(ctx, l, 0);
                    // Standard Mangle for template: Box_method
                    char *mangled = xmalloc(strlen(name1) + strlen(f->func.name) + 3);
                    sprintf(mangled, "%s__%s", name1, f->func.name);
                    free(f->func.name);
                    f->func.name = mangled;

                    // Update args string
                    char *na = patch_self_args(f->func.args, full_struct_name);
                    free(f->func.args);
                    f->func.args = na;

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
                        ASTNode *f = parse_function(ctx, l, 1);
                        f->func.is_async = 1;
                        char *mangled = xmalloc(strlen(name1) + strlen(f->func.name) + 3);
                        sprintf(mangled, "%s__%s", name1, f->func.name);
                        free(f->func.name);
                        f->func.name = mangled;

                        char *na = patch_self_args(f->func.args, full_struct_name);
                        free(f->func.args);
                        f->func.args = na;

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
                    }
                }
                else
                {
                    lexer_next(l);
                }
            }
            free(full_struct_name);
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
            lexer_next(l); // eat {
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
                if (lexer_peek(l).type == TOK_AT)
                {
                    DeclarationAttributes attrs = parse_attributes(ctx, l);
                    if (lexer_peek(l).type == TOK_IDENT &&
                        strncmp(lexer_peek(l).start, "fn", 2) == 0)
                    {
                        ASTNode *f = parse_function(ctx, l, 0);

                        // Standard Mangle: Struct_method
                        char *mangled = xmalloc(strlen(name1) + strlen(f->func.name) + 3);
                        sprintf(mangled, "%s__%s", name1, f->func.name);
                        free(f->func.name);
                        f->func.name = mangled;

                        char *na = patch_self_args(f->func.args, name1);
                        free(f->func.args);
                        f->func.args = na;

                        if (attrs.cfg_condition)
                        {
                            f->cfg_condition = attrs.cfg_condition;
                        }
                        f->func.pure = attrs.is_pure;

                        if (f->func.generic_params)
                        {
                            register_func_template(ctx, mangled, f->func.generic_params, f);
                        }
                        else
                        {
                            register_func(ctx, mangled, f->func.arg_count, f->func.defaults,
                                          f->func.arg_types, f->func.ret_type_info,
                                          f->func.is_varargs, 0, f->func.pure, f->token);
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
                else if (lexer_peek(l).type == TOK_IDENT &&
                         strncmp(lexer_peek(l).start, "fn", 2) == 0)
                {
                    ASTNode *f = parse_function(ctx, l, 0);

                    // Standard Mangle: Struct_method
                    char *mangled = xmalloc(strlen(name1) + strlen(f->func.name) + 3);
                    sprintf(mangled, "%s__%s", name1, f->func.name);
                    free(f->func.name);
                    f->func.name = mangled;

                    char *na = patch_self_args(f->func.args, name1);
                    free(f->func.args);
                    f->func.args = na;

                    if (f->func.generic_params)
                    {
                        register_func_template(ctx, mangled, f->func.generic_params, f);
                    }
                    else
                    {
                        register_func(ctx, mangled, f->func.arg_count, f->func.defaults,
                                      f->func.arg_types, f->func.ret_type_info, f->func.is_varargs,
                                      0, f->func.pure, f->token);
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
                        ASTNode *f = parse_function(ctx, l, 1);
                        f->func.is_async = 1;
                        char *mangled = xmalloc(strlen(name1) + strlen(f->func.name) + 3);
                        sprintf(mangled, "%s__%s", name1, f->func.name);
                        free(f->func.name);
                        f->func.name = mangled;
                        char *na = patch_self_args(f->func.args, name1);
                        free(f->func.args);
                        f->func.args = na;
                        if (f->func.generic_params)
                        {
                            register_func_template(ctx, mangled, f->func.generic_params, f);
                        }
                        else
                        {
                            register_func(ctx, mangled, f->func.arg_count, f->func.defaults,
                                          f->func.arg_types, f->func.ret_type_info,
                                          f->func.is_varargs, 1, f->func.pure, f->token);
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

ASTNode *parse_struct(ParserContext *ctx, Lexer *l, int is_union, int is_opaque)
{

    lexer_next(l); // eat struct or union
    Token n = lexer_next(l);
    check_identifier(ctx, n);
    char *name = token_strdup(n);
    Token name_token = n;

    // Generic Params <T> or <K, V>
    char **gps = NULL;
    int gp_count = 0;
    if (lexer_peek(l).type == TOK_LANGLE)
    {
        lexer_next(l); // eat <
        while (1)
        {
            Token g = lexer_next(l);
            check_identifier(ctx, g);
            gps = realloc(gps, sizeof(char *) * (gp_count + 1));
            gps[gp_count++] = token_strdup(g);

            Token next = lexer_peek(l);
            if (next.type == TOK_COMMA)
            {
                lexer_next(l); // eat ,
            }
            else if (next.type == TOK_RANGLE)
            {
                lexer_next(l); // eat >
                break;
            }
            else
            {
                zpanic_at(next, "Expected ',' or '>' in generic parameter list");
            }
        }

        for (int i = 0; i < gp_count; i++)
        {
            register_generic(ctx, gps[i]);
        }
    }

    // Check for prototype (forward declaration)
    if (lexer_peek(l).type == TOK_SEMICOLON)
    {
        lexer_next(l);
        ASTNode *node = ast_create(NODE_STRUCT);
        node->token = name_token;
        node->strct.name = name;
        node->strct.is_template = (gp_count > 0);
        node->strct.generic_params = gps;
        node->strct.generic_param_count = gp_count;
        node->strct.is_union = is_union;
        node->strct.fields = NULL;
        node->strct.is_incomplete = 1;
        node->strct.is_opaque = is_opaque;

        return node;
    }

    lexer_next(l); // eat {
    ASTNode *h = 0, *tl = 0;

    // Temp storage for used structs
    char **temp_used_structs = NULL;
    int temp_used_count = 0;

    while (1)
    {
        skip_comments(l);
        Token t = lexer_peek(l);

        if (t.type == TOK_RBRACE)
        {
            lexer_next(l);
            break;
        }
        if (t.type == TOK_SEMICOLON || t.type == TOK_COMMA)
        {
            lexer_next(l);
            continue;
        }

        // Handle 'use' (Struct Embedding)
        if (t.type == TOK_USE)
        {
            lexer_next(l); // eat use

            // Check for named use: use name: Type;
            Token t1 = lexer_peek(l);
            Token t2 = lexer_peek2(l);

            if (t1.type == TOK_IDENT && t2.type == TOK_COLON)
            {
                // Named use -> Composition (Add field, don't flatten)
                Token field_name = lexer_next(l);
                check_identifier(ctx, field_name);
                lexer_next(l); // eat :
                Type *ft = parse_type_formal(ctx, l);
                char *field_type_str = type_to_c_string(ft);
                expect(l, TOK_SEMICOLON, "Expected ;");

                ASTNode *nf = ast_create(NODE_FIELD);
                nf->field.name = token_strdup(field_name);
                nf->field.type = field_type_str;
                nf->type_info = ft;

                if (!h)
                {
                    h = nf;
                }
                else
                {
                    tl->next = nf;
                }
                tl = nf;
                continue;
            }

            // Normal use -> Mixin (Flatten)
            // Parse the type (e.g. Header<I32>)
            Type *use_type = parse_type_formal(ctx, l);
            char *use_name = type_to_string(use_type);

            expect(l, TOK_SEMICOLON, "Expected ; after use");

            // Find the definition and COPY fields
            ASTNode *def = find_struct_def(ctx, use_name);
            if (!def && is_known_generic(ctx, use_type->name))
            {
                // Try to force instantiation if not found?
                // For now, rely on parse_type having triggered instantiation.
                char *mangled =
                    type_to_string(use_type); // This works if type_to_string returns mangled name
                def = find_struct_def(ctx, mangled);
                free(mangled);
            }

            if (def && def->type == NODE_STRUCT)
            {
                if (!temp_used_structs)
                {
                    temp_used_structs = xmalloc(sizeof(char *) * 8);
                }
                temp_used_structs[temp_used_count++] = xstrdup(use_name);

                ASTNode *f = def->strct.fields;
                while (f)
                {
                    ASTNode *nf = ast_create(NODE_FIELD);
                    nf->field.name = xstrdup(f->field.name);
                    nf->field.type = xstrdup(f->field.type);
                    nf->type_info = f->type_info;

                    if (!h)
                    {
                        h = nf;
                    }
                    else
                    {
                        tl->next = nf;
                    }
                    tl = nf;
                    f = f->next;
                }
            }
            free(use_name);
            continue;
        }

        if (t.type == TOK_IDENT)
        {
            Token f_name = lexer_next(l);
            check_identifier(ctx, f_name);
            expect(l, TOK_COLON, "Expected :");
            Type *ft = parse_type_formal(ctx, l);
            char *f_type = type_to_c_string(ft);

            ASTNode *f = ast_create(NODE_FIELD);
            f->field.name = token_strdup(f_name);
            f->field.type = f_type;
            f->type_info = ft;
            f->field.bit_width = 0;

            // Optional bit width: name: type : 3
            if (lexer_peek(l).type == TOK_COLON)
            {
                lexer_next(l); // eat :
                Token width_tok = lexer_next(l);
                if (width_tok.type != TOK_INT)
                {
                    zpanic_at(width_tok, "Expected bit width integer");
                }
                f->field.bit_width = atoi(token_strdup(width_tok));
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

            if (lexer_peek(l).type == TOK_SEMICOLON || lexer_peek(l).type == TOK_COMMA)
            {
                lexer_next(l);
            }
        }
        else
        {
            lexer_next(l);
        }
    }

    // Auto-prefix struct name if in module context
    if (ctx->current_module_prefix && gp_count == 0)
    { // Don't prefix generic templates
        char *prefixed_name = xmalloc(strlen(ctx->current_module_prefix) + strlen(name) + 2);
        sprintf(prefixed_name, "%s_%s", ctx->current_module_prefix, name);
        free(name);
        name = prefixed_name;
    }

    // Generic templates are registered separately and may share the base name.
    if (gp_count == 0)
    {
        ASTNode *existing = find_concrete_struct_def(ctx, name);
        if (existing)
        {
            zpanic_at(name_token, "Redefinition of %s '%s'", is_union ? "union" : "struct", name);
        }
    }

    ASTNode *node = ast_create(NODE_STRUCT);
    node->token = name_token;
    add_to_struct_list(ctx, node);

    node->strct.name = name;

    // Initialize Type Info so we can track traits (like Drop)
    node->type_info = type_new(TYPE_STRUCT);
    node->type_info->name = xstrdup(name);
    if (gp_count > 0)
    {
        node->type_info->kind = TYPE_GENERIC;
        node->type_info->arg_count = gp_count;
        node->type_info->args = xmalloc(sizeof(Type *) * gp_count);
        for (int i = 0; i < gp_count; i++)
        {
            node->type_info->args[i] = type_new(TYPE_GENERIC);
            node->type_info->args[i]->name = xstrdup(gps[i]);
        }
    }

    node->strct.fields = h;
    node->strct.generic_params = gps;
    node->strct.generic_param_count = gp_count;
    node->strct.is_union = is_union;
    node->strct.is_opaque = is_opaque;
    node->strct.used_structs = temp_used_structs;
    node->strct.used_struct_count = temp_used_count;
    node->strct.defined_in_file = g_current_filename ? xstrdup(g_current_filename) : NULL;

    if (gp_count > 0)
    {
        node->strct.is_template = 1;
        ctx->known_generics_count -= gp_count;
        register_template(ctx, name, node);
    }

    // Register definition for 'use' lookups and LSP
    if (gp_count == 0)
    {
        register_struct_def(ctx, name, node);
    }

    return node;
}

Type *parse_type_obj(ParserContext *ctx, Lexer *l)
{
    // Parse the base type (int, U32, MyStruct, etc.)
    Type *t = parse_type_base(ctx, l);

    // Handle Pointers
    while (lexer_peek(l).type == TOK_OP && lexer_peek(l).start[0] == '*')
    {
        lexer_next(l); // eat *
        // Wrap the current type in a Pointer type
        Type *ptr = type_new(TYPE_POINTER);
        ptr->inner = t;
        t = ptr;
    }

    return t;
}

ASTNode *parse_enum(ParserContext *ctx, Lexer *l)
{
    lexer_next(l);
    Token n = lexer_next(l);
    check_identifier(ctx, n);
    Token name_token = n;

    char *gp = NULL;
    if (lexer_peek(l).type == TOK_LANGLE)
    {
        lexer_next(l); // eat <
        Token g = lexer_next(l);
        check_identifier(ctx, g);
        gp = token_strdup(g);
        lexer_next(l); // eat >
        register_generic(ctx, gp);
    }

    lexer_next(l); // eat {

    ASTNode *h = 0, *tl = 0;
    int v = 0;
    char *ename = token_strdup(n); // Store enum name

    while (1)
    {
        skip_comments(l);
        Token t = lexer_peek(l);
        if (t.type == TOK_RBRACE)
        {
            lexer_next(l);
            break;
        }
        if (t.type == TOK_COMMA)
        {
            lexer_next(l);
            continue;
        }

        if (t.type == TOK_IDENT)
        {
            Token vt = lexer_next(l);
            check_identifier(ctx, vt);
            char *vname = token_strdup(vt);

            Type *payload = NULL;
            if (lexer_peek(l).type == TOK_LPAREN)
            {
                lexer_next(l); // eat (
                Type *first_t = parse_type_obj(ctx, l);

                if (lexer_peek(l).type == TOK_COMMA)
                {
                    // Multi-arg variant -> Tuple
                    char sig[512];
                    sig[0] = 0;

                    char *s = type_to_string(first_t);
                    if (strlen(s) > 250)
                    { // Safety check
                        zpanic_at(lexer_peek(l), "Type name too long for tuple generation");
                    }
                    strcpy(sig, s);
                    free(s);

                    while (lexer_peek(l).type == TOK_COMMA)
                    {
                        lexer_next(l); // eat ,
                        strcat(sig, "__");
                        Type *next_t = parse_type_obj(ctx, l);
                        char *ns = type_to_string(next_t);
                        if (strlen(sig) + strlen(ns) + 2 > 510)
                        {
                            zpanic_at(lexer_peek(l), "Tuple signature too long");
                        }
                        strcat(sig, ns);
                        free(ns);
                    }

                    register_tuple(ctx, sig);
                    char *clean_sig = sanitize_mangled_name(sig);
                    char *tuple_name = xmalloc(strlen(clean_sig) + 7);
                    sprintf(tuple_name, "Tuple_%s", clean_sig);
                    free(clean_sig);

                    payload = type_new(TYPE_STRUCT);
                    payload->name = tuple_name;
                }
                else
                {
                    payload = first_t;
                }

                if (lexer_next(l).type != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected )");
                }
            }

            ASTNode *va = ast_create(NODE_ENUM_VARIANT);
            va->variant.name = vname;
            va->variant.tag_id = v++;      // Use tag_id instead of value
            va->variant.payload = payload; // Store Type*

            // Register Variant (Mangled name to avoid collisions: Result_Ok)
            size_t mangled_sz = strlen(ename) + strlen(vname) + 3;
            char *mangled = xmalloc(mangled_sz);
            snprintf(mangled, mangled_sz, "%s__%s", ename, vname);
            register_enum_variant(ctx, ename, mangled, va->variant.tag_id);

            // Register Constructor Function Signature
            if (payload && !gp) // Only for non-generic enums for now
            {
                Type **at = xmalloc(sizeof(Type *));
                at[0] = payload;
                Type *ret_t = type_new(TYPE_ENUM);
                ret_t->name = xstrdup(ename);

                register_func(ctx, mangled, 1, NULL, at, ret_t, 0, 0, 0, vt);
            }
            else if (!gp)
            {
                // No payload: fn Name() -> Enum
                Type *ret_t = type_new(TYPE_ENUM);
                ret_t->name = xstrdup(ename);
                register_func(ctx, mangled, 0, NULL, NULL, ret_t, 0, 0, 0, vt);
            }
            free(mangled);

            // Handle explicit assignment: Ok = 5
            if (lexer_peek(l).type == TOK_OP && *lexer_peek(l).start == '=')
            {
                lexer_next(l);
                va->variant.tag_id = atoi(lexer_next(l).start);
                v = va->variant.tag_id + 1;
            }

            if (!h)
            {
                h = va;
            }
            else
            {
                tl->next = va;
            }
            tl = va;
        }
        else
        {
            lexer_next(l);
        }
    }

    // Auto-prefix enum name if in module context
    if (ctx->current_module_prefix && !gp)
    { // Don't prefix generic templates
        char *prefixed_name = xmalloc(strlen(ctx->current_module_prefix) + strlen(ename) + 2);
        sprintf(prefixed_name, "%s_%s", ctx->current_module_prefix, ename);
        free(ename);
        ename = prefixed_name;
    }

    ASTNode *node = ast_create(NODE_ENUM);
    node->token = name_token;
    node->enm.name = ename;

    node->enm.variants = h;
    node->enm.generic_param = gp; // Store generic param

    if (gp)
    {
        node->enm.is_template = 1;
        ctx->known_generics_count--;
        register_template(ctx, node->enm.name, node);
    }

    add_to_enum_list(ctx, node); // Register globally

    return node;
}
