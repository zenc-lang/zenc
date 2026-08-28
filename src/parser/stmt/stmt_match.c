// SPDX-License-Identifier: MIT

#include "parser.h"
#include "constants.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ast/ast.h"
#include "utils/format_expr.h"
#include "plugins/plugin_manager.h"
#include "zen/zen_facts.h"
#include "zprep_plugin.h"
#include "analysis/move_check.h"

char *unmangle_ptr_suffix(const char *s);

ASTNode *parse_match(ParserContext *ctx, Lexer *l)
{
    init_builtins();
    Token start_token = lexer_peek(l);
    lexer_next(l); // eat 'match'
    ASTNode *expr = parse_expression(ctx, l);

    Token t_brace = lexer_next(l);
    if (t_brace.kind != TOK_LBRACE)
    {
        zpanic_at(t_brace, "Expected '{' in match");
        if (ctx->is_fault_tolerant)
        {
            ASTNode *node = ast_create(NODE_MATCH);
            node->token = start_token;
            node->match_stmt.expr = expr;
            node->match_stmt.cases = NULL;
            return node;
        }
        return NULL;
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
        if (lexer_peek(l).kind == TOK_EOF)
        {
            zpanic_at(lexer_peek(l), "Unexpected end of file in match body");
            break;
        }

        size_t pat_cap = 1024;
        char *patterns_buf = xmalloc(pat_cap);
        patterns_buf[0] = 0;
        int pattern_count = 0;

        while (1)
        {
            Token p = lexer_next(l);
            char *p_str = token_strdup(p);

            while (1)
            {
                skip_comments(l);
                Token pk = lexer_peek(l);
                if (pk.kind == TOK_DCOLON)
                {
                    lexer_next(l); // eat ::
                    Token suffix = lexer_next(l);
                    char *tmp = xmalloc(strlen(p_str) + suffix.len + 3);
                    sprintf(tmp, "%s__%.*s", p_str, (int)(suffix.len), suffix.start); /* safe */
                    zfree(p_str);
                    p_str = tmp;
                }
                else if (pk.kind == TOK_LANGLE)
                {
                    lexer_next(l); // eat <
                    int depth = 1;
                    while (depth > 0)
                    {
                        Token t = lexer_next(l);
                        if (t.kind == TOK_LANGLE)
                        {
                            depth++;
                        }
                        else if (t.kind == TOK_RANGLE)
                        {
                            depth--;
                        }
                        else if (t.kind == TOK_EOF)
                        {
                            break;
                        }
                    }
                }
                else
                {
                    break;
                }
            }

            if (lexer_peek(l).kind == TOK_DOTDOT || lexer_peek(l).kind == TOK_DOTDOT_EQ ||
                lexer_peek(l).kind == TOK_DOTDOT_LT)
            {
                int is_inclusive = (lexer_peek(l).kind == TOK_DOTDOT_EQ);
                lexer_next(l); // eat operator
                Token end_tok = lexer_next(l);
                char *end_str = token_strdup(end_tok);

                char *range_str = xmalloc(strlen(p_str) + strlen(end_str) + 5);
                sprintf(range_str, "%s%s%s", p_str, is_inclusive ? "..=" : "..",
                        end_str); /* safe */
                zfree(p_str);
                zfree(end_str);
                p_str = range_str;
            }

            size_t needed = strlen(patterns_buf) + strlen(p_str) + 4;
            if (needed >= pat_cap)
            {
                pat_cap = pat_cap * 2 + needed;
                patterns_buf = xrealloc(patterns_buf, pat_cap);
            }

            if (pattern_count > 0)
            {
                strcat(patterns_buf, "|");
            }
            strcat(patterns_buf, p_str);
            zfree(p_str);
            pattern_count++;

            Token next = lexer_peek(l);
            skip_comments(l);
            int is_or = (next.kind == TOK_OR) ||
                        (next.kind == TOK_OP && next.len == 2 && next.start[0] == '|' &&
                         next.start[1] == '|') ||
                        (next.kind == TOK_COMMA);
            if (is_or)
            {
                lexer_next(l); // eat ||, 'or', or comma
                skip_comments(l);
                continue;
            }
            else
            {
                break;
            }
        }

        char *pattern = xstrdup(patterns_buf);
        int is_default = (strcmp(pattern, "_") == 0);
        int is_destructure = 0;

        char **bindings = NULL;
        int *binding_refs = NULL;
        int binding_count = 0;

        skip_comments(l);
        if (!is_default && pattern_count == 1 &&
            (lexer_peek(l).kind == TOK_LPAREN || lexer_peek(l).kind == TOK_LBRACE))
        {
            int is_brace = (lexer_peek(l).kind == TOK_LBRACE);
            lexer_next(l);

            bindings = xcalloc(8, sizeof(char *));
            binding_refs = xcalloc(8, sizeof(int));
            int binding_cap = 8;

            while (lexer_peek(l).kind != TOK_RPAREN && lexer_peek(l).kind != TOK_RBRACE)
            {
                int is_r = 0;
                if (lexer_peek(l).kind == TOK_IDENT && lexer_peek(l).len == 3 &&
                    strncmp(lexer_peek(l).start, "ref", 3) == 0)
                {
                    lexer_next(l); // eat 'ref'
                    is_r = 1;
                }

                Token b = lexer_next(l);
                if (b.kind != TOK_IDENT)
                {
                    zpanic_at(b, "Expected variable name in pattern");
                    break;
                }

                if (binding_count >= binding_cap)
                {
                    binding_cap *= 2;
                    bindings = xrealloc(bindings, sizeof(char *) * (size_t)(binding_cap));
                    binding_refs = xrealloc(binding_refs, sizeof(int) * (size_t)(binding_cap));
                }

                if (is_brace && lexer_peek(l).kind == TOK_COLON)
                {
                    lexer_next(l); // eat :
                    Token val = lexer_next(l);
                    if (val.kind == TOK_IDENT)
                    {
                        bindings[binding_count] = token_strdup(val);
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
        if (lexer_peek(l).kind == TOK_IDENT && strncmp(lexer_peek(l).start, "if", 2) == 0)
        {
            lexer_next(l);
            guard = parse_expression(ctx, l);
        }

        if (lexer_next(l).kind != TOK_ARROW)
        {
            zpanic_at(lexer_peek(l), "Expected => after match pattern");
            return NULL;
            if (ctx->is_fault_tolerant)
            {
                while (lexer_peek(l).kind != TOK_RBRACE && lexer_peek(l).kind != TOK_COMMA &&
                       lexer_peek(l).kind != TOK_EOF)
                {
                    lexer_next(l);
                }
                if (lexer_peek(l).kind == TOK_COMMA)
                {
                    lexer_next(l);
                }
                continue;
            }
        }

        enter_scope(ctx);
        if (binding_count > 0)
        {
            EnumVariantReg *vreg = find_enum_variant(ctx, pattern);

            ASTNode *payload_node_field = NULL;
            int is_tuple_payload = 0;
            Type *payload_type = NULL;
            ASTNode *enum_def = NULL;

            if (vreg)
            {
                enum_def = find_struct_def(ctx, vreg->enum_name);
                if (enum_def && enum_def->kind == NODE_ENUM)
                {
                    ASTNode *v = enum_def->enm.variants;
                    while (v)
                    {
                        size_t size = strlen(vreg->enum_name) + strlen(v->variant.name) + 2;
                        char *v_full = xmalloc(size + 1);
                        snprintf(v_full, size + 1, "%s__%s", vreg->enum_name, v->variant.name);
                        if (strcmp(v_full, pattern) == 0 && v->variant.payload)
                        {
                            payload_type = v->variant.payload;
                            if (payload_type && payload_type->kind == TYPE_STRUCT &&
                                strncmp(payload_type->name, "Tuple__", 7) == 0)
                            {
                                is_tuple_payload = 1;
                                ASTNode *tuple_def = find_struct_def(ctx, payload_type->name);
                                if (tuple_def)
                                {
                                    payload_node_field = tuple_def->strct.fields;
                                }
                            }
                            zfree(v_full);
                            break;
                        }
                        v = v->next;
                    }
                }
            }
            else if (binding_count == 1 && expr && expr->type_info)
            {
                // Struct constructor pattern, e.g. `Ok(val)` matching a
                // `Result<String>`. The payload type is the constructor's
                // argument type with the struct's generic parameter substituted
                // by the concrete argument in the mangled type name.
                Type *mt = expr->type_info;
                if (mt->name)
                {
                    GenericTemplate *gt = ctx->templates;
                    while (gt)
                    {
                        size_t glen = strlen(gt->name);
                        if (strncmp(mt->name, gt->name, glen) == 0 && mt->name[glen] == '_')
                        {
                            break;
                        }
                        gt = gt->next;
                    }
                    if (gt && gt->struct_node && gt->struct_node->kind == NODE_STRUCT &&
                        gt->struct_node->strct.generic_param_count > 0)
                    {
                        const char *base = gt->name;
                        const char *arg_start = mt->name + strlen(base);
                        while (*arg_start == '_')
                        {
                            arg_start++;
                        }

                        char *ctor = NULL;
                        if (strstr(pattern, "__"))
                        {
                            ctor = xstrdup(pattern);
                        }
                        else
                        {
                            size_t csz = strlen(base) + strlen(pattern) + 3;
                            ctor = xmalloc(csz);
                            sprintf(ctor, "%s__%s", base, pattern); /* safe */
                        }
                        GenericFuncTemplate *ft = find_func_template(ctx, ctor);
                        zfree(ctor);
                        if (ft && ft->func_node && ft->func_node->kind == NODE_FUNCTION &&
                            ft->func_node->func.count >= 1 && ft->func_node->func.arg_types[0] &&
                            ft->func_node->func.arg_types[0]->name)
                        {
                            const char *gparam = gt->struct_node->strct.generic_params[0];
                            const char *pname = ft->func_node->func.arg_types[0]->name;
                            char *real = NULL;
                            size_t plen = strlen(pname);
                            int is_generic_ptr = 0;
                            if (plen > 1 && pname[plen - 1] == '*')
                            {
                                size_t blen = strlen(gparam);
                                if (plen - 1 == blen && strncmp(pname, gparam, blen) == 0)
                                {
                                    is_generic_ptr = 1;
                                }
                            }
                            if (is_generic_ptr || strcmp(pname, gparam) == 0)
                            {
                                // Substitute the generic param with the concrete
                                // argument, unmangling a trailing "Ptr" to '*'.
                                const char *ap = arg_start;
                                size_t al = strlen(arg_start);
                                if (al > 3 && strcmp(arg_start + al - 3, "Ptr") == 0)
                                {
                                    size_t blen = al - 3;
                                    char *b = xmalloc(blen + 2);
                                    strncpy(b, arg_start, blen);
                                    b[blen] = 0;
                                    strcat(b, "*");
                                    real = b;
                                    ap = NULL;
                                }
                                if (ap)
                                {
                                    real = xstrdup(ap);
                                }
                                if (is_generic_ptr)
                                {
                                    char *t = xmalloc(strlen(real) + 2);
                                    sprintf(t, "%s*", real); /* safe */
                                    zfree(real);
                                    real = t;
                                }
                            }
                            else
                            {
                                // Concrete payload (e.g. `char*` for Err).
                                real = xstrdup(pname);
                            }
                            if (real)
                            {
                                payload_type = type_from_string_helper(real);
                                if (!payload_type)
                                {
                                    payload_type = type_new(TYPE_STRUCT);
                                    payload_type->name = xstrdup(real);
                                }
                                zfree(real);
                            }
                        }
                    }
                }
            }

            for (int i = 0; i < binding_count; i++)
            {
                char *binding = bindings[i];
                if (!binding)
                {
                    continue;
                }
                int is_ref = binding_refs[i];
                char *binding_type = is_ref ? "void*" : "unknown";
                Type *binding_type_info = NULL;

                if (payload_type)
                {
                    if (binding_count == 1 && !is_tuple_payload)
                    {
                        binding_type = type_to_string(payload_type);
                        binding_type_info = payload_type;
                    }
                    else if (binding_count == 1 && is_tuple_payload)
                    {
                        binding_type = type_to_string(payload_type);
                        binding_type_info = payload_type;
                    }
                    else if (binding_count > 1 && is_tuple_payload)
                    {
                        if (payload_node_field)
                        {
                            Lexer tmp;
                            lexer_init(&tmp, payload_node_field->field.type, ctx->config,
                                       ctx->current_filename);
                            binding_type_info = parse_type_formal(ctx, &tmp);
                            if (!binding_type_info)
                            {
                                continue;
                            }
                            binding_type = type_to_string(binding_type_info);
                            payload_node_field = payload_node_field->next;
                        }
                    }
                }

                if (is_ref && binding_type_info)
                {
                    Type *ptr = type_new(TYPE_POINTER);
                    ptr->inner = binding_type_info;
                    binding_type_info = ptr;

                    char *ptr_s = xmalloc(strlen(binding_type) + 2);
                    sprintf(ptr_s, "%s*", binding_type); /* safe */
                    binding_type = ptr_s;
                }

                int is_generic_unresolved = 0;

                if (enum_def)
                {
                    if (enum_def->enm.generic_param)
                    {
                        char *param = enum_def->enm.generic_param;
                        if (strstr(binding_type, param))
                        {
                            is_generic_unresolved = 1;
                        }
                    }
                }

                if (!is_generic_unresolved &&
                    (strcmp(binding_type, "T") == 0 || strcmp(binding_type, "T*") == 0))
                {
                    is_generic_unresolved = 1;
                }

                if (is_generic_unresolved)
                {
                    if (is_ref)
                    {
                        binding_type = "unknown*";
                        Type *u = type_new(TYPE_UNKNOWN);
                        Type *p = type_new(TYPE_POINTER);
                        p->inner = u;
                        binding_type_info = p;
                    }
                    else
                    {
                        binding_type = "unknown";
                        binding_type_info = type_new(TYPE_UNKNOWN);
                    }
                }

                if (strcmp(binding, "parsed") == 0)
                {
                }
                add_symbol(ctx, binding, binding_type, binding_type_info, 0);
            }
        }

        ASTNode *body = NULL;
        Token pk = lexer_peek(l);
        if (pk.kind == TOK_LBRACE)
        {
            body = parse_block(ctx, l);
        }
        else if (pk.kind == TOK_EXPECT ||
                 (pk.kind == TOK_IDENT && strncmp(pk.start, "expect", 6) == 0))
        {
            body = parse_expect(ctx, l);
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
        else if (ctx->is_fault_tolerant && (pk.kind == TOK_RBRACE || pk.kind == TOK_COMMA))
        {
            body = ast_create(NODE_BLOCK);
            body->token = pk;
        }
        else
        {
            body = parse_expression(ctx, l);
        }

        if (!body)
        {
            body = ast_create(NODE_BLOCK);
            body->token = pk;
        }

        exit_scope(ctx);

        ASTNode *c = ast_create(NODE_MATCH_CASE);
        c->token = pk;
        c->match_case.pattern = pattern;
        c->match_case.binding_names = bindings;
        c->match_case.binding_count = binding_count;
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
    lexer_next(l); // eat }

    ASTNode *n = ast_create(NODE_MATCH);
    n->line = start_token.line;
    n->token = start_token;
    n->match_stmt.expr = expr;
    n->match_stmt.cases = h;
    return n;
}
