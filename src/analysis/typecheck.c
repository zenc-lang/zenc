
#include "typecheck.h"
#include "diagnostics/diagnostics.h"
#include "move_check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ** Internal Helpers **

void tc_error(TypeChecker *tc, Token t, const char *msg)
{
    if (tc->move_checks_only)
    {
        return;
    }
    zerror_at(t, "%s", msg);
    tc->error_count++;
}

void tc_error_with_hints(TypeChecker *tc, Token t, const char *msg, const char *const *hints)
{
    if (tc->move_checks_only)
    {
        return;
    }
    zerror_with_hints(t, msg, hints);
    tc->error_count++;
}

void tc_move_error_with_hints(TypeChecker *tc, Token t, const char *msg, const char *const *hints)
{
    zerror_with_hints(t, msg, hints);
    tc->error_count++;
}

static void tc_enter_scope(TypeChecker *tc)
{
    Scope *s = malloc(sizeof(Scope));
    if (!s)
    {
        return;
    }
    s->symbols = NULL;
    s->parent = tc->current_scope;
    tc->current_scope = s;
}

static void tc_exit_scope(TypeChecker *tc)
{
    if (!tc->current_scope)
    {
        return;
    }
    Scope *old = tc->current_scope;
    tc->current_scope = old->parent;

    ZenSymbol *sym = old->symbols;
    while (sym)
    {
        ZenSymbol *next = sym->next;
        free(sym);
        sym = next;
    }
    free(old);
}

static void tc_add_symbol(TypeChecker *tc, const char *name, Type *type, Token t)
{
    // Guard against NULL scope (e.g., global variables before entering function)
    if (!tc->current_scope)
    {
        return; // Skip adding to scope - global symbols handled separately
    }

    ZenSymbol *s = malloc(sizeof(ZenSymbol));
    memset(s, 0, sizeof(ZenSymbol));
    s->name = strdup(name);
    s->type_info = type;
    s->decl_token = t;
    s->next = tc->current_scope->symbols;
    tc->current_scope->symbols = s;
}

static ZenSymbol *tc_lookup(TypeChecker *tc, const char *name)
{
    Scope *s = tc->current_scope;
    while (s)
    {
        ZenSymbol *curr = s->symbols;
        while (curr)
        {
            if (0 == strcmp(curr->name, name))
            {
                return curr;
            }
            curr = curr->next;
        }
        s = s->parent;
    }
    return NULL;
}

static int is_char_type(Type *t)
{
    if (!t)
    {
        return 0;
    }
    if (t->kind == TYPE_CHAR || t->kind == TYPE_I8 || t->kind == TYPE_U8 ||
        t->kind == TYPE_C_CHAR || t->kind == TYPE_C_UCHAR)
    {
        return 1;
    }
    // Also handle struct wrappers for char (if any)
    if (t->kind == TYPE_STRUCT && t->name && strcmp(t->name, "char") == 0)
    {
        return 1;
    }
    return 0;
}

static int get_asm_register_size(Type *t)
{
    if (!t)
    {
        return 0;
    }
    if (t->kind == TYPE_F64 || t->kind == TYPE_I64 || t->kind == TYPE_U64 ||
        (t->kind == TYPE_STRUCT && t->name &&
         (0 == strcmp(t->name, "int64_t") || 0 == strcmp(t->name, "uint64_t"))))
    {
        return 64;
    }
    if (t->kind == TYPE_I128 || t->kind == TYPE_U128)
    {
        return 128;
    }
    return 32;
}

static int integer_type_width(Type *t)
{
    if (!t)
    {
        return 0;
    }
    switch (t->kind)
    {
    case TYPE_I8:
    case TYPE_U8:
    case TYPE_BYTE:
    case TYPE_C_CHAR:
    case TYPE_C_UCHAR:
        return 8;
    case TYPE_I16:
    case TYPE_U16:
    case TYPE_C_SHORT:
    case TYPE_C_USHORT:
        return 16;
    case TYPE_I32:
    case TYPE_U32:
    case TYPE_INT:
    case TYPE_UINT:
    case TYPE_RUNE:
    case TYPE_C_INT:
    case TYPE_C_UINT:
        return 32;
    case TYPE_I64:
    case TYPE_U64:
    case TYPE_ISIZE:
    case TYPE_USIZE:
    case TYPE_C_LONG:
    case TYPE_C_ULONG:
    case TYPE_C_LONG_LONG:
    case TYPE_C_ULONG_LONG:
        return 64;
    case TYPE_I128:
    case TYPE_U128:
        return 128;
    default:
        return 0;
    }
}

// ** Node Checkers **

static void check_node(TypeChecker *tc, ASTNode *node);
static void check_expr_lambda(TypeChecker *tc, ASTNode *node);
static int check_type_compatibility(TypeChecker *tc, Type *target, Type *value, Token t);

static void check_move_for_rvalue(TypeChecker *tc, ASTNode *rvalue)
{
    if (!rvalue || !rvalue->type_info)
    {
        return;
    }

    if (is_type_copy(tc->pctx, rvalue->type_info))
    {
        return;
    }

    if (rvalue->type == NODE_EXPR_VAR)
    {
        ZenSymbol *sym = tc_lookup(tc, rvalue->var_ref.name);
        if (sym)
        {
            mark_symbol_moved(tc->pctx, sym, rvalue);
        }
    }
    else if (rvalue->type == NODE_EXPR_UNARY && strcmp(rvalue->unary.op, "*") == 0)
    {
        const char *hints[] = {"This type owns resources and cannot be implicitly copied",
                               "Consider borrowing value via references or implementing Copy",
                               NULL};
        tc_move_error_with_hints(tc, rvalue->token, "Cannot move out of a borrowed reference",
                                 hints);
    }
    else if (rvalue->type == NODE_EXPR_MEMBER)
    {
        // Now allowed, but will be tracked by path
        mark_symbol_moved(tc->pctx, NULL, rvalue);
    }
    else if (rvalue->type == NODE_EXPR_INDEX)
    {
        const char *hints[] = {"Cannot move an element out of an array or slice.", NULL};
        tc_move_error_with_hints(tc, rvalue->token, "Cannot move out of an index expression",
                                 hints);
    }
}

