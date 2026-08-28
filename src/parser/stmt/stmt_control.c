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

static void check_assignment_condition(ASTNode *cond)
{
    if (!cond)
    {
        return;
    }
    if (cond->kind == NODE_EXPR_BINARY)
    {
        if (cond->binary.op && strcmp(cond->binary.op, "=") == 0)
        {
            zwarn_at(cond->token, "Assignment in condition");
            fprintf(stderr, COLOR_CYAN "   = note: " COLOR_RESET "Did you mean '=='?\n");
        }
    }
}

static void auto_import_std_slice(ParserContext *ctx)
{
    GenericTemplate *t = ctx->templates;
    while (t)
    {
        if (strcmp(t->name, "Slice") == 0)
        {
            return; // slice module already loaded
        }
        t = t->next;
    }
    load_std_module(ctx, "std/slice.zc");
}

ASTNode *parse_loop(ParserContext *ctx, Lexer *l)
{
    Token tk = lexer_next(l);
    ctx->cg.loop_depth++;
    ASTNode *b = parse_block(ctx, l);
    ctx->cg.loop_depth--;
    ASTNode *n = ast_create(NODE_LOOP);
    n->token = tk;
    n->loop_stmt.body = b;
    return n;
}

ASTNode *parse_repeat(ParserContext *ctx, Lexer *l)
{
    Token t = lexer_next(l);
    zwarn_at(t, "repeat is deprecated. Use 'for _ in 0..N' instead.");
    char *c = rewrite_expr_methods(ctx, parse_condition_raw(ctx, l));
    ASTNode *b = parse_block(ctx, l);
    ASTNode *n = ast_create(NODE_REPEAT);
    n->token = t;
    n->line = t.line;
    n->repeat_stmt.count = c;
    n->repeat_stmt.body = b;
    return n;
}

ASTNode *parse_unless(ParserContext *ctx, Lexer *l)
{
    Token t = lexer_next(l);
    ASTNode *cond = parse_expression(ctx, l);
    ASTNode *body = parse_block(ctx, l);
    ASTNode *n = ast_create(NODE_UNLESS);
    n->token = t;
    n->line = t.line;
    n->unless_stmt.condition = cond;
    n->unless_stmt.body = body;
    return n;
}

ASTNode *parse_guard(ParserContext *ctx, Lexer *l)
{
    Token tk = lexer_next(l);

    ASTNode *cond = parse_expression(ctx, l);

    Token t = lexer_peek(l);
    if (t.kind != TOK_IDENT || strncmp(t.start, "else", 4) != 0)
    {
        zpanic_at(t, "Expected 'else' after guard condition");
        return NULL;
    }
    lexer_next(l);

    ASTNode *body;
    if (lexer_peek(l).kind == TOK_LBRACE)
    {
        body = parse_block(ctx, l);
    }
    else
    {
        body = parse_statement(ctx, l);
    }

    ASTNode *n = ast_create(NODE_GUARD);
    n->token = tk;
    n->guard_stmt.condition = cond;
    n->guard_stmt.body = body;
    return n;
}

