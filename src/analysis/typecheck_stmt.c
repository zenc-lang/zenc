// SPDX-License-Identifier: MIT
#include "typecheck_internal.h"

void check_block(TypeChecker *tc, ASTNode *block, int depth)
{
    tc_enter_scope(tc);
    ASTNode *stmt = block->block.statements;
    int seen_terminator = 0;
    Token terminator_token = {0};

    while (stmt)
    {
        // Warn if we see code after a terminating statement
        if (seen_terminator && stmt->kind != NODE_LABEL)
        {
            const char *rule = tc->pctx->config->misra_mode ? "MISRA Rule 2.1: " : "";
            const char *hints[] = {"Remove unreachable code or restructure control flow", NULL};
            char msg[256];
            snprintf(msg, sizeof(msg), "%sUnreachable code detected", rule);
            tc_error_with_hints(tc, stmt->token, msg, hints);
            seen_terminator = 0; // Only warn once per block
        }

        if (tc->pctx->config->misra_mode)
        {
            // Rule 2.2: There shall be no dead code (expressions with no effect)
            // We ignore call expressions here as they are handled by Rule 17.7
            if (stmt->kind >= NODE_EXPR_BINARY && stmt->kind <= NODE_EXPR_SLICE &&
                stmt->kind != NODE_EXPR_CALL)
            {
                if (!tc_expr_has_side_effects(stmt))
                {
                    tc_error(tc, stmt->token, "MISRA Rule 2.2: expression statement has no effect");
                }
            }
        }

        int old_stmt_ctx = tc->is_stmt_context;
        tc->is_stmt_context = 1;
        check_node(tc, stmt, depth + 1);
        tc->is_stmt_context = old_stmt_ctx;

        // Track terminating statements
        if (stmt->kind == NODE_RETURN || stmt->kind == NODE_BREAK || stmt->kind == NODE_CONTINUE ||
            stmt->kind == NODE_GOTO)
        {
            seen_terminator = 1;
            terminator_token = stmt->token;
        }

        stmt = stmt->next;
    }
    (void)terminator_token; // May be used for enhanced diagnostics later
    tc_exit_scope(tc);
}

void check_var_decl(TypeChecker *tc, ASTNode *node, int depth)
{
    if (node->var_decl.type_info)
    {
        misra_check_pointer_nesting(tc->pctx, node->var_decl.type_info, node->token);
    }

    // MISRA: Mark type as used
    mark_type_as_used(tc, node->var_decl.type_info);

    // Initialize lifetime depth for escape analysis if this is a local variable
    if (node->var_decl.type_info && tc->current_func)
    {
        node->var_decl.type_info->lifetime_depth = tc->current_depth;
    }

    if (node->var_decl.init_expr)
    {
        if (node->type_info)
        {
            node->var_decl.init_expr->type_info = node->type_info;
        }
        int old_stmt_ctx = tc->is_stmt_context;
        tc->is_stmt_context = 0;
        check_node(tc, node->var_decl.init_expr, depth + 1);
        tc->is_stmt_context = old_stmt_ctx;

        Type *decl_type = node->var_decl.type_info;
        Type *init_type = node->var_decl.init_expr->type_info;

        if (decl_type && init_type)
        {
            apply_implicit_struct_pointer_conversions(tc, &node->var_decl.init_expr, decl_type);
            init_type = node->var_decl.init_expr->type_info;

            // The declared type retains its original lifetime metadata (from init or explicit decl)

            // If initialization exists, check compatibility
            if (node->var_decl.type_info)
            {
                check_type_compatibility(tc, node->var_decl.type_info, init_type, node->token,
                                         node->var_decl.init_expr, 0);
            }

            if (tc->pctx->config->misra_mode)
            {
                misra_check_pointer_conversion(tc->pctx, decl_type, init_type, node->token);

                // Rule 9.3: Arrays shall not be partially initialized.
                if (decl_type->kind == TYPE_ARRAY && init_type->kind == TYPE_ARRAY)
                {
                    if (node->var_decl.init_expr->kind == NODE_EXPR_ARRAY_LITERAL)
                    {
                        if (decl_type->array_size != init_type->array_size)
                        {
                            tc_error(tc, node->token, "MISRA Rule 9.3");
                        }
                    }
                }
            }
        }

        // Move Analysis: Check if the initializer moves a non-copy value.
        check_move_for_rvalue(tc, node->var_decl.init_expr);
    }

    if (node->type_info)
    {
        misra_check_pointer_nesting(tc->pctx, node->var_decl.type_info, node->token);
    }

    // If type is not explicit, we should ideally infer it from init_expr.
    Type *t = node->type_info;
    if (!t && node->var_decl.init_expr)
    {
        t = type_clone(node->var_decl.init_expr->type_info);
        // Ensure inferred local variables get the correct scope depth for escape analysis
        if (t && tc->current_func)
        {
            t->lifetime_depth = tc->current_depth;
        }
        node->type_info = t;
        node->var_decl.type_info = t;
    }

    misra_check_reserved_identifier(tc->pctx, node->var_decl.name, node->token);
    tc_add_symbol(tc, node->var_decl.name, t, node->token, 0);
    ZenSymbol *new_sym = tc_lookup(tc, node->var_decl.name);
    if (new_sym)
    {
        new_sym->is_static = node->var_decl.is_static;
        mark_symbol_valid(tc->pctx, new_sym, node);
    }

    if (tc->pctx->config->misra_mode && t && t->kind == TYPE_ARRAY)
    {
        // Rule 8.11: Array with external linkage shall have explicit size
        ZenSymbol *existing = tc_lookup(tc, node->var_decl.name);
        int is_static = (existing && existing->is_static) || (node->var_decl.is_static);
        int is_local = (existing && existing->is_local) || (tc->current_func != NULL);

        misra_check_external_array_size(tc->pctx, t, node->token, is_static, is_local);

        // Rule 18.8: No variable length arrays
        // In Zen C, all [T; N] arrays have constant size N, so Rule 18.8 is satisfied.
        // We only report if Zen somehow allowed non-constant sizes (which it doesn't in fixed-size
        // arrays).
    }
}

