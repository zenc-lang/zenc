// SPDX-License-Identifier: MIT
#ifndef TYPECHECK_H
#ifndef ZC_ALLOW_INTERNAL
#error "analysis/typecheck.h is internal to Zen C. Include the appropriate public header instead."
#endif

#define TYPECHECK_H

#include "ast.h"
#include "parser.h"

struct MoveState; // Forward declaration

// Type Checker Context
// Holds the state during the semantic analysis pass.
// Unlike the parser, this focuses on semantic validity (types, definitions).
/**
 * @brief Type Checker Context.
 *
 * Holds the state during the semantic analysis pass.
 * Unlike the parser, this focuses on semantic validity (types, definitions, correctness).
 */
typedef struct TypeChecker
{
    ParserContext *pctx;   ///< Reference to global parser context (for lookups).
    ASTNode *current_func; ///< Current function being checked (for return type checks).
    int error_count;       ///< Number of type errors found.
    int warning_count;     // Number of recommendations/warnings.

    // Flow Analysis State
    struct MoveState *move_state; ///< Current state of moved variables.
    int is_unreachable;           ///< Path ends in break/return/continue
    struct MoveState *loop_break_state;
    struct MoveState *loop_continue_state;
    struct MoveState *loop_start_state;
    int in_loop_pass2;

    // Configuration
    int move_checks_only; ///< If true, only report move semantics violations (no type errors).

    // Tracking
    int is_assign_lhs;      ///< If true, currently evaluating LHS of assignment.
    int is_stmt_context;    ///< If true, expression is a top-level statement.
    int is_forget_receiver; ///< If true, receiver of a .forget() call (use-after-move is expected).
    int loop_break_count;   ///< Count of breaks for Rule 15.4
    int func_return_count;  ///< Count of returns for Rule 15.5
    int current_depth;      ///< Current nesting level for escape analysis (0=global).
} TypeChecker;

/**
 * @brief Main Type Checking Entry Point.
 *
 * Performs semantic analysis on the entire AST.
 *
 * @param ctx Global parser context.
 * @param root Root AST node of the program.
 * @return 0 on success (no errors), non-zero if errors occurred.
 */
int check_program(ParserContext *ctx, ASTNode *root);

/**
 * @brief Move-Only Checking Entry Point.
 *
 * Performs only move semantics analysis (use-after-move detection)
 * without reporting type errors. Always runs, even without --typecheck.
 *
 * @param ctx Global parser context.
 * @param root Root AST node of the program.
 * @return 0 on success (no move errors), non-zero if move errors occurred.
 */
/**
 * @brief Move-Only Checking Entry Point.
 */
int check_moves_only(ParserContext *ctx, ASTNode *root);

// Error helpers available to move_check.c
void tc_error(TypeChecker *tc, Token t, const char *msg);
void tc_error_with_hints(TypeChecker *tc, Token t, const char *msg, const char *const *hints);
void tc_ctrl_flow_error(TypeChecker *tc, Token t, const char *msg, const char *const *hints);
void tc_move_error_with_hints(TypeChecker *tc, Token t, const char *msg, const char *const *hints);

// Exported helpers for modularization
Type *resolve_alias(Type *t);
int tc_expr_has_side_effects(ASTNode *node);

#endif // TYPECHECK_H
