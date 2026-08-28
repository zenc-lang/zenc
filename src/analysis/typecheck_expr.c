// SPDX-License-Identifier: MIT
#include "typecheck_internal.h"
#include "../utils/utils.h"

void check_move_for_rvalue(TypeChecker *tc, ASTNode *rvalue)
{
    if (!rvalue || !rvalue->type_info)
    {
        return;
    }

    if (is_type_copy(tc->pctx, rvalue->type_info))
    {
        return;
    }

    if (rvalue->kind == NODE_EXPR_VAR)
    {
        ZenSymbol *sym = tc_lookup(tc, rvalue->var_ref.name);
        if (sym)
        {
            mark_symbol_moved(tc->pctx, sym, rvalue);
        }
    }
    else if (rvalue->kind == NODE_EXPR_UNARY && strcmp(rvalue->unary.op, "*") == 0)
    {
        const char *hints[] = {"This type owns resources and cannot be implicitly copied",
                               "Consider borrowing value via references or implementing Copy",
                               NULL};
        tc_move_error_with_hints(tc, rvalue->token, "Cannot move out of a borrowed reference",
                                 hints);
    }
    else if (rvalue->kind == NODE_EXPR_MEMBER)
    {
        // Now allowed, but will be tracked by path
        mark_symbol_moved(tc->pctx, NULL, rvalue);
    }
    else if (rvalue->kind == NODE_EXPR_INDEX)
    {
        const char *hints[] = {"Cannot move an element out of an array or slice.", NULL};
        tc_move_error_with_hints(tc, rvalue->token, "Cannot move out of an index expression",
                                 hints);
    }
}

Type *resolve_alias(Type *t)
{
    while (t && t->kind == TYPE_ALIAS && t->inner)
    {
        t = t->inner;
    }
    return t;
}

void check_expr_unary(TypeChecker *tc, ASTNode *node, int depth)
{
    check_node(tc, node->unary.operand, depth + 1);

    Type *operand_type = node->unary.operand->type_info;
    const char *op = node->unary.op;

    if (!operand_type)
    {
        return;
    }

    // Logical NOT: !
    if (strcmp(op, "!") == 0)
    {
        node->type_info = type_new(TYPE_BOOL);
        return;
    }

    // Numeric negation: -
    if (strcmp(op, "-") == 0)
    {
        if (!is_integer_type(operand_type) && !is_float_type(operand_type))
        {
            const char *hints[] = {"Negation requires a numeric operand", NULL};
            tc_error_with_hints(tc, node->token, "Cannot negate non-numeric type", hints);
        }
        else
        {
            node->type_info = operand_type;
        }
        return;
    }

    // Dereference: *
    if (strcmp(op, "*") == 0)
    {
        if (operand_type->kind == TYPE_UNKNOWN)
        {
            node->type_info = type_new(TYPE_UNKNOWN);
            return;
        }

        Type *resolved = resolve_alias(operand_type);
        if (resolved->kind != TYPE_POINTER && resolved->kind != TYPE_STRING)
        {
            const char *hints[] = {"Only pointers can be dereferenced", NULL};
            tc_error_with_hints(tc, node->token, "Cannot dereference non-pointer type", hints);
        }
        else if (resolved->kind == TYPE_STRING)
        {
            node->type_info = type_new(TYPE_CHAR);
        }
        else if (resolved->inner)
        {
            node->type_info = resolved->inner;
            CompilerConfig *cfg = &tc->pctx->compiler->config;
            if (cfg->misra_mode)
            {
                misra_check_file_dereference(tc->pctx, operand_type, node->token);
            }
        }
        return;
    }

    // Bitwise NOT: ~
    if (strcmp(op, "~") == 0)
    {
        if (!is_integer_type(operand_type))
        {
            const char *hints[] = {"Bitwise NOT requires an integer operand", NULL};
            tc_error_with_hints(tc, node->token, "Cannot apply ~ to non-integer type", hints);
        }
        else
        {
            misra_check_bitwise_operand(tc->pctx, node->unary.operand->type_info, node->token);
            node->type_info = operand_type;
        }
        return;
    }

    if (strcmp(op, "++") == 0 || strcmp(op, "--") == 0 || strcmp(op, "_post++") == 0 ||
        strcmp(op, "_post--") == 0)
    {
        misra_check_inc_dec_result_used(tc->pctx, node->token);
        // Track as a write
        if (node->unary.operand->kind == NODE_EXPR_VAR)
        {
            ZenSymbol *s = tc_lookup(tc, node->unary.operand->var_ref.name);
            if (s)
            {
                s->is_written_to = 1;
            }
        }
        else if (node->unary.operand->kind == NODE_EXPR_UNARY &&
                 strcmp(node->unary.operand->unary.op, "*") == 0)
        {
            ASTNode *inner = node->unary.operand->unary.operand;
            if (inner->kind == NODE_EXPR_VAR)
            {
                ZenSymbol *s = tc_lookup(tc, inner->var_ref.name);
                if (s)
                {
                    s->is_written_to = 1;
                }
            }
        }
        if (!is_integer_type(operand_type) && !is_float_type(operand_type) &&
            operand_type->kind != TYPE_POINTER)
        {
            tc_error(tc, node->token, "Increment/decrement requires a numeric or pointer operand");
        }
        node->type_info = operand_type;
        return;
    }

    // Address-of: &
    if (strcmp(op, "&") == 0 || strcmp(op, "&_rval") == 0)
    {
        node->type_info = type_new_ptr(operand_type);
        // Record provenance depth for escape analysis
        if (node->unary.operand->kind == NODE_EXPR_VAR)
        {
            ZenSymbol *sym = tc_lookup(tc, node->unary.operand->var_ref.name);
            if (sym)
            {
                node->type_info->lifetime_depth = sym->scope_depth;
            }
        }
        else if (strcmp(op, "&_rval") == 0)
        {
            // R-values are temporaries, their life is the current code block
            if (node->unary.operand->type_info)
            {
                node->type_info->lifetime_depth = node->unary.operand->type_info->lifetime_depth;
            }
            else
            {
                node->type_info->lifetime_depth = tc->current_depth;
            }
        }
        else
        {
            // For complex targets (e.g. &array[0]), inherit from the operand if it has a lifetime
            if (operand_type)
            {
                node->type_info->lifetime_depth = operand_type->lifetime_depth;
            }
        }
        return;
    }
}