static Type *resolve_alias(Type *t)
{
    while (t && t->kind == TYPE_ALIAS && t->inner)
    {
        t = t->inner;
    }
    return t;
}

static void check_expr_unary(TypeChecker *tc, ASTNode *node)
{
    check_node(tc, node->unary.operand);

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
            node->type_info = operand_type;
        }
        return;
    }
}

static void check_expr_binary(TypeChecker *tc, ASTNode *node)
{
    const char *op = node->binary.op;

    if (strcmp(op, "=") == 0)
    {
        int old_is_assign_lhs = tc->is_assign_lhs;
        tc->is_assign_lhs = 1;
        check_node(tc, node->binary.left);
        tc->is_assign_lhs = old_is_assign_lhs;
        if (node->binary.left->type_info && node->binary.right->type == NODE_LAMBDA)
        {
            node->binary.right->type_info = node->binary.left->type_info;
        }
        check_node(tc, node->binary.right);
    }
    else
    {
        check_node(tc, node->binary.left);
        check_node(tc, node->binary.right);
    }

    Type *left_type = node->binary.left->type_info;
    Type *right_type = node->binary.right->type_info;

    // Assignment Logic for Moves (and type compatibility)
    if (strcmp(op, "=") == 0)
    {
        // Check type compatibility for assignment
        if (left_type && right_type)
        {
            check_type_compatibility(tc, left_type, right_type, node->binary.right->token);
        }

        // If RHS is moving a non-copy value, check validity and mark moved
        check_move_for_rvalue(tc, node->binary.right);

        // LHS is being (re-)initialized, so it becomes Valid.
        if (node->binary.left->type == NODE_EXPR_VAR)
        {
            ZenSymbol *lhs_sym = tc_lookup(tc, node->binary.left->var_ref.name);
            if (lhs_sym)
            {
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
            node->binary.right->type == NODE_EXPR_LITERAL)
        {
            LiteralKind kind = node->binary.right->literal.type_kind;
            if (kind == LITERAL_INT && node->binary.right->literal.int_val == 0)
            {
                const char *hints[] = {"Division by zero is undefined behavior", NULL};
                tc_error_with_hints(tc, node->binary.right->token, "Division by zero detected",
                                    hints);
            }
            else if (kind == LITERAL_FLOAT && node->binary.right->literal.float_val == 0.0)
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

                char msg[256];
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
                // Result type: if either is float, result is float; else left type
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
        return;
    }

    // Comparison operators: ==, !=, <, >, <=, >=
    if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 || strcmp(op, "<") == 0 ||
        strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0)
    {
        // Result is always bool
        node->type_info = type_new(TYPE_BOOL);

        // Operands should be comparable
        if (left_type && right_type && !type_eq(left_type, right_type))
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
                char msg[256];
                snprintf(msg, sizeof(msg), "Cannot compare '%s' with incompatible types", op);
                const char *hints[] = {"Ensure both operands have the same or compatible types",
                                       NULL};
                tc_error_with_hints(tc, node->token, msg, hints);
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
        // Shift amount validation for << and >>
        if ((strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) && node->binary.right &&
            node->binary.right->type == NODE_EXPR_LITERAL &&
            node->binary.right->literal.type_kind == LITERAL_INT)
        {
            unsigned long long shift_amt = node->binary.right->literal.int_val;
            // Warn if shift amount >= 64 (undefined for most int types)
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
                char msg[256];
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
                node->type_info = left_type;
            }
        }
        return;
    }
}

