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

Type *parse_type_base(ParserContext *ctx, Lexer *l)
{
    RECURSION_GUARD(ctx, l, type_new(TYPE_UNKNOWN));
    Token t = lexer_peek(l);

    if (t.kind == TOK_IDENT)
    {
        int explicit_struct = 0;
        // Handle "struct Name" or "enum Name"
        if ((t.len == 6 && strncmp(t.start, "struct", 6) == 0) ||
            (t.len == 4 && strncmp(t.start, "enum", 4) == 0))
        {
            if (strncmp(t.start, "struct", 6) == 0)
            {
                explicit_struct = 1;
            }
            lexer_next(l); // consume keyword
            t = lexer_peek(l);
            if (t.kind != TOK_IDENT)
            {
                zpanic_at(t, "Expected identifier after struct/enum");
                return NULL;
            }
        }

        lexer_next(l);
        char *name = token_strdup(t);

        // Check for alias
        TypeAlias *alias_node = find_type_alias_node(ctx, name);
        if (alias_node)
        {
            zfree(name);
            Lexer tmp;
            lexer_init(&tmp, alias_node->original_type, ctx->config, ctx->current_filename);

            if (alias_node->is_opaque)
            {
                Type *underlying = parse_type_formal(ctx, &tmp);
                if (!underlying)
                {
                    RECURSION_EXIT(ctx);
                    return NULL;
                }
                Type *wrapper = type_new(TYPE_ALIAS);
                wrapper->name = xstrdup(alias_node->alias);
                wrapper->inner = underlying;
                wrapper->alias.is_opaque_alias = 1;
                wrapper->alias.alias_defined_in_file =
                    alias_node->defined_in_file ? xstrdup(alias_node->defined_in_file) : NULL;
                RECURSION_EXIT(ctx);
                return wrapper;
            }

            Type *t_res = parse_type_formal(ctx, &tmp);
            if (!t_res)
            {
                RECURSION_EXIT(ctx);
                return NULL;
            }
            RECURSION_EXIT(ctx);
            return t_res;
        }

        // Self type alias: Replace "Self" with current impl struct type
        if (strcmp(name, "Self") == 0 && ctx->current_impl_struct)
        {
            name = xstrdup(ctx->current_impl_struct);
        }

        // Handle Namespace :: (A::B -> A_B)
        while (lexer_peek(l).kind == TOK_DCOLON)
        {
            lexer_next(l); // eat ::
            Token next = lexer_next(l);
            if (next.kind != TOK_IDENT)
            {
                zpanic_at(t, "Expected identifier after ::");
                return NULL;
            }

            char *suffix = token_strdup(next);
            // Map aliases (I32 -> int32_t, string -> char*) using centralized logic
            const char *resolved_suffix = normalize_type_name(suffix);

            // Check if 'name' is a module alias (e.g., m::Vector)
            Module *mod = find_module(ctx, name);
            char *merged;
            if (mod)
            {
                // Module-qualified type
                if (mod->is_c_header)
                {
                    // C header: Use type name directly without prefix
                    // To prevent name mangling, we might consider changing
                    // this to also use the prefix.
                    merged = xstrdup(resolved_suffix);

                    register_extern_symbol(ctx, merged);
                }
                else
                {
                    // Zen module: Use module base name as prefix
                    size_t mer_sz = strlen(mod->base_name) + strlen(resolved_suffix) + 3;
                    merged = xmalloc(mer_sz);
                    snprintf(merged, mer_sz, "%s__%s", mod->base_name, resolved_suffix);
                }
            }
            else
            {
                // Regular namespace or enum variant
                size_t mer_sz = strlen(name) + strlen(resolved_suffix) + 3;
                merged = xmalloc(mer_sz);
                snprintf(merged, mer_sz, "%s__%s", name, resolved_suffix);
            }

            zfree(name);

            name = merged;
        }

        // Check for Primitives (Base types)
        // Check for Primitives (Base types)
        const ZenPrimitive *prim = find_primitive_by_name(name);
        if (prim)
        {
            zfree(name);
            Type *t_prim = type_new(prim->kind);
            RECURSION_EXIT(ctx);
            return t_prim;
        }

        // C23 BitInt Support (i42, u256, etc.)
        if ((name[0] == 'i' || name[0] == 'u') && isdigit(name[1]))
        {
            // Verify it is a purely numeric suffix
            int valid = 1;
            for (size_t k = 1; k < strlen(name); k++)
            {
                if (!isdigit(name[k]))
                {
                    valid = 0;
                    break;
                }
            }
            if (valid)
            {
                int width = (int)strtol(name + 1, NULL, 10);
                if (width > 0)
                {
                    // Map standard widths to standard types for standard ABI/C compabitility
                    if (name[0] == 'i')
                    {
                        if (width == 8)
                        {
                            zfree(name);
                            RECURSION_EXIT(ctx);
                            return type_new(TYPE_I8);
                        }
                        if (width == 16)
                        {
                            zfree(name);
                            RECURSION_EXIT(ctx);
                            return type_new(TYPE_I16);
                        }
                        if (width == 32)
                        {
                            zfree(name);
                            RECURSION_EXIT(ctx);
                            return type_new(TYPE_I32);
                        }
                        if (width == 64)
                        {
                            zfree(name);
                            RECURSION_EXIT(ctx);
                            return type_new(TYPE_I64);
                        }
                        if (width == 128)
                        {
                            zfree(name);
                            RECURSION_EXIT(ctx);
                            return type_new(TYPE_I128);
                        }
                    }
                    else
                    {
                        if (width == 8)
                        {
                            zfree(name);
                            RECURSION_EXIT(ctx);
                            return type_new(TYPE_U8);
                        }
                        if (width == 16)
                        {
                            zfree(name);
                            RECURSION_EXIT(ctx);
                            return type_new(TYPE_U16);
                        }
                        if (width == 32)
                        {
                            zfree(name);
                            RECURSION_EXIT(ctx);
                            return type_new(TYPE_U32);
                        }
                        if (width == 64)
                        {
                            zfree(name);
                            RECURSION_EXIT(ctx);
                            return type_new(TYPE_U64);
                        }
                        if (width == 128)
                        {
                            zfree(name);
                            RECURSION_EXIT(ctx);
                            return type_new(TYPE_U128);
                        }
                    }

                    Type *inner_t = type_new(name[0] == 'u' ? TYPE_UBITINT : TYPE_BITINT);
                    inner_t->array_size = width;
                    zfree(name);
                    RECURSION_EXIT(ctx);
                    return inner_t;
                }
            }
        }

        // Relaxed Type Check: If explicit 'struct Name', trust the user.
        if (explicit_struct)
        {
            Type *ty = type_new(TYPE_STRUCT);
            ty->name = name;
            ty->is_explicit_struct = 1;
            RECURSION_EXIT(ctx);
            return ty;
        }

        // Selective imports ONLY apply when we're NOT in a module context
        if (!ctx->imports.current_module_prefix)
        {
            SelectiveImport *si = find_selective_import(ctx, name);
            if (si)
            {
                // This is a selectively imported symbol
                // Resolve to the actual struct name which was prefixed during module
                // parsing
                zfree(name);
                name = xmalloc(strlen(si->source_module) + strlen(si->symbol) + 3);
                snprintf(name, strlen(si->source_module) + strlen(si->symbol) + 3, "%s__%s",
                         si->source_module, si->symbol);
            }
        }

        // If we're IN a module and no selective import matched, apply module prefix
        // to types defined WITHIN this module. Types imported from other modules
        // (identifiable by existing as bare-named structs/aliases) are not prefixed.
        if (ctx->imports.current_module_prefix && !is_known_generic(ctx, name) &&
            !is_primitive_type_name(name) && strcasecmp(name, "Self") != 0 &&
            !is_extern_symbol(ctx, name))
        {
            // Check if this is a type imported from another module (registered
            // without prefix). If so, don't prefix — the bare name already resolves.
            int is_imported_type =
                (find_struct_def(ctx, name) != NULL) || (find_type_alias(ctx, name) != NULL);

            if (!is_imported_type)
            {
                // Auto-prefix struct name if in module context (unless it's a known
                // primitive/generic or an imported type)
                char *prefixed_name =
                    xmalloc(strlen(ctx->imports.current_module_prefix) + strlen(name) + 3);
                snprintf(prefixed_name,
                         strlen(ctx->imports.current_module_prefix) + strlen(name) + 3, "%s__%s",
                         ctx->imports.current_module_prefix, name);
                zfree(name);
                name = prefixed_name;
            }
        }

        if (!is_known_generic(ctx, name) && strcmp(name, "Self") != 0)
        {
            register_type_usage(ctx, name, t);
        }

        Type *ty = type_new(TYPE_STRUCT);
        ty->name = name;
        ty->is_explicit_struct = explicit_struct;

        // Handle Generics <T> or <K, V>
        if (lexer_peek(l).kind == TOK_LANGLE ||
            (lexer_peek(l).kind == TOK_OP && strncmp(lexer_peek(l).start, "<", 1) == 0))
        {
            lexer_next(l); // eat <
            Type *first_arg = parse_type_formal(ctx, l);
            if (!first_arg)
            {
                RECURSION_EXIT(ctx);
                return NULL;
            }
            char *first_arg_str = type_to_string(first_arg);

            // Check for multi-arg: <K, V>
            Token next_tok = lexer_peek(l);
            if (next_tok.kind == TOK_COMMA)
            {
                // Multi-arg case
                char **args = xmalloc(sizeof(char *) * 8);
                int count = 0;
                args[count++] = xstrdup(first_arg_str);

                while (lexer_peek(l).kind == TOK_COMMA)
                {
                    lexer_next(l); // eat ,
                    Type *arg = parse_type_formal(ctx, l);
                    if (!arg)
                    {
                        RECURSION_EXIT(ctx);
                        return NULL;
                    }
                    char *arg_str = type_to_string(arg);
                    args = realloc(args, sizeof(char *) * (size_t)(count + 1));
                    args[count++] = xstrdup(arg_str);
                    zfree(arg_str);
                }

                // Consume >
                next_tok = lexer_peek(l);
                if (next_tok.kind == TOK_RANGLE)
                {
                    lexer_next(l);
                }
                else if (next_tok.kind == TOK_OP && next_tok.len == 2 &&
                         strncmp(next_tok.start, ">>", 2) == 0)
                {
                    l->pos += 1;
                    l->col += 1;
                }
                else
                {
                    zpanic_at(t, "Expected > after generic");
                    return NULL;
                }

                // Call multi-arg instantiation
                int is_generic_dep = 0;
                for (int i = 0; i < count; ++i)
                {
                    if (is_generic_dependent_str(ctx, args[i]))
                    {
                        is_generic_dep = 1;
                        break;
                    }
                }

                if (!is_generic_dep)
                {
                    instantiate_generic_multi(ctx, name, args, count, t);
                }

                // Build mangled name dynamically
                size_t mangled_len = strlen(name) + 1;
                for (int i = 0; i < count; i++)
                {
                    char *clean = sanitize_mangled_name(args[i]);
                    mangled_len += 2 + strlen(clean);
                    zfree(clean);
                }
                char *mangled = xmalloc(mangled_len);
                strcpy(mangled, name);
                for (int i = 0; i < count; i++)
                {
                    char *clean = sanitize_mangled_name(args[i]);
                    strcat(mangled, "__");
                    strcat(mangled, clean);
                    zfree(clean);
                    zfree(args[i]);
                }
                zfree(args);

                zfree(ty->name);
                ty->name = mangled;
            }
            else
            {
                // Single-arg case - PRESERVE ORIGINAL FLOW EXACTLY
                if (next_tok.kind == TOK_RANGLE)
                {
                    lexer_next(l); // Consume >
                }
                else if (next_tok.kind == TOK_OP && next_tok.len == 2 &&
                         strncmp(next_tok.start, ">>", 2) == 0)
                {
                    // Split >> into two > tokens
                    l->pos += 1;
                    l->col += 1;
                }
                else
                {
                    zpanic_at(t, "Expected > after generic");
                    return NULL;
                }

                char *unmangled_arg = type_to_string(first_arg);

                int is_single_dep = is_generic_dependent_str(ctx, first_arg_str);

                if (!is_single_dep)
                {
                    instantiate_generic(ctx, name, first_arg_str, unmangled_arg, t);
                }
                zfree(unmangled_arg);

                char *clean_arg = sanitize_mangled_name(first_arg_str);
                size_t mangled_sz = strlen(name) + strlen(clean_arg) + 3;
                char *mangled = xmalloc(mangled_sz);
                snprintf(mangled, mangled_sz, "%s__%s", name, clean_arg);
                zfree(clean_arg);

                zfree(ty->name);
                ty->name = mangled;
            }

            zfree(first_arg_str);
            ty->kind = TYPE_STRUCT;
            ty->args = NULL;
            ty->count = 0;
        }
        RECURSION_EXIT(ctx);
        return ty;
    }

    if (t.kind == TOK_LBRACKET)
    {
        lexer_next(l);
        Type *inner = parse_type_formal(ctx, l);
        if (!inner)
        {
            RECURSION_EXIT(ctx);
            return NULL;
        }

        // Check for fixed-size array [T; N]
        if (lexer_peek(l).kind == TOK_SEMICOLON)
        {
            lexer_next(l); // eat ;
            ASTNode *size_expr = parse_expression(ctx, l);
            long long compiled_size = 0;
            int size = 0;
            if (eval_const_int_expr(size_expr, ctx, &compiled_size))
            {
                size = (int)compiled_size;
            }
            else
            {
                zpanic_at(size_expr->token,
                          "Array size must be a compile-time constant or integer literal");
                return NULL;
            }
            if (lexer_next(l).kind != TOK_RBRACKET)
            {
                zpanic_at(lexer_peek(l), "Expected ] after array size");
                return NULL;
            }

            Type *arr = type_new(TYPE_ARRAY);
            arr->inner = inner;
            arr->array_size = size;
            RECURSION_EXIT(ctx);
            return arr;
        }

        // Otherwise it's a slice [T]
        if (lexer_next(l).kind != TOK_RBRACKET)
        {
            zpanic_at(lexer_peek(l), "Expected ] in type");
            return NULL;
        }

        char *inner_str = type_to_string(inner);
        if (!is_known_generic(ctx, inner_str))
        {
            register_slice(ctx, inner_str);
        }

        Type *arr = type_new(TYPE_ARRAY);
        arr->inner = inner;
        arr->array_size = 0; // 0 means slice, not fixed-size
        RECURSION_EXIT(ctx);
        return arr;
    }

    if (t.kind == TOK_LPAREN)
    {
        lexer_next(l);
        char sig[MAX_SHORT_MSG_LEN];
        sig[0] = 0;
        const char *type_names[256];
        int type_count = 0;

        while (1)
        {
            Type *sub = parse_type_formal(ctx, l);
            if (!sub)
            {
                break;
            }
            char *s = type_to_string(sub);
            strcat(sig, s);
            if (type_count < 256)
            {
                type_names[type_count++] = s;
            }
            else
            {
                zfree(s);
            }

            if (lexer_peek(l).kind == TOK_COMMA)
            {
                lexer_next(l);
                strcat(sig, "__");
            }
            else
            {
                break;
            }
        }
        if (lexer_next(l).kind != TOK_RPAREN)
        {
            zpanic_at(lexer_peek(l), "Expected ) in tuple");
            return NULL;
        }

        register_tuple_with_types(ctx, sig, type_names, type_count);
        for (int i = 0; i < type_count; i++)
        {
            zfree((void *)type_names[i]);
        }

        char *clean_sig = sanitize_mangled_name(sig);
        char *tuple_name = xmalloc(strlen(clean_sig) + 8);
        snprintf(tuple_name, strlen(clean_sig) + 8, "Tuple__%s", clean_sig);
        zfree(clean_sig);

        Type *ty = type_new(TYPE_STRUCT);
        ty->name = tuple_name;
        RECURSION_EXIT(ctx);
        return ty;
    }

    // If we have an identifier that wasn't found,
    // assume it is a valid external C type
    // (for example, a struct defined in implementation).
    if (t.kind == TOK_IDENT)
    {
        char *fallback = token_strdup(t);
        lexer_next(l);
        Type *ty = type_new(TYPE_STRUCT);
        ty->name = fallback;
        ty->is_explicit_struct = 0;
        RECURSION_EXIT(ctx);
        return ty;
    }

    RECURSION_EXIT(ctx);
    return type_new(TYPE_UNKNOWN);
}