void check_expr_binary(TypeChecker *tc, ASTNode *node, int depth)
{
    const char *op = node->binary.op;
    Type *contextual_type = node->type_info;

    if (strcmp(op, "=") == 0 ||
        (strlen(op) > 1 && op[strlen(op) - 1] == '=' && strcmp(op, "==") != 0 &&
         strcmp(op, "!=") != 0 && strcmp(op, "<=") != 0 && strcmp(op, ">=") != 0))
    {
        if (!tc->is_stmt_context)
        {
            misra_check_assignment_result_used(tc->pctx, node->token);
        }

        int old_is_assign_lhs = tc->is_assign_lhs;
        int old_stmt_ctx = tc->is_stmt_context;
        tc->is_assign_lhs = 1;
        tc->is_stmt_context = 0;
        check_node(tc, node->binary.left, depth + 1);
        tc->is_assign_lhs = old_is_assign_lhs;

        if (node->binary.left->type_info && node->binary.right->kind == NODE_LAMBDA)
        {
            node->binary.right->type_info = node->binary.left->type_info;
        }
        check_node(tc, node->binary.right, depth + 1);
        tc->is_stmt_context = old_stmt_ctx;

        // Rule 13.2: Side effect collision detection for assignments
        if (tc->pctx->config->misra_mode)
        {
            SymbolSet l_reads = {0}, l_writes = {0};
            SymbolSet r_reads = {0}, r_writes = {0};
            collect_symbols(node->binary.left, &l_reads, &l_writes);
            collect_symbols(node->binary.right, &r_reads, &r_writes);

            // Treat LHS of assignment as a write
            if (node->binary.left->kind == NODE_EXPR_VAR)
            {
                int already_in = 0;
                for (int k = 0; k < l_writes.count; k++)
                {
                    if (l_writes.syms[k] == node->binary.left->var_ref.symbol)
                    {
                        already_in = 1;
                        break;
                    }
                }
                if (!already_in && l_writes.count < 32)
                {
                    l_writes.syms[l_writes.count++] = node->binary.left->var_ref.symbol;
                }
            }

            // Double write collision: i = i++
            int match = 0;
            for (int i = 0; i < l_writes.count; i++)
            {
                ZenSymbol *s = l_writes.syms[i];
                if (!s)
                {
                    continue;
                }
                for (int j = 0; j < r_writes.count; j++)
                {
                    if (s == r_writes.syms[j])
                    {
                        tc_error(tc, node->token,
                                 "MISRA Rule 13.2: symbol modified multiple times in assignment");
                        match = 1;
                        break;
                    }
                }
                if (match)
                {
                    break;
                }

                // Also check if LHS write conflicts with RHS reads (if RHS also writes it)
                // e.g. i = i++ is already covered by double write.
                // But what about i = i + (i++)?
                // r_writes={i}, r_reads={i, i}.
            }
        }
    }
    else
    {
        int old_stmt_ctx = tc->is_stmt_context;
        tc->is_stmt_context = 0;
        check_node(tc, node->binary.left, depth + 1);
        check_node(tc, node->binary.right, depth + 1);
        tc->is_stmt_context = old_stmt_ctx;

        // Rule 13.2: Side effect collision detection for binary operators
        if (strcmp(op, "&&") != 0 && strcmp(op, "||") != 0 && strcmp(op, ",") != 0)
        {
            check_side_effect_collision(tc, node->binary.left, node->binary.right, node->token);
        }
        else if (tc->pctx->config->misra_mode && (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0))
        {
            // Rule 13.5: The RHS of && or || shall not contain persistent side effects
            SymbolSet r_reads = {0}, r_writes = {0};
            collect_symbols(node->binary.right, &r_reads, &r_writes);
            if (r_writes.count > 0)
            {
                tc_error(tc, node->binary.right->token,
                         "MISRA Rule 13.5: persistent side effect in logical RHS");
            }
        }
    }

    // Rule 19.1: Check for self-assignment (x = x)
    if (tc->pctx->config->misra_mode && strcmp(op, "=") == 0)
    {
        misra_check_assignment_overlap(tc->pctx, node->binary.left, node->binary.right,
                                       node->token);
    }

    // Malformed input can produce binary expressions with NULL children
    if (!node->binary.left || !node->binary.right)
    {
        return;
    }
    Type *left_type = node->binary.left->type_info;
    Type *right_type = node->binary.right->type_info;

    // Assignment Logic for Moves (and type compatibility)
    if (strcmp(op, "=") == 0)
    {
        // Check type compatibility for assignment
        if (left_type && right_type)
        {
            apply_implicit_struct_pointer_conversions(tc, &node->binary.right, left_type);
            right_type = node->binary.right->type_info;
            check_type_compatibility(tc, left_type, right_type, node->binary.right->token,
                                     node->binary.right, 0);

            // Mark LHS as written to
            if (node->binary.left->kind == NODE_EXPR_VAR)
            {
                ZenSymbol *s = tc_lookup(tc, node->binary.left->var_ref.name);
                if (s)
                {
                    s->is_written_to = 1;
                }
            }
            else if (node->binary.left->kind == NODE_EXPR_UNARY &&
                     strcmp(node->binary.left->unary.op, "*") == 0)
            {
                ASTNode *inner = node->binary.left->unary.operand;
                if (inner->kind == NODE_EXPR_VAR)
                {
                    ZenSymbol *s = tc_lookup(tc, inner->var_ref.name);
                    if (s)
                    {
                        s->is_written_to = 1;
                    }
                }
            }
            // Also handle array indexing as write
            else if (node->binary.left->kind == NODE_EXPR_INDEX)
            {
                ASTNode *arr = node->binary.left->index.array;
                if (arr->kind == NODE_EXPR_VAR)
                {
                    ZenSymbol *s = tc_lookup(tc, arr->var_ref.name);
                    if (s)
                    {
                        s->is_written_to = 1;
                    }
                }
            }
            // Also handle member access as write
            else if (node->binary.left->kind == NODE_EXPR_MEMBER)
            {
                ASTNode *target = node->binary.left->member.target;
                // Follow the member chain to the base variable
                while (target && target->kind == NODE_EXPR_MEMBER)
                {
                    target = target->member.target;
                }
                if (target && target->kind == NODE_EXPR_VAR)
                {
                    ZenSymbol *s = tc_lookup(tc, target->var_ref.name);
                    if (s)
                    {
                        s->is_written_to = 1;
                    }
                }
            }
        }

        // If RHS is moving a non-copy value, check validity and mark moved
        check_move_for_rvalue(tc, node->binary.right);

        // LHS is being (re-)initialized, so it becomes Valid.
        if (node->binary.left->kind == NODE_EXPR_VAR)
        {
            ZenSymbol *lhs_sym = tc_lookup(tc, node->binary.left->var_ref.name);
            if (lhs_sym)
            {
                if (tc->pctx->config->misra_mode && node->binary.left &&
                    node->binary.left->kind == NODE_EXPR_VAR)
                {
                    misra_check_param_modified(tc->current_func, node->binary.left,
                                               node->binary.left->token);
                }
                if (lhs_sym->is_immutable)
                {
                    tc_error(tc, node->binary.left->token, "Cannot assign to immutable variable");
                }

                if (tc->pctx->config->misra_mode)
                {
                    misra_check_pointer_conversion(tc->pctx, left_type, right_type, node->token);
                }
                mark_symbol_valid(tc->pctx, lhs_sym, node->binary.left);
            }
        }

        // Result type is same as LHS
        node->type_info = left_type;
        return;
    }

    // Arithmetic operators: +, -, *, /, %
    if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 || strcmp(op, "*") == 0 ||
        strcmp(op, "/") == 0 || strcmp(op, "%") == 0)
    {
        // Division by zero detection for / and %
        if ((strcmp(op, "/") == 0 || strcmp(op, "%") == 0) && node->binary.right &&
            node->binary.right->kind == NODE_EXPR_LITERAL)
        {
            LiteralKind kind = node->binary.right->literal.kind;
            if (kind == LITERAL_INT && node->binary.right->literal.int_val == 0)
            {
                const char *hints[] = {"Division by zero is undefined behavior", NULL};
                tc_error_with_hints(tc, node->binary.right->token, "Division by zero detected",
                                    hints);
            }
            else if (kind == LITERAL_FLOAT && node->binary.right->literal.float_val >= -0.0 &&
                     node->binary.right->literal.float_val <= 0.0)
            {
                const char *hints[] = {"Division by zero results in infinity or NaN", NULL};
                tc_error_with_hints(tc, node->binary.right->token, "Division by zero detected",
                                    hints);
            }
        }

        if (left_type && right_type)
        {
            Type *lhs_resolved = resolve_alias(left_type);
            Type *rhs_resolved = resolve_alias(right_type);

            // Pointer Arithmetic
            if (lhs_resolved->kind == TYPE_POINTER || lhs_resolved->kind == TYPE_STRING)
            {
                misra_check_pointer_arithmetic(tc->pctx, lhs_resolved, node->token);

                // Ptr - Ptr -> isize
                if (strcmp(op, "-") == 0 &&
                    (rhs_resolved->kind == TYPE_POINTER || rhs_resolved->kind == TYPE_STRING))
                {
                    node->type_info = type_new(TYPE_ISIZE);
                    return;
                }
                // Ptr + Int -> Ptr
                // Ptr - Int -> Ptr
                if ((strcmp(op, "+") == 0 || strcmp(op, "-") == 0) && is_integer_type(rhs_resolved))
                {
                    node->type_info = left_type;
                    return;
                }
            }
            // Int + Ptr -> Ptr
            if (strcmp(op, "+") == 0 && is_integer_type(lhs_resolved) &&
                (rhs_resolved->kind == TYPE_POINTER || rhs_resolved->kind == TYPE_STRING))
            {
                misra_check_pointer_arithmetic(tc->pctx, rhs_resolved, node->token);
                node->type_info = right_type;
                return;
            }

            int left_numeric = is_integer_type(left_type) || is_float_type(left_type) ||
                               left_type->kind == TYPE_VECTOR;
            int right_numeric = is_integer_type(right_type) || is_float_type(right_type) ||
                                right_type->kind == TYPE_VECTOR;

            if (!left_numeric || !right_numeric)
            {
                if (left_type->kind == TYPE_UNKNOWN || right_type->kind == TYPE_UNKNOWN)
                {
                    node->type_info = type_new(TYPE_UNKNOWN);
                    return;
                }

                char msg[MAX_SHORT_MSG_LEN];
                snprintf(msg, sizeof(msg), "Operator '%s' requires numeric operands", op);
                const char *hints[] = {
                    "Arithmetic operators can only be used with integer, float, or vector types",
                    NULL};
                tc_error_with_hints(tc, node->token, msg, hints);
            }
            else if (left_type->kind == TYPE_VECTOR || right_type->kind == TYPE_VECTOR)
            {
                if (left_type->kind != right_type->kind || !type_eq(left_type, right_type))
                {
                    tc_error(tc, node->token,
                             "Vector operation requires operands of same vector type");
                }
                node->type_info = left_type;
                return;
            }
            else
            {
                // Rule 10.4: Balancing
                misra_check_binary_op_essential_types(tc->pctx, node->binary.left,
                                                      node->binary.right, node->token);

                // MISRA Rule 12.4: Evaluation of constant expressions shall not lead to unsigned
                // wrap Use the contextually pushed down type (stored in node->type_info before this
                // function) or fall back to the inferred operand type.
                Type *target_type = contextual_type ? contextual_type : left_type;

                if (tc->pctx->config->misra_mode && target_type && is_integer_type(target_type))
                {
                    long long lval, rval;
                    if (eval_const_int_expr(node->binary.left, tc->pctx, &lval) &&
                        eval_const_int_expr(node->binary.right, tc->pctx, &rval))
                    {
                        long long res = 0;
                        if (strcmp(op, "+") == 0)
                        {
                            res = lval + rval;
                        }
                        else if (strcmp(op, "-") == 0)
                        {
                            res = lval - rval;
                        }
                        else if (strcmp(op, "*") == 0)
                        {
                            res = lval * rval;
                        }

                        if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 || strcmp(op, "*") == 0)
                        {
                            misra_check_unsigned_wrap(tc->pctx, op, lval, rval, res, target_type,
                                                      node->token);
                        }
                    }
                }

                // Rule 10.2: Character arithmetic
                misra_check_char_arithmetic(tc->pctx, left_type, right_type, op, node->token);

                // Result type: Only infer if not already set by context
                if (!node->type_info)
                {
                    if (is_float_type(left_type) || is_float_type(right_type))
                    {
                        node->type_info = type_new(TYPE_F64);
                    }
                    else
                    {
                        node->type_info = left_type;
                    }
                }
            }
        }
        return;
    }

    // Comparison operators: ==, !=, <, >, <=, >=
    if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 || strcmp(op, "<") == 0 ||
        strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0)
    {
        // Result is always bool
        node->type_info = type_new(TYPE_BOOL);

        // Operands should be comparable
        if (left_type && right_type)
        {
            // Rule 10.4: Balancing
            misra_check_binary_op_essential_types(tc->pctx, node->binary.left, node->binary.right,
                                                  node->token);

            // Rule 18.3: Relational operators on pointers (>, <, >=, <=)
            if (tc->pctx->config->misra_mode && (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
                                                 strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0))
            {
                Type *l_resolved = resolve_alias(left_type);
                Type *r_resolved = resolve_alias(right_type);
                if (l_resolved->kind == TYPE_POINTER || r_resolved->kind == TYPE_POINTER)
                {
                    tc_error(
                        tc, node->token,
                        "MISRA Rule 18.3: relational operator shall not be applied to pointers");
                }
            }

            misra_check_string_compare(tc->pctx, left_type, right_type, node->token);

            if (!type_eq(left_type, right_type))
            {
                // Allow comparison between numeric types
                int left_numeric = is_integer_type(left_type) || is_float_type(left_type);
                int right_numeric = is_integer_type(right_type) || is_float_type(right_type);

                if (!left_numeric || !right_numeric)
                {
                    if ((left_type && left_type->kind == TYPE_UNKNOWN) ||
                        (right_type && right_type->kind == TYPE_UNKNOWN))
                    {
                        node->type_info = type_new(TYPE_BOOL);
                        return;
                    }
                    char msg[MAX_SHORT_MSG_LEN];
                    snprintf(msg, sizeof(msg), "Cannot compare '%s' with incompatible types", op);
                    const char *hints[] = {"Ensure both operands have the same or compatible types",
                                           NULL};
                    tc_error_with_hints(tc, node->token, msg, hints);
                }
            }
        }
        return;
    }

    // Logical operators: &&, ||
    if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0)
    {
        node->type_info = type_new(TYPE_BOOL);
        // Could validate that operands are boolean-like, but C is lax here
        return;
    }

    // Bitwise operators: &, |, ^, <<, >>
    if (strcmp(op, "&") == 0 || strcmp(op, "|") == 0 || strcmp(op, "^") == 0 ||
        strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0)
    {
        // MISRA Rule 12.2: Shift amount validation for << and >>
        if (tc->pctx->config->misra_mode && (strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0))
        {
            long long shift_amt;
            if (eval_const_int_expr(node->binary.right, tc->pctx, &shift_amt))
            {
                int width = integer_type_width(left_type);
                misra_check_shift_amount(tc->pctx, shift_amt, width, node->token);
            }
        }
        else if ((strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) && node->binary.right &&
                 node->binary.right->kind == NODE_EXPR_LITERAL &&
                 node->binary.right->literal.kind == LITERAL_INT)
        {
            // Legacy/Non-MISRA warnings
            unsigned long long shift_amt = node->binary.right->literal.int_val;
            if (shift_amt >= 64)
            {
                const char *hints[] = {"Shift amount exceeds bit width, result is undefined", NULL};
                tc_error_with_hints(tc, node->binary.right->token, "Shift amount too large", hints);
            }
            else if (shift_amt >= 32 && left_type &&
                     (left_type->kind == TYPE_INT || left_type->kind == TYPE_UINT ||
                      left_type->kind == TYPE_I32 || left_type->kind == TYPE_U32 ||
                      left_type->kind == TYPE_C_INT || left_type->kind == TYPE_C_UINT))
            {
                const char *hints[] = {
                    "Shift amount >= 32 is undefined behavior for 32-bit integers", NULL};
                tc_error_with_hints(tc, node->binary.right->token,
                                    "Shift amount exceeds 32-bit type width", hints);
            }
        }

        if (left_type && right_type)
        {
            if ((!is_integer_type(left_type) && left_type->kind != TYPE_VECTOR) ||
                (!is_integer_type(right_type) && right_type->kind != TYPE_VECTOR))
            {
                char msg[MAX_SHORT_MSG_LEN];
                snprintf(msg, sizeof(msg),
                         "Bitwise operator '%s' requires integer or vector operands", op);
                const char *hints[] = {"Bitwise operators only work on integer or vector types",
                                       NULL};
                tc_error_with_hints(tc, node->token, msg, hints);
            }
            else if (left_type->kind == TYPE_VECTOR || right_type->kind == TYPE_VECTOR)
            {
                if (left_type->kind != right_type->kind || !type_eq(left_type, right_type))
                {
                    tc_error(tc, node->token, "Vector bitwise operation requires same vector type");
                }
                node->type_info = left_type;
            }
            else
            {
                if (tc->pctx->config->misra_mode)
                {
                    misra_check_bitwise_operand(tc->pctx, left_type, node->token);
                    misra_check_bitwise_operand(tc->pctx, right_type, node->token);
                    // Rule 10.4: Balancing for &, |, ^
                    if (strcmp(op, "&") == 0 || strcmp(op, "|") == 0 || strcmp(op, "^") == 0)
                    {
                        misra_check_binary_op_essential_types(tc->pctx, node->binary.left,
                                                              node->binary.right, node->token);
                    }
                }
                node->type_info = left_type;
            }
        }
        return;
    }
}

