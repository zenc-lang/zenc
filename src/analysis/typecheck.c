// SPDX-License-Identifier: MIT
#include "../utils/colors.h"
#include "typecheck_internal.h"
#include "../constants.h"

#include "typecheck.h"
#include "comptime_interpreter.h"
#include "diagnostics/diagnostics.h"
#include "move_check.h"
#include "platform/misra.h"
#include <ctype.h>
#include <string.h>

// External helpers from parser

// ** Internal Helpers **

void check_node(TypeChecker *tc, ASTNode *node, int depth)
{
    if (!node || !tc)
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

    switch (node->kind)
    {
    case NODE_ROOT:
    {
        ASTNode *child = node->root.children;
        while (child)
        {
            check_node(tc, child, depth + 1);
            child = child->next;
        }
    }
    break;
    case NODE_BLOCK:
        check_block(tc, node, depth + 1);
        break;
    case NODE_VAR_DECL:
        check_var_decl(tc, node, depth + 1);
        break;
    case NODE_FUNCTION:
        check_function(tc, node, depth + 1);
        break;
    case NODE_TRAIT:
        tc_check_trait(tc, node, depth + 1);
        break;
    case NODE_IMPL:
        tc_check_impl(tc, node, depth + 1);
        break;
    case NODE_IMPL_TRAIT:
        tc_check_impl_trait(tc, node, depth + 1);
        break;
    case NODE_IMPORT:
        check_node(tc, node->import_stmt.module_root, depth + 1);
        break;
    case NODE_EXPR_VAR:
        check_expr_var(tc, node);
        break;
    case NODE_EXPR_LITERAL:
        check_expr_literal(tc, node);
        break;
    case NODE_RETURN:
        tc->func_return_count++;
        if (node->ret.value)
        {
            check_node(tc, node->ret.value, depth + 1);
        }
        // Check return type compatibility with function
        if (tc->current_func)
        {
            const char *ret_type = tc->current_func->func.ret_type;
            int func_is_void = !ret_type || strcmp(ret_type, "void") == 0;

            if (func_is_void && node->ret.value)
            {
                tc_error(tc, node->token, "Return with value in void function");
            }
            else if (!func_is_void && !node->ret.value)
            {
                char msg[MAX_SHORT_MSG_LEN];
                const char *rule = tc->pctx->config->misra_mode ? "MISRA Rule 2.1: " : "";
                snprintf(msg, sizeof(msg), "%sReturn without value in function returning '%s'",
                         rule, ret_type);
                tc_error(tc, node->token, msg);
            }
            else if (node->ret.value && tc->current_func->func.ret_type_info)
            {
                apply_implicit_struct_pointer_conversions(tc, &node->ret.value,
                                                          tc->current_func->func.ret_type_info);
                check_type_compatibility(tc, tc->current_func->func.ret_type_info,
                                         node->ret.value->type_info, node->token, node->ret.value,
                                         0);
            }
        }
        tc->is_unreachable = 1;
        break;

    // Control flow with nested nodes.
    case NODE_IF:
    {
        int old_stmt_ctx = tc->is_stmt_context;
        tc->is_stmt_context = 0;
        check_node(tc, node->if_stmt.condition, depth + 1);
        tc->is_stmt_context = old_stmt_ctx;

        // Validate condition is boolean-compatible
        if (node->if_stmt.condition && node->if_stmt.condition->type_info)
        {
            Type *cond_type = resolve_alias(node->if_stmt.condition->type_info);
            if (tc->pctx->config->misra_mode)
            {
                misra_check_condition_boolean(tc->pctx, node->if_stmt.condition->type_info,
                                              node->if_stmt.condition->token);
                int inv;
                if (is_expression_invariant(tc, node->if_stmt.condition, &inv))
                {
                    misra_check_invariant_condition(tc->pctx, node->if_stmt.condition->token);
                }
            }
            else if (cond_type->kind != TYPE_BOOL && !is_integer_type(cond_type) &&
                     cond_type->kind != TYPE_POINTER && cond_type->kind != TYPE_STRING)
            {
                const char *hints[] = {"If conditions must be boolean, integer, or pointer", NULL};
                tc_error_with_hints(tc, node->if_stmt.condition->token,
                                    "Condition must be a truthy type", hints);
            }
        }

        MoveState *initial_state = tc->pctx->move_state;
        int initial_unreachable = tc->is_unreachable;

        misra_check_compound_body(tc->pctx, node->if_stmt.then_body, "if");
        if (node->if_stmt.else_body)
        {
            if (node->if_stmt.else_body->kind == NODE_IF)
            {
                misra_check_terminal_else(tc->pctx, node);
            }
            else
            {
                misra_check_compound_body(tc->pctx, node->if_stmt.else_body, "else");
            }
        }

        if (initial_state)
        {
            tc->pctx->move_state = move_state_clone(initial_state);
        }
        check_node(tc, node->if_stmt.then_body, depth + 1);
        MoveState *after_then = tc->pctx->move_state;
        int then_unreachable = tc->is_unreachable;

        MoveState *after_else = NULL;
        int else_unreachable = initial_unreachable;
        tc->is_unreachable = initial_unreachable; // Reset for else branch

        if (node->if_stmt.else_body)
        {
            if (initial_state)
            {
                tc->pctx->move_state = move_state_clone(initial_state);
            }
            check_node(tc, node->if_stmt.else_body, depth + 1);
            after_else = tc->pctx->move_state;
            else_unreachable = tc->is_unreachable;
        }

        tc->pctx->move_state = initial_state;

        if (initial_state)
        {
            MoveState *merge_a = then_unreachable ? NULL : after_then;
            MoveState *merge_b =
                else_unreachable ? NULL : (node->if_stmt.else_body ? after_else : initial_state);

            // Only merge reachable paths
            move_state_merge(initial_state, merge_a, merge_b);

            if (after_then)
            {
                move_state_free(after_then);
            }
            if (after_else)
            {
                move_state_free(after_else);
            }
        }

        tc->is_unreachable = then_unreachable && else_unreachable;
        break;
    }
    case NODE_MATCH:
        check_node(tc, node->match_stmt.expr, depth + 1);
        misra_check_match_stmt(tc->pctx, node);
        // Visit each match case
        {
            MoveState *match_initial_state = tc->pctx->move_state;
            MoveState *merged_state = NULL;
            int match_initial_unreachable = tc->is_unreachable;
            int all_unreachable = 1;

            ASTNode *mcase = node->match_stmt.cases;
            int has_default = 0;
            int clause_count = 0;
            (void)clause_count;

            while (mcase)
            {
                if (mcase->kind == NODE_MATCH_CASE)
                {
                    if (mcase->match_case.is_default)
                    {
                        has_default = 1;
                    }
                    clause_count++;

                    if (match_initial_state)
                    {
                        tc->pctx->move_state = move_state_clone(match_initial_state);
                    }
                    tc->is_unreachable = match_initial_unreachable;

                    tc_enter_scope(tc);
                    if (mcase->match_case.binding_count > 0)
                    {
                        for (int i = 0; i < mcase->match_case.binding_count; i++)
                        {
                            char *bname = mcase->match_case.binding_names[i];
                            if (bname)
                            {
                                // For now, we use UNSAFE_ANY as the binding type
                                // In a more complete implementation, we'd infer it from the enum
                                // payload
                                Type *bt = type_new(TYPE_UNSAFE_ANY);
                                if (mcase->match_case.binding_refs &&
                                    mcase->match_case.binding_refs[i])
                                {
                                    bt = type_new_ptr(bt);
                                }
                                tc_add_symbol(tc, bname, bt, mcase->token, 0);
                            }
                        }
                    }

                    check_node(tc, mcase->match_case.body, depth + 1);
                    tc_exit_scope(tc);

                    // MISRA Rule 16.3: An unconditional break or return shall terminate every
                    // switch-clause
                    if (tc->pctx->config->misra_mode && !tc->is_unreachable &&
                        mcase->match_case.body && mcase->match_case.body->kind == NODE_BLOCK &&
                        mcase->match_case.body->block.statements)
                    {
                        tc_error(tc, mcase->token,
                                 "MISRA Rule 16.3: match case must end in break or return");
                    }

                    if (!tc->is_unreachable)
                    {
                        all_unreachable = 0;
                        if (tc->pctx->move_state)
                        {
                            move_state_merge_into(&merged_state, tc->pctx->move_state);
                        }
                    }

                    if (tc->pctx->move_state && tc->pctx->move_state != match_initial_state)
                    {
                        move_state_free(tc->pctx->move_state);
                    }
                }
                mcase = mcase->next;
            }

            if (!has_default)
            {
                all_unreachable = 0;
                if (match_initial_state)
                {
                    move_state_merge_into(&merged_state, match_initial_state);
                }

                if (!tc->pctx->config->misra_mode)
                {
                    const char *hints[] = {"Add a default '_' case to handle all possibilities",
                                           NULL};
                    tc_error_with_hints(tc, node->token,
                                        "Match may not be exhaustive (no default case)", hints);
                }

                misra_check_match_stmt(tc->pctx, node);
            }

            if (match_initial_state && merged_state)
            {
                tc->pctx->move_state = merged_state;
            }
            else if (!merged_state)
            {
                tc->pctx->move_state = match_initial_state;
            }

            tc->is_unreachable = all_unreachable;
        }
        break;
    case NODE_STRUCT:
    case NODE_ENUM:
    case NODE_TYPE_ALIAS:
        if (node->kind == NODE_STRUCT)
        {
            misra_check_reserved_identifier(tc->pctx, node->strct.name, node->token);
            misra_check_struct_decl(tc->pctx, node);
            if (node->strct.is_union)
            {
                misra_check_union(tc->pctx, node->token);
            }
        }
        else if (node->kind == NODE_ENUM)
        {
            misra_check_reserved_identifier(tc->pctx, node->enm.name, node->token);
        }
        else if (node->kind == NODE_TYPE_ALIAS)
        {
            misra_check_reserved_identifier(tc->pctx, node->type_alias.alias, node->token);
        }
        break;
    case NODE_WHILE:
    case NODE_FOR:
        check_loop_passes(tc, node, depth + 1);
        break;
    case NODE_EXPR_BINARY:
        check_expr_binary(tc, node, depth + 1);
        break;
    case NODE_EXPR_UNARY:
        check_expr_unary(tc, node, depth + 1);
        break;
    case NODE_EXPR_CALL:
        check_expr_call(tc, node, depth + 1);
        break;
    case NODE_EXPR_INDEX:
        check_node(tc, node->index.array, depth + 1);
        check_node(tc, node->index.index, depth + 1);

        if (node->index.array->type_info)
        {
            Type *t = node->index.array->type_info;
            int is_ptr = 0;
            if (t->kind == TYPE_POINTER && t->inner && t->inner->kind == TYPE_STRUCT)
            {
                t = t->inner;
                is_ptr = 1;
            }

            // Pointers must use direct array indexing (base[i]), not the
            // __index/__get operator overload: taking the address of the
            // overload's return value is not an lvalue. This matches the
            // parser/codegen policy ("Pointers should use array indexing by
            // default, not operator overload").
            if (t->kind == TYPE_STRUCT && t->name && !is_ptr)
            {
                size_t tname_len = strlen(t->name);
                char *mangled_idx = xmalloc(tname_len + sizeof("__index"));
                snprintf(mangled_idx, tname_len + sizeof("__index"), "%s__index", t->name);
                char *mangled_get = xmalloc(tname_len + sizeof("__get"));
                snprintf(mangled_get, tname_len + sizeof("__get"), "%s__get", t->name);

                FuncSig *sig = find_func(tc->pctx, mangled_idx);
                char *method_name = NULL;
                if (sig)
                {
                    method_name = "index";
                }
                else
                {
                    sig = find_func(tc->pctx, mangled_get);
                    if (sig)
                    {
                        method_name = "get";
                    }
                }

                if (method_name)
                {
                    ASTNode *array = node->index.array;
                    ASTNode *idx = node->index.index;

                    node->kind = NODE_EXPR_CALL;
                    memset(&node->call, 0, sizeof(node->call));

                    ASTNode *callee = ast_create(NODE_EXPR_MEMBER);
                    callee->token = node->token;
                    callee->member.target = array;
                    callee->member.field = xstrdup(method_name);
                    callee->member.is_pointer_access = is_ptr;

                    node->call.callee = callee;
                    node->call.args = idx;

                    check_expr_call(tc, node, depth + 1);
                    zfree(mangled_idx);
                    zfree(mangled_get);
                    break;
                }
                zfree(mangled_idx);
                zfree(mangled_get);
            }
            if (t->kind == TYPE_ARRAY || t->kind == TYPE_POINTER || t->kind == TYPE_VECTOR)
            {
                if (t->kind == TYPE_VECTOR && !t->inner && t->name)
                {
                    ASTNode *def = find_struct_def(tc->pctx, t->name);
                    if (def && def->kind == NODE_STRUCT && def->strct.fields)
                    {
                        t->inner = def->strct.fields->type_info;
                    }
                }
                // Propagate lifetime from array/slice to the indexed element
                node->type_info = type_clone(t->inner);
                if (node->type_info && node->index.array->type_info)
                {
                    node->type_info->lifetime_depth = node->index.array->type_info->lifetime_depth;
                }
            }
        }

        // Validate index is integer
        if (node->index.index && node->index.index->type_info)
        {
            if (!is_integer_type(node->index.index->type_info))
            {
                const char *hints[] = {"Array indices must be integers", NULL};
                tc_error_with_hints(tc, node->index.index->token, "Non-integer array index", hints);
            }
        }
        break;
    case NODE_EXPR_MEMBER:
        if (node->member.field && strcmp(node->member.field, "forget") == 0)
        {
            // .forget() is a consuming operation: it is valid on a value that
            // was already moved into a container (e.g. `vec.push(x); x.forget();`),
            // so the use-after-move report is suppressed for the receiver.
            tc->is_forget_receiver = 1;
            check_node(tc, node->member.target, depth + 1);
            tc->is_forget_receiver = 0;
        }
        else
        {
            check_node(tc, node->member.target, depth + 1);
        }
        if (node->member.target && node->member.target->type_info)
        {
            Type *target_type = get_inner_type(node->member.target->type_info);
            if (target_type->kind == TYPE_STRUCT && target_type->name)
            {
                if (tc->pctx->config->misra_mode)
                {
                    ZenSymbol *struct_sym =
                        symbol_lookup_kind(tc->pctx->global_scope, target_type->name, SYM_STRUCT);
                    if (struct_sym)
                    {
                        struct_sym->is_dereferenced = 1;
                    }
                }
                ASTNode *struct_def = find_struct_def(tc->pctx, target_type->name);
                if (struct_def)
                {
                    ASTNode *field = struct_def->strct.fields;
                    while (field)
                    {
                        if (field->kind == NODE_FIELD && field->field.name &&
                            strcmp(field->field.name, node->member.field) == 0)
                        {
                            // Propagate lifetime from struct container to the member access result
                            node->type_info = type_clone(field->type_info);
                            if (node->type_info && node->member.target->type_info)
                            {
                                // Depth must be at least that of the container.
                                // (If field itself is static/global, it will be 0, but container's
                                // depth will override)
                                node->type_info->lifetime_depth =
                                    node->member.target->type_info->lifetime_depth;
                            }
                            break;
                        }
                        field = field->next;
                    }
                }
            }

            if (!node->type_info)
            {
                int is_ptr = 0;
                char *alloc_name = NULL;
                char *struct_name =
                    resolve_struct_name_from_type(tc->pctx, target_type, &is_ptr, &alloc_name);

                if (struct_name)
                {
                    char buf[MAX_ERROR_MSG_LEN];
                    snprintf(buf, sizeof(buf), "%s__%s", struct_name, node->member.field);
                    char *mangled = merge_underscores(buf);

                    FuncSig *sig = find_func(tc->pctx, mangled);
                    if (sig)
                    {
                        node->type_info = sig->ret_type;
                    }
                    zfree(mangled);
                }
                if (alloc_name)
                {
                    zfree(alloc_name);
                }
            }
        }
        if (!node->type_info)
        {
            // Fallback for failed lookups
            node->type_info = type_new(TYPE_UNKNOWN);
        }

        if (!tc->is_assign_lhs)
        {
            check_use_validity(tc, node);
        }
        break;
    case NODE_DEFER:
        // Check the deferred statement
        check_node(tc, node->defer_stmt.stmt, depth + 1);
        break;
    case NODE_GUARD:
        // Guard clause: if !condition return
        {
            int old_stmt_ctx = tc->is_stmt_context;
            tc->is_stmt_context = 0;
            check_node(tc, node->guard_stmt.condition, depth + 1);
            tc->is_stmt_context = old_stmt_ctx;
        }
        if (node->guard_stmt.condition && node->guard_stmt.condition->type_info)
        {
            Type *cond_type = resolve_alias(node->guard_stmt.condition->type_info);
            if (tc->pctx->config->misra_mode)
            {
                misra_check_condition_boolean(tc->pctx, node->guard_stmt.condition->type_info,
                                              node->guard_stmt.condition->token);
            }
            else if (cond_type->kind != TYPE_BOOL && !is_integer_type(cond_type) &&
                     cond_type->kind != TYPE_POINTER && cond_type->kind != TYPE_STRING)
            {
                const char *hints[] = {"Guard conditions must be boolean, integer, or pointer",
                                       NULL};
                tc_error_with_hints(tc, node->guard_stmt.condition->token,
                                    "Condition must be a truthy type", hints);
            }
        }
        check_node(tc, node->guard_stmt.body, depth + 1);
        break;
    case NODE_UNLESS:
        // Unless is like if !condition
        {
            int old_stmt_ctx = tc->is_stmt_context;
            tc->is_stmt_context = 0;
            check_node(tc, node->unless_stmt.condition, depth + 1);
            tc->is_stmt_context = old_stmt_ctx;
        }
        if (node->unless_stmt.condition && node->unless_stmt.condition->type_info)
        {
            Type *cond_type = resolve_alias(node->unless_stmt.condition->type_info);
            if (tc->pctx->config->misra_mode)
            {
                misra_check_condition_boolean(tc->pctx, node->unless_stmt.condition->type_info,
                                              node->unless_stmt.condition->token);
            }
            else if (cond_type->kind != TYPE_BOOL && !is_integer_type(cond_type) &&
                     cond_type->kind != TYPE_POINTER && cond_type->kind != TYPE_STRING)
            {
                const char *hints[] = {"Unless conditions must be boolean, integer, or pointer",
                                       NULL};
                tc_error_with_hints(tc, node->unless_stmt.condition->token,
                                    "Condition must be a truthy type", hints);
            }
        }
        check_node(tc, node->unless_stmt.body, depth + 1);
        break;
    case NODE_EXPECT:
    case NODE_ASSERT:
        // Check assert/expect condition
        {
            int old_stmt_ctx = tc->is_stmt_context;
            tc->is_stmt_context = 0;
            check_node(tc, node->assert_stmt.condition, depth + 1);
            tc->is_stmt_context = old_stmt_ctx;
        }
        if (node->assert_stmt.condition && node->assert_stmt.condition->type_info)
        {
            Type *cond_type = resolve_alias(node->assert_stmt.condition->type_info);
            if (tc->pctx->config->misra_mode)
            {
                misra_check_condition_boolean(tc->pctx, node->assert_stmt.condition->type_info,
                                              node->assert_stmt.condition->token);
            }
            else if (cond_type->kind != TYPE_BOOL && !is_integer_type(cond_type) &&
                     cond_type->kind != TYPE_POINTER && cond_type->kind != TYPE_STRING)
            {
                const char *hints[] = {
                    "Assert/expect conditions must be boolean, integer, or pointer", NULL};
                tc_error_with_hints(tc, node->assert_stmt.condition->token,
                                    "Assert/expect condition must be a truthy type", hints);
            }
        }
        break;
    case NODE_TEST:
    {
        MoveState *prev_move_state = tc->pctx->move_state;
        tc->pctx->move_state = move_state_create(NULL);

        check_node(tc, node->test_stmt.body, depth + 1);

        move_state_free(tc->pctx->move_state);
        tc->pctx->move_state = prev_move_state;
        break;
    }

    case NODE_EXPR_CAST:
        // Check the expression being cast
        check_node(tc, node->cast.expr, depth + 1);
        // Could add cast safety checks here (e.g., narrowing, pointer-to-int)
        if (node->cast.expr && node->cast.expr->type_info && node->cast.target_type)
        {
            Type *source_type = resolve_alias(node->cast.expr->type_info);
            Type *target_type = type_from_string_helper(node->cast.target_type);

            if (tc->pctx->config->misra_mode && target_type)
            {
                misra_check_cast(tc->pctx, target_type, source_type, node->token,
                                 is_composite_expression(node->cast.expr));
                misra_check_pointer_conversion(tc->pctx, target_type, source_type, node->token);
                misra_check_void_ptr_cast(tc->pctx, target_type, source_type, node->token);
                if (target_type->kind == TYPE_POINTER)
                {
                    misra_check_null_pointer_constant(tc->pctx, node, node->token);
                }
            }

            // Warn on pointer-to-integer casts (potential data loss)
            if (source_type->kind == TYPE_POINTER)
            {
                const char *target = node->cast.target_type;
                if (strcmp(target, "i8") == 0 || strcmp(target, "i16") == 0 ||
                    strcmp(target, "u8") == 0 || strcmp(target, "u16") == 0)
                {
                    const char *hints[] = {"Pointer-to-small-integer casts may lose address bits",
                                           NULL};
                    tc_error_with_hints(tc, node->token, "Potentially unsafe pointer cast", hints);
                }
            }
            node->type_info = target_type;
            mark_type_as_used(tc, target_type);
        }
        break;
    case NODE_EXPR_ARRAY_LITERAL:
    {
        misra_check_initializer_side_effects(tc->pctx, node);
        ASTNode *elem = node->array_literal.elements;
        Type *elem_type = NULL;
        int count = 0;
        while (elem)
        {
            check_node(tc, elem, depth + 1);
            if (!elem_type && elem->type_info && elem->type_info->kind != TYPE_UNKNOWN)
            {
                elem_type = elem->type_info;
            }
            count++;
            elem = elem->next;
        }
        if (elem_type)
        {
            node->type_info = type_new_array(elem_type, count);
        }
        else
        {
            node->type_info = type_new_array(type_new(TYPE_UNKNOWN), count);
        }
    }
    break;
    case NODE_EXPR_TUPLE_LITERAL:
    {
        misra_check_initializer_side_effects(tc->pctx, node);
        ASTNode *elem = node->tuple_literal.elements;
        while (elem)
        {
            check_node(tc, elem, depth + 1);
            elem = elem->next;
        }
    }
    break;
    case NODE_EXPR_STRUCT_INIT:
        misra_check_initializer_side_effects(tc->pctx, node);
        check_struct_init(tc, node, depth + 1);
        break;
    case NODE_LOOP:
    case NODE_REPEAT:
        check_loop_passes(tc, node, depth + 1);
        break;
    case NODE_TERNARY:
        check_node(tc, node->ternary.cond, depth + 1);
        check_node(tc, node->ternary.true_expr, depth + 1);
        check_node(tc, node->ternary.false_expr, depth + 1);
        // Validate condition
        if (node->ternary.cond && node->ternary.cond->type_info)
        {
            Type *t = node->ternary.cond->type_info;
            if (tc->pctx->config->misra_mode)
            {
                misra_check_condition_boolean(tc->pctx, node->ternary.cond->type_info,
                                              node->ternary.cond->token);
                int inv;
                if (is_expression_invariant(tc, node->ternary.cond, &inv))
                {
                    misra_check_invariant_condition(tc->pctx, node->ternary.cond->token);
                }
            }
            else if (t->kind != TYPE_BOOL && !is_integer_type(t) && t->kind != TYPE_POINTER)
            {
                tc_error(tc, node->ternary.cond->token, "Ternary condition must be truthy");
            }
        }
        // Validate branch compatibility
        if (node->ternary.true_expr && node->ternary.false_expr)
        {
            Type *t1 = node->ternary.true_expr->type_info;
            Type *t2 = node->ternary.false_expr->type_info;
            if (t1 && t2)
            {
                // Loose compatibility check
                if (!check_type_compatibility(tc, t1, t2, node->token, NULL, 0))
                {
                    // Error reported by check_type_compatibility
                }
                else
                {
                    node->type_info = t1; // Inherit type
                }
            }
        }
        break;
    case NODE_ASM:
        for (int i = 0; i < node->asm_stmt.output_count; i++)
        {
            ZenSymbol *sym = tc_lookup(tc, node->asm_stmt.outputs[i]);
            if (!sym)
            {
                char msg[MAX_SHORT_MSG_LEN];
                if (tc->pctx->config->misra_mode)
                {
                    snprintf(msg, sizeof(msg),
                             "Undefined output variable in inline assembly: '%s' (MISRA Rule 17.3)",
                             node->asm_stmt.outputs[i]);
                }
                else
                {
                    snprintf(msg, sizeof(msg), "Undefined output variable in inline assembly: '%s'",
                             node->asm_stmt.outputs[i]);
                }
                tc_error(tc, node->token, msg);
            }
            else if (sym->type_info)
            {
                int width = get_asm_register_size(sym->type_info);
                if (width > node->asm_stmt.register_size)
                {
                    node->asm_stmt.register_size = width;
                }
            }
        }
        for (int i = 0; i < node->asm_stmt.input_count; i++)
        {
            ZenSymbol *sym = tc_lookup(tc, node->asm_stmt.inputs[i]);
            if (!sym)
            {
                char msg[MAX_SHORT_MSG_LEN];
                if (tc->pctx->config->misra_mode)
                {
                    snprintf(msg, sizeof(msg),
                             "Undefined input variable in inline assembly: '%s' (MISRA Rule 17.3)",
                             node->asm_stmt.inputs[i]);
                }
                else
                {
                    snprintf(msg, sizeof(msg), "Undefined input variable in inline assembly: '%s'",
                             node->asm_stmt.inputs[i]);
                }
                tc_error(tc, node->token, msg);
            }
            else if (sym->type_info)
            {
                int width = get_asm_register_size(sym->type_info);
                if (width > node->asm_stmt.register_size)
                {
                    node->asm_stmt.register_size = width;
                }
            }
        }
        if (node->asm_stmt.register_size > 64)
        {
            char msg[MAX_SHORT_MSG_LEN];
            snprintf(msg, sizeof(msg),
                     "Unsupported register size is required in inline assembly: %i bits",
                     node->asm_stmt.register_size);
            tc_error(tc, node->token, msg);
        }
        break;
    case NODE_LAMBDA:
        check_expr_lambda(tc, node, depth + 1);
        break;
    case NODE_EXPR_SIZEOF:
        if (node->size_of.expr)
        {
            check_node(tc, node->size_of.expr, depth + 1);

            if (tc->pctx->config->misra_mode && tc_expr_has_side_effects(node->size_of.expr))
            {
                misra_check_side_effects_sizeof(tc->pctx, node->size_of.expr);
            }
        }
        node->type_info = type_new(TYPE_I32);
        break;
    case NODE_FOR_RANGE:
        check_loop_passes(tc, node, depth + 1);
        break;
    case NODE_EXPR_SLICE:
        // Check slice target and indices
        check_node(tc, node->slice.array, depth + 1);
        check_node(tc, node->slice.start, depth + 1);
        check_node(tc, node->slice.end, depth + 1);
        break;
    case NODE_DESTRUCT_VAR:
        if (node->destruct.init_expr)
        {
            check_node(tc, node->destruct.init_expr, depth + 1);
        }
        break;
    case NODE_DO_WHILE:
        check_loop_passes(tc, node, depth + 1);
        break;
    case NODE_BREAK:
        if (tc->loop_break_count > 0)
        {
            misra_check_iteration_termination(tc->pctx, node->token);
        }
        tc->loop_break_count++;

        if (tc->move_checks_only)
        {
            // No-op
        }
        else if (tc->pctx->move_state)
        {
            move_state_merge_into(&tc->loop_break_state, tc->pctx->move_state);
        }
        tc->is_unreachable = 1;
        break;
    case NODE_GOTO:
        if (tc->pctx->config->misra_mode)
        {
            ZenSymbol *lbl = tc_lookup(tc, node->goto_stmt.label_name);
            if (lbl && lbl->decl_token.line != 0)
            {
                misra_check_goto_constraint(tc->pctx, node->token, lbl->decl_token);
            }
        }
        misra_check_goto(tc->pctx, node->token);
        tc->is_unreachable = 1;
        break;

    case NODE_CONTINUE:
        if (tc->pctx->move_state)
        {
            move_state_merge_into(&tc->loop_continue_state, tc->pctx->move_state);
        }
        tc->is_unreachable = 1;
        break;
    case NODE_VA_START:
    case NODE_VA_END:
    case NODE_VA_COPY:
    case NODE_VA_ARG:
        misra_check_stdarg(tc->pctx, node->token);
        break;

    case NODE_RAW_STMT:
        misra_check_raw_block(tc->pctx, node->token);
        break;
    case NODE_PREPROC_DIRECTIVE:
        // Rule Zen 1.4 is already handled by parser_audit_preprocessor
        break;
    case NODE_PLUGIN:
        misra_check_plugin_block(tc->pctx, node->token);
        break;
    case NODE_LABEL:
        if (tc->pctx->config->misra_mode)
        {
            ZenSymbol *lbl =
                symbol_add(tc->pctx->current_scope, node->label_stmt.label_name, SYM_LABEL);
            if (lbl)
            {
                lbl->decl_token = node->token;
            }
        }
        break;
    case NODE_COMPTIME:
    {
        // Register comptime builtins for the body
        register_comptime_builtins(tc->pctx);

        // Type-check the comptime body
        ASTNode *stmt = node->comptime.body;
        while (stmt)
        {
            check_node(tc, stmt, depth + 1);
            stmt = stmt->next;
        }

        // Interpret the comptime body
        char *output =
            interpret_comptime(tc->pctx, node->comptime.body, tc->pctx->current_filename);
        if (!output)
        {
            break;
        }

        // Parse generated source code
        if (output[0])
        {
            Lexer out_l;
            lexer_init(&out_l, output, tc->pctx->config, tc->pctx->current_filename);
            node->comptime.generated = parse_program_nodes(tc->pctx, &out_l);

            // Type-check generated nodes
            ASTNode *gen = node->comptime.generated;
            while (gen)
            {
                check_node(tc, gen, depth + 1);
                gen = gen->next;
            }
        }
        zfree(output);
        break;
    }

    default:
        // Generic recursion for lists and other nodes.
        // Special case for Return to trigger move?
        if (node->kind == NODE_RETURN && node->ret.value)
        {
            // If returning a value, check if it can be moved.
            check_move_for_rvalue(tc, node->ret.value);
        }
        break;
    }
    RECURSION_EXIT(tc->pctx);
}