static ASTNode *loop_body(ASTNode *loop)
{
    switch (loop->kind)
    {
    case NODE_LOOP:
        return loop->loop_stmt.body;
    case NODE_WHILE:
        return loop->while_stmt.body;
    case NODE_FOR:
        return loop->for_stmt.body;
    case NODE_FOR_RANGE:
        return loop->for_range.body;
    case NODE_REPEAT:
        return loop->repeat_stmt.body;
    case NODE_DO_WHILE:
        return loop->do_while_stmt.body;
    default:
        return NULL;
    }
}

// Returns 1 if `node` contains a `break` that can exit the loop whose label is
// `loop_label` (NULL = unlabeled). `nested` is 1 while inside a loop nested
// within the loop we are testing, so unlabeled breaks there do not count.
static int stmt_has_exit_break(ASTNode *node, const char *loop_label, int nested)
{
    if (!node)
    {
        return 0;
    }

    switch (node->kind)
    {
    case NODE_BREAK:
        if (node->break_stmt.target_label)
        {
            return loop_label != NULL && strcmp(node->break_stmt.target_label, loop_label) == 0;
        }
        return !nested;

    case NODE_BLOCK:
        for (ASTNode *s = node->block.statements; s; s = s->next)
        {
            if (stmt_has_exit_break(s, loop_label, nested))
            {
                return 1;
            }
        }
        return 0;

    case NODE_IF:
        return stmt_has_exit_break(node->if_stmt.then_body, loop_label, nested) ||
               stmt_has_exit_break(node->if_stmt.else_body, loop_label, nested);

    case NODE_MATCH:
        for (ASTNode *c = node->match_stmt.cases; c; c = c->next)
        {
            if (stmt_has_exit_break(c, loop_label, nested))
            {
                return 1;
            }
        }
        return 0;

    case NODE_MATCH_CASE:
        return stmt_has_exit_break(node->match_case.body, loop_label, nested);

    case NODE_GUARD:
        return stmt_has_exit_break(node->guard_stmt.body, loop_label, nested);
    case NODE_UNLESS:
        return stmt_has_exit_break(node->unless_stmt.body, loop_label, nested);

    case NODE_DEFER:
        return stmt_has_exit_break(node->defer_stmt.stmt, loop_label, nested);

    case NODE_LOOP:
    case NODE_WHILE:
    case NODE_FOR:
    case NODE_FOR_RANGE:
    case NODE_REPEAT:
    case NODE_DO_WHILE:
        // Nested loop: unlabeled breaks here target the nested loop, but a
        // labeled break naming our loop still exits ours.
        return stmt_has_exit_break(loop_body(node), loop_label, 1);

    case NODE_RETURN:
    case NODE_CONTINUE:
    default:
        return 0;
    }
}