ASTNode *parse_if(ParserContext *ctx, Lexer *l)
{
    Token if_token = lexer_next(l);
    ASTNode *cond = parse_expression(ctx, l);
    check_assignment_condition(cond);

    ASTNode *then_b = NULL;
    if (lexer_peek(l).kind == TOK_LBRACE)
    {
        then_b = parse_block(ctx, l);
    }
    else
    {
        if (ctx->config->misra_mode)
        {
            zerror_at(lexer_peek(l), "MISRA Rule 15.6");
        }
        enter_scope(ctx);
        ASTNode *s = parse_statement(ctx, l);
        exit_scope(ctx);
        then_b = ast_create(NODE_BLOCK);
        then_b->block.statements = s;
    }

    ASTNode *else_b = NULL;
    skip_comments(l);
    if (lexer_peek(l).kind == TOK_IDENT && strncmp(lexer_peek(l).start, "else", 4) == 0)
    {
        lexer_next(l);
        if (lexer_peek(l).kind == TOK_IDENT && strncmp(lexer_peek(l).start, "if", 2) == 0)
        {
            else_b = parse_if(ctx, l);
        }
        else if (lexer_peek(l).kind == TOK_LBRACE)
        {
            else_b = parse_block(ctx, l);
        }
        else
        {
            if (ctx->config->misra_mode)
            {
                zerror_at(lexer_peek(l), "MISRA Rule 15.6");
            }
            enter_scope(ctx);
            ASTNode *s = parse_statement(ctx, l);
            exit_scope(ctx);
            else_b = ast_create(NODE_BLOCK);
            else_b->block.statements = s;
        }
    }
    ASTNode *n = ast_create(NODE_IF);
    n->token = if_token;
    n->if_stmt.condition = cond;
    n->if_stmt.then_body = then_b;
    n->if_stmt.else_body = else_b;
    return n;
}

ASTNode *parse_while(ParserContext *ctx, Lexer *l)
{
    Token tk = lexer_next(l);
    ctx->cg.loop_depth++;
    ASTNode *cond = parse_expression(ctx, l);
    check_assignment_condition(cond);
    if (!cond)
    {
        zpanic_at(lexer_peek(l), "Expected condition expression");
        return ast_create(NODE_BLOCK);
    }

    if ((cond->kind == NODE_EXPR_LITERAL && cond->literal.kind == LITERAL_INT &&
         cond->literal.int_val == 1) ||
        (cond->kind == NODE_EXPR_VAR && strcmp(cond->var_ref.name, "true") == 0))
    {
        if (ctx->hook_zen_trigger)
        {
            ctx->hook_zen_trigger(TRIGGER_WHILE_TRUE, cond->token, ctx->config);
        }
    }
    ASTNode *body;
    if (lexer_peek(l).kind == TOK_LBRACE)
    {
        body = parse_block(ctx, l);
    }
    else
    {
        if (ctx->config->misra_mode)
        {
            zerror_at(lexer_peek(l), "MISRA Rule 15.6: compound-statement body");
        }
        body = parse_statement(ctx, l);
    }
    ctx->cg.loop_depth--;
    ASTNode *n = ast_create(NODE_WHILE);
    n->token = tk;
    n->while_stmt.condition = cond;
    n->while_stmt.body = body;
    return n;
}