static void infer_node_lifetime(TypeChecker *tc, ASTNode *node)
{
    if (!node || node->kind != NODE_FUNCTION)
    {
        return;
    }

    FuncSig *fsig = find_func(tc->pctx, node->func.name);
    if (!fsig)
    {
        return;
    }

    // Default to local argument scope (depth 1)
    int inferred_depth = 1;
    int ptr_param_count = 0;
    int self_depth = -1;
    int elide_idx = -1;

    for (int i = 0; i < fsig->total_args; i++)
    {
        Type *t = (fsig->arg_types && fsig->arg_types[i]) ? fsig->arg_types[i] : NULL;
        if (t && t->kind == TYPE_POINTER)
        {
            ptr_param_count++;
            // Parameters are always at least depth 1 (argument scope)
            if (t->lifetime_depth == 0)
            {
                t->lifetime_depth = 1;
            }

            if (node->func.param_names && node->func.param_names[i] &&
                strcmp(node->func.param_names[i], "self") == 0)
            {
                self_depth = t->lifetime_depth;
                elide_idx = i;
            }
        }
    }

    if (self_depth != -1)
    {
        inferred_depth = self_depth;
    }
    else if (ptr_param_count == 1)
    {
        for (int i = 0; i < fsig->total_args; i++)
        {
            Type *t = (fsig->arg_types && fsig->arg_types[i]) ? fsig->arg_types[i] : NULL;
            if (t && t->kind == TYPE_POINTER)
            {
                inferred_depth = t->lifetime_depth;
                elide_idx = i;
                break;
            }
        }
    }

    node->func.elide_from_idx = elide_idx;
    fsig->elide_from_idx = elide_idx;

    // Update the return type depth in the signature
    if (fsig->ret_type && fsig->ret_type->kind == TYPE_POINTER)
    {
        fsig->ret_type->lifetime_depth = inferred_depth;
    }

    // Also update AST node if types are already resolved there
    if (node->func.ret_type_info && node->func.ret_type_info->kind == TYPE_POINTER)
    {
        node->func.ret_type_info->lifetime_depth = inferred_depth;
    }
}

