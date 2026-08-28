// SPDX-License-Identifier: MIT
#include "../parser/parser.h"
#include "codegen.h"
#include "zprep.h"
#include "../constants.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../plugins/plugin_manager.h"
#include "ast.h"
#include "codegen_internal.h"
#include "zprep_plugin.h"

typedef void (*ExprHandler)(ParserContext *ctx, ASTNode *node);

// Flag to track whether we're emitting a call expression callee.
// When true, codegen_var_expr should not auto-call no-payload enum variants
// because the parent NODE_EXPR_CALL will add the ().
int g_emitting_callee = 0;

void codegen_expression(ParserContext *ctx, ASTNode *node)
{
    if (!node)
    {
        return;
    }

    RECURSION_GUARD_TOKEN(ctx, node->token, );

    static const ExprHandler handlers[256] = {
        [NODE_MATCH] = handle_expr_match,
        [NODE_EXPR_BINARY] = handle_expr_binary,
        [NODE_EXPR_VAR] = handle_expr_var,
        [NODE_LAMBDA] = handle_lambda,
        [NODE_EXPR_LITERAL] = handle_expr_literal,
        [NODE_EXPR_CALL] = handle_expr_call,
        [NODE_EXPR_MEMBER] = handle_expr_member,
        [NODE_EXPR_INDEX] = handle_expr_index,
        [NODE_EXPR_SLICE] = handle_expr_slice,
        [NODE_BLOCK] = handle_block,
        [NODE_IF] = handle_if_expr,
        [NODE_TRY] = handle_try_expr,
        [NODE_RAW_STMT] = handle_raw_stmt,
        [NODE_PLUGIN] = handle_plugin,
        [NODE_EXPR_UNARY] = handle_expr_unary,
        [NODE_VA_START] = handle_va_start,
        [NODE_VA_END] = handle_va_end,
        [NODE_VA_COPY] = handle_va_copy,
        [NODE_AST_COMMENT] = handle_ast_comment,
        [NODE_ERRONEOUS] = handle_erroneous,
        [NODE_VA_ARG] = handle_va_arg,
        [NODE_EXPR_CAST] = handle_expr_cast,
        [NODE_EXPR_SIZEOF] = handle_expr_sizeof,
        [NODE_TYPEOF] = handle_typeof,
        [NODE_REFLECTION] = handle_reflection,
        [NODE_EXPR_STRUCT_INIT] = handle_expr_struct_init,
        [NODE_EXPR_ARRAY_LITERAL] = handle_expr_array_literal,
        [NODE_EXPR_TUPLE_LITERAL] = handle_expr_tuple_literal,
        [NODE_TERNARY] = handle_ternary,
        [NODE_AWAIT] = handle_await,
    };

    if (node->kind < 256 && handlers[node->kind])
    {
        handlers[node->kind](ctx, node);
        RECURSION_EXIT(ctx);
        return;
    }

    RECURSION_EXIT(ctx);
}

void codegen_expression_bare(ParserContext *ctx, ASTNode *node)
{
    if (!node)
    {
        return;
    }

    RECURSION_GUARD_TOKEN(ctx, node->token, );

    if (node->kind == NODE_EXPR_BINARY)
    {
        const char *op = node->binary.op;
        int is_simple = (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 ||
                         strcmp(op, ">=") == 0 || strcmp(op, "+") == 0 || strcmp(op, "-") == 0 ||
                         strcmp(op, "*") == 0 || strcmp(op, "/") == 0 || strcmp(op, "%") == 0 ||
                         strcmp(op, "+=") == 0 || strcmp(op, "-=") == 0 || strcmp(op, "*=") == 0 ||
                         strcmp(op, "/=") == 0 || strcmp(op, "=") == 0);

        if (is_simple)
        {
            codegen_expression(ctx, node->binary.left);
            EMIT(ctx, " %s ", op);
            codegen_expression(ctx, node->binary.right);
            RECURSION_EXIT(ctx);
            return;
        }
    }

    if (node->kind == NODE_EXPR_UNARY && node->unary.op)
    {
        if (strcmp(node->unary.op, "_post++") == 0)
        {
            codegen_expression(ctx, node->unary.operand);
            EMIT(ctx, "++");
            RECURSION_EXIT(ctx);
            return;
        }
        if (strcmp(node->unary.op, "_post--") == 0)
        {
            codegen_expression(ctx, node->unary.operand);
            EMIT(ctx, "--");
            RECURSION_EXIT(ctx);
            return;
        }
    }

    codegen_expression(ctx, node);
    RECURSION_EXIT(ctx);
}