ASTNode *parse_for(ParserContext *ctx, Lexer *l)
{
    Token for_token = lexer_next(l);
    ctx->cg.loop_depth++;

    if (lexer_peek(l).kind == TOK_IDENT)
    {
        int saved_pos = l->pos;
        Token var = lexer_next(l);

        char *enum_idx_name = NULL;
        Token val_tok = {0};
        if (lexer_peek(l).kind == TOK_COMMA)
        {
            lexer_next(l);
            val_tok = lexer_next(l);
            enum_idx_name = xmalloc(var.len + 1);
            strncpy(enum_idx_name, var.start, var.len);
            enum_idx_name[var.len] = 0;
            var = val_tok;
        }

        Token in_tok = lexer_next(l);

        if (in_tok.kind == TOK_IDENT && strncmp(in_tok.start, "in", 2) == 0)
        {
            ASTNode *start_expr = parse_expression(ctx, l);
            Token tk = lexer_peek(l);
            ZenTokenType next_tok = tk.kind;
            if (next_tok == TOK_DOTDOT || next_tok == TOK_DOTDOT_LT || next_tok == TOK_DOTDOT_EQ)
            {
                int is_inclusive = 0;
                if (next_tok == TOK_DOTDOT || next_tok == TOK_DOTDOT_LT)
                {
                    lexer_next(l);
                }
                else if (next_tok == TOK_DOTDOT_EQ)
                {
                    is_inclusive = 1;
                    lexer_next(l);
                }

                if (1)
                {
                    ASTNode *end_expr = parse_expression(ctx, l);

                    ASTNode *n = ast_create(NODE_FOR_RANGE);
                    n->token = for_token;
                    n->for_range.var_name = xmalloc(var.len + 1);
                    strncpy(n->for_range.var_name, var.start, var.len);
                    n->for_range.var_name[var.len] = 0;
                    n->for_range.start = start_expr;
                    n->for_range.end = end_expr;
                    n->for_range.is_inclusive = is_inclusive;

                    if (lexer_peek(l).kind == TOK_IDENT &&
                        strncmp(lexer_peek(l).start, "step", 4) == 0)
                    {
                        lexer_next(l);
                        Token s_tok = lexer_next(l);

                        if (s_tok.kind == TOK_OP && s_tok.len == 1 && s_tok.start[0] == '-')
                        {
                            Token num_tok = lexer_next(l);
                            char *sval = xmalloc(s_tok.len + num_tok.len + 1);
                            strncpy(sval, s_tok.start, s_tok.len);
                            strncpy(sval + s_tok.len, num_tok.start, num_tok.len);
                            sval[s_tok.len + num_tok.len] = 0;
                            n->for_range.step = sval;
                        }
                        else
                        {
                            char *sval = xmalloc(s_tok.len + 1);
                            strncpy(sval, s_tok.start, s_tok.len);
                            sval[s_tok.len] = 0;
                            n->for_range.step = sval;
                        }
                    }
                    else
                    {
                        n->for_range.step = NULL;
                    }

                    enter_scope(ctx);
                    add_symbol(ctx, n->for_range.var_name, "int", type_new(TYPE_INT), 0);
                    if (enum_idx_name)
                    {
                        add_symbol(ctx, enum_idx_name, "int", type_new(TYPE_INT), 0);
                    }

                    ASTNode *user_body = NULL;
                    if (lexer_peek(l).kind == TOK_LBRACE)
                    {
                        user_body = parse_block(ctx, l);
                    }
                    else
                    {
                        if (ctx->config->misra_mode)
                        {
                            zerror_at(lexer_peek(l), "MISRA Rule 15.6: compound-statement body");
                        }
                        user_body = parse_statement(ctx, l);
                    }
                    exit_scope(ctx);

                    if (enum_idx_name)
                    {
                        ASTNode *idx_decl = ast_create(NODE_VAR_DECL);
                        idx_decl->token = tk;
                        idx_decl->var_decl.name = xstrdup("__zc_enum_idx");
                        idx_decl->var_decl.type_str = xstrdup("int");
                        idx_decl->var_decl.type_info = type_new(TYPE_INT);
                        ASTNode *zero_lit = ast_create(NODE_EXPR_LITERAL);
                        zero_lit->literal.kind = LITERAL_INT;
                        zero_lit->literal.int_val = 0;
                        zero_lit->literal.string_val = xstrdup("0");
                        idx_decl->var_decl.init_expr = zero_lit;

                        ASTNode *idx_bind = ast_create(NODE_VAR_DECL);
                        idx_bind->token = tk;
                        idx_bind->var_decl.name = enum_idx_name;
                        idx_bind->var_decl.type_str = xstrdup("int");
                        idx_bind->var_decl.type_info = type_new(TYPE_INT);
                        ASTNode *idx_ref = ast_create(NODE_EXPR_VAR);
                        idx_ref->var_ref.name = xstrdup("__zc_enum_idx");
                        idx_bind->var_decl.init_expr = idx_ref;

                        ASTNode *idx_inc = ast_create(NODE_EXPR_UNARY);
                        idx_inc->unary.op = xstrdup("++");
                        ASTNode *idx_ref2 = ast_create(NODE_EXPR_VAR);
                        idx_ref2->var_ref.name = xstrdup("__zc_enum_idx");
                        idx_inc->unary.operand = idx_ref2;

                        ASTNode *new_body = ast_create(NODE_BLOCK);
                        idx_bind->next = user_body;
                        if (user_body && user_body->kind == NODE_BLOCK)
                        {
                            ASTNode *last = user_body->block.statements;
                            if (last)
                            {
                                while (last->next)
                                {
                                    last = last->next;
                                }
                                last->next = idx_inc;
                            }
                            idx_bind->next = user_body->block.statements;
                            user_body->block.statements = idx_bind;
                            new_body = user_body;
                        }
                        else
                        {
                            if (user_body)
                            {
                                user_body->next = idx_inc;
                            }
                            idx_bind->next = user_body;
                            new_body->block.statements = idx_bind;
                        }

                        n->for_range.body = new_body;

                        ASTNode *outer = ast_create(NODE_BLOCK);
                        idx_decl->next = n;
                        outer->block.statements = idx_decl;
                        return outer;
                    }
                    else
                    {
                        n->for_range.body = user_body;
                        ctx->cg.loop_depth--;
                        return n;
                    }
                }
            }
            else
            {
                char *var_name = xmalloc(var.len + 1);
                strncpy(var_name, var.start, var.len);
                var_name[var.len] = 0;

                ASTNode *obj_expr = start_expr;
                char *iter_method = "iterator";
                ASTNode *slice_decl = NULL;

                if (obj_expr->kind == NODE_EXPR_UNARY && obj_expr->unary.op &&
                    strcmp(obj_expr->unary.op, "&") == 0)
                {
                    obj_expr = obj_expr->unary.operand;
                    iter_method = "iter_ref";
                }

                if (obj_expr->type_info && obj_expr->type_info->kind == TYPE_ARRAY &&
                    obj_expr->type_info->array_size > 0)
                {
                    slice_decl = ast_create(NODE_VAR_DECL);
                    slice_decl->token = tk;
                    slice_decl->var_decl.name = xstrdup("__zc_arr_slice");

                    char *elem_type_str = type_to_string(obj_expr->type_info->inner);
                    char slice_type[MAX_TYPE_NAME_LEN];
                    snprintf(slice_type, sizeof(slice_type), "Slice<%s>", elem_type_str);
                    slice_decl->var_decl.type_str = xstrdup(slice_type);

                    ASTNode *from_array_call = ast_create(NODE_EXPR_CALL);
                    from_array_call->token = tk;
                    ASTNode *static_method = ast_create(NODE_EXPR_VAR);

                    char func_name[MAX_FUNC_NAME_LEN];
                    snprintf(func_name, sizeof(func_name), "Slice__%s__from_array", elem_type_str);
                    static_method->var_ref.name = xstrdup(func_name);

                    from_array_call->call.callee = static_method;

                    ASTNode *arr_addr = ast_create(NODE_EXPR_UNARY);
                    arr_addr->unary.op = xstrdup("&");
                    arr_addr->unary.operand = obj_expr;

                    ASTNode *arr_cast = ast_create(NODE_EXPR_CAST);
                    char cast_type[MAX_TYPE_NAME_LEN];
                    snprintf(cast_type, sizeof(cast_type), "%s*", elem_type_str);
                    arr_cast->cast.target_type = xstrdup(cast_type);
                    arr_cast->cast.expr = arr_addr;

                    ASTNode *size_arg = ast_create(NODE_EXPR_LITERAL);
                    size_arg->literal.kind = LITERAL_INT;
                    size_arg->literal.int_val =
                        (unsigned long long)(obj_expr->type_info->array_size);
                    char size_buf[32];
                    snprintf(size_buf, sizeof(size_buf), "%d", obj_expr->type_info->array_size);
                    size_arg->literal.string_val = xstrdup(size_buf);

                    arr_cast->next = size_arg;
                    from_array_call->call.args = arr_cast;
                    from_array_call->call.count = 2;

                    slice_decl->var_decl.init_expr = from_array_call;

                    auto_import_std_slice(ctx);
                    Token dummy_tok = {0};
                    instantiate_generic(ctx, "Slice", elem_type_str, elem_type_str, dummy_tok);

                    char iter_type[MAX_TYPE_NAME_LEN];
                    snprintf(iter_type, sizeof(iter_type), "SliceIter<%s>",
                             elem_type_str); /* TODO: check buffer size */
                    instantiate_generic(ctx, "SliceIter", elem_type_str, elem_type_str, dummy_tok);

                    char option_type[MAX_TYPE_NAME_LEN];
                    snprintf(option_type, sizeof(option_type), "Option<%s>",
                             elem_type_str); /* TODO: check buffer size */
                    instantiate_generic(ctx, "Option", elem_type_str, elem_type_str, dummy_tok);

                    ASTNode *slice_ref = ast_create(NODE_EXPR_VAR);
                    slice_ref->var_ref.name = xstrdup("__zc_arr_slice");
                    slice_ref->resolved_type = xstrdup(slice_type);
                    obj_expr = slice_ref;

                    zfree(elem_type_str);
                }

                ASTNode *it_decl = ast_create(NODE_VAR_DECL);
                it_decl->token = tk;
                it_decl->var_decl.name = xstrdup("__it");
                it_decl->var_decl.type_str = NULL;

                ASTNode *call_iter = ast_create(NODE_EXPR_CALL);
                ASTNode *memb_iter = ast_create(NODE_EXPR_MEMBER);
                memb_iter->member.target = obj_expr;
                memb_iter->member.field = xstrdup(iter_method);
                memb_iter->token = tk;
                call_iter->token = tk;
                call_iter->call.callee = memb_iter;
                call_iter->call.args = NULL;
                call_iter->call.count = 0;

                it_decl->var_decl.init_expr = call_iter;

                ASTNode *while_loop = ast_create(NODE_FOR);
                ASTNode *true_lit = ast_create(NODE_EXPR_LITERAL);
                true_lit->literal.kind = LITERAL_INT;
                true_lit->literal.int_val = 1;
                true_lit->literal.string_val = xstrdup("1");
                true_lit->token = tk;
                while_loop->token = tk;
                while_loop->for_stmt.condition = true_lit;

                ASTNode *loop_body = ast_create(NODE_BLOCK);
                loop_body->token = tk;
                ASTNode *stmts_head = NULL;
                ASTNode *stmts_tail = NULL;

#define APPEND_STMT(node)                                                                          \
    if (!stmts_head)                                                                               \
    {                                                                                              \
        stmts_head = node;                                                                         \
        stmts_tail = node;                                                                         \
    }                                                                                              \
    else                                                                                           \
    {                                                                                              \
        stmts_tail->next = node;                                                                   \
        stmts_tail = node;                                                                         \
    }

                char *iter_type_ptr = NULL;
                char *option_type_ptr = NULL;
                char *u_type = NULL;
                char *inner = NULL;

                char *coll_type = infer_type(ctx, start_expr);
                if (coll_type)
                {
                    char *t_start = (char *)strchr(coll_type, '<');
                    if (t_start)
                    {
                        char *t_end = (char *)strrchr(coll_type, '>');
                        if (t_end)
                        {
                            ptrdiff_t len = t_end - t_start - 1;
                            inner = xmalloc((size_t)(len + 1));
                            strncpy(inner, t_start + 1, (size_t)(len));
                            inner[len] = 0;
                        }
                    }
                    else
                    {
                        char *m_start = (char *)strstr(coll_type, "__");
                        if (m_start)
                        {
                            m_start += 2;
                            char *m_end = (char *)strchr(m_start, '*');
                            if (!m_end)
                            {
                                m_end = m_start + strlen(m_start);
                            }
                            ptrdiff_t len = m_end - m_start;
                            inner = xmalloc((size_t)len + 1);
                            strncpy(inner, m_start, (size_t)len);
                            inner[len] = 0;
                        }
                    }

                    if (inner)
                    {
                        u_type = xmalloc(strlen(inner) + 16);
                        if (strchr(coll_type, '&') || (coll_type[0] == '&') ||
                            (strchr(coll_type, '*')) || strstr(coll_type, "Ref"))
                        {
                            sprintf(u_type, "%s*", inner); /* safe */
                        }
                        else
                        {
                            strcpy(u_type, inner);
                        }

                        if (strstr(coll_type, "Map"))
                        {
                            char *old_u = u_type;
                            u_type = xmalloc(strlen(inner) + 32);
                            sprintf(u_type, "MapEntry<%s>", inner); /* safe */
                            if (old_u)
                            {
                                zfree(old_u);
                            }

                            option_type_ptr = xmalloc(strlen(inner) + 128);
                            sprintf(option_type_ptr, "MapIterResult<%s>", inner); /* safe */
                        }
                        else if (strstr(coll_type, "Vec") || strstr(coll_type, "Slice"))
                        {
                            option_type_ptr = xmalloc(strlen(inner) + 128);
                            if (strchr(coll_type, '&') || (coll_type[0] == '&') ||
                                (strchr(coll_type, '*')) || strstr(coll_type, "Ref"))
                            {
                                sprintf(option_type_ptr, "VecIterResult<%s>", inner); /* safe */
                            }
                            else
                            {
                                sprintf(option_type_ptr, "Option<%s>", u_type); /* safe */
                            }
                        }
                        else
                        {
                            option_type_ptr = xmalloc(strlen(u_type) + 64);
                            sprintf(option_type_ptr, "Option<%s>", u_type); /* safe */
                        }

                        zfree(inner);
                    }
                }

                if (slice_decl && !u_type)
                {
                    char *slice_t = slice_decl->var_decl.type_str;
                    char *start = (char *)strchr(slice_t, '<');
                    if (start)
                    {
                        char *end = (char *)strrchr(slice_t, '>');
                        if (end)
                        {
                            ptrdiff_t len = end - start - 1;
                            char *elem = xmalloc((size_t)len + 1);
                            strncpy(elem, start + 1, (size_t)len);
                            elem[len] = 0;

                            u_type = xmalloc((size_t)(len + 8));
                            strcpy(u_type, elem);

                            iter_type_ptr = xmalloc(256);
                            snprintf(iter_type_ptr, 256, "SliceIter<%s>", elem);

                            option_type_ptr = xmalloc(256);
                            snprintf(option_type_ptr, 256, "Option<%s>", elem);

                            zfree(elem);
                        }
                    }
                }

                ASTNode *opt_decl = ast_create(NODE_VAR_DECL);
                opt_decl->token = tk;
                opt_decl->var_decl.name = xstrdup("__opt");
                opt_decl->var_decl.type_str = NULL;

                ASTNode *call_next = ast_create(NODE_EXPR_CALL);
                ASTNode *memb_next = ast_create(NODE_EXPR_MEMBER);
                ASTNode *it_ref = ast_create(NODE_EXPR_VAR);
                it_ref->var_ref.name = xstrdup("__it");
                if (iter_type_ptr)
                {
                    it_ref->resolved_type = xstrdup(iter_type_ptr);
                }
                memb_next->member.target = it_ref;
                memb_next->member.field = xstrdup("next");
                memb_next->token = tk;
                call_next->token = tk;
                call_next->call.callee = memb_next;

                opt_decl->var_decl.init_expr = call_next;
                APPEND_STMT(opt_decl);

                ASTNode *call_is_none = ast_create(NODE_EXPR_CALL);
                ASTNode *memb_is_none = ast_create(NODE_EXPR_MEMBER);
                ASTNode *opt_ref1 = ast_create(NODE_EXPR_VAR);
                opt_ref1->var_ref.name = xstrdup("__opt");
                if (option_type_ptr)
                {
                    opt_ref1->resolved_type = xstrdup(option_type_ptr);
                }
                memb_is_none->member.target = opt_ref1;
                memb_is_none->member.field = xstrdup("is_none");
                memb_is_none->token = tk;
                call_is_none->token = tk;
                call_is_none->call.callee = memb_is_none;
                call_is_none->call.args = NULL;
                call_is_none->call.count = 0;

                ASTNode *if_break = ast_create(NODE_IF);
                if_break->token = tk;
                if_break->if_stmt.condition = call_is_none;

                ASTNode *break_blk = ast_create(NODE_BLOCK);
                break_blk->block.statements = ast_create(NODE_BREAK);
                break_blk->block.statements->token = tk;

                if_break->if_stmt.then_body = break_blk;
                if_break->if_stmt.else_body = NULL;
                APPEND_STMT(if_break);

                ASTNode *user_var_decl = ast_create(NODE_VAR_DECL);
                user_var_decl->token = tk;
                user_var_decl->var_decl.name = var_name;
                user_var_decl->var_decl.type_str = u_type;

                ASTNode *call_unwrap = ast_create(NODE_EXPR_CALL);
                ASTNode *memb_unwrap = ast_create(NODE_EXPR_MEMBER);
                ASTNode *opt_ref2 = ast_create(NODE_EXPR_VAR);
                opt_ref2->var_ref.name = xstrdup("__opt");
                if (option_type_ptr)
                {
                    opt_ref2->resolved_type = xstrdup(option_type_ptr);
                }
                memb_unwrap->member.target = opt_ref2;
                memb_unwrap->member.field = xstrdup("unwrap");
                memb_unwrap->token = tk;
                call_unwrap->token = tk;
                call_unwrap->call.callee = memb_unwrap;
                call_unwrap->call.args = NULL;
                call_unwrap->call.count = 0;

                user_var_decl->var_decl.init_expr = call_unwrap;

                if (enum_idx_name)
                {
                    ASTNode *idx_bind = ast_create(NODE_VAR_DECL);
                    idx_bind->token = tk;
                    idx_bind->var_decl.name = enum_idx_name;
                    idx_bind->var_decl.type_str = xstrdup("int");
                    idx_bind->var_decl.type_info = type_new(TYPE_INT);
                    ASTNode *idx_ref = ast_create(NODE_EXPR_VAR);
                    idx_ref->var_ref.name = xstrdup("__zc_enum_idx");
                    idx_bind->var_decl.init_expr = idx_ref;
                    APPEND_STMT(idx_bind);
                }

                APPEND_STMT(user_var_decl);

                enter_scope(ctx);
                add_symbol(ctx, var_name, u_type, NULL, 0);
                if (enum_idx_name)
                {
                    add_symbol(ctx, enum_idx_name, "int", type_new(TYPE_INT), 0);
                }

                ASTNode *stmt = parse_statement(ctx, l);
                ASTNode *user_body_node = stmt;
                if (stmt && stmt->kind != NODE_BLOCK)
                {
                    ASTNode *blk = ast_create(NODE_BLOCK);
                    blk->block.statements = stmt;
                    user_body_node = blk;
                }
                exit_scope(ctx);

                APPEND_STMT(user_body_node);

                if (enum_idx_name)
                {
                    ASTNode *idx_inc = ast_create(NODE_EXPR_UNARY);
                    idx_inc->unary.op = xstrdup("++");
                    ASTNode *idx_ref3 = ast_create(NODE_EXPR_VAR);
                    idx_ref3->var_ref.name = xstrdup("__zc_enum_idx");
                    idx_inc->unary.operand = idx_ref3;
                    while_loop->for_stmt.step = idx_inc;
                }

                loop_body->block.statements = stmts_head;
                while_loop->for_stmt.body = loop_body;

                ASTNode *outer_block = ast_create(NODE_BLOCK);

                ASTNode *enum_idx_decl_node = NULL;
                if (enum_idx_name)
                {
                    enum_idx_decl_node = ast_create(NODE_VAR_DECL);
                    enum_idx_decl_node->token = tk;
                    enum_idx_decl_node->var_decl.name = xstrdup("__zc_enum_idx");
                    enum_idx_decl_node->var_decl.type_str = xstrdup("int");
                    enum_idx_decl_node->var_decl.type_info = type_new(TYPE_INT);
                    ASTNode *zero_lit = ast_create(NODE_EXPR_LITERAL);
                    zero_lit->literal.kind = LITERAL_INT;
                    zero_lit->literal.int_val = 0;
                    zero_lit->literal.string_val = xstrdup("0");
                    enum_idx_decl_node->var_decl.init_expr = zero_lit;
                }

                if (slice_decl)
                {
                    if (enum_idx_decl_node)
                    {
                        enum_idx_decl_node->next = slice_decl;
                        slice_decl->next = it_decl;
                        it_decl->next = while_loop;
                        outer_block->block.statements = enum_idx_decl_node;
                    }
                    else
                    {
                        slice_decl->next = it_decl;
                        it_decl->next = while_loop;
                        outer_block->block.statements = slice_decl;
                    }
                }
                else
                {
                    if (enum_idx_decl_node)
                    {
                        enum_idx_decl_node->next = it_decl;
                        it_decl->next = while_loop;
                        outer_block->block.statements = enum_idx_decl_node;
                    }
                    else
                    {
                        it_decl->next = while_loop;
                        outer_block->block.statements = it_decl;
                    }
                }

                ctx->cg.loop_depth--;
                return outer_block;
            }
        }
        l->pos = saved_pos;
    }

    enter_scope(ctx);
    if (lexer_peek(l).kind == TOK_LPAREN)
    {
        lexer_next(l);
    }

    ASTNode *init = NULL;
    if (lexer_peek(l).kind != TOK_SEMICOLON)
    {
        if (lexer_peek(l).kind == TOK_IDENT && strncmp(lexer_peek(l).start, "let", 3) == 0)
        {
            init = parse_var_decl(ctx, l, 0);
        }
        else
        {
            init = parse_expression(ctx, l);
            if (lexer_peek(l).kind == TOK_SEMICOLON)
            {
                lexer_next(l);
            }
        }
    }
    else
    {
        lexer_next(l);
    }

    ASTNode *cond = NULL;
    if (lexer_peek(l).kind != TOK_SEMICOLON)
    {
        cond = parse_expression(ctx, l);
    }
    else
    {
        ASTNode *true_var = ast_create(NODE_EXPR_VAR);
        true_var->var_ref.name = xstrdup("true");
        true_var->token = for_token;
        cond = true_var;
    }
    if (lexer_peek(l).kind == TOK_SEMICOLON)
    {
        lexer_next(l);
    }

    ASTNode *step = NULL;
    if (lexer_peek(l).kind != TOK_RPAREN && lexer_peek(l).kind != TOK_LBRACE)
    {
        step = parse_expression(ctx, l);
    }

    if (lexer_peek(l).kind == TOK_RPAREN)
    {
        lexer_next(l);
    }

    ASTNode *body;
    if (lexer_peek(l).kind == TOK_LBRACE)
    {
        body = parse_block(ctx, l);
    }
    else
    {
        if (ctx->config->misra_mode)
        {
            zerror_at(lexer_peek(l), "MISRA Rule 15.6: compound-statement body");
        }
        body = parse_statement(ctx, l);
    }
    exit_scope(ctx);

    ASTNode *n = ast_create(NODE_FOR);
    n->token = for_token;
    n->for_stmt.init = init;
    n->for_stmt.condition = cond;
    n->for_stmt.step = step;
    n->for_stmt.body = body;
    ctx->cg.loop_depth--;
    return n;
}
