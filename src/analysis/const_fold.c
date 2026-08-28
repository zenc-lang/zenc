// SPDX-License-Identifier: MIT
#include "../constants.h"
#include "analysis/const_fold.h"
#include "../parser/parser.h"
#include "../ast/symbols.h"
#include <string.h>
#include <stdio.h>

int eval_const_int_expr(ASTNode *node, ParserContext *ctx, long long *out_val)
{
    if (!node)
    {
        return 0;
    }

    switch (node->kind)
    {
    case NODE_EXPR_LITERAL:
        if (node->literal.kind == LITERAL_INT)
        {
            *out_val = (long long int)(node->literal.int_val);
            return 1;
        }
        return 0;

    case NODE_EXPR_VAR:
    {
        if (strcmp(node->var_ref.name, "true") == 0)
        {
            *out_val = 1;
            return 1;
        }
        if (strcmp(node->var_ref.name, "false") == 0)
        {
            *out_val = 0;
            return 1;
        }

        ZenSymbol *sym = find_symbol_entry(ctx, node->var_ref.name);
        if (sym && sym->is_const_value)
        {
            sym->is_used = 1;
            *out_val = sym->const_int_val;
            return 1;
        }
        // Check for enum variants
        EnumVariantReg *ev = find_enum_variant(ctx, node->var_ref.name);
        if (ev)
        {
            *out_val = (long long)ev->tag_id;
            return 1;
        }
        break;
    }

    case NODE_EXPR_BINARY:
    {
        long long left, right;
        if (!eval_const_int_expr(node->binary.left, ctx, &left))
        {
            return 0;
        }
        if (!eval_const_int_expr(node->binary.right, ctx, &right))
        {
            return 0;
        }

        if (strcmp(node->binary.op, "+") == 0)
        {
            *out_val = left + right;
        }
        else if (strcmp(node->binary.op, "-") == 0)
        {
            *out_val = left - right;
        }
        else if (strcmp(node->binary.op, "*") == 0)
        {
            *out_val = left * right;
        }
        else if (strcmp(node->binary.op, "/") == 0)
        {
            if (right == 0)
            {
                return 0; // Division by zero
            }
            *out_val = left / right;
        }
        else if (strcmp(node->binary.op, "%") == 0)
        {
            if (right == 0)
            {
                return 0;
            }
            *out_val = left % right;
        }
        else if (strcmp(node->binary.op, "<<") == 0)
        {
            *out_val = left << right;
        }
        else if (strcmp(node->binary.op, ">>") == 0)
        {
            *out_val = left >> right;
        }
        else if (strcmp(node->binary.op, "&") == 0)
        {
            *out_val = left & right;
        }
        else if (strcmp(node->binary.op, "|") == 0)
        {
            *out_val = left | right;
        }
        else if (strcmp(node->binary.op, "^") == 0)
        {
            *out_val = left ^ right;
        }
        else if (strcmp(node->binary.op, "==") == 0)
        {
            *out_val = (left == right);
        }
        else if (strcmp(node->binary.op, "!=") == 0)
        {
            *out_val = (left != right);
        }
        else if (strcmp(node->binary.op, "<") == 0)
        {
            *out_val = (left < right);
        }
        else if (strcmp(node->binary.op, ">") == 0)
        {
            *out_val = (left > right);
        }
        else if (strcmp(node->binary.op, "<=") == 0)
        {
            *out_val = (left <= right);
        }
        else if (strcmp(node->binary.op, ">=") == 0)
        {
            *out_val = (left >= right);
        }
        else if (strcmp(node->binary.op, "&&") == 0)
        {
            *out_val = (left && right);
        }
        else if (strcmp(node->binary.op, "||") == 0)
        {
            *out_val = (left || right);
        }
        else
        {
            return 0;
        }

        return 1;
    }

    case NODE_EXPR_UNARY:
    {
        long long operand;
        if (!eval_const_int_expr(node->unary.operand, ctx, &operand))
        {
            return 0;
        }

        if (strcmp(node->unary.op, "-") == 0)
        {
            *out_val = -operand;
        }
        else if (strcmp(node->unary.op, "+") == 0)
        {
            *out_val = +operand;
        }
        else if (strcmp(node->unary.op, "~") == 0)
        {
            *out_val = ~operand;
        }
        else if (strcmp(node->unary.op, "!") == 0)
        {
            *out_val = !operand;
        }
        else
        {
            return 0;
        }

        return 1;
    }

    default:
        return 0;
    }
    return 0; // For warning.
}
