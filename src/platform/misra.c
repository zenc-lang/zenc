// SPDX-License-Identifier: MIT
#include "ast/ast.h"
#include "constants.h"
#include "platform/misra.h"
#include "parser/parser.h"
#include "zprep.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// Forward declarations from analysis headers (standalone functions only, no TypeChecker dependency)
Type *resolve_alias(Type *t);
int tc_expr_has_side_effects(ASTNode *node);
int eval_const_int_expr(struct ASTNode *node, struct ParserContext *ctx, long long *out);

// Standard C macro/type names from headers used in the MISRA preamble
// (<stddef.h>, <stdint.h>, <stdbool.h>) plus common names from other
// C standard headers that may appear in FFI contexts.
static const char *STANDARD_MACRO_NAMES[] = {
    // <stddef.h>
    "NULL", "offsetof", "ptrdiff_t", "size_t", "wchar_t", "max_align_t",
    // <stdint.h> exact-width
    "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    // <stdint.h> minimum-width
    "int_least8_t", "int_least16_t", "int_least32_t", "int_least64_t", "uint_least8_t",
    "uint_least16_t", "uint_least32_t", "uint_least64_t",
    // <stdint.h> fastest-width
    "int_fast8_t", "int_fast16_t", "int_fast32_t", "int_fast64_t", "uint_fast8_t", "uint_fast16_t",
    "uint_fast32_t", "uint_fast64_t",
    // <stdint.h> other
    "intptr_t", "uintptr_t", "intmax_t", "uintmax_t",
    // <stdint.h> limit macros
    "INT8_MIN", "INT16_MIN", "INT32_MIN", "INT64_MIN", "INT8_MAX", "INT16_MAX", "INT32_MAX",
    "INT64_MAX", "UINT8_MAX", "UINT16_MAX", "UINT32_MAX", "UINT64_MAX", "INT_LEAST8_MIN",
    "INT_LEAST16_MIN", "INT_LEAST32_MIN", "INT_LEAST64_MIN", "INT_LEAST8_MAX", "INT_LEAST16_MAX",
    "INT_LEAST32_MAX", "INT_LEAST64_MAX", "UINT_LEAST8_MAX", "UINT_LEAST16_MAX", "UINT_LEAST32_MAX",
    "UINT_LEAST64_MAX", "INT_FAST8_MIN", "INT_FAST16_MIN", "INT_FAST32_MIN", "INT_FAST64_MIN",
    "INT_FAST8_MAX", "INT_FAST16_MAX", "INT_FAST32_MAX", "INT_FAST64_MAX", "UINT_FAST8_MAX",
    "UINT_FAST16_MAX", "UINT_FAST32_MAX", "UINT_FAST64_MAX", "INTPTR_MIN", "INTPTR_MAX",
    "UINTPTR_MAX", "INTMAX_MIN", "INTMAX_MAX", "UINTMAX_MAX", "PTRDIFF_MIN", "PTRDIFF_MAX",
    "SIZE_MAX",
    // <stdbool.h>
    "bool", "true", "false", "__bool_true_false_are_defined",
    // Common names from other C headers (may appear via FFI)
    "assert", "errno", "EOF", NULL};

void emit_misra_preamble(FILE *out)
{
    // Minimal standard headers allowed by MISRA C.
    // Explicitly excluding <stdlib.h>, <stdio.h>, and <string.h>.
    fputs("#include <stddef.h>\n", out);
    fputs("#include <stdint.h>\n", out);
    fputs("#include <stdbool.h>\n", out);
}

// SECTION 10/11/12: Essential Type Model & Conversions

typedef enum
{
    ESS_BOOL,
    ESS_CHAR,
    ESS_SIGNED,
    ESS_UNSIGNED,
    ESS_FLOAT,
    ESS_UNKNOWN
} EssentialCategory;

static EssentialCategory get_essential_category(Type *t)
{
    if (!t)
    {
        return ESS_UNKNOWN;
    }
    Type *res = resolve_alias(t);
    if (is_boolean_type(res))
    {
        return ESS_BOOL;
    }
    if (res->kind == TYPE_CHAR || res->kind == TYPE_C_CHAR)
    {
        return ESS_CHAR;
    }
    if (is_unsigned_type(res))
    {
        return ESS_UNSIGNED;
    }
    if (is_signed_type(res))
    {
        return ESS_SIGNED;
    }
    if (is_float_type(res))
    {
        return ESS_FLOAT;
    }
    return ESS_UNKNOWN;
}

static int get_type_width(Type *t)
{
    if (!t)
    {
        return 0;
    }
    Type *res = resolve_alias(t);
    static const int widths[] = {
        [TYPE_I8] = 8,        [TYPE_U8] = 8,          [TYPE_CHAR] = 8,         [TYPE_C_CHAR] = 8,
        [TYPE_C_UCHAR] = 8,   [TYPE_I16] = 16,        [TYPE_U16] = 16,         [TYPE_C_SHORT] = 16,
        [TYPE_C_USHORT] = 16, [TYPE_I32] = 32,        [TYPE_U32] = 32,         [TYPE_INT] = 32,
        [TYPE_UINT] = 32,     [TYPE_C_INT] = 32,      [TYPE_C_UINT] = 32,      [TYPE_I64] = 64,
        [TYPE_U64] = 64,      [TYPE_USIZE] = 64,      [TYPE_ISIZE] = 64,       [TYPE_C_LONG] = 64,
        [TYPE_C_ULONG] = 64,  [TYPE_C_LONGLONG] = 64, [TYPE_C_ULONGLONG] = 64, [TYPE_F32] = 32,
        [TYPE_F64] = 64};
    if (res->kind < (int)(sizeof(widths) / sizeof(widths[0])))
    {
        return widths[res->kind];
    }
    return 0;
}

void misra_check_implicit_conversion(struct ParserContext *ctx, struct Type *target,
                                     struct Type *source, struct ASTNode *source_node, Token token)
{
    if (!ctx->config->misra_mode)
    {
        return;
    }
    EssentialCategory ct = get_essential_category(target);
    EssentialCategory cs = get_essential_category(source);

