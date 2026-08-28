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

ASTNode *parse_enum(ParserContext *ctx, Lexer *l, const char *link_name, int is_export)
{
    lexer_next(l);
    Token n = lexer_next(l);
    check_identifier(n);

    char *gp = NULL;
    if (lexer_peek(l).kind == TOK_LANGLE)
    {
        lexer_next(l); // eat <
        Token g = lexer_next(l);
        check_identifier(g);
        gp = token_strdup(g);
        lexer_next(l); // eat >
        register_generic(ctx, gp);
    }

    lexer_next(l);

    ASTNode *h = 0, *tl = 0;
    int v = 0;
    char *ename = token_strdup(n); // Store enum name

    while (1)
    {
        skip_comments(l);
        Token t = lexer_peek(l);
        if (t.kind == TOK_RBRACE)
        {
            lexer_next(l);
            break;
        }
        if (t.kind == TOK_COMMA)
        {
            lexer_next(l);
            continue;
        }
        if (t.kind == TOK_EOF)
        {
            zpanic_at(t, "Unexpected end of file in enum body");
            break;
        }

        if (t.kind == TOK_IDENT)
        {
            Token vt = lexer_next(l);
            check_identifier(vt);
            char *vname = token_strdup(vt);

            Type *payload = NULL;
            Type **tuple_types = NULL;
            int tuple_count = 0;
            if (lexer_peek(l).kind == TOK_LPAREN)
            {
                lexer_next(l);
                Type *first_t = parse_type_obj(ctx, l);

                if (lexer_peek(l).kind == TOK_COMMA)
                {
                    // Multi-arg variant -> Tuple
                    char sig[MAX_MANGLED_NAME_LEN];
                    sig[0] = 0;

                    char *s = type_to_string(first_t);
                    if (strlen(s) > 250)
                    { // Safety check
                        zpanic_at(lexer_peek(l), "Type name too long for tuple generation");
                        return NULL;
                    }
                    strcpy(sig, s);
                    zfree(s);

                    tuple_types = xmalloc(sizeof(Type *) * 32);
                    int tuple_cap = 32;
                    tuple_types[tuple_count++] = first_t;

                    while (lexer_peek(l).kind == TOK_COMMA)
                    {
                        lexer_next(l); // eat ,
                        strcat(sig, "__");
                        if (tuple_count >= tuple_cap)
                        {
                            tuple_cap *= 2;
                            tuple_types =
                                xrealloc(tuple_types, sizeof(Type *) * (size_t)(tuple_cap));
                        }
                        Type *next_t = parse_type_obj(ctx, l);
                        tuple_types[tuple_count++] = next_t;
                        char *ns = type_to_string(next_t);
                        if (strlen(sig) + strlen(ns) + 2 > 510)
                        {
                            zpanic_at(lexer_peek(l), "Tuple signature too long");
                            return NULL;
                        }
                        strcat(sig, ns);
                        zfree(ns);
                    }

                    const char **type_strs = xmalloc(sizeof(char *) * (size_t)(tuple_count));
                    for (int ti = 0; ti < tuple_count; ti++)
                    {
                        type_strs[ti] = type_to_string(tuple_types[ti]);
                    }
                    register_tuple_with_types(ctx, sig, type_strs, tuple_count);
                    for (int ti = 0; ti < tuple_count; ti++)
                    {
                        zfree((void *)type_strs[ti]);
                    }
                    zfree(type_strs);
                    char *clean_sig = sanitize_mangled_name(sig);
                    char *tuple_name = xmalloc(strlen(clean_sig) + 8);
                    sprintf(tuple_name, "Tuple__%s", clean_sig); /* safe */
                    zfree(clean_sig);

                    payload = type_new(TYPE_STRUCT);
                    payload->name = tuple_name;
                }
                else
                {
                    payload = first_t;
                }

                if (lexer_next(l).kind != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected )");
                    return NULL;
                }
            }

            ASTNode *va = ast_create(NODE_ENUM_VARIANT);
            ATTACH_DOC_COMMENT(ctx, va);
            va->variant.name = vname;
            va->variant.tag_id = v++;      // Use tag_id instead of value
            va->variant.payload = payload; // Store Type*

            // Register Variant (Mangled name to avoid collisions: Result_Ok)
            size_t mangled_sz = strlen(ename) + strlen(vname) + 3;
            char *base_for_mangling = link_name ? (char *)link_name : ename;
            mangled_sz = strlen(base_for_mangling) + strlen(vname) + 3;

            char *mangled_tmp = xmalloc(mangled_sz);
            snprintf(mangled_tmp, mangled_sz, "%s__%s", base_for_mangling, vname);
            char *mangled = merge_underscores(mangled_tmp);
            zfree(mangled_tmp);
            register_enum_variant(ctx, vname, ename, va->variant.tag_id);

            // Register Constructor Function Signature
            if (payload && !gp) // Only for non-generic enums for now
            {
                if (payload->kind == TYPE_STRUCT && strncmp(payload->name, "Tuple__", 7) == 0)
                {
                    Type *ret_t = type_new(TYPE_ENUM);
                    ret_t->name = xstrdup(ename);
                    if (link_name)
                    {
                        ret_t->link_name = xstrdup(link_name);
                    }

                    // We can reuse the tuple_types collected during parsing!
                    register_func(ctx, ctx->current_scope, mangled, tuple_count, NULL, tuple_types,
                                  ret_t, 0, 0, 0, mangled, vt, is_export);
                }
                else
                {
                    Type **at = xmalloc(sizeof(Type *));
                    at[0] = payload;
                    Type *ret_t = type_new(TYPE_ENUM);
                    ret_t->name = xstrdup(ename);
                    if (link_name)
                    {
                        ret_t->link_name = xstrdup(link_name);
                    }
                    register_func(ctx, ctx->current_scope, mangled, 1, NULL, at, ret_t, 0, 0, 0,
                                  mangled, vt, is_export);
                }
            }
            else if (!gp)
            {
                // No payload: don't register as function.
                // Codegen handles calling the constructor via codegen_var_expr.
            }
            zfree(mangled);

            // Handle explicit assignment: Ok = 5
            if (lexer_peek(l).kind == TOK_OP && *lexer_peek(l).start == '=')
            {
                lexer_next(l);
                va->variant.tag_id = (int)strtol(lexer_next(l).start, NULL, 10);
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
    if (ctx->imports.current_module_prefix && !gp)
    { // Don't prefix generic templates
        size_t pref_len = strlen(ctx->imports.current_module_prefix) + strlen(ename) + 3;
        char *prefixed_name = xmalloc(pref_len);
        snprintf(prefixed_name, pref_len, "%s__%s", ctx->imports.current_module_prefix, ename);
        zfree(ename);
        ename = prefixed_name;
    }

    ASTNode *node = ast_create(NODE_ENUM);
    node->token = n;
    node->enm.name = ename;
    node->link_name = link_name ? xstrdup(link_name) : NULL;
    node->type_info = type_new(TYPE_ENUM);
    node->type_info->name = xstrdup(ename);
    if (node->link_name)
    {
        node->type_info->link_name = xstrdup(node->link_name);
    }

    node->enm.variants = h;
    node->enm.generic_param = gp; // Store generic param
    node->enm.is_export = is_export;

    if (gp)
    {
        node->enm.is_template = 1;
        ctx->known_generics_count--;
        register_template(ctx, node->enm.name, node);
    }

    register_struct_def(ctx, node->enm.name, node);

    add_to_enum_list(ctx, node); // Register globally

    return node;
}