// Returns 1 if `loop` (a NODE_LOOP) contains a `break` that can exit it.
static int stmt_loop_has_exit_break(ASTNode *loop)
{
    return stmt_has_exit_break(loop->loop_stmt.body, loop->loop_stmt.loop_label, 0);
}

int stmt_always_returns(ASTNode *stmt)
{
    if (!stmt)
    {
        return 0;
    }

    switch (stmt->kind)
    {
    case NODE_RETURN:
        return 1;

    case NODE_BLOCK:
        return block_always_returns(stmt);

    case NODE_IF:
        // Both branches must return for if to always return
        if (stmt->if_stmt.then_body && stmt->if_stmt.else_body)
        {
            return stmt_always_returns(stmt->if_stmt.then_body) &&
                   stmt_always_returns(stmt->if_stmt.else_body);
        }
        return 0;

    case NODE_MATCH:
    {
        if (!stmt->match_stmt.cases)
        {
            return 0;
        }

        int has_default = 0;
        ASTNode *case_node = stmt->match_stmt.cases;
        while (case_node)
        {
            if (case_node->kind == NODE_MATCH_CASE)
            {
                if (!stmt_always_returns(case_node->match_case.body))
                {
                    return 0;
                }
                if (case_node->match_case.is_default)
                {
                    has_default = 1;
                }
            }
            case_node = case_node->next;
        }

        return has_default;
    }

    case NODE_LOOP:
        // An infinite `loop` only leaves via `break` or `return`. With no
        // exit-break it can never fall through (it returns from within or
        // diverges), so it satisfies the always-returns requirement. This
        // mirrors the move analysis, which already treats a break-less
        // infinite loop as unreachable-after.
        return !stmt_loop_has_exit_break(stmt);

    default:
        return 0;
    }
}

int block_always_returns(ASTNode *block)
{
    if (!block || block->kind != NODE_BLOCK)
    {
        return 0;
    }

    ASTNode *stmt = block->block.statements;
    if (!stmt)
    {
        return 0;
    }

    // Walk all statements except the last
    while (stmt->next)
    {
        if (stmt_always_returns(stmt))
        {
            return 1;
        }
        stmt = stmt->next;
    }

    // The last statement is the implicit return value.
    // Check if it always returns (RETURN, IF with both branches, MATCH with default),
    // or if it's an expression (which serves as the function's return value).
    if (stmt_always_returns(stmt))
    {
        return 1;
    }
    if (stmt->kind == NODE_BLOCK)
    {
        return block_always_returns(stmt);
    }
    // Any expression type can serve as an implicit return value
    if (stmt->kind == NODE_EXPR_BINARY || stmt->kind == NODE_EXPR_UNARY ||
        stmt->kind == NODE_EXPR_LITERAL || stmt->kind == NODE_EXPR_VAR ||
        stmt->kind == NODE_EXPR_CALL || stmt->kind == NODE_EXPR_MEMBER ||
        stmt->kind == NODE_EXPR_INDEX || stmt->kind == NODE_EXPR_CAST ||
        stmt->kind == NODE_EXPR_SIZEOF || stmt->kind == NODE_EXPR_STRUCT_INIT ||
        stmt->kind == NODE_EXPR_ARRAY_LITERAL || stmt->kind == NODE_EXPR_TUPLE_LITERAL ||
        stmt->kind == NODE_EXPR_SLICE || stmt->kind == NODE_TERNARY || stmt->kind == NODE_MATCH)
    {
        return 1;
    }
    return 0;
}

