// SPDX-License-Identifier: MIT
#include "parser.h"
#include "constants.h"
#include "expr_internal.h"
#include "analysis/move_check.h"
#include "utils/utils.h"
#include "zen/zen_facts.h"
#include "ast/ast.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ASTNode *parse_expr_prec_impl(ParserContext *ctx, Lexer *l, Precedence min_prec);

ASTNode *parse_expr_prec(ParserContext *ctx, Lexer *l, Precedence min_prec)
{
    if (++ctx->recursion_depth > 64)
    {
        zpanic_at(lexer_peek(l), "Expression nesting too deep (max 64)");
        ctx->recursion_depth--;
        return ast_create(NODE_ERRONEOUS);
    }
    ASTNode *res = parse_expr_prec_impl(ctx, l, min_prec);
    ctx->recursion_depth--;
    if (!res)
    {
        return ast_create(NODE_ERRONEOUS);
    }
    return res;
}

static ASTNode *parse_expr_prec_impl(ParserContext *ctx, Lexer *l, Precedence min_prec)
{
    Token t = lexer_peek(l);
    ASTNode *lhs = NULL;

    // --- PREFIX: ? prompt expression ---
    if (t.kind == TOK_QUESTION)
    {
        Lexer lookahead = *l;
        lexer_next(&lookahead);
        Token next = lexer_peek(&lookahead);

        if (next.kind == TOK_STRING || next.kind == TOK_FSTRING || next.kind == TOK_RAW_STRING)
        {
            lexer_next(l); // consume '?'
            Token t_str = lexer_next(l);

            char *inner = token_get_string_content(t_str);
            int is_raw = (t_str.kind == TOK_RAW_STRING);

            // Reuse printf sugar to generate the prompt print
            char *print_code =
                process_printf_sugar(ctx, t_str, inner, 1, "stdout", NULL, NULL, 0, is_raw, 0);
            zfree(inner);

            // Checks for (args...) suffix for SCAN mode
            if (lexer_peek(l).kind == TOK_LPAREN)
            {
                lexer_next(l);

                // Parse args
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
                }

                char fmt[MAX_SHORT_MSG_LEN];
                fmt[0] = 0;
                for (int i = 0; i < ac; i++)
                {
                    Type *inner_t = args[i]->type_info;
                    if (!inner_t && args[i]->kind == NODE_EXPR_VAR)
                    {
                        inner_t = find_symbol_type_info(ctx, args[i]->var_ref.name);
                    }

                    if (!inner_t)
                    {
                        strcat(fmt, "%d");
                    }
                    else
                    {
                        if (inner_t->kind == TYPE_INT || inner_t->kind == TYPE_I32 ||
                            inner_t->kind == TYPE_BOOL)
                        {
                            strcat(fmt, "%d");
                        }
                        else if (inner_t->kind == TYPE_F64)
                        {
                            strcat(fmt, "%lf");
                        }
                        else if (inner_t->kind == TYPE_F32 || inner_t->kind == TYPE_FLOAT)
                        {
                            strcat(fmt, "%f");
                        }
                        else if (inner_t->kind == TYPE_STRING)
                        {
                            strcat(fmt, "%ms");
                        }
                        else if (inner_t->kind == TYPE_ARRAY && inner_t->inner &&
                                 (inner_t->inner->kind == TYPE_CHAR ||
                                  inner_t->inner->kind == TYPE_U8 ||
                                  inner_t->inner->kind == TYPE_I8))
                        {
                            strcat(fmt, "%s");
                        }
                        else if (inner_t->kind == TYPE_CHAR || inner_t->kind == TYPE_I8 ||
                                 inner_t->kind == TYPE_U8 || inner_t->kind == TYPE_BYTE)
                        {
                            strcat(fmt, " %c");
                        }
                        else
                        {
                            strcat(fmt, "%d");
                        }
                    }
                    if (i < ac - 1)
                    {
                        strcat(fmt, " ");
                    }
                }

                ASTNode *block = ast_create(NODE_BLOCK);

                ASTNode *s1 = ast_create(NODE_RAW_STMT);
                s1->token = t_str;
                // Append semicolon to ensure it's a valid statement
                char *s1_code = xmalloc(strlen(print_code) + 2);
                sprintf(s1_code, "%s;", print_code); /* safe */
                s1->raw_stmt.content = s1_code;
                zfree(print_code);

                ASTNode *call = ast_create(NODE_EXPR_CALL);
                call->token = t;
                ASTNode *callee = ast_create(NODE_EXPR_VAR);
                callee->var_ref.name = xstrdup("_z_scan_helper");
                call->call.callee = callee;
                call->type_info = type_new(TYPE_INT);

                ASTNode *fmt_node = ast_create(NODE_EXPR_LITERAL);
                fmt_node->literal.kind = LITERAL_STRING;
                fmt_node->literal.string_val = xstrdup(fmt);
                ASTNode *head = fmt_node, *tail = fmt_node;

                for (int i = 0; i < ac; i++)
                {
                    ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                    addr->unary.op = xstrdup("&");
                    addr->unary.operand = args[i];
                    tail->next = addr;
                    tail = addr;
                }
                call->call.args = head;

                // Link Statements
                s1->next = call;
                block->block.statements = s1;

                return block;
            }
            else
            {
                // String Mode (Original)
                size_t len = strlen(print_code);
                if (len > 5)
                {
                    print_code[len - 5] = 0; // Strip "0; })"
                }

                char *final_code = xmalloc(strlen(print_code) + 64);
                snprintf(final_code, strlen(print_code) + 64, "%s readln(); })", print_code);
                zfree(print_code);

                ASTNode *n = ast_create(NODE_RAW_STMT);
                n->token = next;
                char *stmt_code = xmalloc(strlen(final_code) + 2);
                sprintf(stmt_code, "%s;", final_code); /* safe */
                zfree(final_code);
                n->raw_stmt.content = stmt_code;
                return n;
            }
        }
        else
        {
            lexer_next(l);

            ASTNode *args[16];
            int ac = 0;
            while (1)
            {
                if (ac >= 16)
                {
                    zpanic_at(lexer_peek(l), "Too many arguments (max 16)");
                    break;
                }
                args[ac++] = parse_expr_prec(ctx, l, PREC_ASSIGNMENT);
                if (lexer_peek(l).kind == TOK_COMMA)
                {
                    lexer_next(l);
                }
                else
                {
                    break;
                }
            }

            char fmt[MAX_SHORT_MSG_LEN];
            fmt[0] = 0;
            for (int i = 0; i < ac; i++)
            {
                if (!args[i])
                {
                    continue;
                }
                Type *inner_t = args[i]->type_info;
                if (!inner_t && args[i]->kind == NODE_EXPR_VAR)
                {
                    inner_t = find_symbol_type_info(ctx, args[i]->var_ref.name);
                }

                if (!inner_t)
                {
                    strcat(fmt, "%d");
                }
                else
                {
                    if (inner_t->kind == TYPE_INT || inner_t->kind == TYPE_I32 ||
                        inner_t->kind == TYPE_BOOL)
                    {
                        strcat(fmt, "%d");
                    }
                    else if (inner_t->kind == TYPE_F64)
                    {
                        strcat(fmt, "%lf");
                    }
                    else if (inner_t->kind == TYPE_F32 || inner_t->kind == TYPE_FLOAT)
                    {
                        strcat(fmt, "%f");
                    }
                    else if (inner_t->kind == TYPE_STRING ||
                             (inner_t->kind == TYPE_ARRAY && inner_t->inner &&
                              (inner_t->inner->kind == TYPE_CHAR ||
                               inner_t->inner->kind == TYPE_U8 || inner_t->inner->kind == TYPE_I8)))
                    {
                        strcat(fmt, "%s");
                    }
                    else if (inner_t->kind == TYPE_CHAR || inner_t->kind == TYPE_I8 ||
                             inner_t->kind == TYPE_U8 || inner_t->kind == TYPE_BYTE)
                    {
                        strcat(fmt, " %c");
                    }
                    else
                    {
                        strcat(fmt, "%d");
                    }
                }
                if (i < ac - 1)
                {
                    strcat(fmt, " ");
                }
            }

            ASTNode *call = ast_create(NODE_EXPR_CALL);
            call->token = t;
            ASTNode *callee = ast_create(NODE_EXPR_VAR);
            callee->var_ref.name = xstrdup("_z_scan_helper");
            call->call.callee = callee;
            call->type_info = type_new(TYPE_INT);

            ASTNode *fmt_node = ast_create(NODE_EXPR_LITERAL);
            fmt_node->literal.kind = LITERAL_STRING;
            fmt_node->literal.string_val = xstrdup(fmt);
            ASTNode *head = fmt_node, *tail = fmt_node;

            for (int i = 0; i < ac; i++)
            {
                ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                addr->unary.op = xstrdup("&");
                addr->unary.operand = args[i];
                tail->next = addr;
                tail = addr;
            }
            call->call.args = head;

            return call;
        }
    }
    // --- PREFIX: ! stderr print ---
    if (t.kind == TOK_OP && is_token(t, "!"))
    {
        Lexer lookahead = *l;
        lexer_next(&lookahead);
        Token next = lexer_peek(&lookahead);

        if (next.kind == TOK_STRING || next.kind == TOK_FSTRING || next.kind == TOK_RAW_STRING)
        {
            lexer_next(l); // consume '!'
            Token t_str = lexer_next(l);

            char *inner = token_get_string_content(t_str);
            int is_raw = (t_str.kind == TOK_RAW_STRING);

            // Check for .. suffix (.. suppresses newline)
            int newline = 1;
            if (lexer_peek(l).kind == TOK_DOTDOT)
            {
                lexer_next(l); // consume ..
                newline = 0;
            }

            char *code = process_printf_sugar(ctx, t_str, inner, newline, "stderr", NULL, NULL, 1,
                                              is_raw, 0);
            zfree(inner);
            if (!code)
            {
                return NULL;
            }

            ASTNode *n = ast_create(NODE_RAW_STMT);
            n->token = t_str;
            char *stmt_code = xmalloc(strlen(code) + 2);
            sprintf(stmt_code, "%s;", code); /* safe */
            zfree(code);
            n->raw_stmt.content = stmt_code;
            return n;
        }
    }

    // --- PREFIX: await expression ---
    if (t.kind == TOK_AWAIT)
    {
        lexer_next(l); // consume await
        ASTNode *operand = parse_expr_prec(ctx, l, PREC_UNARY);

        lhs = ast_create(NODE_AWAIT);
        lhs->unary.operand = operand;
        // Type inference: await Async<T> yields T
        // If operand is a call to an async function, look up its ret_type (not
        // Async)
        if (operand->kind == NODE_EXPR_CALL && operand->call.callee->kind == NODE_EXPR_VAR)
        {
            FuncSig *sig = find_func(ctx, operand->call.callee->var_ref.name);
            if (sig && sig->is_async && sig->ret_type)
            {
                lhs->type_info = sig->ret_type;
                lhs->resolved_type = type_to_string(sig->ret_type);
            }
            else if (sig && !sig->is_async)
            {
                // Not an async function - shouldn't await it
                lhs->type_info = type_new(TYPE_VOID);
                lhs->resolved_type = xstrdup("void");
            }
            else
            {
                lhs->type_info = type_new_ptr(type_new(TYPE_VOID));
                lhs->resolved_type = xstrdup("void*");
            }
        }
        else
        {
            // Awaiting a variable - look up its type and extract T from Async<T>
            if (operand->kind == NODE_EXPR_VAR)
            {
                ZenSymbol *sym = find_symbol_entry(ctx, operand->var_ref.name);
                if (sym)
                {
                    char *var_type = sym->type_name;
                    if (!var_type && sym->type_info)
                    {
                        var_type = type_to_string(sym->type_info);
                    }
                    if (var_type)
                    {
                        // Extract T from Async<T>
                        char *start = (char *)strchr(var_type, '<');
                        if (start)
                        {
                            start++; // Skip <
                            char *end = (char *)strrchr(var_type, '>');
                            if (end && end > start)
                            {
                                ptrdiff_t len = end - start;
                                char *inner_type = xmalloc((size_t)(len + 1));
                                strncpy(inner_type, start, (size_t)(len));
                                inner_type[len] = 0;
                                lhs->resolved_type = inner_type;
                                // Try to create proper type_info
                                if (is_primitive_type_name(inner_type))
                                {
                                    if (strcmp(inner_type, "string") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_STRING);
                                    }
                                    else if (strcmp(inner_type, "int") == 0 ||
                                             strcmp(inner_type, "i32") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_INT);
                                    }
                                    else if (strcmp(inner_type, "bool") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_BOOL);
                                    }
                                    else if (strcmp(inner_type, "float") == 0 ||
                                             strcmp(inner_type, "f32") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_F32);
                                    }
                                    else if (strcmp(inner_type, "double") == 0 ||
                                             strcmp(inner_type, "f64") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_F64);
                                    }
                                    else if (strcmp(inner_type, "char") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_CHAR);
                                    }
                                    else if (strcmp(inner_type, "void") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_VOID);
                                    }
                                    else if (strcmp(inner_type, "usize") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_USIZE);
                                    }
                                    else if (strcmp(inner_type, "isize") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_ISIZE);
                                    }
                                    else
                                    {
                                        lhs->type_info = type_new(TYPE_INT);
                                    }
                                }
                                else
                                {
                                    lhs->type_info = type_new(TYPE_STRUCT);
                                    lhs->type_info->name = xstrdup(inner_type);
                                }
                                goto await_done;
                            }
                        }
                    }
                }
            }
            // Fallback to void* if we couldn't determine the type
            lhs->type_info = type_new_ptr(type_new(TYPE_VOID));
            lhs->resolved_type = xstrdup("void*");
        await_done:;
        }

        goto after_unary;
    }

    // --- PREFIX: unary operators (-, !, *, &, ~, ++, --, **, not) ---
    if ((t.kind == TOK_OP && (is_token(t, "-") || is_token(t, "!") || is_token(t, "*") ||
                              is_token(t, "&") || is_token(t, "~") || is_token(t, "&&") ||
                              is_token(t, "++") || is_token(t, "--") || is_token(t, "**"))) ||
        t.kind == TOK_NOT)
    {
        lexer_next(l); // consume op

        ASTNode *operand;
        if (is_token(t, "**"))
        {
            operand = parse_expr_prec(ctx, l, PREC_UNARY);
            if (!operand)
            {
                return ast_create(NODE_ERRONEOUS);
            }
            ASTNode *inner_deref = ast_create(NODE_EXPR_UNARY);
            inner_deref->token = t;
            inner_deref->unary.op = xstrdup("*");
            inner_deref->unary.operand = operand;
            if (operand->type_info && operand->type_info->kind == TYPE_POINTER)
            {
                inner_deref->type_info = operand->type_info->inner;
            }
            operand = inner_deref;
            t.len = 1;
        }
        else
        {
            operand = parse_expr_prec(ctx, l, PREC_UNARY);
            if (!operand)
            {
                return ast_create(NODE_ERRONEOUS);
            }
        }

        if (!operand || (is_token(t, "&") && operand->kind == NODE_EXPR_VAR))
        {
            ZenSymbol *s = find_symbol_entry(ctx, operand->var_ref.name);
            if (s && s->is_def)
            {
                zpanic_at(t,
                          "Cannot take address of manifest constant '%s' (use 'let' if you need an "
                          "address)",
                          operand->var_ref.name);
            }
        }

        char *method = NULL;
        if (is_token(t, "-"))
        {
            method = "neg";
        }
        if (is_token(t, "!") || t.kind == TOK_NOT)
        {
            method = "not";
        }
        if (is_token(t, "~"))
        {
            method = "bitnot";
        }

        if (method && operand->type_info)
        {
            Type *ot = operand->type_info;
            int is_ptr = 0;
            char *allocated_name = NULL;
            char *struct_name = resolve_struct_name_from_type(ctx, ot, &is_ptr, &allocated_name);

            if (struct_name)
            {
                char buf[MAX_ERROR_MSG_LEN];
                snprintf(buf, sizeof(buf), "%s__%s", struct_name, method);
                char *mangled = merge_underscores(buf);

                if (find_func(ctx, mangled))
                {
                    // Rewrite: ~x -> Struct_bitnot(x)
                    ASTNode *call = ast_create(NODE_EXPR_CALL);
                    call->token = t;
                    ASTNode *callee = ast_create(NODE_EXPR_VAR);
                    callee->var_ref.name = xstrdup(mangled);
                    call->call.callee = callee;

                    // Handle 'self' argument adjustment (Pointer vs Value)
                    ASTNode *arg = operand;
                    FuncSig *sig = find_func(ctx, mangled);

                    if (sig->total_args > 0 && sig->arg_types[0]->kind == TYPE_POINTER && !is_ptr)
                    {
                        int is_rvalue =
                            (operand->kind == NODE_EXPR_CALL || operand->kind == NODE_EXPR_BINARY ||
                             operand->kind == NODE_MATCH);
                        ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                        addr->unary.op = is_rvalue ? xstrdup("&_rval") : xstrdup("&");
                        addr->unary.operand = operand;
                        addr->type_info = type_new_ptr(ot);
                        arg = addr;
                    }
                    else if (is_ptr && sig->arg_types[0]->kind != TYPE_POINTER)
                    {
                        // Function wants Value, we have Pointer -> Dereference (*)
                        ASTNode *deref = ast_create(NODE_EXPR_UNARY);
                        deref->unary.op = xstrdup("*");
                        deref->unary.operand = operand;
                        deref->type_info = ot->inner;
                        arg = deref;
                    }

                    call->call.args = arg;
                    call->type_info = sig->ret_type;
                    call->resolved_type = type_to_string(sig->ret_type);
                    lhs = call;

                    if (allocated_name)
                    {
                        zfree(allocated_name);
                    }
                    // Skip standard unary node creation
                    goto after_unary;
                }
                if (allocated_name)
                {
                    zfree(allocated_name);
                }
            }
        }

        // Standard Unary Node (for primitives or if no overload found)
        lhs = ast_create(NODE_EXPR_UNARY);
        lhs->token = t;
        if (t.kind == TOK_NOT)
        {
            lhs->unary.op = xstrdup("!");
        }
        else
        {
            lhs->unary.op = token_strdup(t);
        }
        lhs->unary.operand = operand;

        if (operand->type_info)
        {
            if (is_token(t, "&"))
            {
                lhs->type_info = type_new_ptr(operand->type_info);
            }
            else if (is_token(t, "*"))
            {
                if (operand->type_info->kind == TYPE_POINTER)
                {
                    lhs->type_info = operand->type_info->inner;
                }
            }
            else
            {
                lhs->type_info = operand->type_info;
            }
        }

    after_unary:; // Label to skip standard creation if overloaded
    }

    // --- PREFIX: va_start/va_end_args/va_copy/va_arg intrinsics ---
    else if (is_token(t, "va_start"))
    {
        lexer_next(l);
        if (lexer_peek(l).kind != TOK_LPAREN)
        {
            zpanic_at(t, "Expected '(' after va_start");
        }
        lexer_next(l);
        ASTNode *ap = parse_expression(ctx, l);
        if (lexer_next(l).kind != TOK_COMMA)
        {
            zpanic_at(t, "Expected ',' in va_start");
        }
        ASTNode *last = parse_expression(ctx, l);
        if (lexer_next(l).kind != TOK_RPAREN)
        {
            zpanic_at(t, "Expected ')' after va_start args");
        }
        lhs = ast_create(NODE_VA_START);
        lhs->va_start_args.ap = ap;
        lhs->va_start_args.last_arg = last;
    }
    else if (is_token(t, "va_end_args"))
    {
        lexer_next(l);
        if (lexer_peek(l).kind != TOK_LPAREN)
        {
            zpanic_at(t, "Expected '(' after va_end_args");
        }
        lexer_next(l);
        ASTNode *ap = parse_expression(ctx, l);
        if (lexer_next(l).kind != TOK_RPAREN)
        {
            zpanic_at(t, "Expected ')' after va_end_args arg");
        }
        lhs = ast_create(NODE_VA_END);
        lhs->va_end_args.ap = ap;
    }
    else if (is_token(t, "va_copy"))
    {
        lexer_next(l);
        if (lexer_peek(l).kind != TOK_LPAREN)
        {
            zpanic_at(t, "Expected '(' after va_copy");
        }
        lexer_next(l);
        ASTNode *dest = parse_expression(ctx, l);
        if (lexer_next(l).kind != TOK_COMMA)
        {
            zpanic_at(t, "Expected ',' in va_copy");
        }
        ASTNode *src = parse_expression(ctx, l);
        if (lexer_next(l).kind != TOK_RPAREN)
        {
            zpanic_at(t, "Expected ')' after va_copy args");
        }
        lhs = ast_create(NODE_VA_COPY);
        lhs->va_copy_args.dest = dest;
        lhs->va_copy_args.src = src;
    }
    else if (is_token(t, "va_arg"))
    {
        lexer_next(l);
        if (lexer_peek(l).kind != TOK_LPAREN)
        {
            zpanic_at(t, "Expected '(' after va_arg");
        }
        lexer_next(l);
        ASTNode *ap = parse_expression(ctx, l);
        if (lexer_next(l).kind != TOK_COMMA)
        {
            zpanic_at(t, "Expected ',' in va_arg");
        }

        Type *tinfo = parse_type_formal(ctx, l);
        if (!tinfo)
        {
            return NULL;
        }

        if (lexer_next(l).kind != TOK_RPAREN)
        {
            zpanic_at(t, "Expected ')' after va_arg args");
        }

        lhs = ast_create(NODE_VA_ARG);
        lhs->va_arg_val.ap = ap;
        lhs->va_arg_val.type_info = tinfo;
        lhs->type_info = tinfo; // The expression evaluates to this type
    }
    else if (is_token(t, "sizeof"))
    {
        lexer_next(l); // consume sizeof
        lhs = parse_sizeof_expr(ctx, l, t);
    }
    else
    {
        lhs = parse_primary(ctx, l);
        if (!lhs)
        {
            return NULL;
        }
    }

    // --- MAIN LOOP: binary operators + postfix operations ---
    while (1)
    {
        Token op = lexer_peek(l);

        if (op.line > l->line && op.kind == TOK_OP &&
            (is_token(op, "*") || is_token(op, "&") || is_token(op, "+") || is_token(op, "-")))
        {
            break;
        }

        Precedence prec = get_token_precedence(op);

        // Handle postfix ++ and -- (highest postfix precedence)
        if (op.kind == TOK_OP && op.len == 2 &&
            ((op.start[0] == '+' && op.start[1] == '+') ||
             (op.start[0] == '-' && op.start[1] == '-')))
        {
            lexer_next(l); // consume ++ or --
            ASTNode *node = ast_create(NODE_EXPR_UNARY);
            node->token = op;
            node->unary.op = (op.start[0] == '+') ? xstrdup("_post++") : xstrdup("_post--");
            node->unary.operand = lhs;
            node->type_info = lhs->type_info;
            lhs = node;
            continue;
        }

        if (prec == PREC_NONE || prec < min_prec)
        {
            break;
        }

        // Pointer access: ->
        if (op.kind == TOK_ARROW && op.start[0] == '-')
        {
            lexer_next(l);
            Token field = lexer_next(l);
            if (!token_is_field_name(field))
            {
                zpanic_at(field, "Expected field name after ->");
                break;
            }
            ASTNode *node = ast_create(NODE_EXPR_MEMBER);
            node->token = field;
            node->member.target = lhs;
            node->member.field = token_strdup(field);
            node->member.is_pointer_access = 1;

            // Opaque Check
            int is_ptr_dummy = 0;
            char *alloc_name = NULL;
            char *sname =
                resolve_struct_name_from_type(ctx, lhs->type_info, &is_ptr_dummy, &alloc_name);
            if (sname)
            {
                ASTNode *def = find_struct_def(ctx, sname);
                if (def && def->kind == NODE_STRUCT && def->strct.is_opaque)
                {
                    if (!def->strct.defined_in_file ||
                        (ctx->current_filename &&
                         strcmp(def->strct.defined_in_file, ctx->current_filename) != 0))
                    {
                        zpanic_at(field, "Cannot access private field '%s' of opaque struct '%s'",
                                  node->member.field, sname);
                    }
                }
                if (alloc_name)
                {
                    zfree(alloc_name);
                }
            }

            if (lhs->type_info)
            {
                node->type_info = get_field_type(ctx, lhs->type_info, node->member.field);
                if (node->type_info)
                {
                    node->resolved_type = type_to_string(node->type_info);
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

            lhs = node;
            continue;
        }

        // Null-safe access: ?.
        if (op.kind == TOK_Q_DOT)
        {
            lexer_next(l);
            Token field = lexer_next(l);
            if (!token_is_field_name(field))
            {
                zpanic_at(field, "Expected field name after ?.");
                break;
            }
            ASTNode *node = ast_create(NODE_EXPR_MEMBER);
            node->token = field;
            node->member.target = lhs;
            node->member.field = token_strdup(field);
            node->member.is_pointer_access = 2;

            // Opaque Check
            int is_ptr_dummy = 0;
            char *alloc_name = NULL;
            char *sname =
                resolve_struct_name_from_type(ctx, lhs->type_info, &is_ptr_dummy, &alloc_name);
            if (sname)
            {
                ASTNode *def = find_struct_def(ctx, sname);
                if (def && def->kind == NODE_STRUCT && def->strct.is_opaque)
                {
                    if (!def->strct.defined_in_file ||
                        (ctx->current_filename &&
                         strcmp(def->strct.defined_in_file, ctx->current_filename) != 0))
                    {
                        zpanic_at(field, "Cannot access private field '%s' of opaque struct '%s'",
                                  node->member.field, sname);
                    }
                }
                if (alloc_name)
                {
                    zfree(alloc_name);
                }
            }

            node->type_info = get_field_type(ctx, lhs->type_info, node->member.field);
            if (node->type_info)
            {
                node->resolved_type = type_to_string(node->type_info);
            }

            lhs = node;
            continue;
        }

        // Postfix ? (Result Unwrap OR Ternary)
        if (op.kind == TOK_QUESTION)
        {
            // Disambiguate
            Lexer lookahead = *l;
            lexer_next(&lookahead); // skip ?
            Token next = lexer_peek(&lookahead);

            // Heuristic: If next token starts an expression => Ternary
            // (Ident, Number, String, (, {, -, !, *, etc)
            int is_ternary = 0;
            if (next.kind == TOK_INT || next.kind == TOK_FLOAT || next.kind == TOK_STRING ||
                next.kind == TOK_IDENT || next.kind == TOK_LPAREN || next.kind == TOK_LBRACE ||
                next.kind == TOK_SIZEOF || next.kind == TOK_DEFER || next.kind == TOK_AUTOFREE ||
                next.kind == TOK_FSTRING || next.kind == TOK_CHAR)
            {
                is_ternary = 1;
            }
            // Check unary ops
            if (next.kind == TOK_OP)
            {
                if (is_token(next, "-") || is_token(next, "!") || is_token(next, "*") ||
                    is_token(next, "&") || is_token(next, "~") || is_token(next, "."))
                {
                    is_ternary = 1;
                }
            }

            if (is_ternary)
            {
                if (PREC_TERNARY < min_prec)
                {
                    break;
                }

                lexer_next(l); // consume ?
                ASTNode *true_expr = parse_expression(ctx, l);
                z_parse_expect(l, TOK_COLON, "Expected : in ternary");
                ASTNode *false_expr = parse_expr_prec(ctx, l, PREC_TERNARY); // Right associative

                ASTNode *tern = ast_create(NODE_TERNARY);
                if (ctx->hook_zen_trigger)
                {
                    ctx->hook_zen_trigger(TRIGGER_TERNARY, lhs->token, ctx->config);
                }

                tern->token = lhs->token;
                tern->ternary.cond = lhs;
                tern->ternary.true_expr = true_expr;
                tern->ternary.false_expr = false_expr;

                // Type inference hint: Both branches should match?
                // Logic later in codegen/semant.
                lhs = tern;
                continue;
            }

            // Otherwise: Unwrap (High Precedence)
            if (PREC_CALL < min_prec)
            {
                break;
            }

            lexer_next(l);
            ASTNode *n = ast_create(NODE_TRY);
            n->try_stmt.expr = lhs;
            lhs = n;
            continue;
        }

        // Pipe: |>
        if (op.kind == TOK_PIPE || (op.kind == TOK_OP && is_token(op, "|>")))
        {
            lexer_next(l);
            ASTNode *rhs = parse_expr_prec(ctx, l, prec + 1);
            if (rhs->kind == NODE_EXPR_CALL)
            {
                ASTNode *old_args = rhs->call.args;
                lhs->next = old_args;
                rhs->call.args = lhs;
                lhs = rhs;
            }
            else
            {
                ASTNode *call = ast_create(NODE_EXPR_CALL);
                call->token = t;
                call->call.callee = rhs;
                call->call.args = lhs;
                lhs->next = NULL;
                lhs = call;
            }
            continue;
        }

        lexer_next(l); // Consume operator/paren/bracket

        // Call: (...)
        if (op.kind == TOK_LPAREN)
        {
            ASTNode *call = ast_create(NODE_EXPR_CALL);
            call->token = t;

            // Method Resolution Logic (Struct Method -> Trait Method)
            ASTNode *self_arg = NULL;
            FuncSig *resolved_sig = NULL;
            char *resolved_name = NULL;

            if (lhs->kind == NODE_EXPR_MEMBER)
            {
                Type *lt = lhs->member.target->type_info;
                int is_lhs_ptr = 0;
                char *alloc_name = NULL;
                char *struct_name =
                    (lt) ? resolve_struct_name_from_type(ctx, lt, &is_lhs_ptr, &alloc_name) : NULL;

                if (struct_name)
                {
                    char *mangled = mangle_method_symbol(struct_name, NULL, lhs->member.field);

                    FuncSig *sig = find_func(ctx, mangled);

                    if (!sig)
                    {
                        // Trait method lookup: Struct__Trait_Method
                        StructRef *ref = ctx->parsed_impls_list;
                        while (ref)
                        {
                            if (ref->node && ref->node->kind == NODE_IMPL_TRAIT)
                            {
                                if (ref->node->impl_trait.target_type &&
                                    strcmp(ref->node->impl_trait.target_type, struct_name) == 0)
                                {
                                    char *trait_mangled = mangle_method_symbol(
                                        struct_name, ref->node->impl_trait.trait_name,
                                        lhs->member.field);

                                    if (find_func(ctx, trait_mangled))
                                    {
                                        sig = find_func(ctx, trait_mangled);
                                        zfree(mangled);
                                        mangled = trait_mangled;
                                        break;
                                    }
                                    zfree(trait_mangled);
                                }
                            }
                            ref = ref->next;
                        }
                    }

                    if (sig)
                    {
                        // Check if this is a static method being called with dot operator
                        // Static methods don't have 'self' as first parameter
                        int is_static_method = 0;
                        if (sig->total_args == 0)
                        {
                            // No arguments at all - definitely static
                            is_static_method = 1;
                        }
                        else if (sig->arg_types[0])
                        {
                            // Check if first parameter is a pointer to the struct type
                            // Instance methods have: fn method(self) where self is StructType*
                            // Static methods have: fn method(x: int, y: int) etc.
                            Type *first_param = sig->arg_types[0];

                            // If first param is not a pointer, it's likely static
                            // OR if it's a pointer but not to this struct type
                            if (first_param->kind != TYPE_POINTER)
                            {
                                is_static_method = 1;
                            }
                            else if (first_param->inner)
                            {
                                // Check if the inner type matches the struct
                                char *inner_name = NULL;
                                if (first_param->inner->kind == TYPE_STRUCT)
                                {
                                    inner_name = first_param->inner->name;
                                }

                                if (!inner_name || strcmp(inner_name, struct_name) != 0)
                                {
                                    is_static_method = 1;
                                }
                            }
                        }

                        if (is_static_method && (lt && lt->kind != TYPE_ENUM))
                        {
                            zpanic_at(lexer_peek(l),
                                      "Cannot call static method '%s' with dot operator\n"
                                      "   = help: Use '%s::%s(...)' instead of instance.%s(...)",
                                      lhs->member.field, struct_name, lhs->member.field,
                                      lhs->member.field);
                        }

                        resolved_name = xstrdup(mangled);
                        resolved_sig = sig;

                        // Create 'self' argument only for instance methods
                        if (!is_static_method)
                        {
                            ASTNode *obj = lhs->member.target;

                            // Handle Reference/Pointer adjustment based on signature
                            if (sig->total_args > 0 && sig->arg_types[0] &&
                                sig->arg_types[0]->kind == TYPE_POINTER)
                            {
                                if (!is_lhs_ptr)
                                {
                                    // Function expects ptr, have value -> &obj
                                    int is_rvalue =
                                        (obj->kind == NODE_EXPR_CALL ||
                                         obj->kind == NODE_EXPR_BINARY ||
                                         obj->kind == NODE_EXPR_STRUCT_INIT ||
                                         obj->kind == NODE_EXPR_CAST || obj->kind == NODE_MATCH);

                                    ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                                    addr->unary.op = is_rvalue ? xstrdup("&_rval") : xstrdup("&");
                                    addr->unary.operand = obj;
                                    addr->type_info = type_new_ptr(lt);
                                    self_arg = addr;
                                }
                                else
                                {
                                    self_arg = obj;
                                }
                            }
                            else
                            {
                                // Function expects value
                                if (is_lhs_ptr)
                                {
                                    // Have ptr, need value -> *obj
                                    ASTNode *deref = ast_create(NODE_EXPR_UNARY);
                                    deref->unary.op = xstrdup("*");
                                    deref->unary.operand = obj;
                                    if (lt && lt->kind == TYPE_POINTER && lt->inner)
                                    {
                                        deref->type_info = lt->inner;
                                    }
                                    self_arg = deref;
                                }
                                else
                                {
                                    self_arg = obj;
                                }
                            }
                        }
                    }
                }
                if (alloc_name)
                {
                    zfree(alloc_name);
                }
            }

            if (resolved_name)
            {
                ASTNode *callee = ast_create(NODE_EXPR_VAR);
                callee->var_ref.name = resolved_name;
                call->call.callee = callee;
            }
            else
            {
                call->call.callee = lhs;
            }

            ASTNode *head = NULL, *tail = NULL;
            char **arg_names = NULL;
            int count = 0;
            int has_named = 0;

            if (lexer_peek(l).kind != TOK_RPAREN)
            {
                while (1)
                {
                    if (lexer_peek(l).kind == TOK_EOF)
                    {
                        break;
                    }
                    char *arg_name = NULL;

                    // Check for named argument: IDENT : expr
                    Token t1 = lexer_peek(l);
                    if (t1.kind == TOK_IDENT)
                    {
                        // Lookahead for colon
                        Token t2 = lexer_peek2(l);
                        if (t2.kind == TOK_COLON)
                        {
                            arg_name = token_strdup(t1);
                            has_named = 1;
                            lexer_next(l); // eat IDENT
                            lexer_next(l); // eat :
                        }
                    }

                    ASTNode *arg = parse_expression(ctx, l);

                    // Move Semantics Logic
                    check_move_usage(ctx, arg, arg ? arg->token : t1);
                    if (arg && arg->kind == NODE_EXPR_VAR)
                    {
                        Type *inner_t = find_symbol_type_info(ctx, arg->var_ref.name);
                        if (!inner_t)
                        {
                            ZenSymbol *s = find_symbol_entry(ctx, arg->var_ref.name);
                            if (s)
                            {
                                inner_t = s->type_info;
                            }
                        }

                        if (!is_type_copy(ctx, inner_t))
                        {
                            ZenSymbol *s = find_symbol_entry(ctx, arg->var_ref.name);
                            if (s)
                            {
                                s->is_moved = 1;
                            }
                        }
                    }

                    if (!head)
                    {
                        head = arg;
                    }
                    else
                    {
                        tail->next = arg;
                    }
                    tail = arg;

                    // Store arg name
                    arg_names = xrealloc(arg_names, (size_t)(count + 1) * sizeof(char *));
                    arg_names[count] = arg_name;
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
            }
            if (lexer_next(l).kind != TOK_RPAREN)
            {
                zpanic_at(lexer_peek(l), "Expected )");
            }

            // Prepend 'self' argument if resolved
            if (self_arg)
            {
                self_arg->next = head;
                head = self_arg;
                count++;

                if (has_named)
                {
                    // Prepend NULL to arg_names for self
                    char **new_names = xmalloc(sizeof(char *) * (size_t)(count));
                    new_names[0] = NULL;
                    for (int i = 0; i < count - 1; i++)
                    {
                        new_names[i + 1] = arg_names[i];
                    }
                    zfree(arg_names);
                    arg_names = new_names;
                }
            }

            call->call.args = head;
            call->call.arg_names = has_named ? arg_names : NULL;
            call->call.count = count;

            call->resolved_type = xstrdup("unknown");

            if (resolved_sig)
            {
                call->type_info = resolved_sig->ret_type;
                if (call->type_info)
                {
                    call->resolved_type = type_to_string(call->type_info);
                }
            }
            else if (lhs->type_info && lhs->type_info->kind == TYPE_FUNCTION &&
                     lhs->type_info->inner)
            {
                call->type_info = lhs->type_info->inner;
            }

            lhs = call;
            continue;
        }

        // Index: [...] or Slice: [start..end]
        if (op.kind == TOK_LBRACKET || (op.kind == TOK_OP && is_token(op, "[")))
        {
            ASTNode *start = NULL;
            ASTNode *end = NULL;
            int is_slice = 0;

            // Fallback: If LHS is a variable but missing type info, look it up now
            if (!lhs->type_info && lhs->kind == NODE_EXPR_VAR)
            {
                Type *sym_type = find_symbol_type_info(ctx, lhs->var_ref.name);
                if (sym_type)
                {
                    lhs->type_info = sym_type;
                    lhs->resolved_type = type_to_string(sym_type);
                }
            }

            // Case: [..] or [..end]
            if (lexer_peek(l).kind == TOK_DOTDOT || lexer_peek(l).kind == TOK_DOTDOT_LT)
            {
                is_slice = 1;
                lexer_next(l); // consume .. or ..<
                if (lexer_peek(l).kind != TOK_RBRACKET)
                {
                    end = parse_expression(ctx, l);
                }
            }
            else
            {
                // Case: [start] or [start..] or [start..end] or [start, expr, ...]
                start = parse_expression(ctx, l);
                if (lexer_peek(l).kind == TOK_DOTDOT || lexer_peek(l).kind == TOK_DOTDOT_LT)
                {
                    is_slice = 1;
                    lexer_next(l); // consume ..
                    if (lexer_peek(l).kind != TOK_RBRACKET)
                    {
                        end = parse_expression(ctx, l);
                    }
                }
            }

            // Multi-index: [expr, expr, ...] -> collect extra indices
            ASTNode *extra_head = NULL;
            ASTNode *extra_tail = NULL;
            int extra_count = 0;
            if (!is_slice)
            {
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
            }

            if (lexer_next(l).kind != TOK_RBRACKET)
            {
                zpanic_at(lexer_peek(l), "Expected ]");
            }

            if (is_slice)
            {
                ASTNode *node = ast_create(NODE_EXPR_SLICE);
                node->slice.array = lhs;
                node->slice.start = start;
                node->slice.end = end;

                // Type Inference & Registration
                if (lhs->type_info)
                {
                    Type *inner = NULL;
                    if (lhs->type_info->kind == TYPE_ARRAY)
                    {
                        inner = lhs->type_info->inner;
                    }
                    else if (lhs->type_info->kind == TYPE_POINTER)
                    {
                        inner = lhs->type_info->inner;
                    }

                    if (inner)
                    {
                        node->type_info = type_new(TYPE_ARRAY);
                        node->type_info->inner = inner;
                        node->type_info->array_size = 0; // Slice

                        // Clean up string for registration (e.g. "int" from "int*")
                        char *inner_str = type_to_string(inner);

                        // Strip * if it somehow managed to keep one, though
                        // parse_type_formal should handle it For now assume type_to_string
                        // gives base type
                        register_slice(ctx, inner_str);
                    }
                }

                lhs = node;
            }
            else
            {
                ASTNode *node = ast_create(NODE_EXPR_INDEX);
                node->token = t;
                node->index.array = lhs;
                node->index.index = start;
                node->index.extra_indices = extra_head;
                node->index.index_count = 1 + extra_count;

                char *struct_name = NULL;
                Type *inner_t = lhs->type_info;
                int is_ptr = 0;

                if (inner_t)
                {
                    if (inner_t->kind == TYPE_STRUCT)
                    {
                        struct_name = inner_t->name;
                    }
                    /*
                    else if (t->kind == TYPE_POINTER && t->inner && t->inner->kind == TYPE_STRUCT)
                    {
                        // struct_name = t->inner->name;
                        // is_ptr = 1;
                        // DISABLE: Pointers should use array indexing by default, not operator
                    overload.
                        // If users want operator overload, they must dereference first (*ptr)[idx]
                    }
                    */
                }
                if (!struct_name && lhs->resolved_type)
                {
                    char *s = lhs->resolved_type;
                    if (strncmp(s, "struct ", 7) == 0)
                    {
                        s += 7;
                    }
                    ASTNode *def = find_struct_def(ctx, s);
                    if (def && def->kind == NODE_STRUCT)
                    {
                        struct_name = s;
                        if (strchr(lhs->resolved_type, '*'))
                        {
                            is_ptr = 1;
                        }
                    }
                    if (!struct_name)
                    {
                        def = find_struct_def(ctx, lhs->resolved_type);
                        if (def && def->kind == NODE_STRUCT)
                        {
                            struct_name = lhs->resolved_type;
                            // Just assume val type
                        }
                    }
                }

                if (struct_name)
                {
                    char index_raw[MAX_MANGLED_NAME_LEN];
                    snprintf(index_raw, sizeof(index_raw), "%s__index", struct_name);
                    char *mangled_index = merge_underscores(index_raw);

                    char get_raw[MAX_MANGLED_NAME_LEN];
                    snprintf(get_raw, sizeof(get_raw), "%s__get", struct_name);
                    char *mangled_get = merge_underscores(get_raw);

                    FuncSig *sig = find_func(ctx, mangled_index);
                    char *resolved_name = NULL;
                    char *method_name = "index";

                    if (sig)
                    {
                        resolved_name = mangled_index;
                    }
                    else
                    {
                        sig = find_func(ctx, mangled_get);
                        if (sig)
                        {
                            resolved_name = mangled_get;
                            method_name = "get";
                        }
                    }

                    // Fallback for Generics (e.g. Vec<T> -> Vec)
                    int is_generic_template = 0;
                    if (!sig && strchr(struct_name, '<'))
                    {
                        size_t len = strcspn(struct_name, "<");
                        char *base = xmalloc((size_t)(len + 1));
                        strncpy(base, struct_name, (size_t)(len));
                        base[len] = 0;

                        GenericImplTemplate *it = ctx->impl_templates;
                        while (it)
                        {
                            if (strcmp(it->struct_name, base) == 0)
                            {
                                ASTNode *m = it->impl_node->impl.methods;

                                char idx_raw[MAX_MANGLED_NAME_LEN];
                                snprintf(idx_raw, sizeof(idx_raw), "%s__index", base); /* safe */
                                char *mangled_idx = merge_underscores(idx_raw);

                                char g_raw[MAX_MANGLED_NAME_LEN];
                                snprintf(g_raw, sizeof(g_raw), "%s__get", base); /* safe */
                                char *mangled_g = merge_underscores(g_raw);

                                while (m)
                                {
                                    int found_idx =
                                        m->func.name && strcmp(m->func.name, mangled_idx) == 0;
                                    int found_get =
                                        m->func.name && strcmp(m->func.name, mangled_g) == 0;

                                    if (found_idx || found_get)
                                    {
                                        if (found_idx)
                                        {
                                            method_name = "index";
                                        }
                                        else
                                        {
                                            method_name = "get";
                                        }

                                        is_generic_template = 1;

                                        // Construct temporary signature for checking
                                        sig = xmalloc(sizeof(FuncSig));
                                        memset(sig, 0, sizeof(FuncSig));
                                        sig->ret_type = m->func.ret_type_info;
                                        sig->arg_types = m->func.arg_types;
                                        sig->total_args = m->func.count;

                                        break;
                                    }
                                    m = m->next;
                                }
                            }
                            if (is_generic_template)
                            {
                                break;
                            }
                            it = it->next;
                        }
                        zfree(base);
                    }

                    if (sig)
                    {
                        // Rewrite to Call
                        ASTNode *call = ast_create(NODE_EXPR_CALL);
                        call->token = t;
                        ASTNode *callee;

                        if (is_generic_template)
                        {
                            // For generics, keep it as a Member access so instantiation
                            // can re-resolve the correct concrete function name.
                            callee = ast_create(NODE_EXPR_MEMBER);
                            callee->member.target = lhs;
                            callee->member.field = xstrdup(method_name);
                            callee->member.is_pointer_access = is_ptr;
                            // Just a hint, logic below handles adjustments
                        }
                        else
                        {
                            callee = ast_create(NODE_EXPR_VAR);
                            callee->var_ref.name = xstrdup(resolved_name);
                        }

                        callee->token = t;
                        call->call.callee = callee;

                        ASTNode *self_arg = lhs;

                        // Pointer adjustment logic
                        if (sig->total_args > 0 && sig->arg_types[0]->kind == TYPE_POINTER &&
                            !is_ptr)
                        {
                            ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                            addr->unary.op = xstrdup("&");
                            addr->unary.operand = lhs;
                            if (inner_t)
                            {
                                addr->type_info = type_new_ptr(inner_t);
                            }
                            self_arg = addr;
                        }
                        else if (is_ptr && sig->arg_types[0]->kind != TYPE_POINTER)
                        {
                            ASTNode *deref = ast_create(NODE_EXPR_UNARY);
                            deref->unary.op = xstrdup("*");
                            deref->unary.operand = lhs;
                            self_arg = deref;
                        }

                        // If using MEMBER access, we don't pass self as argument in AST
                        // because codegen/semant handles "obj.method(args)" by injecting obj.
                        // BUT here we are manually constructing the call.
                        // If we use MEMBER, we should NOT put self in args?
                        // standard parser produces: Call(Member, args). Member has target=obj.

                        if (is_generic_template)
                        {
                            // Update target of member to be the adjusted self?
                            // Member access usually expects the object as 'target'.
                            // If we adjusted it (e.g. &obj), then target should be &obj.
                            callee->member.target = self_arg;

                            // And we DO NOT add self to call.args
                            call->call.args = start;
                        }
                        else
                        {
                            // For VAR call (direct function call), we MUST pass self as first arg.
                            self_arg->next = start;
                            call->call.args = self_arg;
                        }

                        call->type_info = sig->ret_type;
                        if (call->type_info)
                        {
                            call->resolved_type = type_to_string(sig->ret_type);
                        }
                        else
                        {
                            call->resolved_type = xstrdup("unknown");
                        }

                        lhs = call;
                        zfree(mangled_index);
                        zfree(mangled_get);
                        continue;
                    }
                    zfree(mangled_index);
                    zfree(mangled_get);
                }

                // Static Array Bounds Check
                if (lhs->type_info && lhs->type_info->kind == TYPE_ARRAY &&
                    lhs->type_info->array_size > 0)
                {
                    if (start->kind == NODE_EXPR_LITERAL && start->literal.kind == LITERAL_INT)
                    {
                        int idx = (int)start->literal.int_val;
                        if (idx < 0 || idx >= lhs->type_info->array_size)
                        {
                            warn_array_bounds(op, idx, lhs->type_info->array_size);
                        }
                    }
                }

                // Assign type_info for index access (Fix for nested generics)
                if (lhs->type_info &&
                    (lhs->type_info->kind == TYPE_ARRAY || lhs->type_info->kind == TYPE_POINTER))
                {
                    node->type_info = lhs->type_info->inner;
                }
                if (!node->type_info)
                {
                    node->type_info = type_new(TYPE_INT);
                }

                lhs = node;
            }
            continue;
        }

        // Member: .
        if (op.kind == TOK_OP && is_token(op, "."))
        {
            Token field = lexer_next(l);
            if (!token_is_field_name(field))
            {
                zpanic_at(field, "Expected field name after .");
                break;
            }
            ASTNode *node = ast_create(NODE_EXPR_MEMBER);
            node->token = field;
            node->member.target = lhs;
            node->member.field = token_strdup(field);
            node->member.is_pointer_access = 0;

            // Opaque Check
            int is_ptr_dummy = 0;
            char *alloc_name = NULL;
            char *sname =
                resolve_struct_name_from_type(ctx, lhs->type_info, &is_ptr_dummy, &alloc_name);
            if (sname)
            {
                ASTNode *def = find_struct_def(ctx, sname);
                if (def && def->kind == NODE_STRUCT && def->strct.is_opaque)
                {
                    if (!def->strct.defined_in_file ||
                        (ctx->current_filename &&
                         strcmp(def->strct.defined_in_file, ctx->current_filename) != 0))
                    {
                        zpanic_at(field, "Cannot access private field '%s' of opaque struct '%s'",
                                  node->member.field, sname);
                    }
                }
                if (alloc_name)
                {
                    zfree(alloc_name);
                }
            }

            node->member.field = token_strdup(field);
            node->member.is_pointer_access = 0;

            if (lhs->type_info && lhs->type_info->kind == TYPE_POINTER)
            {
                node->member.is_pointer_access = 1;

                // Special case: .val() on pointer = dereference
                if (strcmp(node->member.field, "val") == 0 && lexer_peek(l).kind == TOK_LPAREN)
                {
                    lexer_next(l);
                    if (lexer_peek(l).kind == TOK_RPAREN)
                    {
                        lexer_next(l); // consume )
                        // Rewrite to dereference: *ptr
                        ASTNode *deref = ast_create(NODE_EXPR_UNARY);
                        deref->unary.op = xstrdup("*");
                        deref->unary.operand = lhs;
                        if (lhs->type_info && lhs->type_info->inner)
                        {
                            deref->type_info = lhs->type_info->inner;
                        }
                        lhs = deref;
                        continue;
                    }
                }
            }
            else if (lhs->kind == NODE_EXPR_VAR)
            {
                char *type = find_symbol_type(ctx, lhs->var_ref.name);
                if (type && strchr(type, '*'))
                {
                    node->member.is_pointer_access = 1;

                    // Special case: .val() on pointer = dereference
                    if (strcmp(node->member.field, "val") == 0 && lexer_peek(l).kind == TOK_LPAREN)
                    {
                        lexer_next(l);
                        if (lexer_peek(l).kind == TOK_RPAREN)
                        {
                            lexer_next(l); // consume )
                            // Rewrite to dereference: *ptr
                            ASTNode *deref = ast_create(NODE_EXPR_UNARY);
                            deref->unary.op = xstrdup("*");
                            deref->unary.operand = lhs;
                            // Try to get inner type
                            if (lhs->type_info && lhs->type_info->kind == TYPE_POINTER)
                            {
                                deref->type_info = lhs->type_info->inner;
                            }
                            lhs = deref;
                            continue;
                        }
                    }
                }
                if (strcmp(lhs->var_ref.name, "self") == 0 && !node->member.is_pointer_access)
                {
                    node->member.is_pointer_access = 1;
                }
            }

            node->type_info = get_field_type(ctx, lhs->type_info, node->member.field);

            if (!node->type_info && lhs->type_info)
            {
                char *struct_name = NULL;
                Type *st = lhs->type_info;
                if (st->kind == TYPE_STRUCT)
                {
                    struct_name = st->name;
                }
                else if (st->kind == TYPE_POINTER && st->inner && st->inner->kind == TYPE_STRUCT)
                {
                    struct_name = st->inner->name;
                }

                if (struct_name)
                {
                    char buf[MAX_ERROR_MSG_LEN];
                    snprintf(buf, sizeof(buf), "%s__%s", struct_name, node->member.field);
                    char *mangled = merge_underscores(buf);

                    FuncSig *sig = find_func(ctx, mangled);

                    if (!sig)
                    {
                        // Try resolving as a trait method: Struct__Trait__Method
                        StructRef *ref = ctx->parsed_impls_list;
                        while (ref)
                        {
                            if (ref->node && ref->node->kind == NODE_IMPL_TRAIT)
                            {
                                const char *t_struct = ref->node->impl_trait.target_type;
                                if (t_struct && strcmp(t_struct, struct_name) == 0)
                                {
                                    char buf_trait[MAX_ERROR_MSG_LEN];
                                    snprintf(buf_trait, sizeof(buf_trait), "%s__%s__%s",
                                             struct_name, ref->node->impl_trait.trait_name,
                                             node->member.field);
                                    char *trait_mangled = merge_underscores(buf_trait);

                                    if (find_func(ctx, trait_mangled))
                                    {
                                        zfree(mangled);
                                        mangled = trait_mangled;
                                        sig = find_func(ctx, mangled);
                                        break;
                                    }
                                    zfree(trait_mangled);
                                }
                            }
                            ref = ref->next;
                        }
                    }

                    if (sig)
                    {
                        // It is a method! Create a Function Type Info to carry the return
                        // type
                        Type *ft = type_new(TYPE_FUNCTION);
                        ft->name = xstrdup(mangled);
                        ft->inner = sig->ret_type;
                        node->type_info = ft;
                    }
                }
            }

            if (node->type_info)
            {
                node->resolved_type = type_to_string(node->type_info);
            }
            else
            {
                node->resolved_type = xstrdup("unknown");
            }

            // Handle Generic Method Call: object.method<T>
            if (lexer_peek(l).kind == TOK_LANGLE)
            {
                Lexer lookahead = *l;
                lexer_next(&lookahead);

                int valid_generic = 0;
                int saved = ctx->is_speculative;
                ctx->is_speculative = 1;

                // Speculatively check if it's a valid generic list
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
                ctx->is_speculative = saved;

                if (valid_generic)
                {
                    lexer_next(l); // consume <

                    int cap = 8;
                    char **concrete = xmalloc((size_t)(cap) * sizeof(char *));
                    char **unmangled = xmalloc((size_t)(cap) * sizeof(char *));
                    int argc = 0;
                    while (1)
                    {
                        if (argc >= cap)
                        {
                            cap *= 2;
                            concrete = xrealloc(concrete, (size_t)(cap) * sizeof(char *));
                            unmangled = xrealloc(unmangled, (size_t)(cap) * sizeof(char *));
                        }
                        Type *inner_t = parse_type_formal(ctx, l);
                        if (!inner_t)
                        {
                            return NULL;
                        }
                        concrete[argc] = type_to_string(inner_t);
                        unmangled[argc] = type_to_c_string(inner_t);
                        argc++;
                        if (lexer_peek(l).kind == TOK_COMMA)
                        {
                            lexer_next(l);
                        }
                        else
                        {
                            break;
                        }
                    }
                    if (lexer_next(l).kind != TOK_RANGLE)
                    {
                        zpanic_at(lexer_peek(l), "Expected >");
                    }

                    // Locate the generic template
                    char *mn = NULL; // method name
                    char *full_name = NULL;

                    // If logic above found a sig, we have a mangled name in node->type_info->name
                    // But for templates, find_func might have failed.
                    // Construct potential template name: Struct__method
                    char *struct_name = NULL;
                    if (lhs->type_info)
                    {
                        if (lhs->type_info->kind == TYPE_STRUCT)
                        {
                            struct_name = lhs->type_info->name;
                        }
                        else if (lhs->type_info->kind == TYPE_POINTER && lhs->type_info->inner &&
                                 lhs->type_info->inner->kind == TYPE_STRUCT)
                        {
                            struct_name = lhs->type_info->inner->name;
                        }
                    }

                    if (struct_name)
                    {
                        char buf[MAX_ERROR_MSG_LEN];
                        snprintf(buf, sizeof(buf), "%s__%s", struct_name, node->member.field);
                        full_name = merge_underscores(buf);

                        // Join types
                        size_t ac_sz = 1024, au_sz = 1024;
                        char *all_concrete = xmalloc((size_t)(ac_sz));
                        char *all_unmangled = xmalloc((size_t)(au_sz));
                        all_concrete[0] = 0;
                        all_unmangled[0] = 0;
                        for (int i = 0; i < argc; i++)
                        {
                            if (i > 0)
                            {
                                if (strlen(all_concrete) + 2 >= ac_sz)
                                {
                                    ac_sz *= 2;
                                    all_concrete = xrealloc(all_concrete, ac_sz);
                                }
                                if (strlen(all_unmangled) + 2 >= au_sz)
                                {
                                    au_sz *= 2;
                                    all_unmangled = xrealloc(all_unmangled, au_sz);
                                }
                                strcat(all_concrete, ",");
                                strcat(all_unmangled, ",");
                            }
                            if (strlen(all_concrete) + strlen(concrete[i]) + 1 >= ac_sz)
                            {
                                ac_sz += strlen(concrete[i]) + 1;
                                all_concrete = xrealloc(all_concrete, ac_sz);
                            }
                            if (strlen(all_unmangled) + strlen(unmangled[i]) + 1 >= au_sz)
                            {
                                au_sz += strlen(unmangled[i]) + 1;
                                all_unmangled = xrealloc(all_unmangled, au_sz);
                            }
                            strcat(all_concrete, concrete[i]);
                            strcat(all_unmangled, unmangled[i]);
                            zfree(concrete[i]);
                            zfree(unmangled[i]);
                        }
                        zfree(concrete);
                        zfree(unmangled);

                        mn = instantiate_function_template(ctx, full_name, all_concrete,
                                                           all_unmangled);
                        if (mn)
                        {
                            char *p = (char *)strstr(mn, "__");
                            if (p)
                            {
                                zfree(node->member.field);
                                node->member.field = xstrdup(p + 2);
                            }

                            Type *ft = type_new(TYPE_FUNCTION);
                            ft->name = xstrdup(mn);
                            FuncSig *isig = find_func(ctx, mn);
                            if (isig)
                            {
                                ft->inner = isig->ret_type;
                            }
                            node->type_info = ft;
                        }
                        if (full_name)
                        {
                            zfree(full_name);
                        }
                        if (all_concrete)
                        {
                            zfree(all_concrete);
                        }
                        if (all_unmangled)
                        {
                            zfree(all_unmangled);
                        }
                    }
                }
            }

            lhs = node;
            continue;
        }

        int next_prec = (int)(prec + 1);
        if (op.kind == TOK_OP && (is_token(op, "**") || is_token(op, "**=")))
        {
            next_prec = (int)(prec);
        }

        ASTNode *rhs = parse_expr_prec(ctx, l, (Precedence)(next_prec));
        if (!rhs)
        {
            // parse_expr_prec can return NULL on malformed input. Without the
            // RHS we cannot form a binary expression, so bail out.
            return lhs;
        }
        ASTNode *bin = ast_create(NODE_EXPR_BINARY);
        bin->token = op;
        if (op.kind == TOK_OP)
        {
            if (is_token(op, "&") || is_token(op, "|") || is_token(op, "^"))
            {
                if (ctx->hook_zen_trigger)
                {
                    ctx->hook_zen_trigger(TRIGGER_BITWISE, op, ctx->config);
                }
            }
            else if (is_token(op, "<<") || is_token(op, ">>"))
            {
                if (ctx->hook_zen_trigger)
                {
                    ctx->hook_zen_trigger(TRIGGER_BITWISE, op, ctx->config);
                }
            }
        }
        bin->binary.left = lhs;
        bin->binary.right = rhs;

        // Move Semantics Logic
        if (op.kind == TOK_OP && is_token(op, "=")) // Assignment "="
        {
            // 1. RHS is being read: Check validity
            check_move_usage(ctx, rhs, op);

            // 2. Mark RHS as moved (Transfer ownership) if it's a Move type
            if (rhs->kind == NODE_EXPR_VAR)
            {
                Type *inner_t = find_symbol_type_info(ctx, rhs->var_ref.name);
                // If type info not on var, try looking up symbol
                if (!inner_t)
                {
                    ZenSymbol *s = find_symbol_entry(ctx, rhs->var_ref.name);
                    if (s)
                    {
                        inner_t = s->type_info;
                    }
                }

                if (!is_type_copy(ctx, inner_t))
                {
                    ZenSymbol *s = find_symbol_entry(ctx, rhs->var_ref.name);
                    if (s)
                    {
                        s->is_moved = 1;
                    }
                }
            }

            // 3. LHS is being written: Resurrect (it is now valid)
            if (lhs->kind == NODE_EXPR_VAR)
            {
                ZenSymbol *s = find_symbol_entry(ctx, lhs->var_ref.name);
                if (s)
                {
                    s->is_moved = 0;
                }
            }

            // 4. Trait Object Wrapping for Assignment
            char *raw_lhs_type = NULL;
            int allocated_lhs = 0;
            if (lhs->kind == NODE_EXPR_VAR)
            {
                raw_lhs_type = find_symbol_type(ctx, lhs->var_ref.name);
            }
            if (!raw_lhs_type && lhs->resolved_type)
            {
                raw_lhs_type = lhs->resolved_type;
            }
            if (!raw_lhs_type && lhs->type_info)
            {
                raw_lhs_type = type_to_string(lhs->type_info);
                allocated_lhs = 1;
            }

            if (raw_lhs_type)
            {
                char *clean_lhs_type = raw_lhs_type;
                if (strncmp(clean_lhs_type, "const ", 6) == 0)
                {
                    clean_lhs_type += 6;
                }

                if (is_trait(clean_lhs_type) && rhs)
                {
                    rhs = transform_to_trait_object(ctx, clean_lhs_type, rhs);
                    bin->binary.right = rhs;
                }
                if (allocated_lhs)
                {
                    zfree(raw_lhs_type);
                }
            }
        }
        else // All other binary ops (including +=, -=, etc. which read LHS first)
        {
            check_move_usage(ctx, lhs, op);
            check_move_usage(ctx, rhs, op);
        }

        if (op.kind == TOK_LANGLE)
        {
            bin->binary.op = xstrdup("<");
        }
        else if (op.kind == TOK_RANGLE)
        {
            bin->binary.op = xstrdup(">");
        }
        else if (op.kind == TOK_AND)
        {
            bin->binary.op = xstrdup("&&");
        }
        else if (op.kind == TOK_OR)
        {
            bin->binary.op = xstrdup("||");
        }
        else
        {
            bin->binary.op = token_strdup(op);
        }

        if (is_comparison_op(bin->binary.op))
        {
            // Check for identical operands (x == x)
            if (lhs->kind == NODE_EXPR_VAR && rhs->kind == NODE_EXPR_VAR)
            {
                if (strcmp(lhs->var_ref.name, rhs->var_ref.name) == 0)
                {
                    if (strcmp(bin->binary.op, "==") == 0 || strcmp(bin->binary.op, ">=") == 0 ||
                        strcmp(bin->binary.op, "<=") == 0)
                    {
                        warn_comparison_always_true(op, "Comparing a variable to itself");
                    }
                    else if (strcmp(bin->binary.op, "!=") == 0 ||
                             strcmp(bin->binary.op, ">") == 0 || strcmp(bin->binary.op, "<") == 0)
                    {
                        warn_comparison_always_false(op, "Comparing a variable to itself");
                    }
                }
            }
            else if (lhs->kind == NODE_EXPR_LITERAL && lhs->literal.kind == LITERAL_INT &&
                     rhs->kind == NODE_EXPR_LITERAL && rhs->literal.kind == LITERAL_INT)
            {
                // Check if literals make sense (e.g. 5 > 5)
                if (lhs->literal.int_val == rhs->literal.int_val)
                {
                    if (strcmp(bin->binary.op, "==") == 0 || strcmp(bin->binary.op, ">=") == 0 ||
                        strcmp(bin->binary.op, "<=") == 0)
                    {
                        warn_comparison_always_true(op, "Comparing identical literals");
                    }
                    else
                    {
                        warn_comparison_always_false(op, "Comparing identical literals");
                    }
                }
            }

            if (lhs->type_info && type_is_unsigned(lhs->type_info))
            {
                if (rhs->kind == NODE_EXPR_LITERAL && rhs->literal.kind == LITERAL_INT &&
                    rhs->literal.int_val == 0)
                {
                    if (strcmp(bin->binary.op, ">=") == 0)
                    {
                        warn_comparison_always_true(op, "Unsigned value is always >= 0");
                    }
                    else if (strcmp(bin->binary.op, "<") == 0)
                    {
                        warn_comparison_always_false(op, "Unsigned value is never < 0");
                    }
                }
            }
        }

        if (strcmp(bin->binary.op, "=") == 0 || strcmp(bin->binary.op, "+=") == 0 ||
            strcmp(bin->binary.op, "-=") == 0 || strcmp(bin->binary.op, "*=") == 0 ||
            strcmp(bin->binary.op, "/=") == 0)
        {

            if (lhs->kind == NODE_EXPR_VAR)
            {
                // Check if the variable is const
                Type *inner_t = find_symbol_type_info(ctx, lhs->var_ref.name);
                if (inner_t && inner_t->is_const)
                {
                    zpanic_at(op, "Cannot assign to const variable '%s'", lhs->var_ref.name);
                }
            }
            else if (lhs->kind == NODE_EXPR_INDEX || lhs->kind == NODE_EXPR_MEMBER)
            {
                ASTNode *base = lhs;
                while (base)
                {
                    if (base->kind == NODE_EXPR_INDEX)
                    {
                        base = base->index.array;
                    }
                    else if (base->kind == NODE_EXPR_MEMBER)
                    {
                        base = base->member.target;
                    }
                    else
                    {
                        break;
                    }
                }
                if (base && base->kind == NODE_EXPR_VAR)
                {
                    Type *inner_t = find_symbol_type_info(ctx, base->var_ref.name);
                    if (inner_t && inner_t->is_const)
                    {
                        zpanic_at(op, "Cannot assign to element of const variable '%s'",
                                  base->var_ref.name);
                    }
                }
            }
        }

        int is_compound = 0;
        size_t op_len = strlen(bin->binary.op);

        // Check if operator ends with '=' but is not ==, !=, <=, >=
        if (op_len > 1 && bin->binary.op[op_len - 1] == '=')
        {
            char c = bin->binary.op[0];
            if (c != '=' && c != '!' && c != '<' && c != '>')
            {
                is_compound = 1;
            }
            // Special handle for <<= and >>=
            if (strcmp(bin->binary.op, "<<=") == 0 || strcmp(bin->binary.op, ">>=") == 0)
            {
                is_compound = 1;
            }
        }

        if (is_compound)
        {
            ASTNode *op_node = ast_create(NODE_EXPR_BINARY);
            op_node->binary.left = lhs;
            op_node->binary.right = rhs;

            // Extract the base operator (remove last char '=')
            size_t inner_op_len = op_len - 1;
            char *inner_op = xmalloc((size_t)(inner_op_len + 1));
            strncpy(inner_op, bin->binary.op, (size_t)(inner_op_len));
            inner_op[inner_op_len] = '\0';
            op_node->binary.op = inner_op;

            // Inherit type info temporarily
            if (lhs->type_info && rhs->type_info && type_eq(lhs->type_info, rhs->type_info))
            {
                op_node->type_info = lhs->type_info;
            }

            const char *inner_method = get_operator_method(inner_op);
            if (inner_method)
            {
                Type *lt = lhs->type_info;
                int is_lhs_ptr = 0;
                char *allocated_name = NULL;
                char *struct_name =
                    resolve_struct_name_from_type(ctx, lt, &is_lhs_ptr, &allocated_name);

                if (struct_name)
                {
                    char buf_m[MAX_ERROR_MSG_LEN];
                    snprintf(buf_m, sizeof(buf_m), "%s__%s", struct_name, inner_method);
                    char *mangled = merge_underscores(buf_m);
                    FuncSig *sig = find_func(ctx, mangled);
                    if (sig)
                    {
                        // Rewrite op_node from BINARY -> CALL
                        ASTNode *call = ast_create(NODE_EXPR_CALL);
                        call->token = t;
                        ASTNode *callee = ast_create(NODE_EXPR_VAR);
                        callee->var_ref.name = xstrdup(mangled);
                        call->call.callee = callee;

                        // Handle 'self' argument
                        ASTNode *arg1 = lhs;
                        if (sig->total_args > 0 && sig->arg_types[0]->kind == TYPE_POINTER &&
                            !is_lhs_ptr)
                        {
                            ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                            addr->unary.op = xstrdup("&");
                            addr->unary.operand = lhs;
                            addr->type_info = type_new_ptr(lt);
                            arg1 = addr;
                        }
                        else if (is_lhs_ptr && sig->arg_types[0]->kind != TYPE_POINTER)
                        {
                            ASTNode *deref = ast_create(NODE_EXPR_UNARY);
                            deref->unary.op = xstrdup("*");
                            deref->unary.operand = lhs;
                            arg1 = deref;
                        }

                        call->call.args = arg1;
                        arg1->next = rhs;
                        rhs->next = NULL;
                        call->type_info = sig->ret_type;

                        // Replace op_node with the call
                        op_node = call;
                    }
                }
                if (allocated_name)
                {
                    zfree(allocated_name);
                }
            }

            zfree(bin->binary.op);
            bin->binary.op = xstrdup("=");
            bin->binary.right = op_node;
        }

        // Index Set Overload: Call(get, idx) = val  -->  Call(set, idx, val)
        if (strcmp(bin->binary.op, "=") == 0 && lhs->kind == NODE_EXPR_CALL)
        {
            if (lhs->call.callee->kind == NODE_EXPR_VAR)
            {
                char *name = lhs->call.callee->var_ref.name;
                // Check if it ends in "_get"
                size_t len = strlen(name);
                if (len > 4 && strcmp(name + len - 4, "_get") == 0)
                {
                    char *set_name = xstrdup(name);
                    set_name[len - 3] = 's'; // Replace 'g' with 's' -> _set
                    set_name[len - 2] = 'e';
                    set_name[len - 1] = 't';

                    if (find_func(ctx, set_name))
                    {
                        // Create NEW Call Node for Set
                        ASTNode *set_call = ast_create(NODE_EXPR_CALL);
                        set_call->token = t;
                        ASTNode *set_callee = ast_create(NODE_EXPR_VAR);
                        set_callee->var_ref.name = set_name;
                        set_call->call.callee = set_callee;

                        // Clone argument list (Shallow copy of arg nodes to preserve chain
                        // for get)
                        ASTNode *lhs_args = lhs->call.args;
                        ASTNode *new_head = NULL;
                        ASTNode *new_tail = NULL;

                        while (lhs_args)
                        {
                            ASTNode *arg_copy = xmalloc(sizeof(ASTNode));
                            memcpy(arg_copy, lhs_args, sizeof(ASTNode));
                            arg_copy->next = NULL;

                            if (!new_head)
                            {
                                new_head = arg_copy;
                            }
                            else
                            {
                                new_tail->next = arg_copy;
                            }
                            new_tail = arg_copy;

                            lhs_args = lhs_args->next;
                        }

                        // Append RHS to new args
                        ASTNode *val_expr = bin->binary.right;
                        if (new_tail)
                        {
                            new_tail->next = val_expr;
                        }
                        else
                        {
                            new_head = val_expr;
                        }

                        set_call->call.args = new_head;
                        set_call->type_info = type_new(TYPE_VOID);

                        lhs = set_call; // Use the new Set call as the result
                        continue;
                    }
                    else
                    {
                        zfree(set_name);
                    }
                }
            }
        }

        const char *method = get_operator_method(bin->binary.op);

        if (method && lhs)
        {
            Type *lt = lhs->type_info;
            int is_lhs_ptr = 0;
            char *allocated_name = NULL;
            char *struct_name =
                resolve_struct_name_from_type(ctx, lt, &is_lhs_ptr, &allocated_name);

            // If we are comparing pointers with == or !=, do NOT rewrite to .eq()
            // We want pointer equality, not value equality (which requires dereferencing)
            // But strict check: Only if BOTH are pointers. If one is value, we might need rewrite.
            if (is_lhs_ptr && struct_name &&
                (strcmp(bin->binary.op, "==") == 0 || strcmp(bin->binary.op, "!=") == 0))
            {
                int is_rhs_ptr = 0;
                char *r_alloc = NULL;

                // This gives a warning as "unused" but it's needed for the rewrite.
                char *r_name =
                    resolve_struct_name_from_type(ctx, rhs->type_info, &is_rhs_ptr, &r_alloc);
                (void)r_name;
                if (r_alloc)
                {
                    zfree(r_alloc);
                }

                if (is_rhs_ptr)
                {
                    // Both are pointers: Skip rewrite to allow pointer comparison
                    if (allocated_name)
                    {
                        zfree(allocated_name);
                    }
                    struct_name = NULL;
                }
            }

            if (struct_name)
            {
                char buf[MAX_ERROR_MSG_LEN];
                snprintf(buf, sizeof(buf), "%s__%s", struct_name, method);
                char *mangled = merge_underscores(buf);

                FuncSig *sig = find_func(ctx, mangled);

                if (!sig)
                {
                    // Try resolving as a trait method: Struct__Trait__Method
                    StructRef *ref = ctx->parsed_impls_list;
                    while (ref)
                    {
                        if (ref->node && ref->node->kind == NODE_IMPL_TRAIT)
                        {
                            const char *t_struct = ref->node->impl_trait.target_type;
                            if (t_struct && strcmp(t_struct, struct_name) == 0)
                            {
                                char buf_t[MAX_ERROR_MSG_LEN];
                                snprintf(buf_t, sizeof(buf_t), "%s__%s__%s", struct_name,
                                         ref->node->impl_trait.trait_name, method);
                                char *trait_mangled = merge_underscores(buf_t);

                                if (find_func(ctx, trait_mangled))
                                {
                                    zfree(mangled);
                                    mangled = trait_mangled;
                                    sig = find_func(ctx, mangled);
                                    break;
                                }
                                zfree(trait_mangled);
                            }
                        }
                        ref = ref->next;
                    }
                }

                if (sig)
                {
                    ASTNode *call = ast_create(NODE_EXPR_CALL);
                    call->token = t;
                    ASTNode *callee = ast_create(NODE_EXPR_VAR);
                    callee->var_ref.name = xstrdup(mangled);
                    call->call.callee = callee;

                    ASTNode *arg1 = lhs;

                    // Check if function expects a pointer for 'self'
                    if (sig->total_args > 0 && sig->arg_types[0] &&
                        sig->arg_types[0]->kind == TYPE_POINTER)
                    {
                        if (!is_lhs_ptr)
                        {
                            // Value -> Pointer.
                            int is_rvalue =
                                (lhs->kind == NODE_EXPR_CALL || lhs->kind == NODE_EXPR_BINARY ||
                                 lhs->kind == NODE_EXPR_STRUCT_INIT ||
                                 lhs->kind == NODE_EXPR_CAST || lhs->kind == NODE_MATCH);

                            ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                            addr->unary.op = is_rvalue ? xstrdup("&_rval") : xstrdup("&");
                            addr->unary.operand = lhs;
                            addr->type_info = type_new_ptr(lt);
                            arg1 = addr;
                        }
                    }
                    else
                    {
                        // Function expects value
                        if (is_lhs_ptr)
                        {
                            // Have pointer, need value -> *lhs
                            ASTNode *deref = ast_create(NODE_EXPR_UNARY);
                            deref->unary.op = xstrdup("*");
                            deref->unary.operand = lhs;
                            if (lt && lt->kind == TYPE_POINTER)
                            {
                                deref->type_info = lt->inner;
                            }
                            arg1 = deref;
                        }
                    }

                    // Handle RHS (Argument 2) Auto-Ref if needed
                    ASTNode *arg2 = rhs;
                    if (sig->total_args > 1 && sig->arg_types[1] &&
                        sig->arg_types[1]->kind == TYPE_POINTER)
                    {
                        Type *rt = rhs->type_info;

                        // If rhs is a variable reference without type_info, look it up
                        if (!rt && rhs->kind == NODE_EXPR_VAR)
                        {
                            ZenSymbol *sym = find_symbol_entry(ctx, rhs->var_ref.name);
                            if (sym && sym->type_info)
                            {
                                rt = sym->type_info;
                                rhs->type_info = rt;
                                if (sym->type_name)
                                {
                                    rhs->resolved_type = xstrdup(sym->type_name);
                                }
                            }
                        }

                        int is_rhs_ptr = (rt && rt->kind == TYPE_POINTER);
                        if (!is_rhs_ptr) // Need pointer, have value
                        {
                            int is_rvalue =
                                (rhs->kind == NODE_EXPR_CALL || rhs->kind == NODE_EXPR_BINARY ||
                                 rhs->kind == NODE_EXPR_STRUCT_INIT ||
                                 rhs->kind == NODE_EXPR_CAST || rhs->kind == NODE_MATCH);

                            ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                            addr->unary.op = is_rvalue ? xstrdup("&_rval") : xstrdup("&");
                            addr->unary.operand = rhs;
                            if (rt)
                            {
                                addr->type_info = type_new_ptr(rt);
                            }
                            arg2 = addr;
                        }
                    }

                    call->call.args = arg1;
                    arg1->next = arg2;
                    arg2->next = NULL;

                    call->type_info = sig->ret_type;
                    call->resolved_type = type_to_string(sig->ret_type);

                    lhs = call;
                    if (allocated_name)
                    {
                        zfree(allocated_name);
                    }
                    continue; // Loop again with result as new lhs
                }
                if (allocated_name)
                {
                    zfree(allocated_name);
                }
            }
        }

        // Standard Type Checking (if no overload found)
        if (lhs->type_info && rhs->type_info)
        {
            // Ensure type_info is set for variables (critical for inference)
            if (lhs->kind == NODE_EXPR_VAR && !lhs->type_info)
            {
                ZenSymbol *s = find_symbol_entry(ctx, lhs->var_ref.name);
                if (s)
                {
                    lhs->type_info = s->type_info;
                }
            }
            if (rhs->kind == NODE_EXPR_VAR && !rhs->type_info)
            {
                ZenSymbol *s = find_symbol_entry(ctx, rhs->var_ref.name);
                if (s)
                {
                    rhs->type_info = s->type_info;
                }
            }

            // Backward Inference for Lambda Params
            // LHS is Unknown Var, RHS is Known
            if (lhs->kind == NODE_EXPR_VAR && lhs->type_info &&
                lhs->type_info->kind == TYPE_UNKNOWN && rhs->type_info &&
                rhs->type_info->kind != TYPE_UNKNOWN)
            {
                // Infer LHS type from RHS
                ZenSymbol *sym = find_symbol_entry(ctx, lhs->var_ref.name);
                if (sym)
                {
                    // Update ZenSymbol
                    sym->type_info = rhs->type_info;
                    sym->type_name = type_to_string(rhs->type_info);

                    // Update AST Node
                    lhs->type_info = rhs->type_info;
                    lhs->resolved_type = xstrdup(sym->type_name);
                }
            }

            // RHS is Unknown Var, LHS is Known
            if (rhs->kind == NODE_EXPR_VAR && rhs->type_info &&
                rhs->type_info->kind == TYPE_UNKNOWN && lhs->type_info &&
                lhs->type_info->kind != TYPE_UNKNOWN)
            {
                // Infer RHS type from LHS
                ZenSymbol *sym = find_symbol_entry(ctx, rhs->var_ref.name);
                if (sym)
                {
                    // Update ZenSymbol
                    sym->type_info = lhs->type_info;
                    sym->type_name = type_to_string(lhs->type_info);

                    // Update AST Node
                    rhs->type_info = lhs->type_info;
                    rhs->resolved_type = xstrdup(sym->type_name);
                }
            }

            if (is_comparison_op(bin->binary.op))
            {
                bin->type_info = type_new(TYPE_INT); // bool
                char *t1 = type_to_string(lhs->type_info);
                char *t2 = type_to_string(rhs->type_info);
                // Skip type check if either operand is void* (escape hatch type)
                // or if either operand is a generic type parameter (T, K, V, etc.)
                int skip_check = (strcmp(t1, "void*") == 0 || strcmp(t2, "void*") == 0);
                if (lhs->type_info->kind == TYPE_GENERIC || rhs->type_info->kind == TYPE_GENERIC)
                {
                    skip_check = 1;
                }
                // Also check if type name is a single uppercase letter (common generic param)
                if ((strlen(t1) == 1 && isupper(t1[0])) || (strlen(t2) == 1 && isupper(t2[0])))
                {
                    skip_check = 1;
                }

                // Allow comparing pointers/strings with integer literal 0 (NULL)
                if (!skip_check)
                {
                    int lhs_is_ptr =
                        (lhs->type_info->kind == TYPE_POINTER ||
                         lhs->type_info->kind == TYPE_STRING || (t1 && strstr(t1, "*")));
                    int rhs_is_ptr =
                        (rhs->type_info->kind == TYPE_POINTER ||
                         rhs->type_info->kind == TYPE_STRING || (t2 && strstr(t2, "*")));

                    if (lhs_is_ptr && rhs->kind == NODE_EXPR_LITERAL && rhs->literal.int_val == 0)
                    {
                        skip_check = 1;
                    }
                    if (rhs_is_ptr && lhs->kind == NODE_EXPR_LITERAL && lhs->literal.int_val == 0)
                    {
                        skip_check = 1;
                    }
                }

                int lhs_is_num = is_integer_type(lhs->type_info) || is_float_type(lhs->type_info);
                int rhs_is_num = is_integer_type(rhs->type_info) || is_float_type(rhs->type_info);

                if (!skip_check && !type_eq(lhs->type_info, rhs->type_info) &&
                    !(lhs_is_num && rhs_is_num))
                {
                    char msg[MAX_SHORT_MSG_LEN];
                    snprintf(msg, sizeof(msg),
                             "Type mismatch in comparison: cannot compare '%s' and '%s'",
                             t1, /* safe */
                             t2);

                    char suggestion[MAX_SHORT_MSG_LEN];
                    snprintf(suggestion, sizeof(suggestion),
                             "Both operands must have compatible types for comparison"); /* safe */

                    if (ctx->config->mode_lsp)
                    {
                        zwarn_at(op, "LSP: %s", msg);
                        // Assume result is int (bool) to continue
                        bin->type_info = type_new(TYPE_INT);
                    }
                    else
                    {
                        zpanic_with_suggestion(op, msg, suggestion);
                    }
                }
            }
            else
            {
                if (type_eq(lhs->type_info, rhs->type_info) ||
                    check_opaque_alias_compat(ctx, lhs->type_info, rhs->type_info))
                {
                    bin->type_info = lhs->type_info;
                }
                else
                {
                    // Check aliases
                    char *al = NULL, *ar = NULL;
                    int pl = 0, pr = 0;
                    char *sl = resolve_struct_name_from_type(ctx, lhs->type_info, &pl, &al);
                    char *sr = resolve_struct_name_from_type(ctx, rhs->type_info, &pr, &ar);

                    int alias_match = 0;
                    if (sl && sr && strcmp(sl, sr) == 0 && pl == pr)
                    {
                        alias_match = 1;
                        bin->type_info = lhs->type_info;
                    }
                    if (al)
                    {
                        zfree(al);
                    }
                    if (ar)
                    {
                        zfree(ar);
                    }

                    char *t1 = type_to_string(lhs->type_info);
                    char *t2 = type_to_string(rhs->type_info);

                    // Allow pointer arithmetic: ptr + int, ptr - int, int + ptr
                    int is_ptr_arith = 0;
                    if (!alias_match)
                    {
                        if (strcmp(bin->binary.op, "+") == 0 || strcmp(bin->binary.op, "-") == 0)
                        {
                            int lhs_is_ptr = (lhs->type_info->kind == TYPE_POINTER ||
                                              lhs->type_info->kind == TYPE_STRING ||
                                              (t1 && strstr(t1, "*") != NULL));
                            int rhs_is_ptr = (rhs->type_info->kind == TYPE_POINTER ||
                                              rhs->type_info->kind == TYPE_STRING ||
                                              (t2 && strstr(t2, "*") != NULL));
                            int lhs_is_int =
                                (is_integer_type(lhs->type_info) || (t1 && str_is_int_type(t1)) ||
                                 (t1 && str_is_usize_type(t1)) || (t1 && str_is_isize_type(t1)));
                            int rhs_is_int =
                                (is_integer_type(rhs->type_info) || (t2 && str_is_int_type(t2)) ||
                                 (t2 && str_is_usize_type(t2)) || (t2 && str_is_isize_type(t2)));

                            if ((lhs_is_ptr && rhs_is_int) || (lhs_is_int && rhs_is_ptr))
                            {
                                is_ptr_arith = 1;
                                bin->type_info = lhs_is_ptr ? lhs->type_info : rhs->type_info;
                            }
                        }
                    }

                    if (!is_ptr_arith && !alias_match)
                    {
                        // ** Backward Inference for Binary Ops **
                        // Case 1: LHS is Unknown Var, RHS is Known
                        if (lhs->kind == NODE_EXPR_VAR && lhs->type_info &&
                            lhs->type_info->kind == TYPE_UNKNOWN && rhs->type_info &&
                            rhs->type_info->kind != TYPE_UNKNOWN)
                        {
                            // Infer LHS type from RHS
                            ZenSymbol *sym = find_symbol_entry(ctx, lhs->var_ref.name);
                            if (sym)
                            {
                                // Update ZenSymbol
                                sym->type_info = rhs->type_info;
                                sym->type_name = type_to_string(rhs->type_info);

                                // Update AST Node
                                lhs->type_info = rhs->type_info;
                                lhs->resolved_type = xstrdup(sym->type_name);

                                bin->type_info = rhs->type_info;
                                goto bin_inference_success;
                            }
                        }

                        // Case 2: RHS is Unknown Var, LHS is Known
                        if (rhs->kind == NODE_EXPR_VAR && rhs->type_info &&
                            rhs->type_info->kind == TYPE_UNKNOWN && lhs->type_info &&
                            lhs->type_info->kind != TYPE_UNKNOWN)
                        {
                            // Infer RHS type from LHS
                            ZenSymbol *sym = find_symbol_entry(ctx, rhs->var_ref.name);
                            if (sym)
                            {
                                // Update ZenSymbol
                                sym->type_info = lhs->type_info;
                                sym->type_name = type_to_string(lhs->type_info);

                                // Update AST Node
                                rhs->type_info = lhs->type_info;
                                rhs->resolved_type = xstrdup(sym->type_name);

                                bin->type_info = lhs->type_info;
                                goto bin_inference_success;
                            }
                        }

                        // Allow assigning 0 to pointer (NULL)
                        int is_null_assign = 0;
                        if (strcmp(bin->binary.op, "=") == 0)
                        {
                            int lhs_is_ptr = (lhs->type_info->kind == TYPE_POINTER ||
                                              lhs->type_info->kind == TYPE_STRING ||
                                              (t1 && strstr(t1, "*") != NULL));
                            if (lhs_is_ptr && rhs->kind == NODE_EXPR_LITERAL &&
                                rhs->literal.int_val == 0)
                            {
                                is_null_assign = 1;
                            }
                        }

                        if (!is_null_assign)
                        {
                            // Check for arithmetic promotion (Int * Float, etc)
                            int lhs_is_num = is_integer_type(lhs->type_info) ||
                                             lhs->type_info->kind == TYPE_F32 ||
                                             lhs->type_info->kind == TYPE_F64 ||
                                             lhs->type_info->kind == TYPE_FLOAT;
                            int rhs_is_num = is_integer_type(rhs->type_info) ||
                                             rhs->type_info->kind == TYPE_F32 ||
                                             rhs->type_info->kind == TYPE_F64 ||
                                             rhs->type_info->kind == TYPE_FLOAT;

                            int valid_arith = 0;
                            if (lhs_is_num && rhs_is_num)
                            {
                                if (strcmp(bin->binary.op, "+") == 0 ||
                                    strcmp(bin->binary.op, "-") == 0 ||
                                    strcmp(bin->binary.op, "*") == 0 ||
                                    strcmp(bin->binary.op, "/") == 0)
                                {
                                    valid_arith = 1;
                                    // Result is the float type if one is float
                                    if (lhs->type_info->kind == TYPE_F64 ||
                                        rhs->type_info->kind == TYPE_F64)
                                    {
                                        bin->type_info = lhs->type_info->kind == TYPE_F64
                                                             ? lhs->type_info
                                                             : rhs->type_info;
                                    }
                                    else if (lhs->type_info->kind == TYPE_F32 ||
                                             rhs->type_info->kind == TYPE_F32 ||
                                             lhs->type_info->kind == TYPE_FLOAT ||
                                             rhs->type_info->kind == TYPE_FLOAT)
                                    {
                                        // Pick the float type. If both float, pick lhs.
                                        if (lhs->type_info->kind == TYPE_F32 ||
                                            lhs->type_info->kind == TYPE_FLOAT)
                                        {
                                            bin->type_info = lhs->type_info;
                                        }
                                        else
                                        {
                                            bin->type_info = rhs->type_info;
                                        }
                                    }
                                    else
                                    {
                                        // Both int (but failed equality check previously? - rare
                                        // but possible if diff int types) If diff int types, we
                                        // usually allow it in C (promotion). For now, assume LHS
                                        // dominates or standard promotion.
                                        bin->type_info = lhs->type_info;
                                    }
                                }
                            }

                            if (!valid_arith)
                            {
                                char msg[MAX_SHORT_MSG_LEN];
                                snprintf(msg, sizeof(msg),
                                         "Type mismatch in binary operation '%s'", /* safe */
                                         bin->binary.op);

                                char suggestion[MAX_MANGLED_NAME_LEN];
                                sprintf(/* safe */
                                        suggestion,
                                        "Left operand has type '%s', right operand has type '%s'\n "
                                        "  = "
                                        "note: Consider casting one operand to match the other",
                                        t1, t2);

                                zpanic_with_suggestion(op, msg, suggestion);
                            }
                        }

                    bin_inference_success:;
                    }
                }
            }
        }

        lhs = bin;
    }
    return lhs;
}