    if (ct != cs && ct != ESS_UNKNOWN && cs != ESS_UNKNOWN)
    {
        if (ct == ESS_BOOL || cs == ESS_BOOL)
        {
            zerror_at(token, "MISRA Rule 10.4");
        }
        else if ((ct == ESS_SIGNED && cs == ESS_UNSIGNED) ||
                 (ct == ESS_UNSIGNED && cs == ESS_SIGNED))
        {
            zerror_at(token, "MISRA Rule 10.4");
        }
    }

    int tw = get_type_width(target);
    int sw = get_type_width(source);
    if (sw > tw)
    {
        zerror_at(token, "MISRA Rule 10.3");
    }
    else if (tw > sw && source_node && is_composite_expression(source_node))
    {
        // Rule 10.6: Composite expression assigned to wider type
        zerror_at(token, "MISRA Rule 10.6");
    }

    // Rule 11.1 & 11.4: Pointer <-> Integer conversions
    Type *rt = resolve_alias(target);
    Type *rs = resolve_alias(source);
    if ((rt->kind == TYPE_POINTER && is_integer_type(rs)) ||
        (is_integer_type(rt) && rs->kind == TYPE_POINTER))
    {
        // Check if literal 0 (Rule 11.9 handled elsewhere, but non-zero is 11.4)
        int is_zero = 0;
        if (source_node && source_node->kind == NODE_EXPR_LITERAL &&
            source_node->literal.kind == LITERAL_INT && source_node->literal.int_val == 0)
        {
            is_zero = 1;
        }

        if (!is_zero)
        {
            if ((rs->kind == TYPE_POINTER && rs->inner &&
                 resolve_alias(rs->inner)->kind == TYPE_FUNCTION) ||
                (rt->kind == TYPE_POINTER && rt->inner &&
                 resolve_alias(rt->inner)->kind == TYPE_FUNCTION))
            {
                zerror_at(token, "MISRA Rule 11.1");
            }
            else
            {
                zerror_at(token, "MISRA Rule 11.4");
            }
        }
    }
}

void misra_check_char_arithmetic(ParserContext *ctx, Type *left, Type *right, const char *op,
                                 Token token)
{
    if (!ctx->config->misra_mode)
    {
        return;
    }
    EssentialCategory cl = get_essential_category(left);
    EssentialCategory cr = get_essential_category(right);

    if (cl == ESS_CHAR || cr == ESS_CHAR)
    {
        if (strcmp(op, "-") == 0)
        {
            // char - char is allowed (Rule 10.2)
            if (cl == ESS_CHAR && cr == ESS_CHAR)
            {
                return;
            }
        }

        zerror_at(token, "MISRA Rule 10.2");
    }
}

void misra_check_bitwise_operand(ParserContext *ctx, Type *t, Token token)
{
    if (!ctx->config->misra_mode)
    {
        return;
    }
    if (get_essential_category(t) != ESS_UNSIGNED)
    {
        zerror_at(token, "MISRA Rule 10.1");
    }
}

void misra_check_shift_amount(ParserContext *ctx, long long amount, int width, Token token)
{
    if (!ctx->config->misra_mode)
    {
        return;
    }
    if (amount < 0 || (width > 0 && amount >= width))
    {
        zerror_at(token, "MISRA Rule 12.2");
    }
}

void misra_check_pointer_conversion(ParserContext *ctx, Type *target, Type *source, Token token)
{
    if (!ctx->config->misra_mode)
    {
        return;
    }
    Type *rt = resolve_alias(target);
    Type *rs = resolve_alias(source);

    if ((rt->kind == TYPE_POINTER || rt->kind == TYPE_FUNCTION) &&
        (rs->kind == TYPE_POINTER || rs->kind == TYPE_FUNCTION))
    {
        Type *rti = (rt->kind == TYPE_POINTER) ? resolve_alias(rt->inner) : rt;
        Type *rsi = (rs->kind == TYPE_POINTER) ? resolve_alias(rs->inner) : rs;

        // Rule 11.8: Cast shall not remove const qualification from the type pointed to
        if (rsi && (rsi->is_const || (rs->is_const && rsi == rs->inner)) && rti && !rti->is_const)
        {
            zerror_at(token, "MISRA Rule 11.8");
        }

        // Rule 11.3: pointer to different object type
        // We only trigger this if the underlying object types are fundamentally different.
        // Qualification differences are handled by Rule 11.8.
        if (rti && rsi)
        {
            // Rule 11.2: Pointer to incomplete type
            if (is_incomplete_type(ctx, rti) || is_incomplete_type(ctx, rsi))
            {
                zerror_at(token, "MISRA Rule 11.2");
            }

            TypeKind tk = rti->kind;
            TypeKind sk = rsi->kind;

            // Treat all integer kinds as the same "object type" for 11.3;
            // other rules handle narrowing/etc.
            int t_is_int = is_integer_type(rti);
            int s_is_int = is_integer_type(rsi);

            if (tk != sk && !(t_is_int && s_is_int))
            {
                // Rule 11.1: func pointer vs other
                if (tk == TYPE_FUNCTION || sk == TYPE_FUNCTION)
                {
                    zerror_at(token, "MISRA Rule 11.1");
                }
                else
                {
                    zerror_at(token, "MISRA Rule 11.3");
                }
            }
        }
    }
    else if (rt->kind == TYPE_POINTER && is_integer_type(rs))
    {
        // Rule 11.4 allows NULL. In Zen C we check for literal 0.
        // We'll further refine this in misra_check_null_pointer_constant.
    }
}

void misra_check_void_ptr_cast(ParserContext *ctx, Type *target, Type *source, Token token)
{
    if (!ctx->config->misra_mode || !target || !source)
    {
        return;
    }
    Type *rt = resolve_alias(target);
    Type *rs = resolve_alias(source);

    // Rule 11.5: A conversion should not be performed from pointer to void into pointer to object
    if (rs->kind == TYPE_POINTER && rs->inner)
    {
        Type *rs_inner = resolve_alias(rs->inner);
        if (rs_inner->kind == TYPE_VOID)
        {
            if (rt->kind == TYPE_POINTER && rt->inner)
            {
                Type *rt_inner = resolve_alias(rt->inner);
                if (rt_inner->kind != TYPE_VOID)
                {
                    zerror_at(token, "MISRA Rule 11.5");
                }
            }
        }
    }
}