static void check_expr_call(TypeChecker *tc, ASTNode *node)
{
    check_node(tc, node->call.callee);

    const char *func_name = NULL;
    FuncSig *sig = NULL;

    // Check if the function exists (for simple direct calls)
    if (node->call.callee && node->call.callee->type == NODE_EXPR_VAR)
    {
        func_name = node->call.callee->var_ref.name;

        // Look up function signature
        sig = find_func(tc->pctx, func_name);

        if (!sig)
        {
            // Check if it's a built-in macro injected by the compiler
            if (strcmp(func_name, "_z_str") == 0)
            {
                // _z_str is a generic format macro from ZC_C_GENERIC_STR
                check_node(tc, node->call.args); // Still check the argument
                node->type_info = type_new(TYPE_STRING);
                return;
            }

            // Check local scope first, then global symbols
            ZenSymbol *sym = tc_lookup(tc, func_name);
            if (!sym)
            {
                ZenSymbol *global_sym = find_symbol_in_all(tc->pctx, func_name);
                if (!global_sym && !should_suppress_undef_warning(tc->pctx, func_name))
                {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Undefined function '%s'", func_name);
                    const char *hints[] = {"Check if the function is defined or imported", NULL};
                    tc_error_with_hints(tc, node->call.callee->token, msg, hints);
                }
            }
        }
    }

    // Check purity restriction
    if (func_name && tc->is_pure && tc->current_func && (!sig || !sig->is_pure))
    {
        char msg[256];
        snprintf(msg, sizeof(msg), "Calling possibly impure function '%s' from pure function '%s'",
                 func_name, tc->current_func->func.name);
        const char *hints[] = {"Consider if the called function is @pure", NULL};
        tc_error_with_hints(tc, node->call.callee->token, msg, hints);
    }

    // Count arguments
    int arg_count = 0;
    ASTNode *arg = node->call.args;
    while (arg)
    {
        arg_count++;
        arg = arg->next;
    }

    // Validate argument count if we have a signature
    if (sig)
    {
        int min_args = sig->total_args;

        // Count required args (those without defaults)
        if (sig->defaults)
        {
            min_args = 0;
            for (int i = 0; i < sig->total_args; i++)
            {
                if (!sig->defaults[i])
                {
                    min_args++;
                }
            }
        }

        if (arg_count < min_args)
        {
            char msg[256];
            snprintf(msg, sizeof(msg), "Too few arguments: '%s' expects at least %d, got %d",
                     func_name, min_args, arg_count);

            const char *hints[] = {"Check the function signature for required parameters", NULL};
            tc_error_with_hints(tc, node->token, msg, hints);
        }
        else if (arg_count > sig->total_args && !sig->is_varargs)
        {
            char msg[256];
            snprintf(msg, sizeof(msg), "Too many arguments: '%s' expects %d, got %d", func_name,
                     sig->total_args, arg_count);

            const char *hints[] = {
                "Remove extra arguments or check if you meant to call a different function", NULL};
            tc_error_with_hints(tc, node->token, msg, hints);
        }
    }

    // Check argument types
    arg = node->call.args;
    int arg_idx = 0;
    while (arg)
    {
        Type *expected = NULL;
        if (sig && arg_idx < sig->total_args && sig->arg_types && sig->arg_types[arg_idx])
        {
            expected = sig->arg_types[arg_idx];
        }
        else if (!sig && node->call.callee->type_info)
        {
            Type *callee_t = get_inner_type(node->call.callee->type_info);
            if (callee_t->kind == TYPE_FUNCTION && arg_idx < callee_t->arg_count && callee_t->args)
            {
                expected = callee_t->args[arg_idx];
            }
        }

        // Propagate expected type to lambda for inference
        if (arg->type == NODE_LAMBDA && expected)
        {
            arg->type_info = expected;
        }

        check_node(tc, arg);

        // Validate type against signature
        Type *actual = arg->type_info;
        if (expected && actual)
        {
            Type *e_resolved = get_inner_type(expected);
            Type *a_resolved = get_inner_type(actual);

            if (e_resolved->kind == TYPE_UNKNOWN && a_resolved->kind != TYPE_UNKNOWN)
            {
                // Backward type inference: we passed an actual type to a lambda taking unknown
                *e_resolved = *a_resolved;
            }
            else if (e_resolved->kind == TYPE_FUNCTION && a_resolved->kind == TYPE_FUNCTION)
            {
                for (int j = 0; j < e_resolved->arg_count && j < a_resolved->arg_count; j++)
                {
                    if (a_resolved->args && a_resolved->args[j] &&
                        a_resolved->args[j]->kind == TYPE_UNKNOWN && e_resolved->args &&
                        e_resolved->args[j] && e_resolved->args[j]->kind != TYPE_UNKNOWN)
                    {
                        *a_resolved->args[j] = *e_resolved->args[j];
                    }
                }
                if (a_resolved->inner && a_resolved->inner->kind == TYPE_UNKNOWN &&
                    e_resolved->inner)
                {
                    *a_resolved->inner = *e_resolved->inner;
                }
            }
            check_type_compatibility(tc, expected, actual, arg->token);
        }

        // If argument is passed by VALUE, check if it can be moved.
        check_move_for_rvalue(tc, arg);

        arg = arg->next;
        arg_idx++;
    }

    // Propagate return type from function signature
    if (sig && sig->ret_type && !node->type_info)
    {
        node->type_info = sig->ret_type;
    }
}

static void check_block(TypeChecker *tc, ASTNode *block)
{
    tc_enter_scope(tc);
    ASTNode *stmt = block->block.statements;
    int seen_terminator = 0;
    Token terminator_token = {0};

    while (stmt)
    {
        // Warn if we see code after a terminating statement
        if (seen_terminator && stmt->type != NODE_LABEL)
        {
            const char *hints[] = {"Remove unreachable code or restructure control flow", NULL};
            tc_error_with_hints(tc, stmt->token, "Unreachable code detected", hints);
            seen_terminator = 0; // Only warn once per block
        }

        check_node(tc, stmt);

        // Track terminating statements
        if (stmt->type == NODE_RETURN || stmt->type == NODE_BREAK || stmt->type == NODE_CONTINUE ||
            stmt->type == NODE_GOTO)
        {
            seen_terminator = 1;
            terminator_token = stmt->token;
        }

        stmt = stmt->next;
    }
    (void)terminator_token; // May be used for enhanced diagnostics later
    tc_exit_scope(tc);
}

static int check_type_compatibility(TypeChecker *tc, Type *target, Type *value, Token t)
{
    if (!target || !value)
    {
        return 1; // Can't check incomplete types
    }

    // Fast path: exact match
    if (type_eq(target, value))
    {
        return 1;
    }

    // Resolve type aliases (str -> string, etc.)
    Type *resolved_target = target;
    Type *resolved_value = value;

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
        resolved_value->inner->kind == TYPE_VOID)
    {
        return 1;
    }

    // Integer compatibility (promotion/demotion)
    if (is_integer_type(resolved_target) && is_integer_type(resolved_value))
    {
        // Warn on narrowing conversions
        int target_width = integer_type_width(resolved_target);
        int value_width = integer_type_width(resolved_value);
        if (target_width > 0 && value_width > 0 && target_width < value_width)
        {
            char *t_str = type_to_string(target);
            char *v_str = type_to_string(value);
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Implicit narrowing conversion from '%s' (%d-bit) to '%s' (%d-bit)", v_str,
                     value_width, t_str, target_width);
            zwarn_at(t, "%s", msg);
            if (tc)
            {
                tc->warning_count++;
            }
            free(t_str);
            free(v_str);
        }
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

    // Type mismatch - report error
    char *t_str = type_to_string(target);
    char *v_str = type_to_string(value);

    char msg[512];
    snprintf(msg, sizeof(msg), "Type mismatch: expected '%s', but found '%s'", t_str, v_str);

    const char *hints[] = {
        "Check if you need an explicit cast",
        "Ensure the types match exactly (no implicit conversions for strict types)", NULL};

    tc_error_with_hints(tc, t, msg, hints);
    free(t_str);
    free(v_str);
    return 0;
}

