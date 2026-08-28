// SPDX-License-Identifier: MIT
#include "parser.h"
#include "constants.h"
#include "expr_internal.h"
#include "analysis/move_check.h"
#include "utils/utils.h"
#include "ast/ast.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ASTNode *parse_primary_impl(ParserContext *ctx, Lexer *l);

ASTNode *parse_primary(ParserContext *ctx, Lexer *l)
{
    return parse_primary_impl(ctx, l);
}

ASTNode *parse_primary_impl(ParserContext *ctx, Lexer *l)
{
    ASTNode *node = NULL;

    Token peek = lexer_peek(l);
    if (ctx->in_method_with_self && peek.kind == TOK_OP && peek.len == 1 && peek.start[0] == '.')
    {
        Token dot_tok = lexer_next(l); // consume .
        Token member_tok = lexer_peek(l);
        if (member_tok.kind == TOK_IDENT)
        {
            lexer_next(l); // consume identifier
            // Create self node
            ASTNode *self_node = ast_create(NODE_EXPR_VAR);
            self_node->var_ref.name = xstrdup("self");
            self_node->token = dot_tok;

            Type *inner_type = type_new(TYPE_STRUCT);
            if (ctx->current_impl_struct)
            {
                inner_type->name = xstrdup(ctx->current_impl_struct);
            }
            else
            {
                inner_type->name = xstrdup("Self");
            }
            self_node->type_info = type_new_ptr(inner_type);
            if (ctx->current_impl_struct)
            {
                char *rt = xmalloc(strlen(ctx->current_impl_struct) + 2);
                sprintf(rt, "%s*", ctx->current_impl_struct); /* safe */
                self_node->resolved_type = rt;
            }
            else
            {
                self_node->resolved_type = xstrdup("Self*");
            }

            node = ast_create(NODE_EXPR_MEMBER);
            node->member.target = self_node;
            node->member.field = token_strdup(member_tok);
            node->member.is_pointer_access = 1;
            node->token = dot_tok;

            // Handle chained member access (.x.y) or method calls (.method())
            return node;
        }
        else
        {
            // Not an identifier after dot - error or other handling
            zpanic_at(dot_tok, "Expected identifier after '.' in self shorthand");
            return NULL;
        }
    }

    Token t = lexer_next(l);

    // ** Prefixes **

    // Literals
    if (t.kind == TOK_INT)
    {
        node = parse_int_literal(ctx, t);
    }
    else if (t.kind == TOK_FLOAT)
    {
        node = parse_float_literal(t);
    }
    else if (t.kind == TOK_STRING)
    {
        node = parse_string_literal(ctx, t);
    }
    else if (t.kind == TOK_FSTRING)
    {
        node = parse_fstring_literal(ctx, t);
    }
    else if (t.kind == TOK_RAW_STRING)
    {
        node = ast_create(NODE_EXPR_LITERAL);
        node->token = t;
        node->literal.kind = LITERAL_RAW_STRING;
        node->literal.string_val = token_get_string_content(t);
        node->type_info = type_new(TYPE_STRING);
    }

    else if (t.kind == TOK_CHAR)
    {
        node = parse_char_literal(t);
    }

    else if (t.kind == TOK_SIZEOF)
    {
        node = parse_sizeof_expr(ctx, l, t);
    }

    else if (t.kind == TOK_IDENT && strncmp(t.start, "typeof", 6) == 0 && t.len == 6)
    {
        node = parse_typeof_expr(ctx, l);
    }

    else if (t.kind == TOK_AT)
    {
        node = parse_intrinsic(ctx, l);
    }

    else if (t.kind == TOK_IDENT && strncmp(t.start, "if", 2) == 0 && t.len == 2)
    {
        // If-expression: parse inline (already consumed 'if')
        ASTNode *cond = parse_expression(ctx, l);

        ASTNode *then_b = NULL;
        if (lexer_peek(l).kind == TOK_LBRACE)
        {
            then_b = parse_block(ctx, l);
        }
        else
        {
            enter_scope(ctx);
            ASTNode *s = parse_statement(ctx, l);
            exit_scope(ctx);
            then_b = ast_create(NODE_BLOCK);
            then_b->block.statements = s;
        }

        ASTNode *else_b = NULL;
        skip_comments(l);
        if (lexer_peek(l).kind == TOK_IDENT && strncmp(lexer_peek(l).start, "else", 4) == 0 &&
            lexer_peek(l).len == 4)
        {
            lexer_next(l); // eat 'else'
            if (lexer_peek(l).kind == TOK_LBRACE)
            {
                else_b = parse_block(ctx, l);
            }
            else
            {
                enter_scope(ctx);
                ASTNode *s = parse_statement(ctx, l);
                exit_scope(ctx);
                else_b = ast_create(NODE_BLOCK);
                else_b->block.statements = s;
            }
        }

        node = ast_create(NODE_IF);
        node->token = t;
        node->if_stmt.condition = cond;
        node->if_stmt.then_body = then_b;
        node->if_stmt.else_body = else_b;
    }

    else if (t.kind == TOK_IDENT && strncmp(t.start, "match", 5) == 0 && t.len == 5)
    {
        ASTNode *expr = parse_expression(ctx, l);
        skip_comments(l);
        {
            Token inner_t = lexer_next(l);
            if (inner_t.kind != TOK_LBRACE)
            {
                zpanic_at(inner_t, "Expected { after match expression");
                return NULL;
            }
        }

        ASTNode *h = 0, *tl = 0;
        while (1)
        {
            skip_comments(l);
            if (lexer_peek(l).kind == TOK_RBRACE)
            {
                break;
            }
            if (lexer_peek(l).kind == TOK_EOF)
            {
                zpanic_at(lexer_peek(l), "Unexpected end of file in match body");
                break;
            }
            if (lexer_peek(l).kind == TOK_COMMA)
            {
                lexer_next(l);
            }

            skip_comments(l);
            if (lexer_peek(l).kind == TOK_RBRACE)
            {
                break;
            }

            char pat_buf[MAX_ERROR_MSG_LEN] = {0};
            while (1)
            {
                skip_comments(l);
                Token mpeek2 = lexer_peek(l);
                if (mpeek2.kind == TOK_DCOLON)
                {
                    lexer_next(l); // eat ::
                    strncat(pat_buf, "__", sizeof(pat_buf) - strlen(pat_buf) - 1);
                }
                else if (mpeek2.kind == TOK_LANGLE)
                {
                    // Handle <T> in patterns by skipping it
                    lexer_next(l); // eat <
                    int depth = 1;
                    while (depth > 0)
                    {
                        Token tk = lexer_next(l);
                        if (tk.kind == TOK_LANGLE)
                        {
                            depth++;
                        }
                        else if (tk.kind == TOK_RANGLE)
                        {
                            depth--;
                        }
                        else if (tk.kind == TOK_EOF)
                        {
                            break;
                        }
                    }
                }
                else if (mpeek2.kind == TOK_COMMA)
                {
                    lexer_next(l);
                    strncat(pat_buf, "|", sizeof(pat_buf) - strlen(pat_buf) - 1);
                }
                else if (mpeek2.kind == TOK_IDENT)
                {
                    Token pt = lexer_next(l);
                    char *s = token_strdup(pt);
                    strncat(pat_buf, s, sizeof(pat_buf) - strlen(pat_buf) - 1);
                    zfree(s);
                }
                else
                {
                    break;
                }
            }

            char *pattern = xstrdup(pat_buf);
            int is_default = (strcmp(pattern, "_") == 0);

            // Handle Destructuring: Ok(v) or Rect(w, h) or Point{x, y}
            char **bindings = NULL;
            int *binding_refs = NULL;
            int binding_count = 0;
            int is_destructure = 0;

            skip_comments(l);
            if (!is_default &&
                (lexer_peek(l).kind == TOK_LPAREN || lexer_peek(l).kind == TOK_LBRACE))
            {
                int is_brace = (lexer_peek(l).kind == TOK_LBRACE);
                lexer_next(l);
                bindings = xcalloc(8, sizeof(char *));  // Initial capacity
                binding_refs = xcalloc(8, sizeof(int)); // unused but consistent

                while (lexer_peek(l).kind != TOK_RPAREN && lexer_peek(l).kind != TOK_RBRACE)
                {
                    int is_r = 0;
                    if (lexer_peek(l).kind == TOK_IDENT && lexer_peek(l).len == 3 &&
                        strncmp(lexer_peek(l).start, "ref", 3) == 0)
                    {
                        lexer_next(l); // eat ref
                        is_r = 1;
                    }
                    Token b = lexer_next(l);
                    if (b.kind != TOK_IDENT)
                    {
                        zpanic_at(b, "Expected binding");
                        break;
                    }

                    if (is_brace && lexer_peek(l).kind == TOK_COLON)
                    {
                        lexer_next(l); // eat :
                        Token val = lexer_next(l);
                        if (val.kind == TOK_IDENT)
                        {
                            char *s = token_strdup(val);
                            if (strcmp(s, "true") == 0 || strcmp(s, "false") == 0)
                            {
                                zfree(s);
                                bindings[binding_count] = NULL;
                            }
                            else
                            {
                                bindings[binding_count] = s;
                            }
                        }
                        else
                        {
                            bindings[binding_count] = NULL;
                        }
                    }
                    else
                    {
                        bindings[binding_count] = token_strdup(b);
                    }

                    binding_refs[binding_count] = is_r;
                    if (bindings[binding_count])
                    {
                        binding_count++;
                    }

                    if (lexer_peek(l).kind == TOK_COMMA)
                    {
                        lexer_next(l);
                        continue;
                    }
                    break;
                }
                Token end = lexer_next(l);
                if (is_brace && end.kind != TOK_RBRACE)
                {
                    zpanic_at(end, "Expected }");
                    return NULL;
                }
                else if (!is_brace && end.kind != TOK_RPAREN)
                {
                    zpanic_at(end, "Expected )");
                    return NULL;
                }
                is_destructure = 1;
            }

            ASTNode *guard = NULL;
            skip_comments(l);
            if (lexer_peek(l).kind == TOK_IDENT && strncmp(lexer_peek(l).start, "if", 2) == 0)
            {
                lexer_next(l);
                guard = parse_expression(ctx, l);
            }

            skip_comments(l);
            if (lexer_next(l).kind != TOK_ARROW)
            {
                zpanic_at(lexer_peek(l), "Expected '=>'");
                return NULL;
            }

            // Create scope for the case to hold the binding
            enter_scope(ctx);
            if (binding_count > 0)
            {
                for (int i = 0; i < binding_count; i++)
                {
                    if (bindings[i])
                    {
                        add_symbol(ctx, bindings[i], NULL, type_new(TYPE_UNSAFE_ANY), 0);
                    }
                }
            }

            ASTNode *body;
            skip_comments(l);
            Token pk = lexer_peek(l);
            if (pk.kind == TOK_LBRACE)
            {
                body = parse_block(ctx, l);
            }
            else if (pk.kind == TOK_ASSERT ||
                     (pk.kind == TOK_IDENT && strncmp(pk.start, "assert", 6) == 0))
            {
                body = parse_assert(ctx, l);
            }
            else if (pk.kind == TOK_IDENT && strncmp(pk.start, "return", 6) == 0)
            {
                body = parse_return(ctx, l);
            }
            else
            {
                body = parse_expression(ctx, l);
            }

            exit_scope(ctx);

            ASTNode *c = ast_create(NODE_MATCH_CASE);
            c->token = pk;
            c->match_case.pattern = pattern;
            c->match_case.binding_names = bindings;      // New multi-binding field
            c->match_case.binding_count = binding_count; // New binding count field
            c->match_case.binding_refs = binding_refs;
            c->match_case.is_destructuring = is_destructure;
            c->match_case.guard = guard;
            c->match_case.body = body;
            c->match_case.is_default = is_default;
            if (!h)
            {
                h = c;
            }
            else
            {
                tl->next = c;
            }
            tl = c;
        }
        lexer_next(l);
        node = ast_create(NODE_MATCH);
        node->token = t; // 'match' token
        node->line = t.line;
        node->match_stmt.expr = expr;
        node->match_stmt.cases = h;
    }

    else if (t.kind == TOK_IDENT)
    {
        if (t.len == 2 && strncmp(t.start, "fn", 2) == 0 &&
            (lexer_peek(l).kind == TOK_LPAREN || lexer_peek(l).kind == TOK_LBRACKET))
        {
            l->pos -= (int)(t.len);
            l->col -= (int)(t.len);
            return parse_lambda(ctx, l);
        }

        char *ident = token_strdup(t);

        if (lexer_peek(l).kind == TOK_OP && lexer_peek(l).start[0] == '!' && lexer_peek(l).len == 1)
        {
            node = parse_macro_call(ctx, l, ident);
            if (node)
            {
                zfree(ident);
                return node;
            }
        }

        if (lexer_peek(l).kind == TOK_COLON)
        {
            Lexer lookahead = *l;
            lexer_next(&lookahead); // consume ':'

            int found_arrow = 0;
            Lexer arrow_scan = lookahead;
            int angles = 0;
            while (1)
            {
                Token tk = lexer_peek(&arrow_scan);
                if (tk.kind == TOK_EOF || tk.kind == TOK_SEMICOLON || tk.kind == TOK_COMMA ||
                    tk.kind == TOK_LBRACE || tk.kind == TOK_RBRACE ||
                    (tk.kind == TOK_OP && tk.start[0] == '=' && tk.len == 1))
                {
                    break;
                }
                if (tk.kind == TOK_LANGLE)
                {
                    angles++;
                }
                else if (tk.kind == TOK_RANGLE)
                {
                    if (angles > 0)
                    {
                        angles--;
                    }
                }
                else if (tk.kind == TOK_ARROW && angles == 0)
                {
                    found_arrow = 1;
                    break;
                }
                lexer_next(&arrow_scan);
            }

            if (found_arrow)
            {
                Type *param_type = parse_type_formal(ctx, &lookahead);
                if (!param_type)
                {
                    return NULL;
                }
                if (lexer_peek(&lookahead).kind == TOK_ARROW)
                {
                    *l = lookahead;
                    lexer_next(l); // consume '->'
                    char **params = xmalloc(sizeof(char *));
                    Type **param_types = xmalloc(sizeof(Type *));
                    params[0] = ident; // pass ownership of ident
                    param_types[0] = param_type;
                    return parse_arrow_lambda_multi(ctx, l, params, param_types, 1, 0);
                }
                // If it fails to match exactly, we'll just fall through to the rest of the parsing
            }
        }

        if (lexer_peek(l).kind == TOK_ARROW)
        {
            lexer_next(l);
            return parse_arrow_lambda_single(ctx, l, ident, 0);
        }

        char *acc = ident;
        while (1)
        {
            if (lexer_peek(l).kind == TOK_EOF)
            {
                break;
            }
            int changed = 0;
            if (lexer_peek(l).kind == TOK_DCOLON)
            {
                lexer_next(l);
                Token suffix = lexer_next(l);
                if (suffix.kind != TOK_IDENT)
                {
                    zpanic_at(suffix, "Expected identifier after ::");
                    return NULL;
                }

                SelectiveImport *si =
                    (!ctx->imports.current_module_prefix) ? find_selective_import(ctx, acc) : NULL;
                if (si)
                {
                    char *tmp =
                        xmalloc(strlen(si->source_module) + strlen(si->symbol) + suffix.len + 5);

                    char base_raw[MAX_TYPE_NAME_LEN];
                    snprintf(base_raw, sizeof(base_raw), "%s__%s", si->source_module, si->symbol);
                    char *base = merge_underscores(base_raw);
                    ASTNode *def = find_struct_def(ctx, base);
                    int is_type = (def != NULL);

                    if (is_type)
                    {
                        // Check Enum Variant
                        if (def->kind == NODE_ENUM)
                        {
                            ASTNode *v = def->enm.variants;
                            char sbuf[128];
                            if (suffix.len < (int)sizeof(sbuf))
                            {
                                strncpy(sbuf, suffix.start, suffix.len);
                                sbuf[suffix.len] = 0;
                            }
                            else
                            {
                                // Suffix too long for sbuf
                                sbuf[0] = 0;
                            }
                            while (v)
                            {
                                if (strcmp(v->variant.name, sbuf) == 0)
                                {
                                    break;
                                }
                                v = v->next;
                            }
                        }

                        char tmp_raw[MAX_MANGLED_NAME_LEN];
                        snprintf(tmp_raw, sizeof(tmp_raw), "%s__%s__%.*s", si->source_module,
                                 si->symbol, (int)suffix.len, suffix.start);
                        tmp = merge_underscores(tmp_raw);
                    }
                    else
                    {
                        char tmp_raw[MAX_MANGLED_NAME_LEN];
                        snprintf(tmp_raw, sizeof(tmp_raw), "%s__%s__%.*s", si->source_module,
                                 si->symbol, (int)suffix.len, suffix.start);
                        tmp = merge_underscores(tmp_raw);
                    }

                    zfree(acc);
                    acc = tmp;
                }
                else
                {
                    Module *mod = find_module(ctx, acc);
                    if (mod)
                    {
                        if (mod->is_c_header)
                        {
                            char *tmp = xmalloc(suffix.len + 1);
                            strncpy(tmp, suffix.start, suffix.len);
                            tmp[suffix.len] = 0;
                            zfree(acc);
                            acc = tmp;

                            register_extern_symbol(ctx, acc);
                        }
                        else
                        {
                            char sbuf[MAX_TYPE_NAME_LEN];
                            size_t sblen = suffix.len;
                            if (sblen >= sizeof(sbuf))
                            {
                                sblen = sizeof(sbuf) - 1;
                            }
                            strncpy(sbuf, suffix.start, sblen);
                            sbuf[sblen] = 0;

                            if (is_extern_symbol(ctx, sbuf))
                            {
                                zfree(acc);
                                acc = xstrdup(sbuf);
                            }
                            else
                            {
                                char tmp2_raw[MAX_MANGLED_NAME_LEN];
                                char *type_name =
                                    find_method_owner_type_scoped(ctx, mod->base_name, sbuf);
                                if (type_name)
                                {
                                    snprintf(tmp2_raw, sizeof(tmp2_raw), "%s__%s", type_name, sbuf);
                                }
                                else
                                {
                                    snprintf(tmp2_raw, sizeof(tmp2_raw), "%s__%s", mod->base_name,
                                             sbuf);
                                }
                                char *tmp2 = merge_underscores(tmp2_raw);
                                zfree(acc);
                                acc = tmp2;
                            }
                        }
                    }
                    else
                    {
                        ASTNode *def = find_struct_def(ctx, acc);

                        int is_opaque_alias = 0;
                        if (!def && ctx->imports.current_module_prefix)
                        {
                            // Module prefix is set — try the prefixed name
                            // (the struct was defined WITHIN this module, so it's
                            //  registered as prefix__name, not bare name).
                            char *prefixed = xmalloc(strlen(ctx->imports.current_module_prefix) +
                                                     strlen(acc) + 3);
                            sprintf(prefixed, "%s__%s", ctx->imports.current_module_prefix,
                                    acc); /* safe */
                            def = find_struct_def(ctx, prefixed);
                            if (def)
                            {
                                zfree(acc);
                                acc = prefixed;
                            }
                            else
                            {
                                zfree(prefixed);
                            }
                        }
                        if (!def)
                        {
                            TypeAlias *ta = find_type_alias_node(ctx, acc);
                            if (ta)
                            {
                                is_opaque_alias = ta->is_opaque;
                                if (!is_opaque_alias)
                                {
                                    const char *aliased = find_type_alias(ctx, acc);
                                    if (aliased)
                                    {
                                        zfree(acc);
                                        acc = xstrdup(aliased);
                                        def = find_struct_def(ctx, acc);
                                    }
                                }
                            }
                        }

                        char *method_name = xmalloc(suffix.len + 1);
                        strncpy(method_name, suffix.start, suffix.len);
                        method_name[suffix.len] = 0;
                        char *tmp = xmalloc(strlen(acc) + suffix.len + 32);

                        if (def || is_opaque_alias)
                        {
                            // Check for enum variant first
                            int is_variant = 0;
                            if (def && def->kind == NODE_ENUM)
                            {
                                ASTNode *v = def->enm.variants;
                                while (v)
                                {
                                    if (strcmp(v->variant.name, method_name) == 0)
                                    {
                                        is_variant = 1;
                                        break;
                                    }
                                    v = v->next;
                                }
                            }

                            if (is_variant)
                            {
                                char v_mangled_raw[MAX_MANGLED_NAME_LEN];
                                snprintf(v_mangled_raw, sizeof(v_mangled_raw), "%s__%s", acc,
                                         method_name);
                                char *v_mangled = merge_underscores(v_mangled_raw);

                                if (!find_func(ctx, v_mangled))
                                {
                                    char *concrete_type =
                                        find_method_owner_type_scoped(ctx, acc, method_name);
                                    if (concrete_type)
                                    {
                                        char concrete_raw[MAX_MANGLED_NAME_LEN];
                                        snprintf(concrete_raw, sizeof(concrete_raw), "%s__%s",
                                                 concrete_type, method_name);
                                        char *concrete = merge_underscores(concrete_raw);
                                        if (find_func(ctx, concrete))
                                        {
                                            zfree(acc);
                                            acc = concrete;
                                            strcpy(tmp, acc);
                                            zfree(v_mangled);
                                            zfree(concrete_type);
                                            break;
                                        }
                                        zfree(concrete);
                                        zfree(concrete_type);
                                    }
                                }
                                if (v_mangled)
                                {
                                    zfree(acc);
                                    acc = v_mangled;
                                    strcpy(tmp, acc);
                                }
                            }
                            else
                            {
                                char direct_raw[MAX_MANGLED_NAME_LEN];
                                snprintf(direct_raw, sizeof(direct_raw), "%s__%s", acc,
                                         method_name);
                                char *direct = merge_underscores(direct_raw);

                                if (find_func(ctx, direct))
                                {
                                    zfree(acc);
                                    acc = direct;
                                    strcpy(tmp, acc);
                                }
                                else
                                {
                                    char *concrete_type =
                                        find_method_owner_type_scoped(ctx, acc, method_name);
                                    if (concrete_type)
                                    {
                                        char concrete_raw[MAX_MANGLED_NAME_LEN];
                                        snprintf(concrete_raw, sizeof(concrete_raw), "%s__%s",
                                                 concrete_type, method_name);
                                        char *concrete = merge_underscores(concrete_raw);
                                        if (find_func(ctx, concrete))
                                        {
                                            zfree(acc);
                                            acc = concrete;
                                            strcpy(tmp, acc);
                                            zfree(method_name);
                                            zfree(concrete_type);
                                            break;
                                        }
                                        zfree(concrete);
                                        zfree(concrete_type);
                                    }

                                    // Fallback: check for trait implementations
                                    StructRef *ref = ctx->parsed_impls_list;
                                    int found_trait = 0;
                                    while (ref)
                                    {
                                        if (ref->node && ref->node->kind == NODE_IMPL_TRAIT &&
                                            strcmp(ref->node->impl_trait.target_type, acc) == 0)
                                        {
                                            const char *tname = ref->node->impl_trait.trait_name;
                                            const char *sep = (strcmp(tname, "Drop") == 0 ||
                                                               strcmp(tname, "Clone") == 0 ||
                                                               strcmp(tname, "Eq") == 0 ||
                                                               strcmp(tname, "Copy") == 0 ||
                                                               strcmp(tname, "Iterable") == 0)
                                                                  ? "_"
                                                                  : "__";

                                            char t_m_raw[MAX_MANGLED_NAME_LEN];
                                            snprintf(t_m_raw, sizeof(t_m_raw), "%s__%s%s%s", acc,
                                                     tname, sep, method_name);
                                            char *t_m = merge_underscores(t_m_raw);

                                            if (find_func(ctx, t_m))
                                            {
                                                zfree(acc);
                                                acc = xstrdup(t_m);
                                                strcpy(tmp, acc);
                                                found_trait = 1;
                                                break;
                                            }
                                        }
                                        ref = ref->next;
                                    }
                                    if (!found_trait)
                                    {
                                        zfree(acc);
                                        acc = direct;
                                        strcpy(tmp, acc);
                                    }
                                    else
                                    {
                                        zfree(direct);
                                    }
                                }
                            }
                        }
                        else
                        {
                            // Handle generics or other cases
                            int handled_as_generic = 0;
                            for (int i = 0; i < ctx->known_generics_count; i++)
                            {
                                char *gname = ctx->known_generics[i];
                                size_t glen = strlen(gname);
                                if (strncmp(acc, gname, (size_t)(glen)) == 0 && acc[glen] == '_' &&
                                    acc[glen + 1] == '_')
                                {
                                    ASTNode *tpl_def = find_struct_def(ctx, gname);
                                    if (tpl_def)
                                    {
                                        int is_v = 0;
                                        if (tpl_def->kind == NODE_ENUM)
                                        {
                                            ASTNode *v = tpl_def->enm.variants;
                                            while (v)
                                            {
                                                if (strcmp(v->variant.name, method_name) == 0)
                                                {
                                                    is_v = 1;
                                                    break;
                                                }
                                                v = v->next;
                                            }
                                        }
                                        if (is_v)
                                        {
                                            char tmp_raw[MAX_MANGLED_NAME_LEN];
                                            snprintf(tmp_raw, sizeof(tmp_raw), "%s__%s", acc,
                                                     method_name);
                                            tmp = merge_underscores(tmp_raw);
                                            handled_as_generic = 1;
                                        }
                                    }
                                }
                            }

                            // Also check registered templates list
                            if (!handled_as_generic)
                            {
                                GenericTemplate *gt = ctx->templates;
                                while (gt)
                                {
                                    char *gname = gt->name;
                                    size_t glen = strlen(gname);
                                    if ((strncmp(acc, gname, (size_t)(glen)) == 0 &&
                                         acc[glen] == '_' && acc[glen + 1] == '_') ||
                                        strcmp(acc, gname) == 0)
                                    {
                                        ASTNode *tpl_def = gt->struct_node;
                                        if (tpl_def)
                                        {
                                            int is_variant = 0;
                                            if (tpl_def->kind == NODE_ENUM)
                                            {
                                                ASTNode *v = tpl_def->enm.variants;
                                                char sbuf[128];
                                                if (suffix.len < (int)sizeof(sbuf))
                                                {
                                                    strncpy(sbuf, suffix.start, suffix.len);
                                                    sbuf[suffix.len] = 0;
                                                }
                                                else
                                                {
                                                    sbuf[0] = 0;
                                                }
                                                while (v)
                                                {
                                                    if (strcmp(v->variant.name, sbuf) == 0)
                                                    {
                                                        is_variant = 1;
                                                        break;
                                                    }
                                                    v = v->next;
                                                }
                                            }
                                            if (is_variant)
                                            {
                                                char tmp_raw[MAX_MANGLED_NAME_LEN];
                                                snprintf(tmp_raw, sizeof(tmp_raw), "%s__%.*s", acc,
                                                         (int)suffix.len, suffix.start);
                                                char *direct = merge_underscores(tmp_raw);
                                                // Do not resolve a generic-dependent path (e.g.
                                                // Option__V) to an existing concrete instantiation:
                                                // the concrete arg is a placeholder substituted at
                                                // instantiation time.
                                                if (!is_generic_dependent_str(ctx, acc) &&
                                                    !find_func(ctx, direct))
                                                {
                                                    char mbuf[128];
                                                    if (suffix.len < (int)sizeof(mbuf))
                                                    {
                                                        strncpy(mbuf, suffix.start, suffix.len);
                                                        mbuf[suffix.len] = 0;
                                                    }
                                                    else
                                                    {
                                                        mbuf[0] = 0;
                                                    }
                                                    char *concrete_type =
                                                        find_method_owner_type_scoped(ctx, acc,
                                                                                      mbuf);
                                                    if (concrete_type)
                                                    {
                                                        char cr[MAX_MANGLED_NAME_LEN];
                                                        snprintf(cr, sizeof(cr), "%s__%s",
                                                                 concrete_type, mbuf);
                                                        char *concrete = merge_underscores(cr);
                                                        if (find_func(ctx, concrete))
                                                        {
                                                            tmp = concrete;
                                                            zfree(direct);
                                                            zfree(concrete_type);
                                                            handled_as_generic = 1;
                                                            break;
                                                        }
                                                        zfree(concrete);
                                                        zfree(concrete_type);
                                                    }
                                                }
                                                if (!handled_as_generic)
                                                {
                                                    tmp = direct;
                                                }
                                            }
                                            else
                                            {
                                                char tmp_raw[MAX_MANGLED_NAME_LEN];
                                                snprintf(tmp_raw, sizeof(tmp_raw), "%s__%.*s", acc,
                                                         (int)suffix.len, suffix.start);
                                                char *direct = merge_underscores(tmp_raw);
                                                // Do not resolve a generic-dependent path (e.g.
                                                // Option__V) to an existing concrete instantiation:
                                                // the concrete arg is a placeholder substituted at
                                                // instantiation time.
                                                if (!is_generic_dependent_str(ctx, acc) &&
                                                    !find_func(ctx, direct))
                                                {
                                                    char mbuf[128];
                                                    if (suffix.len < (int)sizeof(mbuf))
                                                    {
                                                        strncpy(mbuf, suffix.start, suffix.len);
                                                        mbuf[suffix.len] = 0;
                                                    }
                                                    else
                                                    {
                                                        mbuf[0] = 0;
                                                    }
                                                    char *concrete_type =
                                                        find_method_owner_type_scoped(ctx, acc,
                                                                                      mbuf);
                                                    if (concrete_type)
                                                    {
                                                        char cr[MAX_MANGLED_NAME_LEN];
                                                        snprintf(cr, sizeof(cr), "%s__%s",
                                                                 concrete_type, mbuf);
                                                        char *concrete = merge_underscores(cr);
                                                        if (find_func(ctx, concrete))
                                                        {
                                                            tmp = concrete;
                                                            zfree(direct);
                                                            zfree(concrete_type);
                                                            handled_as_generic = 1;
                                                            break;
                                                        }
                                                        zfree(concrete);
                                                        zfree(concrete_type);
                                                    }
                                                }
                                                if (!handled_as_generic)
                                                {
                                                    tmp = direct;
                                                }
                                            }
                                            handled_as_generic = 1;
                                            break;
                                        }
                                    }
                                    gt = gt->next;
                                }
                            }

                            if (!handled_as_generic)
                            {
                                char combined_raw[MAX_MANGLED_NAME_LEN];
                                snprintf(combined_raw, sizeof(combined_raw), "%s__%.*s", acc,
                                         (int)suffix.len, suffix.start);
                                char *combined = merge_underscores(combined_raw);
                                zfree(acc);
                                acc = combined;
                                strcpy(tmp, acc);
                            }
                        }
                        zfree(method_name);
                        zfree(acc);
                        acc = tmp;
                    }
                }
                changed = 1;
            }

            if (lexer_peek(l).kind == TOK_LANGLE)
            {
                Lexer lookahead = *l;
                lexer_next(&lookahead);

                int valid_generic = 0;
                int saved_speculative = ctx->is_speculative;
                ctx->is_speculative = 1;
                while (1)
                {
                    parse_type(ctx, &lookahead);
                    if (lexer_peek(&lookahead).kind == TOK_COMMA)
                    {
                        lexer_next(&lookahead);
                        continue;
                    }
                    if (lexer_peek(&lookahead).kind == TOK_RANGLE)
                    {
                        valid_generic = 1;
                    }
                    break;
                }
                ctx->is_speculative = saved_speculative;

                if (valid_generic)
                {
                    lexer_next(l); // eat <

                    char **concrete_types = xmalloc(sizeof(char *) * 8);
                    char **unmangled_types = xmalloc(sizeof(char *) * 8);
                    int count = 0;

                    while (1)
                    {
                        if (count >= 8)
                        {
                            zpanic_at(lexer_peek(l), "Too many generic arguments (max 8)");
                            break;
                        }
                        Type *formal_type = parse_type_formal(ctx, l);
                        if (!formal_type)
                        {
                            return NULL;
                        }
                        concrete_types[count] = type_to_string(formal_type);
                        unmangled_types[count] = type_to_string(formal_type);
                        count++;

                        if (lexer_peek(l).kind == TOK_COMMA)
                        {
                            lexer_next(l);
                            continue;
                        }
                        break;
                    }
                    lexer_next(l); // eat >

                    int is_struct = 0;
                    GenericTemplate *st = ctx->templates;
                    while (st)
                    {
                        if (strcmp(st->name, acc) == 0)
                        {
                            is_struct = 1;
                            break;
                        }
                        st = st->next;
                    }
                    if (!is_struct && (strcmp(acc, "Result") == 0 || strcmp(acc, "Option") == 0))
                    {
                        is_struct = 1;
                    }

                    if (is_struct)
                    {
                        // Calculate mangled length
                        size_t mangled_len = strlen(acc) + 1;
                        for (int i = 0; i < count; ++i)
                        {
                            char *clean = sanitize_mangled_name(concrete_types[i]);
                            mangled_len += 2 + strlen(clean);
                            zfree(clean);
                        }
                        char *mangled = xmalloc(mangled_len);
                        strcpy(mangled, acc);
                        for (int i = 0; i < count; ++i)
                        {
                            char *clean = sanitize_mangled_name(concrete_types[i]);
                            strcat(mangled, "__");
                            strcat(mangled, clean);
                            zfree(clean);
                        }

                        int is_generic_dep = 0;
                        for (int i = 0; i < count; ++i)
                        {
                            if (is_generic_dependent_str(ctx, concrete_types[i]))
                            {
                                is_generic_dep = 1;
                                break;
                            }
                        }

                        if (count == 1)
                        {
                            // Single-arg: only instantiate if not generic dependent
                            if (!is_generic_dep)
                            {
                                instantiate_generic(ctx, acc, concrete_types[0], unmangled_types[0],
                                                    t);
                            }
                            zfree(acc);
                            acc = mangled;
                        }
                        else
                        {
                            // Multi-arg struct instantiation
                            if (!is_generic_dep)
                            {
                                instantiate_generic_multi(ctx, acc, concrete_types, count, t);
                            }
                            zfree(acc);
                            acc = mangled;
                        }
                    }
                    else
                    {
                        // Function Template
                        // Join types with comma
                        char full_concrete[MAX_ERROR_MSG_LEN] = {0};
                        char full_unmangled[MAX_ERROR_MSG_LEN] = {0};

                        for (int i = 0; i < count; ++i)
                        {
                            if (i > 0)
                            {
                                strncat(full_concrete, ",",
                                        sizeof(full_concrete) - strlen(full_concrete) - 1);
                                strncat(full_unmangled, ",",
                                        sizeof(full_unmangled) - strlen(full_unmangled) - 1);
                            }
                            strncat(full_concrete, concrete_types[i],
                                    sizeof(full_concrete) - strlen(full_concrete) - 1);
                            strncat(full_unmangled, unmangled_types[i],
                                    sizeof(full_unmangled) - strlen(full_unmangled) - 1);
                        }

                        char *m =
                            instantiate_function_template(ctx, acc, full_concrete, full_unmangled);
                        if (m)
                        {
                            zfree(acc);
                            acc = m;
                        }
                        else
                        {
                            zpanic_at(t, "Unknown generic %s", acc);
                            return NULL;
                        }
                    }

                    // Cleanup
                    for (int i = 0; i < count; ++i)
                    {
                        zfree(concrete_types[i]);
                        zfree(unmangled_types[i]);
                    }
                    zfree(concrete_types);
                    zfree(unmangled_types);

                    changed = 1;
                }
            }
            if (!changed)
            {
                break;
            }
        }

        if (lexer_peek(l).kind == TOK_LBRACE)
        {
            int is_struct_init = 0;
            Lexer pl = *l;
            lexer_next(&pl);
            Token fi = lexer_peek(&pl);

            if (fi.kind == TOK_RBRACE)
            {
                // Empty struct init often conflicts with block start (e.g. if x == y {})
                // We allow it only if we verify 'acc' is a struct name.
                if (find_struct_def(ctx, acc) || is_known_generic(ctx, acc) ||
                    is_primitive_type_name(acc))
                {
                    is_struct_init = 1;
                }
                else
                {
                    // Fallback: Check if it's a generic instantiation (e.g. Optional_T)
                    // We check if 'acc' starts with any known struct name followed by '_'
                    StructRef *sr = ctx->parsed_structs_list;
                    while (sr)
                    {
                        if (sr->node && sr->node->kind == NODE_STRUCT)
                        {
                            size_t len = strlen(sr->node->strct.name);
                            if (strncmp(acc, sr->node->strct.name, (size_t)(len)) == 0 &&
                                acc[len] == '_' && acc[len + 1] == '_')
                            {
                                is_struct_init = 1;
                                break;
                            }
                        }
                        sr = sr->next;
                    }

                    if (!is_struct_init && ctx->cg.global_user_structs)
                    {
                        ASTNode *gn = ctx->cg.global_user_structs;
                        while (gn)
                        {
                            if (gn->kind == NODE_STRUCT)
                            {
                                size_t len = strlen(gn->strct.name);
                                if (strncmp(acc, gn->strct.name, (size_t)(len)) == 0 &&
                                    acc[len] == '_')
                                {
                                    is_struct_init = 1;
                                    break;
                                }
                            }
                            gn = gn->next;
                        }
                    }

                    if (!is_struct_init)
                    {
                        is_struct_init = 0;
                    }
                }
            }
            else if (fi.kind == TOK_IDENT)
            {
                lexer_next(&pl);
                if (lexer_peek(&pl).kind == TOK_COLON)
                {
                    is_struct_init = 1;
                }
            }
            else
            {
                // Vector positional init: e.g. f32x4{1.0, 2.0, 3.0, 4.0}
                // First token is not an ident (it's a literal/expr), check if this is a vector type
                ASTNode *vec_def = find_struct_def(ctx, acc);
                if (vec_def && vec_def->kind == NODE_STRUCT && vec_def->type_info &&
                    vec_def->type_info->kind == TYPE_VECTOR)
                {
                    is_struct_init = 1;
                }
            }

            // Allow primitive types to be initialized with positional values e.g., int{5}
            if (!is_struct_init && is_primitive_type_name(acc))
            {
                is_struct_init = 1;
            }

            if (is_struct_init)
            {
                // Special case for primitive types (e.g. i32{})
                if (is_primitive_type_name(acc))
                {
                    lexer_next(l); // Eat {
                    if (lexer_peek(l).kind == TOK_RBRACE)
                    {
                        lexer_next(l); // Eat }
                        // Return 0 for empty primitive init
                        node = ast_create(NODE_EXPR_LITERAL);
                        node->literal.kind = LITERAL_INT;
                        node->literal.int_val = 0;
                        // Determine type kind from name
                        TypeKind tk = get_primitive_type_kind(acc);
                        if (tk == TYPE_UNKNOWN)
                        {
                            tk = TYPE_INT; // fallback
                        }

                        if (tk == TYPE_F32 || tk == TYPE_F64 || tk == TYPE_FLOAT)
                        {
                            node->literal.kind = LITERAL_FLOAT;
                            node->literal.float_val = 0.0;
                        }

                        node->type_info = type_new(tk);
                        return node;
                    }
                    else
                    {
                        node = parse_expression(ctx, l);
                        if (lexer_peek(l).kind != TOK_RBRACE)
                        {
                            zpanic_at(lexer_peek(l), "Expected '}' after primitive initialization");
                            return NULL;
                        }
                        lexer_next(l); // Eat }

                        if (is_trait(acc))
                        {
                            return transform_to_trait_object(ctx, acc, node);
                        }

                        ASTNode *cast = ast_create(NODE_EXPR_CAST);
                        cast->cast.target_type = xstrdup(acc);
                        cast->cast.expr = node;

                        TypeKind tk = get_primitive_type_kind(acc);
                        if (tk == TYPE_UNKNOWN)
                        {
                            tk = TYPE_INT;
                        }

                        cast->type_info = type_new(tk);
                        return cast;
                    }
                }

                char *struct_name = acc;
                if (!ctx->imports.current_module_prefix)
                {
                    SelectiveImport *si = find_selective_import(ctx, acc);
                    if (si)
                    {
                        char struct_name_raw[MAX_MANGLED_NAME_LEN];
                        snprintf(struct_name_raw, sizeof(struct_name_raw), "%s__%s",
                                 si->source_module, si->symbol); /* TODO: check buffer size */
                        struct_name = merge_underscores(struct_name_raw);
                    }
                }
                if (struct_name == acc && ctx->imports.current_module_prefix &&
                    !is_known_generic(ctx, acc))
                {
                    // Only prefix module-defined types, not imported types.
                    // Imports are registered with bare names, so if find_struct_def
                    // succeeds on the bare name, this is an imported type — skip.
                    if (!find_struct_def(ctx, acc) && !find_type_alias(ctx, acc))
                    {
                        char prefixed_raw[MAX_MANGLED_NAME_LEN];
                        snprintf(prefixed_raw, sizeof(prefixed_raw), "%s__%s",
                                 ctx->imports.current_module_prefix,
                                 acc); /* TODO: check buffer size */
                        struct_name = merge_underscores(prefixed_raw);
                    }
                }

                // Opaque Struct Check
                ASTNode *def = find_struct_def(ctx, struct_name);
                if (def && def->kind == NODE_STRUCT && def->strct.is_opaque)
                {
                    if (!def->strct.defined_in_file ||
                        (ctx->current_filename &&
                         strcmp(def->strct.defined_in_file, ctx->current_filename) != 0))
                    {
                        zpanic_at(lexer_peek(l),
                                  "Cannot initialize opaque struct '%s' outside its module",
                                  struct_name);
                        return NULL;
                    }
                }
                lexer_next(l);
                node = ast_create(NODE_EXPR_STRUCT_INIT);
                node->token = t;
                node->struct_init.struct_name = struct_name;
                Type *init_type = type_new(TYPE_STRUCT);
                init_type->name = xstrdup(struct_name);
                node->type_info = init_type;

                ASTNode *head = NULL, *tail = NULL;
                int first = 1;

                // Check if this is a vector type for positional init
                int is_vector_init = 0;
                if (def && def->kind == NODE_STRUCT && def->type_info &&
                    def->type_info->kind == TYPE_VECTOR)
                {
                    // Peek ahead: if first value is not followed by ':',
                    // this is positional vector init like f32x4{1.0, 2.0, 3.0, 4.0}
                    Lexer saved = *l;
                    Token first_tok = lexer_next(l);
                    if (first_tok.kind != TOK_RBRACE)
                    {
                        Token maybe_colon = lexer_peek(l);
                        if (maybe_colon.kind != TOK_COLON)
                        {
                            is_vector_init = 1;
                        }
                    }
                    *l = saved; // restore lexer state
                }

                if (is_vector_init)
                {
                    // Parse positional values: f32x4{1.0, 2.0, 3.0, 4.0}
                    int idx = 0;
                    while (lexer_peek(l).kind != TOK_RBRACE)
                    {
                        if (idx > 0 && lexer_peek(l).kind == TOK_COMMA)
                        {
                            lexer_next(l);
                        }
                        if (lexer_peek(l).kind == TOK_RBRACE)
                        {
                            break;
                        }
                        ASTNode *val = parse_expression(ctx, l);
                        ASTNode *assign = ast_create(NODE_VAR_DECL);
                        char name[16];
                        snprintf(name, sizeof(name), "_v%d", idx);
                        assign->token = t;
                        assign->var_decl.name = xstrdup(name);
                        assign->var_decl.init_expr = val;
                        if (!head)
                        {
                            head = assign;
                        }
                        else
                        {
                            tail->next = assign;
                        }
                        tail = assign;
                        idx++;
                    }
                    lexer_next(l); // eat '}'
                }
                else
                {
                    while (lexer_peek(l).kind != TOK_RBRACE)
                    {
                        if (!first && lexer_peek(l).kind == TOK_COMMA)
                        {
                            lexer_next(l);
                        }
                        if (lexer_peek(l).kind == TOK_RBRACE)
                        {
                            break;
                        }
                        Token fn = lexer_next(l);
                        if (lexer_next(l).kind != TOK_COLON)
                        {
                            zpanic_at(lexer_peek(l), "Expected :");
                            return NULL;
                        }
                        ASTNode *val = parse_expression(ctx, l);
                        ASTNode *assign = ast_create(NODE_VAR_DECL);
                        assign->token = t;
                        assign->var_decl.name = token_strdup(fn);
                        assign->var_decl.init_expr = val;
                        if (!head)
                        {
                            head = assign;
                        }
                        else
                        {
                            tail->next = assign;
                        }
                        tail = assign;
                        first = 0;
                    }
                    lexer_next(l);
                }
                node->struct_init.fields = head;

                GenericTemplate *gtpl = ctx->templates;
                while (gtpl)
                {
                    if (strcmp(gtpl->name, acc) == 0 || strcmp(gtpl->name, struct_name) == 0)
                    {
                        break;
                    }
                    gtpl = gtpl->next;
                }
                if (gtpl && gtpl->struct_node && gtpl->struct_node->kind == NODE_STRUCT)
                {
                    const char *gen_param = (gtpl->struct_node->strct.generic_param_count > 0)
                                                ? gtpl->struct_node->strct.generic_params[0]
                                                : "T";

                    char *inferred = NULL;
                    ASTNode *init_field = head;
                    while (init_field && !inferred)
                    {
                        if (init_field->var_decl.init_expr && init_field->var_decl.name)
                        {
                            ASTNode *tpl_field = gtpl->struct_node->strct.fields;
                            while (tpl_field)
                            {
                                if (tpl_field->kind == NODE_FIELD && tpl_field->field.name &&
                                    strcmp(tpl_field->field.name, init_field->var_decl.name) == 0)
                                {
                                    break;
                                }
                                tpl_field = tpl_field->next;
                            }

                            if (tpl_field && tpl_field->field.type)
                            {
                                const char *ft = tpl_field->field.type;
                                Type *val_type = init_field->var_decl.init_expr->type_info;

                                if (strcmp(ft, gen_param) == 0 && val_type)
                                {
                                    inferred = type_to_string(val_type);
                                }
                                else if (val_type && strncmp(ft, "Slice__", 7) == 0 &&
                                         strcmp(ft + 7, gen_param) == 0)
                                {
                                    if (val_type->kind == TYPE_ARRAY && val_type->inner)
                                    {
                                        inferred = type_to_string(val_type->inner);
                                    }
                                }
                                else if (val_type && ft[0] == '[')
                                {
                                    size_t ftl = strlen(ft);
                                    if (ftl >= 3 && ft[ftl - 1] == ']')
                                    {
                                        char inner_name[MAX_TYPE_NAME_LEN];
                                        size_t inner_len = ftl - 2;
                                        if (inner_len < sizeof(inner_name))
                                        {
                                            memcpy(inner_name, ft + 1, (size_t)(inner_len));
                                            inner_name[inner_len] = '\0';
                                            if (strcmp(inner_name, gen_param) == 0 &&
                                                val_type->kind == TYPE_ARRAY && val_type->inner)
                                            {
                                                inferred = type_to_string(val_type->inner);
                                            }
                                        }
                                    }
                                }
                                else if (val_type)
                                {
                                    size_t ftl = strlen(ft);
                                    if (ftl >= 2 && ft[ftl - 1] == '*')
                                    {
                                        char base_name[MAX_TYPE_NAME_LEN];
                                        size_t base_len = ftl - 1;
                                        if (base_len < sizeof(base_name))
                                        {
                                            memcpy(base_name, ft, (size_t)(base_len));
                                            base_name[base_len] = '\0';
                                            if (strcmp(base_name, gen_param) == 0 &&
                                                val_type->kind == TYPE_POINTER && val_type->inner)
                                            {
                                                inferred = type_to_string(val_type->inner);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        init_field = init_field->next;
                    }

                    if (inferred)
                    {
                        Token t_tok = lexer_peek(l);
                        instantiate_generic(ctx, gtpl->name, inferred, inferred, t_tok);

                        char *clean = sanitize_mangled_name(inferred);
                        char mangled_raw[MAX_MANGLED_NAME_LEN];
                        snprintf(mangled_raw, sizeof(mangled_raw), "%s__%s", gtpl->name, clean);
                        char *mangled = merge_underscores(mangled_raw);
                        zfree(clean);

                        node->struct_init.struct_name = mangled;
                        struct_name = mangled;

                        zfree(inferred);
                    }
                }

                Type *st = type_new(TYPE_STRUCT);
                st->name = xstrdup(struct_name);
                node->type_info = st;
                return node;
            }
        }

        FuncSig *sig = find_func(ctx, acc);
        if (strcmp(acc, "readln") == 0 && lexer_peek(l).kind == TOK_LPAREN)
        {
            lexer_next(l);
            ASTNode *args[16];
            int ac = 0;
            if (lexer_peek(l).kind != TOK_RPAREN)
            {
                while (1)
                {
                    if (ac >= 16)
                    {
                        zpanic_at(lexer_peek(l), "Too many arguments (max 16)");
                        break;
                    }
                    args[ac++] = parse_expression(ctx, l);
                    if (lexer_peek(l).kind == TOK_COMMA)
                    {
                        lexer_next(l);
                    }
                    else
                    {
                        break;
                    }
                }
            }
            if (lexer_next(l).kind != TOK_RPAREN)
            {
                zpanic_at(lexer_peek(l), "Expected )");
                return NULL;
            }

            if (ac == 0)
            {
                node = ast_create(NODE_EXPR_CALL);
                node->token = t;
                ASTNode *callee = ast_create(NODE_EXPR_VAR);
                callee->var_ref.name = xstrdup("_z_readln_raw");
                node->call.callee = callee;
                node->type_info = type_new(TYPE_STRING);
            }
            else
            {
                char *fmt = infer_printf_format(ctx, args, ac);

                node = ast_create(NODE_EXPR_CALL);
                node->token = t;
                ASTNode *callee = ast_create(NODE_EXPR_VAR);
                callee->var_ref.name = xstrdup("_z_scan_helper");
                node->call.callee = callee;
                node->type_info = type_new(TYPE_INT);

                ASTNode *fmt_node = ast_create(NODE_EXPR_LITERAL);
                fmt_node->literal.kind = LITERAL_STRING; // string
                fmt_node->literal.string_val = xstrdup(fmt);

                ASTNode *head = fmt_node, *tail = fmt_node;

                for (int i = 0; i < ac; i++)
                {
                    // Create Unary & (AddressOf) node wrapping the arg
                    ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                    addr->unary.op = xstrdup("&");
                    addr->unary.operand = args[i];
                    // Link
                    tail->next = addr;
                    tail = addr;
                }
                node->call.args = head;
                zfree(fmt);
            }
            zfree(acc);
        }
        else if (sig && lexer_peek(l).kind == TOK_LPAREN)
        {
            (void)lexer_next(l);
            CallArgs args_call = parse_call_args(ctx, l, sig);
            ASTNode *head = args_call.head;
            ASTNode *tail = args_call.tail;
            char **arg_names = args_call.arg_names;
            int args_provided = args_call.count;
            int has_named = args_call.has_named;

            if (lexer_next(l).kind != TOK_RPAREN)
            {
                zpanic_at(lexer_peek(l), "Expected )");
                return NULL;
            }
            for (int i = args_provided; sig->arg_types && i < sig->total_args; i++)
            {
                if (sig->defaults[i])
                {
                    Lexer def_l;
                    lexer_init(&def_l, sig->defaults[i], ctx->config, ctx->current_filename);
                    ASTNode *def = parse_expression(ctx, &def_l);
                    Type *expected = sig->arg_types[i];
                    if (expected && expected->name && is_trait(expected->name))
                    {
                        Type *arg_type = def->type_info
                                             ? def->type_info
                                             : ((def->kind == NODE_EXPR_VAR)
                                                    ? find_symbol_type_info(ctx, def->var_ref.name)
                                                    : NULL);

                        if (!arg_type && def->kind == NODE_EXPR_UNARY &&
                            strcmp(def->unary.op, "&") == 0)
                        {
                            if (def->unary.operand->kind == NODE_EXPR_VAR)
                            {
                                Type *inner =
                                    find_symbol_type_info(ctx, def->unary.operand->var_ref.name);
                                if (inner && inner->kind == TYPE_STRUCT)
                                {
                                    if (check_impl(ctx, expected->name, inner->name))
                                    {
                                        ASTNode *init = ast_create(NODE_EXPR_STRUCT_INIT);
                                        init->struct_init.struct_name = xstrdup(expected->name);

                                        Type *trait_type = type_new(TYPE_STRUCT);
                                        trait_type->name = xstrdup(expected->name);
                                        init->type_info = trait_type;

                                        ASTNode *f_self = ast_create(NODE_VAR_DECL);
                                        f_self->var_decl.name = xstrdup("self");
                                        f_self->var_decl.init_expr = def;

                                        char v_raw[MAX_MANGLED_NAME_LEN];
                                        snprintf(v_raw, sizeof(v_raw), "%s__%s__VTable",
                                                 inner->name, /* TODO: check buffer size */
                                                 expected->name);
                                        char *vtable_name = merge_underscores(v_raw);

                                        ASTNode *vtable_var = ast_create(NODE_EXPR_VAR);
                                        vtable_var->var_ref.name = xstrdup(vtable_name);

                                        ASTNode *vtable_ref = ast_create(NODE_EXPR_UNARY);
                                        vtable_ref->unary.op = xstrdup("&");
                                        vtable_ref->unary.operand = vtable_var;

                                        ASTNode *f_vtable = ast_create(NODE_VAR_DECL);
                                        f_vtable->var_decl.name = xstrdup("vtable");
                                        f_vtable->var_decl.init_expr = vtable_ref;

                                        f_self->next = f_vtable;
                                        init->struct_init.fields = f_self;

                                        def = init;
                                    }
                                }
                            }
                        }
                        else if (arg_type && arg_type->kind == TYPE_POINTER && arg_type->inner &&
                                 arg_type->inner->kind == TYPE_STRUCT)
                        {
                            if (check_impl(ctx, expected->name, arg_type->inner->name))
                            {
                                ASTNode *init = ast_create(NODE_EXPR_STRUCT_INIT);
                                init->struct_init.struct_name = xstrdup(expected->name);

                                Type *trait_type = type_new(TYPE_STRUCT);
                                trait_type->name = xstrdup(expected->name);
                                init->type_info = trait_type;

                                ASTNode *f_self = ast_create(NODE_VAR_DECL);
                                f_self->var_decl.name = xstrdup("self");
                                f_self->var_decl.init_expr = def;

                                char v_raw[MAX_MANGLED_NAME_LEN];
                                snprintf(v_raw, sizeof(v_raw), "%s__%s__VTable",
                                         arg_type->inner->name, /* TODO: check buffer size */
                                         expected->name);
                                char *vtable_name = merge_underscores(v_raw);

                                ASTNode *vtable_var = ast_create(NODE_EXPR_VAR);
                                vtable_var->var_ref.name = xstrdup(vtable_name);

                                ASTNode *vtable_ref = ast_create(NODE_EXPR_UNARY);
                                vtable_ref->unary.op = xstrdup("&");
                                vtable_ref->unary.operand = vtable_var;

                                ASTNode *f_vtable = ast_create(NODE_VAR_DECL);
                                f_vtable->var_decl.name = xstrdup("vtable");
                                f_vtable->var_decl.init_expr = vtable_ref;

                                f_self->next = f_vtable;
                                init->struct_init.fields = f_self;

                                def = init;
                            }
                        }
                    }

                    if (!head)
                    {
                        head = def;
                    }
                    else
                    {
                        tail->next = def;
                    }
                    tail = def;
                }
            }

            if (has_named && arg_names)
            {
                ASTNode *def = find_function_definition(ctx, sig->name);
                if (def)
                {
                    validate_named_arguments(t, sig->name, arg_names, args_provided, def);
                }
            }

            node = ast_create(NODE_EXPR_CALL);
            node->token = t;
            ASTNode *callee = ast_create(NODE_EXPR_VAR);
            callee->var_ref.name = acc;
            node->call.callee = callee;
            node->call.args = head;
            node->call.arg_names = has_named ? arg_names : NULL;
            node->call.count = args_provided;
            if (sig)
            {
                node->definition_token = sig->decl_token;
            }
            if (sig->is_async)
            {
                // Async functions return their inner type directly (no Async<T> wrapper)
                if (sig->ret_type)
                {
                    node->type_info = sig->ret_type;
                    node->resolved_type = type_to_string(sig->ret_type);
                }
                else
                {
                    node->resolved_type = xstrdup("void");
                }
            }
            else if (sig->ret_type)
            {
                node->type_info = sig->ret_type;
                node->resolved_type = type_to_string(sig->ret_type);
            }
            else
            {
                node->resolved_type = xstrdup("void");
            }
        }
        else if (!sig && !find_symbol_entry(ctx, acc) && lexer_peek(l).kind == TOK_LPAREN)
        {
            (void)lexer_next(l);
            CallArgs args_call = parse_call_args(ctx, l, NULL);
            ASTNode *head = args_call.head;
            char **arg_names = args_call.arg_names;
            int has_named = args_call.has_named;
            int args_provided = args_call.count;

            if (lexer_next(l).kind != TOK_RPAREN)
            {
                zpanic_at(lexer_peek(l), "Expected )");
                return NULL;
            }

            node = ast_create(NODE_EXPR_CALL);
            node->token = t;
            ASTNode *callee = ast_create(NODE_EXPR_VAR);
            callee->token = t;
            callee->var_ref.name = acc;
            node->call.callee = callee;
            node->call.args = head;
            node->call.arg_names = has_named ? arg_names : NULL;
            node->call.count = args_provided;
            node->resolved_type = xstrdup("unknown");

            GenericFuncTemplate *tpl = find_func_template(ctx, acc);
            if (tpl && tpl->func_node && args_provided > 0 && head)
            {
                char *inferred_type = NULL;
                ASTNode *func_node = tpl->func_node;
                char *gen_param = tpl->generic_param;

                if (func_node->func.arg_types && gen_param && !strchr(gen_param, ','))
                {
                    ASTNode *actual_arg = head;
                    for (int i = 0; i < func_node->func.count && actual_arg; i++)
                    {
                        Type *formal = func_node->func.arg_types[i];
                        if (!formal)
                        {
                            actual_arg = actual_arg->next;
                            continue;
                        }

                        Type *actual_type = actual_arg->type_info;
                        if (!actual_type && actual_arg->kind == NODE_EXPR_VAR)
                        {
                            actual_type = find_symbol_type_info(ctx, actual_arg->var_ref.name);
                        }
                        if (!actual_type)
                        {
                            actual_arg = actual_arg->next;
                            continue;
                        }

                        if (formal->kind == TYPE_STRUCT && formal->name &&
                            strcmp(formal->name, gen_param) == 0)
                        {
                            inferred_type = type_to_string(actual_type);
                            break;
                        }

                        if (formal->kind == TYPE_ARRAY && formal->inner &&
                            formal->inner->kind == TYPE_STRUCT && formal->inner->name &&
                            strcmp(formal->inner->name, gen_param) == 0)
                        {
                            if (actual_type->kind == TYPE_ARRAY && actual_type->inner)
                            {
                                inferred_type = type_to_string(actual_type->inner);
                            }
                            break;
                        }

                        if (formal->kind == TYPE_POINTER && formal->inner &&
                            formal->inner->kind == TYPE_STRUCT && formal->inner->name &&
                            strcmp(formal->inner->name, gen_param) == 0)
                        {
                            if (actual_type->kind == TYPE_POINTER && actual_type->inner)
                            {
                                inferred_type = type_to_string(actual_type->inner);
                            }
                            break;
                        }

                        actual_arg = actual_arg->next;
                    }
                }

                if (inferred_type)
                {
                    char *mangled =
                        instantiate_function_template(ctx, acc, inferred_type, inferred_type);
                    if (mangled)
                    {
                        // Rewrite the callee to point to the instantiated function
                        zfree(callee->var_ref.name);
                        callee->var_ref.name = mangled;

                        // Update type info from the newly registered FuncSig
                        FuncSig *inst_sig = find_func(ctx, mangled);
                        if (inst_sig)
                        {
                            node->definition_token = inst_sig->decl_token;
                            if (inst_sig->ret_type)
                            {
                                node->type_info = inst_sig->ret_type;
                                node->resolved_type = type_to_string(inst_sig->ret_type);
                            }
                            else
                            {
                                node->resolved_type = xstrdup("void");
                            }
                        }
                        else
                        {
                            node->resolved_type = xstrdup("unknown");
                        }
                    }
                    else
                    {
                        node->resolved_type = xstrdup("unknown");
                    }
                    zfree(inferred_type);
                }
                else
                {
                    // Could not infer type - leave as unknown
                    node->resolved_type = xstrdup("unknown");
                }
            }
            else
            {
                // Unknown return type - let codegen infer it
                node->resolved_type = xstrdup("unknown");
            }
            // Fall through to Postfix
        }
        else
        {
            ZenSymbol *sym = find_symbol_entry(ctx, acc);
            if (sym && sym->is_def && sym->is_const_value)
            {
                sym->is_used = 1;

                // Constant Folding for 'def', emits literal
                node = ast_create(NODE_EXPR_LITERAL);
                node->token = t;
                node->literal.kind = LITERAL_INT; // INT (assumed for now from const_int_val)
                node->literal.int_val = (unsigned long long)(sym->const_int_val);
                node->type_info = type_new(TYPE_INT);
                // No need for resolution
            }
            else
            {
                node = ast_create(NODE_EXPR_VAR);
                node->token = t;
                node->var_ref.name = acc;
                node->type_info = find_symbol_type_info(ctx, acc);

                if (sym)
                {
                    sym->is_used = 1;
                    node->definition_token = sym->decl_token;
                }

                char *type_str = find_symbol_type(ctx, acc);

                if (type_str)
                {
                    node->resolved_type = type_str;
                    node->var_ref.suggestion = NULL;
                }
                else
                {
                    node->resolved_type = xstrdup("unknown");
                    if (sym || should_suppress_undef_warning(ctx, acc))
                    {
                        node->var_ref.suggestion = NULL;
                    }
                    else
                    {
                        node->var_ref.suggestion = find_similar_symbol(ctx, acc);
                    }
                }
            }
        }
    }

    else if (t.kind == TOK_LPAREN)
    {

        Lexer lookahead = *l;
        int is_lambda = 0;
        char **params = xmalloc(sizeof(char *) * 16);
        Type **param_types = xmalloc(sizeof(Type *) * 16);
        int nparams = 0;

        while (1)
        {
            if (lexer_peek(&lookahead).kind != TOK_IDENT)
            {
                if (nparams == 0 && lexer_peek(&lookahead).kind == TOK_RPAREN)
                {
                    lexer_next(&lookahead);
                    if (lexer_peek(&lookahead).kind == TOK_ARROW)
                    {
                        lexer_next(&lookahead);
                        is_lambda = 1;
                    }
                }
                break;
            }
            if (nparams >= 16)
            {
                zpanic_at(lexer_peek(&lookahead), "Too many lambda parameters (max 16)");
                break;
            }
            params[nparams] = token_strdup(lexer_next(&lookahead));
            param_types[nparams] = NULL;
            lookahead.pos = lookahead.pos;

            if (lexer_peek(&lookahead).kind == TOK_COLON)
            {
                lexer_next(&lookahead);
                while (1)
                {
                    Token tk = lexer_peek(&lookahead);
                    if (tk.kind == TOK_EOF || tk.kind == TOK_COMMA || tk.kind == TOK_RPAREN)
                    {
                        break;
                    }
                    lexer_next(&lookahead);
                }
            }
            nparams++;

            Token sep = lexer_peek(&lookahead);
            if (sep.kind == TOK_COMMA)
            {
                lexer_next(&lookahead);
                continue;
            }
            else if (sep.kind == TOK_RPAREN)
            {
                lexer_next(&lookahead);
                if (lexer_peek(&lookahead).kind == TOK_ARROW)
                {
                    lexer_next(&lookahead);
                    is_lambda = 1;
                }
                break;
            }
            else
            {
                break;
            }
        }

        if (is_lambda)
        {
            Lexer actual_parse = *l;
            if (nparams > 0)
            {
                for (int i = 0; i < nparams; i++)
                {
                    lexer_next(&actual_parse);
                    if (lexer_peek(&actual_parse).kind == TOK_COLON)
                    {
                        lexer_next(&actual_parse);
                        param_types[i] = parse_type_formal(ctx, &actual_parse);
                        if (!param_types[i])
                        {
                            return NULL;
                        }
                    }
                    Token sep = lexer_next(&actual_parse);
                    if (sep.kind == TOK_RPAREN)
                    {
                        lexer_next(&actual_parse);
                        break;
                    }
                }
            }
            else
            {
                lexer_next(&actual_parse); // consume ')'
                lexer_next(&actual_parse); // consume '->'
            }
            *l = actual_parse;

            ASTNode *node_multi = parse_arrow_lambda_multi(ctx, l, params, param_types, nparams, 0);
            zfree(param_types);
            return node_multi;
        }
        else
        {
            for (int i = 0; i < nparams; i++)
            {
                zfree(params[i]);
            }
            zfree(params);
            zfree(param_types);
        }

        int saved = l->pos;

        // SPECULATIVE CAST LOOKAHEAD (Identifiers and Suffix Pointers: T*)
        if (lexer_peek(l).kind == TOK_IDENT)
        {
            Lexer cast_look = *l;
            lexer_next(&cast_look);
            while (lexer_peek(&cast_look).kind == TOK_DCOLON)
            { // handle A::B
                lexer_next(&cast_look);
                if (lexer_peek(&cast_look).kind == TOK_IDENT)
                {
                    lexer_next(&cast_look);
                }
                else
                {
                    break;
                }
            }
            while (lexer_peek(&cast_look).kind == TOK_OP && lexer_peek(&cast_look).start[0] == '*')
            {
                Token st = lexer_peek(&cast_look);
                int valid = 1;
                for (size_t i = 0; i < st.len; i++)
                {
                    if (st.start[i] != '*')
                    {
                        valid = 0;
                    }
                }
                if (!valid)
                {
                    break;
                }
                lexer_next(&cast_look);
            }

            if (lexer_peek(&cast_look).kind == TOK_RPAREN)
            {
                lexer_next(&cast_look);
                Token next = lexer_peek(&cast_look);
                if (next.kind == TOK_STRING || next.kind == TOK_INT || next.kind == TOK_FLOAT ||
                    next.kind == TOK_CHAR || next.kind == TOK_FSTRING ||
                    next.kind == TOK_RAW_STRING ||
                    (next.kind == TOK_OP && (is_token(next, "&") || is_token(next, "*") ||
                                             is_token(next, "**") || is_token(next, "!"))) ||
                    next.kind == TOK_IDENT || next.kind == TOK_LPAREN)
                {

                    Type *cast_type_obj = parse_type_formal(ctx, l);
                    if (!cast_type_obj)
                    {
                        return NULL;
                    }
                    char *cast_type = type_to_string(cast_type_obj);
                    {
                        Token inner_t = lexer_next(l);
                        if (inner_t.kind != TOK_RPAREN)
                        {
                            zpanic_at(inner_t, "Expected ) after cast");
                            return NULL;
                        }
                    }
                    ASTNode *target = parse_expr_prec(ctx, l, PREC_UNARY);

                    if (is_trait(cast_type))
                    {
                        zfree(cast_type);
                        return transform_to_trait_object(ctx, type_to_string(cast_type_obj),
                                                         target);
                    }

                    node = ast_create(NODE_EXPR_CAST);
                    node->token = next;
                    node->cast.target_type = cast_type;
                    node->cast.expr = target;
                    node->type_info = cast_type_obj;
                    return node;
                }
            }
        }
        l->pos = saved;

        ASTNode *expr = parse_expression(ctx, l);

        if (lexer_peek(l).kind == TOK_COMMA)
        {
            return parse_tuple_expression(ctx, l, NULL, expr);
        }
        else
        {
            if (lexer_next(l).kind != TOK_RPAREN)
            {
                zpanic_at(lexer_peek(l), "Expected )");
                return NULL;
            }
            node = expr;
        }
    }

    else if (t.kind == TOK_LBRACKET)
    {
        int default_capture_mode = -1;
        Lexer capture_look = *l;
        if (lexer_peek(&capture_look).kind == TOK_OP && lexer_peek(&capture_look).len == 1)
        {
            char op = lexer_peek(&capture_look).start[0];
            if (op == '&')
            {
                default_capture_mode = 1;
            }
            else if (op == '=')
            {
                default_capture_mode = 0;
            }

            if (default_capture_mode != -1)
            {
                lexer_next(&capture_look);
                if (lexer_peek(&capture_look).kind == TOK_RBRACKET)
                {
                    lexer_next(&capture_look);

                    Token next = lexer_peek(&capture_look);
                    if (next.kind == TOK_IDENT)
                    {
                        lexer_next(&capture_look);
                        if (lexer_peek(&capture_look).kind == TOK_ARROW)
                        {
                            lexer_next(l);
                            lexer_next(l);
                            Token param = lexer_next(l);
                            lexer_next(l);
                            return parse_arrow_lambda_single(ctx, l, token_strdup(param),
                                                             default_capture_mode);
                        }
                    }
                    else if (next.kind == TOK_LPAREN)
                    {
                        int depth = 0;
                        Lexer scan = capture_look;
                        int is_arrow = 0;
                        while (1)
                        {
                            Token tk = lexer_next(&scan);
                            if (tk.kind == TOK_EOF)
                            {
                                break;
                            }
                            if (tk.kind == TOK_LPAREN)
                            {
                                depth++;
                            }
                            else if (tk.kind == TOK_RPAREN)
                            {
                                depth--;
                                if (depth == 0)
                                {
                                    if (lexer_peek(&scan).kind == TOK_ARROW)
                                    {
                                        is_arrow = 1;
                                    }
                                    break;
                                }
                            }
                        }

                        if (is_arrow)
                        {
                            lexer_next(l); // consume '&'
                            lexer_next(l); // consume ']'

                            lexer_next(l); // consume '('
                            char **params = xmalloc(sizeof(char *) * 16);
                            Type **param_types = xmalloc(sizeof(Type *) * 16);
                            int nparams = 0;
                            while (1)
                            {
                                if (lexer_peek(l).kind != TOK_IDENT)
                                {
                                    break;
                                }
                                if (nparams >= 16)
                                {
                                    zpanic_at(lexer_peek(l), "Too many parameters (max 16)");
                                    break;
                                }
                                params[nparams] = token_strdup(lexer_next(l));
                                param_types[nparams] = NULL;

                                if (lexer_peek(l).kind == TOK_COLON)
                                {
                                    lexer_next(l);
                                    param_types[nparams] = parse_type_formal(ctx, l);
                                    if (!param_types[nparams])
                                    {
                                        return NULL;
                                    }
                                }
                                nparams++;

                                Token sep = lexer_peek(l);
                                if (sep.kind == TOK_COMMA)
                                {
                                    lexer_next(l);
                                    continue;
                                }
                                else if (sep.kind == TOK_RPAREN)
                                {
                                    lexer_next(l);
                                    break;
                                }
                                else
                                {
                                    break;
                                }
                            }

                            if (lexer_next(l).kind != TOK_ARROW)
                            {
                                zpanic_at(t, "Expected ->");
                                return NULL;
                            }

                            ASTNode *node_multi = parse_arrow_lambda_multi(
                                ctx, l, params, param_types, nparams, default_capture_mode);
                            zfree(param_types);
                            return node_multi;
                        }
                    }
                }
            }
        }

        ASTNode *head = NULL, *tail = NULL;
        int count = 0;
        while (lexer_peek(l).kind != TOK_RBRACKET)
        {
            ASTNode *elem = parse_expression(ctx, l);
            count++;
            if (!head)
            {
                head = elem;
                tail = elem;
            }
            else
            {
                tail->next = elem;
                tail = elem;
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
        if (lexer_next(l).kind != TOK_RBRACKET)
        {
            zpanic_at(lexer_peek(l), "Expected ] after array literal");
            return NULL;
        }
        node = ast_create(NODE_EXPR_ARRAY_LITERAL);
        node->token = t;
        node->array_literal.elements = head;
        node->array_literal.count = count;
        if (head && head->type_info)
        {
            Type *elem_type = head->type_info;
            Type *arr_type = type_new(TYPE_ARRAY);
            arr_type->inner = elem_type;
            arr_type->array_size = count;
            node->type_info = arr_type;
        }
    }
    else
    {
        const char *hints[] = {"Valid primary expressions include:",
                               "- Identifiers (variables, functions, fields)",
                               "- Literals (numbers, strings, booleans, runes)",
                               "- Parenthesized expressions `(...)`",
                               "- Array literals `[...]`",
                               "- Block expressions `{...}`",
                               NULL};
        char msg[MAX_SHORT_MSG_LEN];
        snprintf(msg, sizeof(msg), "Unexpected token '%.*s' while parsing expression", (int)(t.len),
                 t.start);
        zpanic_with_hints(t, msg, hints);
        return NULL;
    }

    while (1)
    {
        if (lexer_peek(l).kind == TOK_EOF)
        {
            break;
        }
        if (lexer_peek(l).kind == TOK_LPAREN)
        {
            (void)lexer_next(l); // consume '('
            CallArgs args = parse_call_args(ctx, l, NULL);

            if (lexer_next(l).kind != TOK_RPAREN)
            {
                zpanic_at(lexer_peek(l), "Expected ) after call arguments");
                return NULL;
            }

            ASTNode *call = ast_create(NODE_EXPR_CALL);
            call->token = t;
            call->call.callee = node;
            call->call.args = args.head;
            call->call.arg_names = args.has_named ? args.arg_names : NULL;
            call->call.count = args.count;
            check_format_string(call, t);

            // Try to infer type if callee has function type info
            call->resolved_type = xstrdup("unknown"); // Default (was int)
            if (node->type_info && node->type_info->kind == TYPE_FUNCTION && node->type_info->inner)
            {
                call->type_info = node->type_info->inner;

                // Update resolved_type based on real return
                // (Optional: type_to_string(call->type_info))
            }
            node = call;
        }

        else if (lexer_peek(l).kind == TOK_LBRACKET)
        {
            Token bracket = lexer_next(l); // consume '['
            ASTNode *index = parse_expression(ctx, l);

            ASTNode *extra_head = NULL;
            ASTNode *extra_tail = NULL;
            int extra_count = 0;
            while (lexer_peek(l).kind == TOK_COMMA)
            {
                lexer_next(l); // eat comma
                ASTNode *idx = parse_expression(ctx, l);
                if (!extra_head)
                {
                    extra_head = idx;
                }
                else
                {
                    extra_tail->next = idx;
                }
                extra_tail = idx;
                extra_count++;
            }

            {
                Token inner_t = lexer_next(l);
                if (inner_t.kind != TOK_RBRACKET)
                {
                    zpanic_at(inner_t, "Expected ] after index");
                    return NULL;
                }
            }

            // Static Array Bounds Check (only for single-index)
            if (extra_count == 0 && node->type_info && node->type_info->kind == TYPE_ARRAY &&
                node->type_info->array_size > 0)
            {
                if (index->kind == NODE_EXPR_LITERAL && index->literal.kind == LITERAL_INT)
                {
                    int idx = (int)index->literal.int_val;
                    if (idx < 0 || idx >= node->type_info->array_size)
                    {
                        warn_array_bounds(bracket, idx, node->type_info->array_size);
                    }
                }
            }

            int overloaded_get = 0;
            if (node->type_info && node->type_info->kind != TYPE_ARRAY &&
                node->type_info->kind == TYPE_STRUCT)
            {
                Type *st = node->type_info;
                char *struct_name = (st->kind == TYPE_STRUCT) ? st->name : st->inner->name;
                int is_ptr = (st->kind == TYPE_POINTER);

                char mangled_raw[MAX_MANGLED_NAME_LEN];
                snprintf(mangled_raw, sizeof(mangled_raw), "%s____get", struct_name);
                char *mangled = merge_underscores(mangled_raw);
                FuncSig *sig = find_func(ctx, mangled);
                if (!sig)
                {
                    snprintf(mangled_raw, sizeof(mangled_raw), "%s__get", struct_name);
                    mangled = merge_underscores(mangled_raw);
                    sig = find_func(ctx, mangled);
                }
                if (sig)
                {
                    // Rewrite to Call: node.get(index, ...)
                    ASTNode *call = ast_create(NODE_EXPR_CALL);
                    call->token = t;
                    ASTNode *callee = ast_create(NODE_EXPR_VAR);
                    callee->var_ref.name = xstrdup(mangled);
                    call->call.callee = callee;

                    // Arg 1: Self
                    ASTNode *arg1 = node;
                    if (sig->arg_types && sig->total_args > 0 &&
                        sig->arg_types[0]->kind == TYPE_POINTER && !is_ptr)
                    {
                        // Needs ptr, have value -> &node
                        ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                        addr->unary.op = xstrdup("&");
                        addr->unary.operand = node;
                        addr->type_info = type_new_ptr(st);
                        arg1 = addr;
                    }
                    else if (sig->arg_types && is_ptr && sig->arg_types[0]->kind != TYPE_POINTER)
                    {
                        // Needs value, have ptr -> *node
                        ASTNode *deref = ast_create(NODE_EXPR_UNARY);
                        deref->unary.op = xstrdup("*");
                        deref->unary.operand = node;
                        arg1 = deref;
                    }

                    // Arg 2: Index
                    arg1->next = index;
                    // Chain extra indices as additional args
                    if (extra_head)
                    {
                        index->next = extra_head;
                    }
                    else
                    {
                        index->next = NULL;
                    }
                    call->call.args = arg1;

                    call->type_info = sig->ret_type;
                    call->resolved_type = type_to_string(sig->ret_type);

                    node = call;
                    overloaded_get = 1;
                }
                zfree(mangled);
            }

            if (!overloaded_get)
            {
                ASTNode *idx_node = ast_create(NODE_EXPR_INDEX);
                idx_node->token = t;
                idx_node->index.array = node;
                idx_node->index.index = index;
                idx_node->index.extra_indices = extra_head;
                idx_node->index.index_count = 1 + extra_count;

                // Resolve array type_info from symbol table if needed
                Type *arr_type = node->type_info;
                if (!arr_type && node->kind == NODE_EXPR_VAR)
                {
                    arr_type = find_symbol_type_info(ctx, node->var_ref.name);
                }

                if (arr_type && arr_type->inner)
                {
                    idx_node->type_info = arr_type->inner;
                }
                else if (arr_type && arr_type->name &&
                         (arr_type->kind == TYPE_VECTOR || arr_type->kind == TYPE_STRUCT ||
                          arr_type->kind == TYPE_ARRAY))
                {
                    // Look up struct def to check if it's a vector type
                    ASTNode *def = find_struct_def(ctx, arr_type->name);
                    if (def && def->kind == NODE_STRUCT && def->type_info &&
                        def->type_info->kind == TYPE_VECTOR && def->strct.fields &&
                        def->strct.fields->type_info)
                    {
                        arr_type->inner = def->strct.fields->type_info;
                        idx_node->type_info = def->strct.fields->type_info;
                    }
                    else
                    {
                        idx_node->type_info = type_new(TYPE_INT);
                    }
                }
                else
                {
                    idx_node->type_info = type_new(TYPE_INT);
                }
                node = idx_node;
            }
        }

        else
        {
            break;
        }
    }

    return node;
}