void misra_check_cast(ParserContext *ctx, Type *target, Type *source, Token token,
                      bool is_composite)
{
    if (!ctx->config->misra_mode)
    {
        return;
    }
    if (is_composite)
    {
        int tw = get_type_width(target);
        int sw = get_type_width(source);
        if (tw > sw)
        {
            zerror_at(token, "MISRA Rule 10.8");
        }
        if (get_essential_category(target) != get_essential_category(source))
        {
            zerror_at(token, "MISRA Rule 10.8");
        }
    }
}

void misra_check_null_pointer_constant(ParserContext *ctx, struct ASTNode *node, Token token)
{
    if (!ctx->config->misra_mode || !node)
    {
        return;
    }

    struct ASTNode *expr = node;
    while (expr && expr->kind == NODE_EXPR_CAST)
    {
        expr = expr->cast.expr;
    }

    // Rule 11.9: The macro NULL shall be the only permitted form of integer null pointer constant.
    if (expr && expr->kind == NODE_EXPR_LITERAL && expr->literal.kind == LITERAL_INT)
    {
        // In MISRA C, we expect the NULL macro. In Zen C, we prefer the 'null' keyword.
        // If we found a literal 0, it means the user used '0' or '(type)0' instead of 'null'.
        if (expr->literal.int_val == 0)
        {
            zerror_at(token, "MISRA Rule 11.9");
        }
        else
        {
            zerror_at(token, "MISRA Rule 11.4");
        }
    }
}

void misra_check_binary_op_essential_types(ParserContext *ctx, struct ASTNode *left,
                                           struct ASTNode *right, Token token)
{
    if (!ctx->config->misra_mode || !left || !right || !left->type_info || !right->type_info)
    {
        return;
    }

    Type *rl = resolve_alias(left->type_info);
    Type *rr = resolve_alias(right->type_info);

    // Only applies to numeric essential types for Rule 10.4
    if (!is_integer_type(rl) || !is_integer_type(rr))
    {
        return;
    }

    EssentialCategory cl = get_essential_category(rl);
    EssentialCategory cr = get_essential_category(rr);

    if (cl != cr && cl != ESS_UNKNOWN && cr != ESS_UNKNOWN)
    {
        if (cl == ESS_BOOL || cr == ESS_BOOL)
        {
            zerror_at(token, "MISRA Rule 10.4");
        }
        else if ((cl == ESS_SIGNED && cr == ESS_UNSIGNED) ||
                 (cl == ESS_UNSIGNED && cr == ESS_SIGNED))
        {
            zerror_at(token, "MISRA Rule 10.4");
        }
    }

    // Rule 10.6/10.7: Composite expressions
    if (is_composite_expression(left) || is_composite_expression(right))
    {
        int lw = get_type_width(rl);
        int rw = get_type_width(rr);
        if (is_composite_expression(left) && rw > lw)
        {
            zerror_at(token, "MISRA Rule 10.7");
        }
        if (is_composite_expression(right) && lw > rw)
        {
            zerror_at(token, "MISRA Rule 10.7");
        }
    }
}

// SECTION 13/14/15: Expressions & Control Flow

void misra_check_side_effects_sizeof(ParserContext *ctx, ASTNode *expr)
{
    if (ctx->config->misra_mode)
    {
        // Simple heuristic: if it contains a call or assignment/inc/dec it has potential side
        // effects. We assume typechecker already validated this for Rule 13.6 if applicable.
        zerror_at(expr->token, "MISRA Rule 12.5");
        zerror_at(expr->token, "MISRA Rule 13.6");
    }
}

void misra_check_assignment_result_used(ParserContext *ctx, Token token)
{
    if (ctx->config->misra_mode)
    {
        zerror_at(token, "MISRA Rule 13.4");
    }
}

void misra_check_inc_dec_result_used(ParserContext *ctx, Token token)
{
    if (ctx->config->misra_mode)
    {
        zerror_at(token, "MISRA Rule 13.3");
    }
}

void misra_check_condition_boolean(ParserContext *ctx, Type *t, Token token)
{
    if (ctx->config->misra_mode && t)
    {
        if (get_essential_category(t) != ESS_BOOL)
        {
            zerror_at(token, "MISRA Rule 14.4");
        }
    }
}

void misra_check_invariant_condition(ParserContext *ctx, Token token)
{
    if (ctx->config->misra_mode)
    {
        zerror_at(token, "MISRA Rule 14.3");
    }
}

void misra_check_loop_counter_float(ParserContext *ctx, Type *t, Token token)
{
    if (ctx->config->misra_mode && is_float_type(t))
    {
        zerror_at(token, "MISRA Rule 14.1");
    }
}

void misra_check_initializer_side_effects(ParserContext *ctx, ASTNode *node)
{
    if (ctx->config->misra_mode)
    {
        if (tc_expr_has_side_effects(node))
        {
            zerror_at(node->token, "MISRA Rule 13.1");
        }
    }
}

// SECTION 16: Match/Switch

/**
 * @brief Enforces Rules 16.4, 16.5, 16.6, and 16.7 for match statements.
 */
void misra_check_match_stmt(ParserContext *ctx, ASTNode *node)
{
    if (!ctx->config->misra_mode || !node || node->kind != NODE_MATCH)
    {
        return;
    }

    misra_check_strict_match(ctx, node);

    int has_default = 0;
    int case_count = 0;
    int is_first_or_last = 0;

    ASTNode *case_node = node->match_stmt.cases;
    while (case_node)
    {
        case_count++;
        if (case_node->match_case.is_default)
        {
            has_default = 1;
            if (case_count == 1 || case_node->next == NULL)
            {
                is_first_or_last = 1;
            }
        }
        case_node = case_node->next;
    }

    if (!has_default)
    {
        zerror_at(node->token, "MISRA Rule 16.4");
    }
    else if (!is_first_or_last)
    {
        zerror_at(node->token, "MISRA Rule 16.5");
    }

    if (case_count < 2)
    {
        zerror_at(node->token, "MISRA Rule 16.6");
    }

    if (node->match_stmt.expr && node->match_stmt.expr->type_info)
    {
        Type *expr_type = resolve_alias(node->match_stmt.expr->type_info);
        if (expr_type->kind == TYPE_BOOL)
        {
            zerror_at(node->match_stmt.expr->token, "MISRA Rule 16.7");
        }
    }
}

// SECTION 17: Functions

/**
 * @brief Rule 17.2 (Required): Functions shall not call themselves, either directly or indirectly.
 */