static void check_program_prepass(TypeChecker *tc, ASTNode *root, int depth)
{
    if (!root || root->kind != NODE_ROOT)
    {
        return;
    }
    RECURSION_GUARD_TOKEN(tc->pctx, root->token, );

    if (depth > 64)
    {
        RECURSION_EXIT(tc->pctx);
        return;
    }

    ASTNode *n = root->root.children;
    while (n)
    {
        if (n->kind == NODE_ROOT)
        {
            check_program_prepass(tc, n, depth + 1);
        }
        else if (n->kind == NODE_FUNCTION)
        {
            infer_node_lifetime(tc, n);
        }
        else if (n->kind == NODE_IMPL)
        {
            ASTNode *method = n->impl.methods;
            while (method)
            {
                if (method->kind == NODE_FUNCTION)
                {
                    infer_node_lifetime(tc, method);
                }
                method = method->next;
            }
        }
        else if (n->kind == NODE_IMPL_TRAIT)
        {
            ASTNode *method = n->impl_trait.methods;
            while (method)
            {
                if (method->kind == NODE_FUNCTION)
                {
                    infer_node_lifetime(tc, method);
                }
                method = method->next;
            }
        }
        else if (n->kind == NODE_IMPORT)
        {
            // Imports are conceptually ROOTs of their own module
            check_program_prepass(tc, n->import_stmt.module_root, depth + 1);
        }
        n = n->next;
    }
    RECURSION_EXIT(tc->pctx);
}