void extract_base_name(const char *full_name, char *base_buf, size_t max_len)
{
    if (!full_name)
    {
        base_buf[0] = '\0';
        return;
    }
    size_t i = 0;
    while (full_name[i] && full_name[i] != '<' && full_name[i] != '_' && i < max_len - 1)
    {
        base_buf[i] = full_name[i];
        i++;
    }
    base_buf[i] = '\0';
}

int is_struct_base_match(Type *base, Type *instantiated)
{
    if (!base || !base->name || !instantiated || !instantiated->name)
    {
        return 0;
    }
    if (strcmp(base->name, instantiated->name) == 0)
    {
        return 1;
    }

    char base_str[MAX_TYPE_NAME_LEN];
    char inst_str[MAX_TYPE_NAME_LEN];
    extract_base_name(base->name, base_str, sizeof(base_str));
    extract_base_name(instantiated->name, inst_str, sizeof(inst_str));

    if (base_str[0] != '\0' && strcmp(base_str, inst_str) == 0)
    {
        return 1;
    }
    return 0;
}

void apply_implicit_struct_pointer_conversions(TypeChecker *tc, ASTNode **expr_ptr,
                                               Type *expected_type)
{
    if (!expr_ptr || !*expr_ptr || !expected_type)
    {
        return;
    }
    ASTNode *expr = *expr_ptr;
    Type *actual_type = expr->type_info;
    if (!actual_type)
    {
        return;
    }

    Type *e_res = get_inner_type(expected_type);
    Type *a_res = get_inner_type(actual_type);

    // T* (actual) -> T (expected) => Implicit Dereference *
    // This allows `return self` to return the struct value when self is a pointer.
    // We only do this if the type is Copy, otherwise it would trigger a
    // move-from-borrowed-reference error.
    if (a_res->kind == TYPE_POINTER && a_res->inner &&
        (a_res->inner->kind == TYPE_STRUCT || a_res->inner->kind == TYPE_ENUM) &&
        (type_eq(a_res->inner, e_res) || is_struct_base_match(a_res->inner, e_res)) &&
        is_type_copy(tc->pctx, a_res->inner))

    {
        ASTNode *deref = ast_create(NODE_EXPR_UNARY);
        deref->unary.op = xstrdup("*");
        deref->unary.operand = expr;
        deref->type_info = a_res->inner;
        deref->token = expr->token;
        deref->next = expr->next;
        expr->next = NULL;
        *expr_ptr = deref;
    }
    // T (actual) -> T* (expected) => Implicit Address-Of &
    else if (e_res->kind == TYPE_POINTER && e_res->inner &&
             (a_res->kind == TYPE_STRUCT || a_res->kind == TYPE_ENUM) &&
             (type_eq(a_res, e_res->inner) || is_struct_base_match(a_res, e_res->inner)))
    {
        ASTNode *addr = ast_create(NODE_EXPR_UNARY);
        int is_rvalue = (expr->kind == NODE_EXPR_CALL || expr->kind == NODE_EXPR_BINARY ||
                         expr->kind == NODE_MATCH);
        addr->unary.op = is_rvalue ? xstrdup("&_rval") : xstrdup("&");
        addr->unary.operand = expr;
        addr->type_info = e_res;
        addr->token = expr->token;
        addr->next = expr->next;
        expr->next = NULL;
        *expr_ptr = addr;
    }
}