void misra_check_recursion(ParserContext *ctx, Token token)
{
    if (ctx->config->misra_mode)
    {
        zerror_at(token, "MISRA Rule 17.2");
    }
}

/**
 * @brief Rule 17.7 (Required): The value returned by a function having non-void return type shall
 * be used.
 */
void misra_check_function_return_usage(ParserContext *ctx, ASTNode *node)
{
    if (ctx->config->misra_mode && node && node->type_info)
    {
        Type *rt = resolve_alias(node->type_info);
        if (rt->kind != TYPE_VOID)
        {
            if (rt->kind == TYPE_STRUCT && rt->name &&
                (strstr(rt->name, "Result") || strstr(rt->name, "Option") ||
                 strstr(rt->name, "Error")))
            {
                zerror_at(node->token, "MISRA Dir 4.7: error information is not tested");
            }
            else
            {
                zerror_at(node->token, "MISRA Rule 17.7");
            }
        }
    }
}

/**
 * @brief Rule 17.5 (Advisory): Array size mismatch in function parameters.
 */
void misra_check_array_param_size(ParserContext *ctx, int expected, int actual, Token token)
{
    if (ctx->config->misra_mode && expected > 0 && actual < expected)
    {
        zerror_at(token, "MISRA Rule 17.5");
    }
}

void misra_check_unused_param(ParserContext *ctx, const char *name, Token token)
{
    (void)ctx;
    (void)name;
    zerror_at(token, "MISRA Rule 2.7");
}

void misra_check_const_ptr_param(ParserContext *ctx, const char *name, Token token)
{
    (void)ctx;
    (void)name;
    zerror_at(token, "MISRA Rule 8.13");
}

/**
 * @brief Rule 17.8 (Advisory): A function parameter should not be modified.
 */
void misra_check_param_modified(ASTNode *current_func, ASTNode *left, Token token)
{
    if (!current_func || !left || left->kind != NODE_EXPR_VAR)
    {
        return;
    }

    const char *name = left->var_ref.name;
    for (int i = 0; i < current_func->func.count; i++)
    {
        if (strcmp(current_func->func.param_names[i], name) == 0)
        {
            zerror_at(token, "MISRA Rule 17.8");
            return;
        }
    }
}

// SECTION 18: Pointers & Arrays

/**
 * @brief Rule 18.4 (Advisory): The +, -, += and -= operators should not be applied to an
 * expression of pointer type.
 */
void misra_check_pointer_arithmetic(ParserContext *ctx, Type *t, Token token)
{
    if (ctx->config->misra_mode && t)
    {
        Type *resolved = resolve_alias(t);
        if (resolved->kind == TYPE_POINTER)
        {
            zerror_at(token, "MISRA Rule 18.4");
        }
    }
}

static int get_pointer_nesting_depth(Type *t)
{
    if (!t)
    {
        return 0;
    }
    Type *resolved = resolve_alias(t);
    if (resolved->kind == TYPE_POINTER)
    {
        return 1 + get_pointer_nesting_depth(resolved->inner);
    }
    return 0;
}

/**
 * @brief Rule 18.5 (Advisory): Declarations should contain no more than two levels of pointer
 * nesting.
 */
void misra_check_pointer_nesting(ParserContext *ctx, Type *t, Token token)
{
    if (!ctx->config->misra_mode || !t)
    {
        return;
    }

    int depth = get_pointer_nesting_depth(t);
    if (depth > 2)
    {
        zerror_at(token, "MISRA Rule 18.5");
    }
}

/**
 * @brief Ensures struct fields comply with Rule 18.5.
 */
void misra_check_struct_decl(ParserContext *ctx, ASTNode *node)
{
    if (!ctx->config->misra_mode)
    {
        return;
    }
    if (!node || node->kind != NODE_STRUCT)
    {
        return;
    }

    ASTNode *field = node->strct.fields;
    while (field)
    {
        if (field->kind == NODE_FIELD)
        {
            misra_check_pointer_nesting(ctx, field->type_info, field->token);

            // Rule 6.1 & 6.2: Bit-fields
            if (field->field.bit_width > 0)
            {
                Type *t = resolve_alias(field->type_info);
                if (t->kind != TYPE_BOOL && t->kind != TYPE_U8 && t->kind != TYPE_U16 &&
                    t->kind != TYPE_U32 && t->kind != TYPE_U64 && t->kind != TYPE_I8 &&
                    t->kind != TYPE_I16 && t->kind != TYPE_I32 && t->kind != TYPE_I64 &&
                    t->kind != TYPE_UINT && t->kind != TYPE_INT && t->kind != TYPE_BYTE)
                {
                    zerror_at(field->token, "MISRA Rule 6.1");
                }

                if (field->field.bit_width == 1 && is_signed_type(t))
                {
                    zerror_at(field->token, "MISRA Rule 6.2");
                }
            }

            // Rule 18.7: Flexible array members
            if (field->type_info && field->type_info->kind == TYPE_ARRAY &&
                field->type_info->array_size == 0 && field->next == NULL)
            {
                zerror_at(field->token, "MISRA Rule 18.7");
            }
        }
        field = field->next;
    }
}

/**
 * @brief Rule 15.6 (Required): The body of an if, while, for, or do-while shall be a compound
 * statement.
 */
void misra_check_compound_body(ParserContext *ctx, ASTNode *body, const char *stmt_name)
{
    if (!ctx->config->misra_mode || !body)
    {
        return;
    }

    if (body->kind != NODE_BLOCK)
    {
        (void)stmt_name;
        zerror_at(body->token, "MISRA Rule 15.6");
    }
}

/**
 * @brief Rule 15.7 (Required): All if ... else if constructs shall be terminated with an else
 * statement.
 */
void misra_check_terminal_else(ParserContext *ctx, ASTNode *if_node)
{
    if (!ctx->config->misra_mode || !if_node || if_node->kind != NODE_IF)
    {
        return;
    }

    // Traverse the if-else-if chain.
    ASTNode *curr = if_node;
    while (curr && curr->kind == NODE_IF)
    {
        if (curr->if_stmt.else_body)
        {
            if (curr->if_stmt.else_body->kind == NODE_IF)
            {
                // Another else-if, continue traversing.
                curr = curr->if_stmt.else_body;
            }
            else
            {
                // Found a terminal else (usually a NODE_BLOCK), Rule satisfied.
                return;
            }
        }
        else
        {
            // Found an if/else-if without an else body.
            // Check if this was a chain.
            if (curr != if_node)
            {
                zerror_at(if_node->token, "MISRA Rule 15.7");
            }
            return;
        }
    }
}