// ** Entry Point **

int check_program(ParserContext *ctx, ASTNode *root)
{
    TypeChecker tc = {0};
    tc.pctx = ctx;
    ctx->current_scope = ctx->global_scope;

    if (!ctx->move_state)
    {
        ctx->move_state = move_state_create(NULL);
    }

    check_program_prepass(&tc, root, 0);

    check_node(&tc, root, 0);
    root->is_checked = 1;

    // Fixed-point iteration to handle secondary instantiations
    int changed = 1;
    while (changed)
    {
        changed = 0;
        ASTNode *inst_func = ctx->instantiated_funcs;
        while (inst_func)
        {
            if (!inst_func->is_checked)
            {
                check_node(&tc, inst_func, 0);
                inst_func->is_checked = 1;
                changed = 1;
                // Restart from head to catch newly added instantiations that might be before us
                break;
            }
            inst_func = inst_func->next;
        }
    }

    if (ctx->move_state)
    {
        move_state_free(ctx->move_state);
        ctx->move_state = NULL;
    }

    if (tc.pctx->config->misra_mode)
    {
        misra_audit_unused_symbols(tc.pctx);
        misra_audit_block_scope(tc.pctx);
        misra_audit_identifier_uniqueness(tc.pctx);
    }

    if (g_error_count > 0)
    {
        fprintf(stderr,
                COLOR_BOLD COLOR_RED "     error" COLOR_RESET
                                     ": semantic analysis found %d error%s\n",
                g_error_count, g_error_count == 1 ? "" : "s");
        return 1;
    }

    return 0;
}

int check_moves_only(ParserContext *ctx, ASTNode *root)
{
    TypeChecker tc = {0};
    tc.pctx = ctx;
    tc.move_checks_only = 1;
    ctx->current_scope = ctx->global_scope;

    if (!ctx->move_state)
    {
        ctx->move_state = move_state_create(NULL);
    }

    check_node(&tc, root, 0);

    if (ctx->move_state)
    {
        move_state_free(ctx->move_state);
        ctx->move_state = NULL;
    }

    return g_error_count;
}