int check_type_compatibility(TypeChecker *tc, Type *target, Type *value, Token t,
                             ASTNode *value_node, int is_call_arg)
{
    if (!target || !value)
    {
        return 1; // Can't check incomplete types
    }

    Type *resolved_target = resolve_alias(target);
    Type *resolved_value = resolve_alias(value);

    // MISRA Pointer & Constant Checks (Rules 11.5, 11.9, etc.)
    if (tc->pctx->config->misra_mode && resolved_target->kind == TYPE_POINTER)
    {
        misra_check_null_pointer_constant(tc->pctx, value_node, t);
        misra_check_void_ptr_cast(tc->pctx, target, value, t);
        misra_check_pointer_conversion(tc->pctx, target, value, t);
    }

    // Resolution of Integer compatibility (Rule 10.3)
    // This MUST happen before type_eq fast-path because type_eq is lax for integers.
    if (is_integer_type(resolved_target) && is_integer_type(resolved_value))
    {
        int target_width = integer_type_width(resolved_target);
        int value_width = integer_type_width(resolved_value);

        if (tc->pctx->config->misra_mode)
        {
            misra_check_implicit_conversion(tc->pctx, target, value, value_node, t);
        }
        else
        {
            // Warn on narrowing conversions in non-MISRA mode
            if (target_width > 0 && value_width > 0 && target_width < value_width)
            {
                char *t_str = type_to_string(target);
                char *v_str = type_to_string(value);
                char msg[MAX_SHORT_MSG_LEN];
                snprintf(msg, sizeof(msg),
                         "Implicit narrowing conversion from '%s' (%d-bit) to '%s' (%d-bit)", v_str,
                         value_width, t_str, target_width);
                zwarn_at(t, "%s", msg);
                if (tc)
                {
                    tc->warning_count++;
                }
                zfree(t_str);
                zfree(v_str);
            }
        }
        return 1; // All integer pairs compatible (modulo MISRA/Warning checks above)
    }

    if (!is_call_arg && resolved_target->kind == TYPE_POINTER &&
        resolved_value->kind == TYPE_POINTER)
    {
        // Higher depth = shorter scope.
        if (resolved_target->lifetime_depth < resolved_value->lifetime_depth)
        {
            char msg[MAX_ERROR_MSG_LEN];
            snprintf(msg, sizeof(msg),
                     "Escape analysis error: pointer assigned to a location that outlives it");
            const char *hints[] = {"The source pointer belongs to a child scope and will become "
                                   "invalid when that scope ends.",
                                   "Consider copying the value instead of taking a reference.",
                                   NULL};
            tc_error_with_hints(tc, t, msg, hints);
            return 0;
        }
    }

    // Fast path: exact match
    if (type_eq(target, value))
    {
        // MISRA Rule 11.8: Check const qualification during pointer assignment
        if (tc->pctx->config->misra_mode && resolved_target->kind == TYPE_POINTER &&
            resolved_value->kind == TYPE_POINTER)
        {
            misra_check_pointer_conversion(tc->pctx, target, value, t);
        }
        return 1;
    }

    if (tc->pctx->config->misra_mode)
    {
        // Remaining pointer nesting and array param size checks are modularized
        // in their respective node visitors.
    }

    // Resolve type aliases (str -> string, etc.) for non-integer types
    // (Integer aliases handled by resolve_alias above)
    if (target->kind == TYPE_ALIAS && target->name)
    {
        const char *alias = find_type_alias(tc->pctx, target->name);
        if (alias)
        {
            // Check if resolved names match
            if (value->name && strcmp(alias, value->name) == 0)
            {
                return 1;
            }
        }
    }
    if (value->kind == TYPE_ALIAS && value->name)
    {
        const char *alias = find_type_alias(tc->pctx, value->name);
        if (alias)
        {
            if (target->name && strcmp(alias, target->name) == 0)
            {
                return 1;
            }
        }
    }

    // String types: str, string, *char are compatible
    if ((target->kind == TYPE_STRING || (target->name && strcmp(target->name, "str") == 0)) &&
        (value->kind == TYPE_STRING || (value->name && strcmp(value->name, "string") == 0)))
    {
        return 1;
    }

    // void* is generic pointer
    if (resolved_target->kind == TYPE_POINTER && resolved_target->inner &&
        resolved_target->inner->kind == TYPE_VOID)
    {
        return 1;
    }
    if (resolved_value->kind == TYPE_POINTER && resolved_value->inner &&
        resolve_alias(resolved_value->inner)->kind == TYPE_VOID)
    {
        return 1;
    }

    // Array decay: Array[T] -> T*
    // This allows passing a fixed-size array where a pointer is expected.
    if (resolved_target->kind == TYPE_POINTER && resolved_value->kind == TYPE_ARRAY)
    {
        if (resolved_target->inner && resolved_value->inner)
        {
            // Recursive check for inner types (e.g. char* <- char[10])
            if (type_eq(resolved_target->inner, resolved_value->inner))
            {
                return 1;
            }
            // Allow char* <- char[N] explicitly if type_eq is too strict
            if (is_char_type(resolved_target->inner) && is_char_type(resolved_value->inner))
            {
                return 1;
            }
        }
    }

    if (is_integer_type(resolved_target) && is_integer_type(resolved_value))
    {
        return 1;
    }

    // Float compatibility
    if (is_float_type(resolved_target) && is_float_type(resolved_value))
    {
        return 1;
    }

    // Trait object compatibility: Struct -> Trait
    if (resolved_target->name && is_trait(resolved_target->name) &&
        resolved_value->kind == TYPE_STRUCT && resolved_value->name)
    {
        if (check_impl(tc->pctx, resolved_target->name, resolved_value->name))
        {
            return 1;
        }
    }

    // Type mismatch - report error
    char *t_str = type_to_string(target);
    char *v_str = type_to_string(value);

    char msg[MAX_ERROR_MSG_LEN];
    snprintf(msg, sizeof(msg), "Type mismatch: expected '%s', but found '%s'", t_str, v_str);

    const char *hints[] = {
        "Check if you need an explicit cast",
        "Ensure the types match exactly (no implicit conversions for strict types)", NULL};

    tc_error_with_hints(tc, t, msg, hints);
    zfree(t_str);
    zfree(v_str);
    return 0;
}