/**
 * @brief Ensures Rule 18.5 compliance for function parameters.
 */
void misra_check_param_nesting(ParserContext *ctx, ASTNode *func_node)
{
    if (!ctx->config->misra_mode || !func_node || func_node->kind != NODE_FUNCTION)
    {
        return;
    }

    for (int i = 0; i < func_node->func.count; ++i)
    {
        if (func_node->func.arg_types[i])
        {
            misra_check_pointer_nesting(ctx, func_node->func.arg_types[i], func_node->token);
        }
    }
}

/**
 * @brief Rule 15.1 (Advisory): The goto statement should not be used.
 */
void misra_check_goto(ParserContext *ctx, Token token)
{
    if (!ctx->config->misra_mode)
    {
        return;
    }
    zerror_at(token, "MISRA Rule 15.1");
}

void misra_check_goto_constraint(ParserContext *ctx, Token goto_tok, Token label_tok)
{
    if (!ctx->config->misra_mode)
    {
        return;
    }

    // Rule 15.2/15.3: Jumping backwards/generic goto constraints
    if (label_tok.line < goto_tok.line)
    {
        zerror_at(goto_tok, "MISRA Rule 15.2");
        zerror_at(goto_tok, "MISRA Rule 15.3");
    }

    // Note: Rule 15.2 (jumping into nested blocks) is partially handled
    // by Zen C's block-scoped labels if implemented that way,
    // but we add a generic error if we detect it.
}

/**
 * @brief Rule 15.4 (Advisory): There shall be no more than one break or goto statement used
 * to terminate any iteration statement.
 */
void misra_check_iteration_termination(ParserContext *ctx, Token token)
{
    if (!ctx->config->misra_mode)
    {
        return;
    }
    zerror_at(token, "MISRA Rule 15.4");
}

/**
 * @brief Rule 19.2 (Advisory): The union keyword should not be used.
 */
void misra_check_union(ParserContext *ctx, Token token)
{
    if (!ctx->config->misra_mode)
    {
        return;
    }
    zerror_at(token, "MISRA Rule 19.2");
}

/**
 * @brief Rule 17.1 (Required): Features of <stdarg.h> shall not be used.
 */
void misra_check_stdarg(ParserContext *ctx, Token token)
{
    if (!ctx->config->misra_mode)
    {
        return;
    }
    zerror_at(token, "MISRA Rule 17.1");
}

void misra_audit_unused_symbols(ParserContext *ctx)
{
    if (!ctx->config->misra_mode || !ctx->global_scope)
    {
        return;
    }

    ZenSymbol *sym = ctx->global_scope->symbols;
    while (sym)
    {
        // Skip exported symbols, underscored symbols, and special names like 'main'
        // Also skip built-ins (line 0)
        if (!sym->is_used && !sym->is_export && sym->name && sym->name[0] != '_' &&
            0 != strcmp(sym->name, "main") && sym->decl_token.line != 0)
        {
            switch (sym->kind)
            {
            case SYM_ALIAS:
                zerror_at(sym->decl_token, "MISRA Rule 2.3");
                break;
            case SYM_STRUCT:
            case SYM_ENUM:
                zerror_at(sym->decl_token, "MISRA Rule 2.4");
                break;
            case SYM_CONSTANT:
                zerror_at(sym->decl_token, "MISRA Rule 2.5");
                break;
            default:
                break;
            }
        }
        else if (sym->is_used && sym->kind == SYM_STRUCT && !sym->is_dereferenced &&
                 sym->decl_token.line != 0)
        {
            if (sym->data.node && sym->data.node->kind == NODE_STRUCT &&
                !sym->data.node->strct.is_opaque)
            {
                zerror_at(
                    sym->decl_token,
                    "MISRA Dir 4.8: pointer to struct is never dereferenced (make it opaque)");
            }
        }
        sym = sym->next;
    }
}

void misra_check_identifier_collision(Token tok, const char *name1, const char *name2, int limit)
{
    if (!name1 || !name2)
    {
        return;
    }

    if (strncmp(name1, name2, (size_t)(limit)) == 0)
    {
        char msg[MAX_SHORT_MSG_LEN];
        if (limit == 31)
        {
            snprintf(msg, sizeof(msg), "MISRA Rule 5.1");
        }
        else
        {
            snprintf(msg, sizeof(msg), "MISRA Rule 5.2");
        }
        zerror_at(tok, "%s", msg);
    }
}

/**
 * @brief Checks for identifier uniqueness across the entire project (Rules 5.8 and 5.9).
 */