static void check_var_decl(TypeChecker *tc, ASTNode *node)
{
    if (node->var_decl.init_expr)
    {
        if (node->type_info && node->var_decl.init_expr->type == NODE_LAMBDA)
        {
            node->var_decl.init_expr->type_info = node->type_info;
        }
        check_node(tc, node->var_decl.init_expr);

        Type *decl_type = node->type_info;
        Type *init_type = node->var_decl.init_expr->type_info;

        if (decl_type && init_type)
        {
            check_type_compatibility(tc, decl_type, init_type, node->token);
        }

        // Move Analysis: Check if the initializer moves a non-copy value.
        check_move_for_rvalue(tc, node->var_decl.init_expr);
    }

    // If type is not explicit, we should ideally infer it from init_expr.
    Type *t = node->type_info;
    if (!t && node->var_decl.init_expr)
    {
        t = node->var_decl.init_expr->type_info;
        node->type_info = t;
    }

    tc_add_symbol(tc, node->var_decl.name, t, node->token);
    ZenSymbol *new_sym = tc_lookup(tc, node->var_decl.name);
    if (new_sym)
    {
        mark_symbol_valid(tc->pctx, new_sym, node);
    }
}

static int block_always_returns(ASTNode *block);

static int stmt_always_returns(ASTNode *stmt)
{
    if (!stmt)
    {
        return 0;
    }

    switch (stmt->type)
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
            if (case_node->type == NODE_MATCH_CASE)
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
        return 0;

    default:
        return 0;
    }
}

static int block_always_returns(ASTNode *block)
{
    if (!block || block->type != NODE_BLOCK)
    {
        return 0;
    }

    ASTNode *stmt = block->block.statements;
    while (stmt)
    {
        if (stmt_always_returns(stmt))
        {
            return 1;
        }
        stmt = stmt->next;
    }
    return 0;
}

static void check_function(TypeChecker *tc, ASTNode *node)
{
    // Just to suppress the warning.
    (void)tc_error;

    tc->current_func = node;
    tc_enter_scope(tc);

    int prev_pure = tc->is_pure;
    tc->is_pure = node->func.pure;

    int prev_unreachable = tc->is_unreachable;
    tc->is_unreachable = 0;

    MoveState *prev_move_state = tc->pctx->move_state;
    tc->pctx->move_state = move_state_create(NULL);

    for (int i = 0; i < node->func.arg_count; i++)
    {
        if (node->func.param_names && node->func.param_names[i])
        {
            Type *param_type =
                (node->func.arg_types && node->func.arg_types[i]) ? node->func.arg_types[i] : NULL;
            tc_add_symbol(tc, node->func.param_names[i], param_type, node->token);
        }
    }

    check_node(tc, node->func.body);

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
            char msg[256];
            snprintf(msg, sizeof(msg), "Function '%s' may not return a value on all code paths",
                     node->func.name);

            const char *hints[] = {"Ensure all execution paths return a value",
                                   "Consider adding a default return at the end of the function",
                                   NULL};
            tc_error_with_hints(tc, node->token, msg, hints);
        }
    }

    move_state_free(tc->pctx->move_state);
    tc->pctx->move_state = prev_move_state;

    tc->is_unreachable = prev_unreachable;
    tc->is_pure = prev_pure;
    tc_exit_scope(tc);
    tc->current_func = NULL;
}

static void check_expr_var(TypeChecker *tc, ASTNode *node)
{
    ZenSymbol *sym = tc_lookup(tc, node->var_ref.name);

    if (sym && sym->type_info)
    {
        node->type_info = sym->type_info;
    }
    else
    {
        // Check if it's a mangled function name (e.g. from :: operator)
        FuncSig *sig = find_func(tc->pctx, node->var_ref.name);
        if (sig)
        {
            Type *fn_type = type_new(TYPE_FUNCTION);
            fn_type->is_raw = 1;
            fn_type->inner = sig->ret_type ? sig->ret_type : type_new(TYPE_VOID);
            fn_type->arg_count = sig->total_args;
            if (sig->total_args > 0)
            {
                fn_type->args = xmalloc(sizeof(Type *) * sig->total_args);
                for (int i = 0; i < sig->total_args; i++)
                {
                    fn_type->args[i] = sig->arg_types[i];
                }
            }
            node->type_info = fn_type;
        }
    }

    if (!tc->is_assign_lhs)
    {
        check_use_validity(tc, node);
    }
}

static void check_expr_literal(TypeChecker *tc, ASTNode *node)
{
    (void)tc;
    switch (node->literal.type_kind)
    {
    case LITERAL_INT:
        node->type_info = type_new(TYPE_I32); // Default to i32, or use suffix if we had one
        break;
    case LITERAL_FLOAT:
        node->type_info = type_new(TYPE_F64); // Default to f64
        break;
    case LITERAL_STRING:
        node->type_info = type_new(TYPE_STRING);
        break;
    case LITERAL_CHAR:
        node->type_info = type_new(TYPE_CHAR);
        break;
    default:
        break;
    }
}