void check_expr_var(TypeChecker *tc, ASTNode *node)
{
    // Check if it's an enum variant FIRST, prioritizing value over function constructor if no
    // payload
    EnumVariantReg *ev = find_enum_variant(tc->pctx, node->var_ref.name);
    if (ev)
    {
        FuncSig *sig = find_func(tc->pctx, node->var_ref.name);
        // If it's a no-payload variant, treat it as an enum value (TYPE_ENUM)
        // If it has payloads, it's a constructor function (TYPE_FUNCTION)
        if (!sig || sig->total_args == 0)
        {
            Type *et = type_new(TYPE_ENUM);
            et->name = xstrdup(ev->enum_name);
            node->type_info = et;
            return;
        }
    }

    ZenSymbol *sym = tc_lookup(tc, node->var_ref.name);
    node->var_ref.symbol = sym; // Store for MISRA audits

    if (sym && sym->type_info)
    {
        // Clone the type to keep metadata (like lifetime_depth) isolated per usage site
        node->type_info = type_clone(sym->type_info);

        // Rule 8.9 tracking: Identify globals used in only one function
        if (!sym->is_local && sym->kind == SYM_VARIABLE && tc->current_func)
        {
            ZenSymbol *orig = sym->original ? sym->original : sym;
            if (orig->first_using_func == NULL)
            {
                orig->first_using_func = tc->current_func;
            }
            else if (orig->first_using_func != tc->current_func)
            {
                orig->multi_func_use = 1;
            }
        }
    }
    else
    {
        // Fallback: Check if it's a mangled function name (e.g. from :: operator or enum
        // constructor)
        FuncSig *sig = find_func(tc->pctx, node->var_ref.name);
        if (sig)
        {
            Type *fn_type = type_new(TYPE_FUNCTION);
            fn_type->is_raw = 1;
            fn_type->inner = sig->ret_type ? sig->ret_type : type_new(TYPE_VOID);
            fn_type->count = sig->total_args;
            if (sig->total_args > 0)
            {
                fn_type->args = xmalloc(sizeof(Type *) * (size_t)(sig->total_args));
                for (int i = 0; i < sig->total_args; i++)
                {
                    fn_type->args[i] = sig->arg_types[i];
                }
            }
            node->type_info = fn_type;
        }
        else if (tc->pctx->config->misra_mode)
        {
            char msg[MAX_SHORT_MSG_LEN];
            snprintf(msg, sizeof(msg), "Undefined variable '%s' (MISRA Rule 17.3)",
                     node->var_ref.name);
            tc_error(tc, node->token, msg);
        }
    }

    if (!tc->is_assign_lhs)
    {
        check_use_validity(tc, node);
    }
}