void misra_audit_identifier_uniqueness(ParserContext *ctx)
{
    if (!ctx->config->misra_mode || !ctx->all_symbols)
    {
        return;
    }

    ZenSymbol *s1 = ctx->all_symbols;
    while (s1)
    {
        // Skip built-ins and special names
        if (s1->decl_token.line == 0 || !s1->name || s1->name[0] == '_' ||
            strcmp(s1->name, "main") == 0 || strcmp(s1->name, "it") == 0 ||
            strcmp(s1->name, "self") == 0)
        {
            s1 = s1->next;
            continue;
        }

        // Determine linkage of s1
        // For Zen, we consider module-level symbols as having linkage.
        // symbols with is_export=1 have external linkage.
        int linkage1 = 0; // 1 = external, 2 = internal

        // Skip local variables — they don't have linkage and should
        // not be compared across different scopes (handled by 5.1/5.2/5.3)
        if (s1->is_local)
        {
            s1 = s1->next;
            continue;
        }

        if (s1->is_export || s1->link_name)
        {
            linkage1 = 1;
        }
        else if (s1->kind == SYM_FUNCTION || s1->kind == SYM_VARIABLE || s1->kind == SYM_CONSTANT)
        {
            // Module-level functions, variables, and constants have internal linkage
            linkage1 = 2;
        }

        if (linkage1 == 0)
        {
            s1 = s1->next;
            continue;
        }

        ZenSymbol *s2 = s1->next;
        while (s2)
        {
            // Skip same checks
            if (s2->decl_token.line == 0 || !s2->name || s2->name[0] == '_' ||
                strcmp(s2->name, "main") == 0 || strcmp(s2->name, "it") == 0 ||
                strcmp(s2->name, "self") == 0)
            {
                s2 = s2->next;
                continue;
            }

            // Skip local variables (same reasoning as outer loop)
            if (s2->is_local)
            {
                s2 = s2->next;
                continue;
            }

            int linkage2 = 0;
            if (s2->is_export || s2->link_name)
            {
                linkage2 = 1;
            }
            else if (s2->kind == SYM_FUNCTION || s2->kind == SYM_VARIABLE ||
                     s2->kind == SYM_CONSTANT)
            {
                linkage2 = 2;
            }

            if (linkage2 == 0)
            {
                s2 = s2->next;
                continue;
            }

            // Rule 5.8: External identifiers shall be unique
            if (linkage1 == 1 && linkage2 == 1)
            {
                // Check exact name collision
                if (strcmp(s1->name, s2->name) == 0)
                {
                    zerror_at(s2->decl_token, "MISRA Rule 5.8");
                }
                // Check @link_name collision specifically
                else if (s1->link_name && s2->link_name &&
                         strcmp(s1->link_name, s2->link_name) == 0)
                {
                    zerror_at(s2->decl_token, "MISRA Rule 5.8");
                }
            }
            // Rule 5.9: Internal identifiers should be unique (Advisory)
            else if (linkage1 == 2 && linkage2 == 2)
            {
                if (strcmp(s1->name, s2->name) == 0)
                {
                    zerror_at(s2->decl_token, "MISRA Rule 5.9");
                }
            }

            s2 = s2->next;
        }

        s1 = s1->next;
    }
}

void misra_check_raw_block(struct ParserContext *ctx, Token token)
{
    if (ctx->config->misra_mode)
    {
        zerror_at(token, "MISRA Rule Zen 1.1");
    }
}

void misra_check_plugin_block(struct ParserContext *ctx, Token token)
{
    if (ctx->config->misra_mode)
    {
        zerror_at(token, "MISRA Rule Zen 1.2");
    }
}

void misra_check_preprocessor_expression_parser(struct ParserContext *ctx, Token tok,
                                                const char *expression)
{
    if (!ctx || !expression)
    {
        return;
    }

    // Manual scan for Rule 20.9 (Undefined identifiers)
    const char *p = expression;
    while (*p)
    {
        while (*p && !isalpha(*p) && *p != '_')
        {
            p++;
        }
        if (!*p)
        {
            break;
        }

        const char *start = p;
        while (isalnum(*p) || *p == '_')
        {
            p++;
        }
        ptrdiff_t len = p - start;

        char name[128];
        if (len >= 128)
        {
            len = 127;
        }
        strncpy(name, start, (size_t)(len));
        name[len] = 0;

        // Skip 'defined' operator
        if (strcmp(name, "defined") == 0)
        {
            while (*p && !isalnum(*p) && *p != '_')
            {
                p++;
            }
            while (*p && (isalnum(*p) || *p == '_'))
            {
                p++;
            }
            continue;
        }

        // Rule 20.9 check
        int is_defined = 0;
        // 1. Check CLI defines
        for (size_t i = 0; i < ctx->config->cfg_defines.length; i++)
        {
            if (strcmp(ctx->config->cfg_defines.data[i], name) == 0)
            {
                is_defined = 1;
                break;
            }
        }
        // 2. Check Symbol Table (via find_symbol_entry)
        if (!is_defined && ctx)
        {
            if (find_symbol_entry(ctx, name))
            {
                is_defined = 1;
            }
        }

        if (!is_defined)
        {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "MISRA Rule 20.9: Identifier '%s' in preprocessor expression is not defined",
                     name);
            zerror_at(tok, "%s", msg);
        }
    }

    // Rule 20.8 Evaluation (Simplified check for 0/1)
    // We try to parse and evaluate the expression using Zen's internal constant folder
    if (ctx)
    {
        Lexer l;
        lexer_init(&l, expression, ctx->config, ctx->current_filename);
        ASTNode *expr_node = parse_expression(ctx, &l);
        if (expr_node)
        {
            long long val;
            if (eval_const_int_expr(expr_node, ctx, &val))
            {
                if (val != 0 && val != 1)
                {
                    zerror_at(tok,
                              "MISRA Rule 20.8: Controlling expression must evaluate to 0 or 1");
                }
            }
        }
    }
}

void misra_check_strict_match(ParserContext *ctx, ASTNode *node)
{
    if (!ctx->config->misra_mode || !node || node->kind != NODE_MATCH || !node->match_stmt.expr)
    {
        return;
    }

    Type *expr_type = resolve_alias(node->match_stmt.expr->type_info);
    if (!expr_type || expr_type->kind != TYPE_ENUM)
    {
        return;
    }

    ASTNode *case_node = node->match_stmt.cases;
    while (case_node)
    {
        if (case_node->match_case.is_default)
        {
            zerror_at(case_node->token, "MISRA Rule Zen 1.3");
        }
        case_node = case_node->next;
    }
}

void misra_check_shadowing(ParserContext *ctx, const char *name, Token loc)
{
    if (!ctx->config->misra_mode || !name || !ctx->current_scope || !ctx->current_scope->parent)
    {
        return;
    }

    // 'self' is a special receiver keyword in Zen (like 'this' in other languages).
    // It appears in every method definition and is intentionally reused — not a real shadowing
    // concern.
    if (strcmp(name, "self") == 0)
    {
        return;
    }

    ZenSymbol *shadowed = symbol_lookup(ctx->current_scope->parent, name);
    if (shadowed)
    {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "MISRA Rule Zen 1.8: Identifier '%s' shadows an existing symbol in an outer scope",
                 name);
        zerror_at(loc, "%s", msg);
    }
}

void misra_check_double_initialization(struct ParserContext *ctx, const char *field_name,
                                       Token token)
{
    if (!ctx->config->misra_mode)
    {
        return;
    }
    char msg[256];
    snprintf(msg, sizeof(msg), "MISRA Rule 9.4: Re-initialization of struct field '%s'",
             field_name);
    zerror_at(token, "%s", msg);
}