static void check_struct_init(TypeChecker *tc, ASTNode *node)
{
    // Find struct definition
    ASTNode *def = find_struct_def(tc->pctx, node->struct_init.struct_name);
    if (!def)
    {
        char msg[256];
        snprintf(msg, sizeof(msg), "Unknown struct '%s'", node->struct_init.struct_name);
        tc_error(tc, node->token, msg);
        return;
    }

    // Iterate provided fields
    ASTNode *field_init = node->struct_init.fields;
    while (field_init)
    {
        // Find corresponding field in definition
        ASTNode *def_field = def->strct.fields;
        Type *expected_type = NULL;
        int found = 0;

        while (def_field)
        {
            if (def_field->type == NODE_FIELD &&
                strcmp(def_field->field.name, field_init->var_decl.name) == 0)
            {
                found = 1;
                expected_type = def_field->type_info;
                break;
            }
            def_field = def_field->next;
        }

        if (found && expected_type && field_init->var_decl.init_expr->type == NODE_LAMBDA)
        {
            field_init->var_decl.init_expr->type_info = expected_type;
        }

        // Check the initialization expression
        check_node(tc, field_init->var_decl.init_expr);

        if (!found)
        {
            char msg[256];
            snprintf(msg, sizeof(msg), "Struct '%s' has no field named '%s'",
                     node->struct_init.struct_name, field_init->var_decl.name);
            tc_error(tc, field_init->token, msg);
        }
        else if (expected_type && field_init->var_decl.init_expr->type_info)
        {
            check_type_compatibility(tc, expected_type, field_init->var_decl.init_expr->type_info,
                                     field_init->token);
        }

        // Move Analysis: Check if the initializer moves a non-copy value.
        check_move_for_rvalue(tc, field_init->var_decl.init_expr);

        field_init = field_init->next;
    }

    // Check for missing required fields
    ASTNode *def_field = def->strct.fields;
    while (def_field)
    {
        if (def_field->type == NODE_FIELD && def_field->field.name)
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
                char msg[256];
                snprintf(msg, sizeof(msg), "Missing field '%s' in initializer for struct '%s'",
                         def_field->field.name, node->struct_init.struct_name);
                const char *hints[] = {"All struct fields must be initialized", NULL};
                tc_error_with_hints(tc, node->token, msg, hints);
            }
        }
        def_field = def_field->next;
    }

    node->type_info = def->type_info;
}