void check_function(TypeChecker *tc, ASTNode *node, int depth)
{
    if (!node)
    {
        return;
    }
    misra_check_param_nesting(tc->pctx, node);
    // Mark arg types as used
    for (int i = 0; i < node->func.count; i++)
    {
        mark_type_as_used(tc, node->func.arg_types[i]);
    }

    // Mark return type as used
    mark_type_as_used(tc, node->func.ret_type_info);

    // Rule Zen 1.4: Reserved identifiers
    misra_check_reserved_identifier(tc->pctx, node->func.name, node->token);

    tc->current_func = node;
    tc_enter_scope(tc);

    int prev_unreachable = tc->is_unreachable;
    tc->is_unreachable = 0;
    tc->func_return_count = 0;

    MoveState *prev_move_state = tc->pctx->move_state;
    tc->pctx->move_state = move_state_create(NULL);

    for (int i = 0; i < node->func.count; i++)
    {
        if (node->func.param_names && node->func.param_names[i])
        {
            Type *param_type =
                (node->func.arg_types && node->func.arg_types[i]) ? node->func.arg_types[i] : NULL;

            misra_check_tuple_size(tc->pctx, param_type, node->token);
            misra_check_pointer_nesting(tc->pctx, param_type, node->token);
            misra_check_reserved_identifier(tc->pctx, node->func.param_names[i], node->token);
            tc_add_symbol(tc, node->func.param_names[i], param_type, node->token,
                          tc->pctx->config->misra_mode);
        }
    }

    if (node->func.ret_type_info)
    {
        misra_check_tuple_size(tc->pctx, node->func.ret_type_info, node->token);
        misra_check_pointer_nesting(tc->pctx, node->func.ret_type_info, node->token);

        // Lifetime Elision result was already computed in pre-pass for named functions.
        // For lambdas or if it somehow missed the pre-pass, ensure it's set.
        if (node->func.elide_from_idx == -1)
        {
            // Simple re-run if needed
        }
    }

    check_node(tc, node->func.body, depth + 1);

    // Control flow analysis: Check if non-void function always returns
    const char *ret_type = node->func.ret_type;
    int is_void = !ret_type || strcmp(ret_type, "void") == 0;

    // Special case: 'main' is allowed to fall off the end (C99 implicit return 0)
    int is_main = node->func.name && strcmp(node->func.name, "main") == 0;

    if (is_main && is_void)
    {
        warn_void_main(node->token);
    }

    if (!is_void && !is_main && node->func.body)
    {
        if (!block_always_returns(node->func.body))
        {
            char msg[MAX_SHORT_MSG_LEN];
            snprintf(msg, sizeof(msg), "Function '%s' may not return a value on all code paths",
                     node->func.name);

            const char *hints[] = {"Ensure all execution paths return a value",
                                   "Consider adding a default return at the end of the function",
                                   NULL};
            tc_ctrl_flow_error(tc, node->token, msg, hints);
        }
    }

    move_state_free(tc->pctx->move_state);
    tc->pctx->move_state = prev_move_state;

    // MISRA audits before leaving function scope
    if (tc->pctx->config->misra_mode && tc->pctx->current_scope)
    {
        for (int i = 0; i < node->func.count; i++)
        {
            if (node->func.param_names && node->func.param_names[i])
            {
                ZenSymbol *psym =
                    symbol_lookup_local(tc->pctx->current_scope, node->func.param_names[i]);
                if (psym)
                {
                    // Rule 2.7: Unused parameter
                    if (!psym->is_used)
                    {
                        misra_check_unused_param(tc->pctx, psym->name, psym->decl_token);
                    }
                    // Rule 8.13: Pointer to const
                    if (psym->type_info && psym->type_info->kind == TYPE_POINTER)
                    {
                        Type *inner = resolve_alias(psym->type_info->inner);
                        if (inner && !inner->is_const && !psym->type_info->is_const &&
                            !psym->is_written_to)
                        {
                            misra_check_const_ptr_param(tc->pctx, psym->name, psym->decl_token);
                        }
                    }
                }
            }
        }
    }

    // MISRA Rule 15.5: A function shall have a single point of exit at the end.
    if (tc->pctx->config->misra_mode && tc->func_return_count > 1)
    {
        char msg[MAX_ERROR_MSG_LEN];
        snprintf(msg, sizeof(msg),
                 "MISRA Rule 15.5: function '%s' has %d return points (must have 1)",
                 node->func.name ? node->func.name : "anonymous", tc->func_return_count);
        tc_error(tc, node->token, msg);
    }

    tc->is_unreachable = prev_unreachable;
    tc_exit_scope(tc);
    tc->current_func = NULL;
}