void misra_check_reserved_identifier(struct ParserContext *ctx, const char *name, Token token)
{
    if (!ctx->config->misra_mode || !name)
    {
        return;
    }

    // Rule Zen 2.1: Reserved identifiers
    if ((name[0] == '_' && (name[1] == '_' || (name[1] >= 'A' && name[1] <= 'Z'))) ||
        (strncmp(name, "_z_", 3) == 0))
    {
        zerror_at(token, "MISRA Rule Zen 2.1");
    }
}

void misra_check_unsigned_wrap(struct ParserContext *ctx, const char *op, long long left,
                               long long right, long long res, struct Type *type, Token token)
{
    (void)res;
    if (!ctx->config->misra_mode || !type)
    {
        return;
    }

    Type *resolved = resolve_alias(type);
    TypeKind k = resolved->kind;
    if (k != TYPE_U8 && k != TYPE_U16 && k != TYPE_U32 && k != TYPE_U64 && k != TYPE_USIZE)
    {
        return;
    }

    unsigned long long max_val;
    switch (k)
    {
    case TYPE_U8:
        max_val = 255;
        break;
    case TYPE_U16:
        max_val = 65535;
        break;
    case TYPE_U32:
        max_val = 4294967295ULL;
        break;
    case TYPE_U64:
        max_val = 18446744073709551615ULL;
        break;
    case TYPE_USIZE:
        max_val = (sizeof(void *) == 8) ? 18446744073709551615ULL : 4294967295ULL;
        break;
    default:
        return;
    }

    bool wrap = false;
    unsigned long long l = (unsigned long long)left;
    unsigned long long r = (unsigned long long)right;

    if (strcmp(op, "+") == 0)
    {
        if (l > max_val - r)
        {
            wrap = true;
        }
    }
    else if (strcmp(op, "-") == 0)
    {
        if (r > l)
        {
            wrap = true;
        }
    }
    else if (strcmp(op, "*") == 0)
    {
        if (l != 0 && r > max_val / l)
        {
            wrap = true;
        }
    }

    if (wrap)
    {
        zerror_at(token,
                  "MISRA Rule 12.4: Evaluation of constant expression leads to unsigned integer "
                  "wrap-around");
    }
}

void misra_audit_block_scope(struct ParserContext *ctx)
{
    if (!ctx->config->misra_mode || !ctx->global_scope)
    {
        return;
    }

    ZenSymbol *sym = ctx->global_scope->symbols;
    while (sym)
    {
        // Rule 8.9: An object should be defined at block scope if its identifier only appears
        // in a single function.
        // Heuristic: Global variables (not exported, not static) used in exactly one function.
        // We also skip built-ins (line 0).
        if (sym->kind == SYM_VARIABLE && !sym->is_export && !sym->is_static &&
            sym->decl_token.line != 0 && sym->first_using_func != NULL && !sym->multi_func_use)
        {
            zerror_at(sym->decl_token, "MISRA Rule 8.9");
        }
        sym = sym->next;
    }
}

void misra_check_standard_macro_name(Token tok, const char *name)
{
    if (!name)
    {
        return;
    }

    for (int i = 0; STANDARD_MACRO_NAMES[i] != NULL; i++)
    {
        if (strcmp(name, STANDARD_MACRO_NAMES[i]) == 0)
        {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "MISRA Rule 5.10: identifier '%s' has the same name as standard macro '%s'",
                     name, STANDARD_MACRO_NAMES[i]);
            zerror_at(tok, "%s", msg);
            return;
        }
    }
}

void misra_check_external_array_size(ParserContext *ctx, Type *t, Token token, int is_static,
                                     int is_local)
{
    if (!ctx->config->misra_mode || is_static || is_local || !t)
    {
        return;
    }

    Type *resolved = resolve_alias(t);
    if (resolved->kind == TYPE_ARRAY && resolved->array_size == 0)
    {
        zerror_at(token, "MISRA Rule 8.11: External array shall have explicit size");
    }
}

static struct
{
    const char *name;
    const char *rule;
} banned_funcs[] = {{"malloc", "Rule 21.3"},         {"calloc", "Rule 21.3"},
                    {"realloc", "Rule 21.3"},        {"free", "Rule 21.3"},
                    {"printf", "Rule 21.6"},         {"fprintf", "Rule 21.6"},
                    {"scanf", "Rule 21.6"},          {"fscanf", "Rule 21.6"},
                    {"atof", "Rule 21.7"},           {"atoi", "Rule 21.7"},
                    {"atol", "Rule 21.7"},           {"atoll", "Rule 21.7"},
                    {"abort", "Rule 21.8"},          {"exit", "Rule 21.8"},
                    {"getenv", "Rule 21.8"},         {"system", "Rule 21.8"},
                    {"bsearch", "Rule 21.9"},        {"qsort", "Rule 21.9"},
                    {"asctime", "Rule 21.10"},       {"ctime", "Rule 21.10"},
                    {"gmtime", "Rule 21.10"},        {"localtime", "Rule 21.10"},
                    {"time", "Rule 21.10"},          {"setjmp", "Rule 21.4"},
                    {"longjmp", "Rule 21.4"},        {"signal", "Rule 21.5"},
                    {"raise", "Rule 21.5"},          {"sqrt", "Rule 21.11"},
                    {"sin", "Rule 21.11"},           {"cos", "Rule 21.11"},
                    {"tan", "Rule 21.11"},           {"asin", "Rule 21.11"},
                    {"acos", "Rule 21.11"},          {"atan", "Rule 21.11"},
                    {"atan2", "Rule 21.11"},         {"exp", "Rule 21.11"},
                    {"log", "Rule 21.11"},           {"log10", "Rule 21.11"},
                    {"pow", "Rule 21.11"},           {"fabs", "Rule 21.11"},
                    {"floor", "Rule 21.11"},         {"ceil", "Rule 21.11"},
                    {"fmod", "Rule 21.11"},          {"feclearexcept", "Rule 21.12"},
                    {"feraiseexcept", "Rule 21.12"}, {NULL, NULL}};

void misra_check_banned_function(struct ParserContext *ctx, const char *name, Token tok)
{
    if (!ctx->config->misra_mode || !name)
    {
        return;
    }

    for (int i = 0; banned_funcs[i].name != NULL; i++)
    {
        if (strcmp(name, banned_funcs[i].name) == 0)
        {
            char msg[128];
            snprintf(msg, sizeof(msg), "MISRA %s: Use of banned standard library function '%s'",
                     banned_funcs[i].rule, name);
            zerror_at(tok, "%s", msg);
            return;
        }
    }
}