static void check_loop_passes(TypeChecker *tc, ASTNode *node)
{
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

    int initial_unreachable = tc->is_unreachable;

    // Pass 1: standard typecheck and move check
    tc->is_unreachable = 0; // The loop start is assumed reachable if we got here

    switch (node->type)
    {
    case NODE_WHILE:
        check_node(tc, node->while_stmt.condition);
        if (node->while_stmt.condition && node->while_stmt.condition->type_info)
        {
            Type *cond_type = resolve_alias(node->while_stmt.condition->type_info);
            if (cond_type->kind != TYPE_BOOL && !is_integer_type(cond_type) &&
                cond_type->kind != TYPE_POINTER && cond_type->kind != TYPE_STRING)
            {
                const char *hints[] = {"While conditions must be boolean, integer, or pointer",
                                       NULL};
                tc_error_with_hints(tc, node->while_stmt.condition->token,
                                    "Condition must be a truthy type", hints);
            }
        }
        check_node(tc, node->while_stmt.body);
        break;

    case NODE_FOR:
        tc_enter_scope(tc); // For loop init variable is scoped
        check_node(tc, node->for_stmt.init);

        // Loop start is conceptually here for FOR
        if (loop_start)
        {
            move_state_free(loop_start);
        }
        loop_start = tc->pctx->move_state ? move_state_clone(tc->pctx->move_state) : NULL;
        tc->loop_start_state = loop_start;

        check_node(tc, node->for_stmt.condition);
        if (node->for_stmt.condition && node->for_stmt.condition->type_info)
        {
            Type *cond_type = resolve_alias(node->for_stmt.condition->type_info);
            if (cond_type->kind != TYPE_BOOL && !is_integer_type(cond_type) &&
                cond_type->kind != TYPE_POINTER && cond_type->kind != TYPE_STRING)
            {
                const char *hints[] = {"For conditions must be boolean, integer, or pointer", NULL};
                tc_error_with_hints(tc, node->for_stmt.condition->token,
                                    "Condition must be a truthy type", hints);
            }
        }
        check_node(tc, node->for_stmt.body);
        check_node(tc, node->for_stmt.step); // step happens after body
        break;

    case NODE_FOR_RANGE:
        check_node(tc, node->for_range.start);
        check_node(tc, node->for_range.end);

        // Loop start conceptually here
        if (loop_start)
        {
            move_state_free(loop_start);
        }
        loop_start = tc->pctx->move_state ? move_state_clone(tc->pctx->move_state) : NULL;
        tc->loop_start_state = loop_start;

        check_node(tc, node->for_range.body);
        break;

    case NODE_LOOP:
        check_node(tc, node->loop_stmt.body);
        break;

    case NODE_REPEAT:
        check_node(tc, node->repeat_stmt.body);
        break;

    case NODE_DO_WHILE:
        check_node(tc, node->do_while_stmt.body);
        check_node(tc, node->do_while_stmt.condition);
        if (node->do_while_stmt.condition && node->do_while_stmt.condition->type_info)
        {
            Type *cond_type = resolve_alias(node->do_while_stmt.condition->type_info);
            if (cond_type->kind != TYPE_BOOL && !is_integer_type(cond_type) &&
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
        switch (node->type)
        {
        case NODE_WHILE:
            check_node(tc, node->while_stmt.condition);
            check_node(tc, node->while_stmt.body);
            break;
        case NODE_FOR:
            check_node(tc, node->for_stmt.condition);
            check_node(tc, node->for_stmt.body);
            check_node(tc, node->for_stmt.step);
            break;
        case NODE_FOR_RANGE:
            check_node(tc, node->for_range.body);
            break;
        case NODE_LOOP:
            check_node(tc, node->loop_stmt.body);
            break;
        case NODE_REPEAT:
            check_node(tc, node->repeat_stmt.body);
            break;
        case NODE_DO_WHILE:
            check_node(tc, node->do_while_stmt.body);
            check_node(tc, node->do_while_stmt.condition);
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
        if (node->type != NODE_LOOP)
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
    if (next_iter_state)
    {
        move_state_free(next_iter_state);
    }
    if (loop_start)
    {
        move_state_free(loop_start);
    }

    // Restore outer context
    if (node->type == NODE_FOR)
    {
        tc_exit_scope(tc);
    }

    // If the loop is an infinite loop and has no breaks, it is unconditionally unreachable after.
    if ((node->type == NODE_LOOP || node->type == NODE_REPEAT) && !final_state)
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
}

static void check_node(TypeChecker *tc, ASTNode *node)
{
    if (!node)
    {
        return;
    }

    switch (node->type)
    {
    case NODE_ROOT:
    {
        ASTNode *child = node->root.children;
        while (child)
        {
            check_node(tc, child);
            child = child->next;
        }
    }
    break;
    case NODE_IMPL:
        if (node->impl.methods)
        {
            ASTNode *prev_impl_methods = tc->pctx->current_impl_methods;
            tc->pctx->current_impl_methods = node->impl.methods;

            ASTNode *next = node->impl.methods;
            while (next)
            {
                check_node(tc, next);
                next = next->next;
            }

            tc->pctx->current_impl_methods = prev_impl_methods;
        }
        break;
    case NODE_BLOCK:
        check_block(tc, node);
        break;
    case NODE_VAR_DECL:
        check_var_decl(tc, node);
        break;
    case NODE_FUNCTION:
        check_function(tc, node);
        break;
    case NODE_EXPR_VAR:
        check_expr_var(tc, node);
        break;
    case NODE_EXPR_LITERAL:
        check_expr_literal(tc, node);
        break;
    case NODE_RETURN:
        if (node->ret.value)
        {
            check_node(tc, node->ret.value);
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
                char msg[256];
                snprintf(msg, 255, "Return without value in function returning '%s'", ret_type);

                const char *hints[] = {"This function declares a non-void return type",
                                       "Return a value of the expected type", NULL};
                tc_error_with_hints(tc, node->token, msg, hints);
            }
        }
        tc->is_unreachable = 1;
        break;

    // Control flow with nested nodes.
    case NODE_IF:
        check_node(tc, node->if_stmt.condition);
        // Validate condition is boolean-compatible
        if (node->if_stmt.condition && node->if_stmt.condition->type_info)
        {
            Type *cond_type = resolve_alias(node->if_stmt.condition->type_info);
            if (cond_type->kind != TYPE_BOOL && !is_integer_type(cond_type) &&
                cond_type->kind != TYPE_POINTER && cond_type->kind != TYPE_STRING)
            {
                const char *hints[] = {"If conditions must be boolean, integer, or pointer", NULL};
                tc_error_with_hints(tc, node->if_stmt.condition->token,
                                    "Condition must be a truthy type", hints);
            }
        }

        MoveState *initial_state = tc->pctx->move_state;
        int initial_unreachable = tc->is_unreachable;

        if (initial_state)
        {
            tc->pctx->move_state = move_state_clone(initial_state);
        }
        check_node(tc, node->if_stmt.then_body);
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
            check_node(tc, node->if_stmt.else_body);
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
    case NODE_MATCH:
        check_node(tc, node->match_stmt.expr);
        // Visit each match case
        {
            MoveState *match_initial_state = tc->pctx->move_state;
            MoveState *merged_state = NULL;
            int match_initial_unreachable = tc->is_unreachable;
            int all_unreachable = 1;

            ASTNode *mcase = node->match_stmt.cases;
            int has_default = 0;
            while (mcase)
            {
                if (mcase->type == NODE_MATCH_CASE)
                {
                    if (match_initial_state)
                    {
                        tc->pctx->move_state = move_state_clone(match_initial_state);
                    }
                    tc->is_unreachable = match_initial_unreachable;

                    check_node(tc, mcase->match_case.body);

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

                    if (mcase->match_case.is_default)
                    {
                        has_default = 1;
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
                const char *hints[] = {"Add a default '_' case to handle all possibilities", NULL};
                tc_error_with_hints(tc, node->token,
                                    "Match may not be exhaustive (no default case)", hints);
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
    case NODE_WHILE:
    case NODE_FOR:
        check_loop_passes(tc, node);
        break;
    case NODE_EXPR_BINARY:
        check_expr_binary(tc, node);
        break;
    case NODE_EXPR_UNARY:
        check_expr_unary(tc, node);
        break;
    case NODE_EXPR_CALL:
        check_expr_call(tc, node);
        break;
    case NODE_EXPR_INDEX:
        check_node(tc, node->index.array);
        check_node(tc, node->index.index);

        if (node->index.array->type_info)
        {
            Type *t = node->index.array->type_info;
            int is_ptr = 0;
            if (t->kind == TYPE_POINTER && t->inner && t->inner->kind == TYPE_STRUCT)
            {
                t = t->inner;
                is_ptr = 1;
            }

            if (t->kind == TYPE_STRUCT && t->name)
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

                    node->type = NODE_EXPR_CALL;
                    memset(&node->call, 0, sizeof(node->call));

                    ASTNode *callee = ast_create(NODE_EXPR_MEMBER);
                    callee->token = node->token;
                    callee->member.target = array;
                    callee->member.field = xstrdup(method_name);
                    callee->member.is_pointer_access = is_ptr;

                    node->call.callee = callee;
                    node->call.args = idx;

                    check_expr_call(tc, node);
                    free(mangled_idx);
                    free(mangled_get);
                    break;
                }
                free(mangled_idx);
                free(mangled_get);
            }
            if (t->kind == TYPE_ARRAY || t->kind == TYPE_POINTER || t->kind == TYPE_VECTOR)
            {
                if (t->kind == TYPE_VECTOR && !t->inner && t->name)
                {
                    ASTNode *def = find_struct_def(tc->pctx, t->name);
                    if (def && def->type == NODE_STRUCT && def->strct.fields)
                    {
                        t->inner = def->strct.fields->type_info;
                    }
                }
                node->type_info = t->inner;
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
        check_node(tc, node->member.target);
        if (node->member.target && node->member.target->type_info)
        {
            Type *target_type = get_inner_type(node->member.target->type_info);
            // Look up struct field type
            if (target_type->kind == TYPE_STRUCT && target_type->name)
            {
                ASTNode *struct_def = find_struct_def(tc->pctx, target_type->name);
                if (struct_def)
                {
                    ASTNode *field = struct_def->strct.fields;
                    while (field)
                    {
                        if (field->type == NODE_FIELD && field->field.name &&
                            strcmp(field->field.name, node->member.field) == 0)
                        {
                            node->type_info = field->type_info;
                            break;
                        }
                        field = field->next;
                    }
                }
            }
        }
        if (!node->type_info)
        {
            // Fallback for method calls or failed lookups
            node->type_info = type_new(TYPE_UNKNOWN);
        }

        if (!tc->is_assign_lhs)
        {
            check_use_validity(tc, node);
        }
        break;
    case NODE_DEFER:
        // Check the deferred statement
        check_node(tc, node->defer_stmt.stmt);
        break;
    case NODE_GUARD:
        // Guard clause: if !condition return
        check_node(tc, node->guard_stmt.condition);
        if (node->guard_stmt.condition && node->guard_stmt.condition->type_info)
        {
            Type *cond_type = resolve_alias(node->guard_stmt.condition->type_info);
            if (cond_type->kind != TYPE_BOOL && !is_integer_type(cond_type) &&
                cond_type->kind != TYPE_POINTER && cond_type->kind != TYPE_STRING)
            {
                const char *hints[] = {"Guard conditions must be boolean, integer, or pointer",
                                       NULL};
                tc_error_with_hints(tc, node->guard_stmt.condition->token,
                                    "Condition must be a truthy type", hints);
            }
        }
        check_node(tc, node->guard_stmt.body);
        break;
    case NODE_UNLESS:
        // Unless is like if !condition
        check_node(tc, node->unless_stmt.condition);
        if (node->unless_stmt.condition && node->unless_stmt.condition->type_info)
        {
            Type *cond_type = resolve_alias(node->unless_stmt.condition->type_info);
            if (cond_type->kind != TYPE_BOOL && !is_integer_type(cond_type) &&
                cond_type->kind != TYPE_POINTER && cond_type->kind != TYPE_STRING)
            {
                const char *hints[] = {"Unless conditions must be boolean, integer, or pointer",
                                       NULL};
                tc_error_with_hints(tc, node->unless_stmt.condition->token,
                                    "Condition must be a truthy type", hints);
            }
        }
        check_node(tc, node->unless_stmt.body);
        break;
    case NODE_ASSERT:
        // Check assert condition
        check_node(tc, node->assert_stmt.condition);
        if (node->assert_stmt.condition && node->assert_stmt.condition->type_info)
        {
            Type *cond_type = resolve_alias(node->assert_stmt.condition->type_info);
            if (cond_type->kind != TYPE_BOOL && !is_integer_type(cond_type) &&
                cond_type->kind != TYPE_POINTER && cond_type->kind != TYPE_STRING)
            {
                const char *hints[] = {"Assert conditions must be boolean, integer, or pointer",
                                       NULL};
                tc_error_with_hints(tc, node->assert_stmt.condition->token,
                                    "Assert condition must be a truthy type", hints);
            }
        }
        break;
    case NODE_TEST:
    {
        MoveState *prev_move_state = tc->pctx->move_state;
        tc->pctx->move_state = move_state_create(NULL);

        check_node(tc, node->test_stmt.body);

        move_state_free(tc->pctx->move_state);
        tc->pctx->move_state = prev_move_state;
        break;
    }

    case NODE_EXPR_CAST:
        // Check the expression being cast
        check_node(tc, node->cast.expr);
        // Could add cast safety checks here (e.g., narrowing, pointer-to-int)
        if (node->cast.expr && node->cast.expr->type_info && node->cast.target_type)
        {
            Type *from_type = node->cast.expr->type_info;
            // Warn on pointer-to-integer casts (potential data loss)
            if (from_type->kind == TYPE_POINTER)
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
        }
        break;
    case NODE_EXPR_ARRAY_LITERAL:
    {
        ASTNode *elem = node->array_literal.elements;
        Type *elem_type = NULL;
        int count = 0;
        while (elem)
        {
            check_node(tc, elem);
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
        ASTNode *elem = node->tuple_literal.elements;
        while (elem)
        {
            check_node(tc, elem);
            elem = elem->next;
        }
    }
    break;
    case NODE_EXPR_STRUCT_INIT:
        check_struct_init(tc, node);
        break;
    case NODE_LOOP:
    case NODE_REPEAT:
        check_loop_passes(tc, node);
        break;
    case NODE_TERNARY:
        check_node(tc, node->ternary.cond);
        check_node(tc, node->ternary.true_expr);
        check_node(tc, node->ternary.false_expr);
        // Validate condition
        if (node->ternary.cond && node->ternary.cond->type_info)
        {
            Type *t = node->ternary.cond->type_info;
            if (t->kind != TYPE_BOOL && !is_integer_type(t) && t->kind != TYPE_POINTER)
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
                if (!check_type_compatibility(tc, t1, t2, node->token))
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
        for (int i = 0; i < node->asm_stmt.num_outputs; i++)
        {
            ZenSymbol *sym = tc_lookup(tc, node->asm_stmt.outputs[i]);
            if (!sym)
            {
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined output variable in inline assembly: '%s'",
                         node->asm_stmt.outputs[i]);
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
        for (int i = 0; i < node->asm_stmt.num_inputs; i++)
        {
            ZenSymbol *sym = tc_lookup(tc, node->asm_stmt.inputs[i]);
            if (!sym)
            {
                char msg[256];
                snprintf(msg, sizeof(msg), "Undefined input variable in inline assembly: '%s'",
                         node->asm_stmt.inputs[i]);
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
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Unsupported register size is required in inline assembly: %i bits",
                     node->asm_stmt.register_size);
            tc_error(tc, node->token, msg);
        }
        break;
    case NODE_LAMBDA:
        check_expr_lambda(tc, node);
        break;
    case NODE_EXPR_SIZEOF:
        if (node->size_of.expr)
        {
            check_node(tc, node->size_of.expr);
        }
        node->type_info = type_new(TYPE_I32);
        break;
    case NODE_FOR_RANGE:
        check_loop_passes(tc, node);
        break;
    case NODE_EXPR_SLICE:
        // Check slice target and indices
        check_node(tc, node->slice.array);
        check_node(tc, node->slice.start);
        check_node(tc, node->slice.end);
        break;
    case NODE_DESTRUCT_VAR:
        if (node->destruct.init_expr)
        {
            check_node(tc, node->destruct.init_expr);
        }
        break;
    case NODE_DO_WHILE:
        check_loop_passes(tc, node);
        break;
    case NODE_BREAK:
        if (tc->pctx->move_state)
        {
            move_state_merge_into(&tc->loop_break_state, tc->pctx->move_state);
        }
        tc->is_unreachable = 1;
        break;
    case NODE_CONTINUE:
        if (tc->pctx->move_state)
        {
            move_state_merge_into(&tc->loop_continue_state, tc->pctx->move_state);
        }
        tc->is_unreachable = 1;
        break;
    case NODE_GOTO:
    case NODE_LABEL:
        break;
    default:
        // Generic recursion for lists and other nodes.
        // Special case for Return to trigger move?
        if (node->type == NODE_RETURN && node->ret.value)
        {
            // If returning a value, check if it can be moved.
            check_move_for_rvalue(tc, node->ret.value);
        }
        break;
    }
}

static void check_expr_lambda(TypeChecker *tc, ASTNode *node)
{
    Type *expected = get_inner_type(node->type_info);
    if (expected && expected->kind == TYPE_FUNCTION && expected->is_raw)
    {
        if (node->lambda.num_captures == 0)
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
        for (int i = 0; i < node->lambda.num_captures; i++)
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

    for (int i = 0; i < node->lambda.num_params; i++)
    {
        char *pname = node->lambda.param_names[i];
        Type *ptype = NULL;
        Type *node_ti = get_inner_type(node->type_info);
        if (node_ti && node_ti->kind == TYPE_FUNCTION && node_ti->args)
        {
            if (i < node_ti->arg_count)
            {
                ptype = node_ti->args[i];
            }
        }
        tc_add_symbol(tc, pname, ptype, node->token);
    }

    MoveState *prev_move_state = tc->pctx->move_state;
    tc->pctx->move_state = move_state_create(NULL);

    int prev_unreachable = tc->is_unreachable;
    tc->is_unreachable = 0;

    if (node->lambda.body)
    {
        if (node->lambda.body->type == NODE_BLOCK)
        {
            check_block(tc, node->lambda.body);
        }
        else
        {
            check_node(tc, node->lambda.body);
        }
    }

    move_state_free(tc->pctx->move_state);
    tc->pctx->move_state = prev_move_state;

    tc->is_unreachable = prev_unreachable;
    tc_exit_scope(tc);
}

// ** Entry Point **

int check_program(ParserContext *ctx, ASTNode *root)
{
    TypeChecker tc = {0};
    tc.pctx = ctx;

    if (!ctx->move_state)
    {
        ctx->move_state = move_state_create(NULL);
    }

    check_node(&tc, root);

    if (ctx->move_state)
    {
        move_state_free(ctx->move_state);
        ctx->move_state = NULL;
    }

    if (tc.error_count > 0)
    {
        fprintf(stderr,
                COLOR_BOLD COLOR_RED "     error" COLOR_RESET
                                     ": semantic analysis found %d error%s\n",
                tc.error_count, tc.error_count == 1 ? "" : "s");
        return 1;
    }

    return 0;
}

int check_moves_only(ParserContext *ctx, ASTNode *root)
{
    TypeChecker tc = {0};
    tc.pctx = ctx;
    tc.move_checks_only = 1;

    if (!ctx->move_state)
    {
        ctx->move_state = move_state_create(NULL);
    }

    check_node(&tc, root);

    if (ctx->move_state)
    {
        move_state_free(ctx->move_state);
        ctx->move_state = NULL;
    }

    return tc.error_count;
}