void check_expr_literal(TypeChecker *tc, ASTNode *node)
{
    (void)tc;
    if (node->type_info && node->type_info->kind != TYPE_UNKNOWN)
    {
        return;
    }

    switch (node->literal.kind)
    {
    case LITERAL_INT:
        node->type_info = type_new(TYPE_I32); // Default to i32
        break;
    case LITERAL_FLOAT:
        node->type_info = type_new(TYPE_F64); // Default to f64
        break;
    case LITERAL_STRING:
    case LITERAL_RAW_STRING:
        node->type_info = type_new(TYPE_STRING);
        break;
    case LITERAL_CHAR:
        node->type_info = type_new(TYPE_CHAR);
        break;
    default:
        break;
    }
}

void check_struct_init(TypeChecker *tc, ASTNode *node, int depth)
{
    if (!node)
    {
        return;
    }
    RECURSION_GUARD_TOKEN(tc->pctx, node->token, );

    // MISRA: Mark struct type as used
    mark_type_as_used(tc, node->type_info);
    if (tc->pctx->config->misra_mode)
    {
        ZenSymbol *struct_sym =
            symbol_lookup_kind(tc->pctx->global_scope, node->struct_init.struct_name, SYM_STRUCT);
        if (struct_sym)
        {
            struct_sym->is_dereferenced = 1;
        }
    }

    // Find struct definition
    ASTNode *def = find_struct_def(tc->pctx, node->struct_init.struct_name);
    if (!def)
    {
        char msg[MAX_SHORT_MSG_LEN];
        snprintf(msg, sizeof(msg), "Unknown struct '%s'", node->struct_init.struct_name);
        tc_error(tc, node->token, msg);
        RECURSION_EXIT(tc->pctx);
        return;
    }

    // Iterate provided fields
    ASTNode *field_init = node->struct_init.fields;
    while (field_init)
    {
        // Rule 9.4: Double initialization check
        if (tc->pctx->config->misra_mode)
        {
            ASTNode *prev = node->struct_init.fields;
            while (prev != field_init)
            {
                if (strcmp(prev->var_decl.name, field_init->var_decl.name) == 0)
                {
                    misra_check_double_initialization(tc->pctx, field_init->var_decl.name,
                                                      field_init->token);
                    break;
                }
                prev = prev->next;
            }
        }

        // Find corresponding field in definition
        ASTNode *def_field = def->strct.fields;
        Type *expected_type = NULL;
        int found = 0;

        while (def_field)
        {
            if (def_field->kind == NODE_FIELD &&
                strcmp(def_field->field.name, field_init->var_decl.name) == 0)
            {
                found = 1;
                expected_type = def_field->type_info;
                break;
            }
            def_field = def_field->next;
        }

        if (found)
        {
            field_init->type_info = expected_type;
        }

        if (found && expected_type && field_init->var_decl.init_expr->kind == NODE_LAMBDA)
        {
            field_init->var_decl.init_expr->type_info = expected_type;
        }

        // Check the initialization expression
        check_node(tc, field_init->var_decl.init_expr, depth + 1);

        if (!found)
        {
            char msg[MAX_SHORT_MSG_LEN];
            snprintf(msg, sizeof(msg), "Struct '%s' has no field named '%s'",
                     node->struct_init.struct_name, field_init->var_decl.name);
            tc_error(tc, field_init->token, msg);
        }
        else if (expected_type && field_init->var_decl.init_expr->type_info)
        {
            // Localize expected field type depth for initialization check.
            // A local struct's fields expect lifetimes compatible with the struct's own scope.
            Type *localized_expected = type_clone(expected_type);
            if (localized_expected && tc->current_func)
            {
                localized_expected->lifetime_depth = tc->current_depth;
            }
            check_type_compatibility(tc, localized_expected,
                                     field_init->var_decl.init_expr->type_info, field_init->token,
                                     NULL, 0);
        }

        // Move Analysis: Check if the initializer moves a non-copy value.
        check_move_for_rvalue(tc, field_init->var_decl.init_expr);

        field_init = field_init->next;
    }

    // Check for missing required fields
    ASTNode *def_field = def->strct.fields;
    while (def_field)
    {
        if (def_field->kind == NODE_FIELD && def_field->field.name)
        {
            int provided = 0;
            ASTNode *fi = node->struct_init.fields;
            while (fi)
            {
                if (fi->var_decl.name && strcmp(fi->var_decl.name, def_field->field.name) == 0)
                {
                    provided = 1;
                    break;
                }
                fi = fi->next;
            }
            if (!provided)
            {
                char msg[MAX_SHORT_MSG_LEN];
                snprintf(msg, sizeof(msg), "Missing field '%s' in initializer for struct '%s'",
                         def_field->field.name, node->struct_init.struct_name);
                const char *hints[] = {"All struct fields must be initialized", NULL};
                tc_error_with_hints(tc, node->token, msg, hints);
            }
        }
        def_field = def_field->next;
    }

    node->type_info = def->type_info;
    RECURSION_EXIT(tc->pctx);
}