void misra_check_file_dereference(struct ParserContext *ctx, struct Type *type, Token tok)
{
    if (!ctx->config->misra_mode || !type)
    {
        return;
    }

    Type *resolved = resolve_alias(type);
    if (resolved->kind == TYPE_POINTER && resolved->inner)
    {
        Type *inner = resolve_alias(resolved->inner);
        // rule 22.5: FILE object shall not be dereferenced
        // In Zen, we check if the inner type's name is 'FILE'
        // as it's typically an opaque struct imported from C
        if (inner->kind == TYPE_STRUCT && inner->name && strcmp(inner->name, "FILE") == 0)
        {
            zerror_at(tok, "MISRA Rule 22.5: FILE object should not be dereferenced");
        }
    }
}

#include <ctype.h>

static void canonicalize_ambiguous_chars(const char *src, char *dst, size_t dest_size)
{
    size_t i = 0;
    while (src[i] && i < dest_size - 1)
    {
        char c = src[i];
        if (c == 'l' || c == 'I')
        {
            c = '1';
        }
        else if (c == 'O')
        {
            c = '0';
        }
        else if (c == 'S')
        {
            c = '5';
        }
        else if (c == 'Z')
        {
            c = '2';
        }
        else if (c == 'n')
        {
            c = 'h';
        }
        else if (c == 'B')
        {
            c = '8';
        }
        dst[i] = (char)tolower((unsigned char)c);
        i++;
    }
    dst[i] = '\0';
}

void misra_check_typographic_ambiguity(struct ParserContext *ctx, const char *new_name, Token loc)
{
    if (!ctx || !ctx->config->misra_mode || !new_name)
    {
        return;
    }

    char new_canon[256];
    canonicalize_ambiguous_chars(new_name, new_canon, sizeof(new_canon));

    Scope *s = ctx->current_scope;
    while (s)
    {
        ZenSymbol *sym = s->symbols;
        while (sym)
        {
            // Do not compare against itself (though initially it shouldn't be listed yet)
            if (sym->name && strcmp(sym->name, new_name) != 0)
            {
                char exist_canon[256];
                canonicalize_ambiguous_chars(sym->name, exist_canon, sizeof(exist_canon));

                if (strcmp(new_canon, exist_canon) == 0)
                {
                    char msg[512];
                    snprintf(
                        msg, sizeof(msg),
                        "MISRA Dir 4.5: identifier '%s' is typographically ambiguous with '%s'",
                        new_name, sym->name);
                    zerror_at(loc, "%s", msg);
                    return; // Warn once
                }
            }
            sym = sym->next;
        }
        s = s->parent;
    }
}

void misra_check_tuple_size(struct ParserContext *ctx, struct Type *t, Token token)
{
    if (!ctx->config->misra_mode || !t || t->kind != TYPE_STRUCT || !t->name)
    {
        return;
    }
    // Tuple types have names like "Tuple__int__string"
    if (strncmp(t->name, "Tuple__", 7) != 0)
    {
        return;
    }
    // Look up the tuple in the registry to get field count
    TupleType *tup = ctx->used_tuples;
    while (tup)
    {
        char *clean_sig = sanitize_mangled_name(tup->sig);
        char expected[1024];
        snprintf(expected, sizeof(expected), "Tuple__%s", clean_sig);
        zfree(clean_sig);
        if (strcmp(t->name, expected) == 0)
        {
            if (tup->count >= 3)
            {
                zerror_at(token, "MISRA Rule Zen 2.2: tuple with 3 or more fields shall be "
                                 "replaced with a named struct (use 'struct' instead of "
                                 "positional tuple)");
            }
            break;
        }
        tup = tup->next;
    }
}

void misra_check_string_compare(struct ParserContext *ctx, struct Type *left, struct Type *right,
                                Token token)
{
    if (!ctx->config->misra_mode)
    {
        return;
    }
    if (!left || !right)
    {
        return;
    }
    // Check if both sides are string types
    int left_is_str =
        (left->kind == TYPE_STRING || (left->name && strcmp(left->name, "string") == 0));
    int right_is_str =
        (right->kind == TYPE_STRING || (right->name && strcmp(right->name, "string") == 0));
    if (left_is_str && right_is_str)
    {
        zerror_at(token, "MISRA Rule Zen 2.3: 'string == string' shall not be used; "
                         "use strcmp() instead for string comparison");
    }
}

/**
 * @brief Rule 19.1 (Required): An object shall not be assigned to an overlapping object.
 * Detects self-assignment (x = x) which is undefined behavior in C.
 */
void misra_check_assignment_overlap(struct ParserContext *ctx, struct ASTNode *left,
                                    struct ASTNode *right, Token token)
{
    if (!ctx->config->misra_mode || !left || !right)
    {
        return;
    }

    // Check for self-assignment: same variable on both sides
    if (left->kind == NODE_EXPR_VAR && right->kind == NODE_EXPR_VAR)
    {
        if (left->var_ref.symbol && right->var_ref.symbol &&
            left->var_ref.symbol == right->var_ref.symbol)
        {
            zerror_at(token,
                      "MISRA Rule 19.1: object shall not be assigned to itself (self-assignment)");
        }
    }
}

/**
 * @brief Rule 13.2 extension (Required): Expression evaluation order shall not be relied upon.
 * This is a conservative check that flags function call arguments where multiple arguments
 * have side effects on the same variables (unspecified behaviour in C).
 */
void misra_check_evaluation_order(struct ParserContext *ctx, struct ASTNode *expr)
{
    if (!ctx->config->misra_mode || !expr || expr->kind != NODE_EXPR_CALL)
    {
        return;
    }

    // Simple heuristic: flag if multiple arguments have side effects.
    // Detailed analysis is handled by Rule 13.2 in the typechecker.
    int count = 0;
    ASTNode *arg = expr->call.args;
    while (arg)
    {
        if (tc_expr_has_side_effects(arg))
        {
            count++;
            if (count >= 2)
            {
                zerror_at(expr->token, "MISRA Rule 13.2: function call arguments have conflicting "
                                       "side effects (evaluation order unspecified)");
                return;
            }
        }
        arg = arg->next;
    }
}
