// SPDX-License-Identifier: MIT
#ifndef PLATFORM_MISRA_H
#ifndef ZC_ALLOW_INTERNAL
#error "platform/misra.h is internal to Zen C. Include the appropriate public header instead."
#endif

#define PLATFORM_MISRA_H

#include <stdio.h>
#include <stdbool.h>
#include "../token.h"

typedef struct Type Type;
typedef struct ASTNode ASTNode;
typedef struct ParserContext ParserContext;

/**
 * @brief Emits a strictly stripped-down C preamble devoid of dynamic
 *        memory and I/O headers, ensuring 100% MISRA Compliance of the Core artifact.
 */
void emit_misra_preamble(FILE *out);

// --- MISRA C:2012 Compliance Modules ---

// Section 10/11/12: Essential Type Model & Conversions
void misra_check_implicit_conversion(ParserContext *ctx, struct Type *target, struct Type *source,
                                     struct ASTNode *source_node, Token token);
void misra_check_char_arithmetic(ParserContext *ctx, struct Type *left, struct Type *right,
                                 const char *op, Token token);
void misra_check_bitwise_operand(ParserContext *ctx, struct Type *t, Token token);
void misra_check_shift_amount(ParserContext *ctx, long long amount, int width, Token token);
void misra_check_pointer_conversion(ParserContext *ctx, struct Type *target, struct Type *source,
                                    Token token);
void misra_check_void_ptr_cast(ParserContext *ctx, struct Type *target, struct Type *source,
                               Token token);
void misra_check_cast(ParserContext *ctx, struct Type *target, struct Type *source, Token token,
                      bool is_composite);
void misra_check_binary_op_essential_types(ParserContext *ctx, struct ASTNode *left,
                                           struct ASTNode *right, Token token);
void misra_check_unsigned_wrap(ParserContext *ctx, const char *op, long long left, long long right,
                               long long res, struct Type *type, Token token);

// Section 13/14/15: Expressions & Control Flow
void misra_check_side_effects_sizeof(ParserContext *ctx, struct ASTNode *expr);
void misra_check_assignment_result_used(ParserContext *ctx, Token token);
void misra_check_inc_dec_result_used(ParserContext *ctx, Token token);
void misra_check_condition_boolean(ParserContext *ctx, struct Type *t, Token token);
void misra_check_invariant_condition(ParserContext *ctx, Token token);
void misra_check_loop_counter_float(ParserContext *ctx, struct Type *t, Token token);
void misra_check_initializer_side_effects(ParserContext *ctx, struct ASTNode *node);

// Section 16: Match/Switch
void misra_check_match_stmt(ParserContext *ctx, struct ASTNode *node);
void misra_check_strict_match(ParserContext *ctx, struct ASTNode *node);
void misra_check_shadowing(ParserContext *ctx, const char *name, Token token);
void misra_check_double_initialization(ParserContext *ctx, const char *field_name, Token token);

// Section 17: Functions
void misra_check_recursion(ParserContext *ctx, Token token);
void misra_check_function_return_usage(ParserContext *ctx, struct ASTNode *node);
void misra_check_array_param_size(ParserContext *ctx, int expected, int actual, Token token);
void misra_check_external_array_size(ParserContext *ctx, Type *t, Token token, int is_static,
                                     int is_local);
void misra_check_param_modified(struct ASTNode *current_func, struct ASTNode *left, Token token);
void misra_check_pointer_arithmetic(ParserContext *ctx, Type *t, Token token);
void misra_check_pointer_nesting(ParserContext *ctx, Type *t, Token token);
void misra_check_struct_decl(ParserContext *ctx, struct ASTNode *node);
void misra_check_compound_body(ParserContext *ctx, struct ASTNode *body, const char *stmt_name);
void misra_check_terminal_else(ParserContext *ctx, struct ASTNode *if_node);
void misra_check_param_nesting(ParserContext *ctx, struct ASTNode *func_node);
void misra_check_unused_param(ParserContext *ctx, const char *name, Token token);
void misra_check_const_ptr_param(ParserContext *ctx, const char *name, Token token);
void misra_check_goto(ParserContext *ctx, Token token);
void misra_check_goto_constraint(ParserContext *ctx, Token goto_tok, Token label_tok);
void misra_check_union(ParserContext *ctx, Token token);
void misra_check_iteration_termination(ParserContext *ctx, Token token);
void misra_check_stdarg(ParserContext *ctx, Token token);
void misra_check_identifier_collision(Token tok, const char *name1, const char *name2, int limit);

// Section 2: Unused Code
void misra_audit_unused_symbols(ParserContext *ctx);

// Section 5: Identifiers
void misra_audit_identifier_uniqueness(ParserContext *ctx);
void misra_audit_block_scope(ParserContext *ctx);
void misra_check_standard_macro_name(Token tok, const char *name);

// Section 19: Overlapping Storage
void misra_check_assignment_overlap(ParserContext *ctx, struct ASTNode *left, struct ASTNode *right,
                                    Token token);

// Zen C Extensions
void misra_check_raw_block(ParserContext *ctx, Token token);
void misra_check_preprocessor_expression_parser(struct ParserContext *ctx, Token tok,
                                                const char *expression);
void misra_check_plugin_block(ParserContext *ctx, Token token);
void misra_check_reserved_identifier(ParserContext *ctx, const char *name, Token token);
void misra_check_banned_function(ParserContext *ctx, const char *name, Token tok);
void misra_check_file_dereference(ParserContext *ctx, struct Type *type, Token tok);
void misra_check_tuple_size(ParserContext *ctx, struct Type *ret_type, Token token);
void misra_check_string_compare(ParserContext *ctx, struct Type *left, struct Type *right,
                                Token token);

// Phase 3 MISRA
void misra_check_evaluation_order(ParserContext *ctx, struct ASTNode *expr);
void misra_check_typographic_ambiguity(ParserContext *ctx, const char *new_name, Token loc);
void misra_check_null_pointer_constant(ParserContext *ctx, struct ASTNode *node, Token token);

#endif // PLATFORM_MISRA_H