void check_expr_lambda(TypeChecker *tc, ASTNode *node, int depth)
{
    Type *expected = get_inner_type(node->type_info);
    if (expected && expected->kind == TYPE_FUNCTION && expected->is_raw)
    {
        if (node->lambda.capture_count == 0)
        {
            node->lambda.is_bare = 1;
        }
        else
        {
            const char *hints[] = {
                "Only non-capturing lambdas can be converted to raw function pointers", NULL};
            tc_error_with_hints(tc, node->token,
                                "Cannot convert capturing lambda to raw function pointer", hints);
        }
    }

    if (node->lambda.captured_vars)
    {
        for (int i = 0; i < node->lambda.capture_count; i++)
        {
            char *var_name = node->lambda.captured_vars[i];
            int mode = node->lambda.capture_modes ? node->lambda.capture_modes[i]
                                                  : node->lambda.default_capture_mode;

            ZenSymbol *sym = tc_lookup(tc, var_name);
            if (!sym)
            {
                continue;
            }

            check_path_validity(tc, var_name, node->token);

            if (mode == 0)
            {
                Type *t = sym->type_info;
                if (!is_type_copy(tc->pctx, t))
                {
                    mark_symbol_moved(tc->pctx, sym, node);
                }
            }
        }
    }

    tc_enter_scope(tc);

    for (int i = 0; i < node->lambda.count; i++)
    {
        char *pname = node->lambda.param_names[i];
        Type *ptype = NULL;
        Type *node_ti = get_inner_type(node->type_info);
        if (node_ti && node_ti->kind == TYPE_FUNCTION && node_ti->args)
        {
            if (i < node_ti->count)
            {
                ptype = node_ti->args[i];
            }
        }
        tc_add_symbol(tc, pname, ptype, node->token, 0);
    }

    // Add captured variables to the scope to ensure visibility and immutability inside the lambda.
    if (node->lambda.captured_vars)
    {
        int saved_silent = tc->pctx->silent_warnings;
        tc->pctx->silent_warnings = 1;
        for (int i = 0; i < node->lambda.capture_count; i++)
        {
            char *var_name = node->lambda.captured_vars[i];
            int mode = node->lambda.capture_modes ? node->lambda.capture_modes[i]
                                                  : node->lambda.default_capture_mode;

            // Lookup original symbol to get its type
            ZenSymbol *orig_sym = tc_lookup(tc, var_name);
            if (orig_sym)
            {
                // Shadow the original variable in the lambda scope
                // Value captures are immutable.
                tc_add_symbol(tc, var_name, orig_sym->type_info, node->token, (mode == 0));
            }
        }
        tc->pctx->silent_warnings = saved_silent;
    }

    MoveState *prev_move_state = tc->pctx->move_state;
    tc->pctx->move_state = move_state_create(NULL);

    int prev_unreachable = tc->is_unreachable;
    tc->is_unreachable = 0;

    if (node->lambda.body)
    {
        if (node->lambda.body->kind == NODE_BLOCK)
        {
            check_block(tc, node->lambda.body, depth + 1);
        }
        else
        {
            check_node(tc, node->lambda.body, depth + 1);
        }
    }

    move_state_free(tc->pctx->move_state);
    tc->pctx->move_state = prev_move_state;

    tc->is_unreachable = prev_unreachable;
    tc_exit_scope(tc);
}

// INFERENCE & ENTRY POINTS
