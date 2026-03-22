#ifndef TYPECHECK_H
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
    Scope *current_scope;  ///< Current lexical scope during traversal.
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
    int is_assign_lhs; ///< If true, currently evaluating LHS of assignment.
    int is_pure; ///< If true, currently checking a @pure function to ensure it only calls other pure functions.
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
void tc_move_error_with_hints(TypeChecker *tc, Token t, const char *msg, const char *const *hints);

#endif // TYPECHECK_H