void tc_check_trait(TypeChecker *tc, ASTNode *node, int depth)
{
    ASTNode *method = node->trait.methods;
    while (method)
    {
        check_node(tc, method, depth + 1);
        method = method->next;
    }
}

void tc_check_impl(TypeChecker *tc, ASTNode *node, int depth)
{
    // Skip templates
    if (node->impl.struct_name && strchr(node->impl.struct_name, '<'))
    {
        return;
    }

    ASTNode *method = node->impl.methods;
    while (method)
    {
        check_node(tc, method, depth + 1);
        method = method->next;
    }
}

void tc_check_impl_trait(TypeChecker *tc, ASTNode *node, int depth)
{
    // Skip templates
    if (node->impl_trait.target_type && strchr(node->impl_trait.target_type, '<'))
    {
        return;
    }

    ASTNode *method = node->impl_trait.methods;
    while (method)
    {
        check_node(tc, method, depth + 1);
        method = method->next;
    }
}

void check_loop_passes(TypeChecker *tc, ASTNode *node, int depth)
{
    if (!node)
    {
        return;
    }
    RECURSION_GUARD_TOKEN(tc->pctx, node->token, );
    if (depth > 1024)
    {
        tc_error(tc, node->token, "Expression too deep");
        RECURSION_EXIT(tc->pctx);
        return;
    }

    MoveState *prev_break = tc->loop_break_state;
    MoveState *prev_cont = tc->loop_continue_state;
    tc->loop_break_state = NULL;
    tc->loop_continue_state = NULL;

    MoveState *initial_state = tc->pctx->move_state;
    MoveState *loop_start = initial_state ? move_state_clone(initial_state) : NULL;
    MoveState *outer_start_state = tc->loop_start_state;
    tc->loop_start_state = loop_start;

    int outer_in_pass2 = tc->in_loop_pass2;
    tc->in_loop_pass2 = 0;

    int outer_break_count = tc->loop_break_count;
    tc->loop_break_count = 0;

    int initial_unreachable = tc->is_unreachable;

    // Pass 1: standard typecheck and move check
    tc->is_unreachable = 0; // The loop start is assumed reachable if we got here

    switch (node->kind)
    {
    case NODE_WHILE:
    {
        misra_check_compound_body(tc->pctx, node->while_stmt.body, "while");
        int old_stmt_ctx = tc->is_stmt_context;
        tc->is_stmt_context = 0;
        check_node(tc, node->while_stmt.condition, depth + 1);
        tc->is_stmt_context = old_stmt_ctx;

        if (node->while_stmt.condition && node->while_stmt.condition->type_info)
        {
            Type *cond_type = resolve_alias(node->while_stmt.condition->type_info);
            if (tc->pctx->config->misra_mode)
            {
                if (cond_type->kind != TYPE_BOOL)
                {
                    misra_check_condition_boolean(tc->pctx, node->while_stmt.condition->type_info,
                                                  node->while_stmt.condition->token);
                }
                int inv;
                if (is_expression_invariant(tc, node->while_stmt.condition, &inv))
                {
                    misra_check_invariant_condition(tc->pctx, node->while_stmt.condition->token);
                }
            }
            else if (cond_type->kind != TYPE_BOOL && !is_integer_type(cond_type) &&
                     cond_type->kind != TYPE_POINTER && cond_type->kind != TYPE_STRING)
            {
                const char *hints[] = {"While conditions must be boolean, integer, or pointer",
                                       NULL};
                tc_error_with_hints(tc, node->while_stmt.condition->token,
                                    "Condition must be a truthy type", hints);
            }
        }
        check_node(tc, node->while_stmt.body, depth + 1);
        break;
    }

    case NODE_FOR:
        misra_check_compound_body(tc->pctx, node->for_stmt.body, "for");
        tc_enter_scope(tc); // For loop init variable is scoped
        check_node(tc, node->for_stmt.init, depth + 1);

        // Loop start is conceptually here for FOR
        if (loop_start)
        {
            move_state_free(loop_start);
        }
        loop_start = tc->pctx->move_state ? move_state_clone(tc->pctx->move_state) : NULL;
        tc->loop_start_state = loop_start;

        check_node(tc, node->for_stmt.condition, depth + 1);
        if (node->for_stmt.condition && node->for_stmt.condition->type_info)
        {
            Type *cond_type = resolve_alias(node->for_stmt.condition->type_info);
            if (tc->pctx->config->misra_mode)
            {
                if (cond_type->kind != TYPE_BOOL)
                {
                    misra_check_condition_boolean(tc->pctx, node->for_stmt.condition->type_info,
                                                  node->for_stmt.condition->token);
                }
                int inv;
                if (is_expression_invariant(tc, node->for_stmt.condition, &inv))
                {
                    misra_check_invariant_condition(tc->pctx, node->for_stmt.condition->token);
                }
            }
            else if (cond_type->kind != TYPE_BOOL && !is_integer_type(cond_type) &&
                     cond_type->kind != TYPE_POINTER && cond_type->kind != TYPE_STRING)
            {
                const char *hints[] = {"For conditions must be boolean, integer, or pointer", NULL};
                tc_error_with_hints(tc, node->for_stmt.condition->token,
                                    "Condition must be a truthy type", hints);
            }
        }
        check_node(tc, node->for_stmt.body, depth + 1);
        check_node(tc, node->for_stmt.step, depth + 1); // step happens after body

        if (tc->pctx->config->misra_mode && node->for_stmt.step)
        {
            if (node->for_stmt.step->kind == NODE_EXPR_BINARY)
            {
                const char *step_op = node->for_stmt.step->binary.op;
                if (strstr(step_op, "=") && node->for_stmt.step->binary.left->type_info)
                {
                    misra_check_loop_counter_float(tc->pctx,
                                                   node->for_stmt.step->binary.left->type_info,
                                                   node->for_stmt.step->token);
                }
            }
        }
        break;

    case NODE_FOR_RANGE:
        check_node(tc, node->for_range.start, depth + 1);
        check_node(tc, node->for_range.end, depth + 1);

        // Loop start conceptually here
        if (loop_start)
        {
            move_state_free(loop_start);
        }
        loop_start = tc->pctx->move_state ? move_state_clone(tc->pctx->move_state) : NULL;
        tc->loop_start_state = loop_start;

        check_node(tc, node->for_range.body, depth + 1);
        break;

    case NODE_LOOP:
        check_node(tc, node->loop_stmt.body, depth + 1);
        break;

    case NODE_REPEAT:
        check_node(tc, node->repeat_stmt.body, depth + 1);
        break;

    case NODE_DO_WHILE:
    {
        misra_check_compound_body(tc->pctx, node->do_while_stmt.body, "do-while");
        int old_stmt_ctx = tc->is_stmt_context;
        tc->is_stmt_context = 0;
        check_node(tc, node->do_while_stmt.body, depth + 1);
        check_node(tc, node->do_while_stmt.condition, depth + 1);
        tc->is_stmt_context = old_stmt_ctx;

        if (node->do_while_stmt.condition && node->do_while_stmt.condition->type_info)
        {
            Type *cond_type = resolve_alias(node->do_while_stmt.condition->type_info);
            if (tc->pctx->config->misra_mode)
            {
                if (cond_type->kind != TYPE_BOOL)
                {
                    misra_check_condition_boolean(tc->pctx,
                                                  node->do_while_stmt.condition->type_info,
                                                  node->do_while_stmt.condition->token);
                }
                int inv;
                if (is_expression_invariant(tc, node->do_while_stmt.condition, &inv))
                {
                    misra_check_invariant_condition(tc->pctx, node->do_while_stmt.condition->token);
                }
            }
            else if (cond_type->kind != TYPE_BOOL && !is_integer_type(cond_type) &&
                     cond_type->kind != TYPE_POINTER && cond_type->kind != TYPE_STRING)
            {
                const char *hints[] = {"Do-while conditions must be boolean, integer, or pointer",
                                       NULL};
                tc_error_with_hints(tc, node->do_while_stmt.condition->token,
                                    "Condition must be a truthy type", hints);
            }
        }
        break;
    default:
        break;
    }
    }

    tc->loop_break_count = 0; // Reset for pass 2 (avoid double counts)

    // Determine next iter state based on continue and fallthrough
    MoveState *fallthrough_state = tc->pctx->move_state;
    int fallthrough_unreachable = tc->is_unreachable;

    MoveState *next_iter_state = NULL;
    if (!fallthrough_unreachable && fallthrough_state)
    {
        move_state_merge_into(&next_iter_state, fallthrough_state);
    }
    if (tc->loop_continue_state)
    {
        move_state_merge_into(&next_iter_state, tc->loop_continue_state);
    }

    // Pass 2: Re-run with next_iter_state to catch use-after-move across iterations
    if (next_iter_state)
    {
        int prev_move_checks_only = tc->move_checks_only;
        tc->move_checks_only = 1; // suppress type errors
        tc->in_loop_pass2 = 1;

        tc->pctx->move_state = move_state_clone(next_iter_state);
        tc->is_unreachable = 0;

        tc->loop_break_state = NULL;
        tc->loop_continue_state = NULL;

        // Re-run appropriate parts
        switch (node->kind)
        {
        case NODE_WHILE:
            check_node(tc, node->while_stmt.condition, depth + 1);
            check_node(tc, node->while_stmt.body, depth + 1);
            break;
        case NODE_FOR:
            check_node(tc, node->for_stmt.condition, depth + 1);
            check_node(tc, node->for_stmt.body, depth + 1);
            check_node(tc, node->for_stmt.step, depth + 1);
            break;
        case NODE_FOR_RANGE:
            check_node(tc, node->for_range.body, depth + 1);
            break;
        case NODE_LOOP:
            check_node(tc, node->loop_stmt.body, depth + 1);
            break;
        case NODE_REPEAT:
            check_node(tc, node->repeat_stmt.body, depth + 1);
            break;
        case NODE_DO_WHILE:
            misra_check_compound_body(tc->pctx, node->do_while_stmt.body, "do-while");
            check_node(tc, node->do_while_stmt.body, depth + 1);
            check_node(tc, node->do_while_stmt.condition, depth + 1);
            break;
        default:
            break;
        }

        if (tc->pctx->move_state)
        {
            move_state_free(tc->pctx->move_state);
        }
        if (tc->loop_break_state)
        {
            move_state_free(tc->loop_break_state);
        }
        if (tc->loop_continue_state)
        {
            move_state_free(tc->loop_continue_state);
        }

        tc->move_checks_only = prev_move_checks_only;
    }

    // Compute final move state exiting the loop
    MoveState *final_state = NULL;
    // Loops can exit via condition falsification (next_iter_state) or breaks
    if (next_iter_state)
    {
        // Assume infinite loops (like NODE_LOOP) don't exit naturally unless broken,
        // but for safety we'll merge next_iter_state for all, treating condition as maybe false.
        if (node->kind != NODE_LOOP)
        {
            move_state_merge_into(&final_state, next_iter_state);
        }
    }
    if (tc->loop_break_state)
    {
        move_state_merge_into(&final_state, tc->loop_break_state);
    }

    // Cleanup Pass 1 states
    if (tc->loop_break_state)
    {
        move_state_free(tc->loop_break_state);
    }
    if (tc->loop_continue_state)
    {
        move_state_free(tc->loop_continue_state);
    }

    tc->loop_break_count = outer_break_count;
    if (next_iter_state)
    {
        move_state_free(next_iter_state);
    }
    if (loop_start)
    {
        move_state_free(loop_start);
    }

    // Restore outer context
    if (node->kind == NODE_FOR)
    {
        tc_exit_scope(tc);
    }

    // If the loop is an infinite loop and has no breaks, it is unconditionally unreachable after.
    if ((node->kind == NODE_LOOP || node->kind == NODE_REPEAT) && !final_state)
    {
        tc->is_unreachable = 1;
    }
    else if (final_state)
    {
        tc->is_unreachable = 0;
    }
    else
    {
        tc->is_unreachable = initial_unreachable;
    }

    if (tc->pctx->move_state)
    {
        move_state_free(tc->pctx->move_state);
    }
    tc->pctx->move_state = final_state ? final_state : initial_state;

    tc->loop_break_state = prev_break;
    tc->loop_continue_state = prev_cont;
    tc->loop_start_state = outer_start_state;
    tc->in_loop_pass2 = outer_in_pass2;
    RECURSION_EXIT(tc->pctx);
}

// MAIN DISPATCH: check_node
