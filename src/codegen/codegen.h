// SPDX-License-Identifier: MIT

#ifndef ZC_ALLOW_INTERNAL
#error "codegen/codegen.h is internal to Zen C. Include the appropriate public header instead."
#endif

#ifndef CODEGEN_H
#define CODEGEN_H

#include "../ast/ast.h"
#include "../token.h"
#include "codegen_backend.h"
#include <stdio.h>
#include "../utils/emitter.h"

typedef struct ParserContext ParserContext;

#define EMIT(ctx, ...) emitter_printf(&(ctx)->cg.emitter, ##__VA_ARGS__)

// Main codegen entry points.

/**
 * @brief Generates code for a given AST node using the selected backend.
 * Dispatches through the registered backend's emit_program.
 *
 * @param ctx Parser context.
 * @param node The AST node to generate code for.
 */
void codegen_node(ParserContext *ctx, ASTNode *node);

/**
 * @brief Internal C codegen entry (used as the default backend).
 * Called by codegen_node() when the "c" backend is active.
 */
void codegen_c_program(ParserContext *ctx, ASTNode *node);

/**
 * @brief Generates code for a single AST node (non-recursive for siblings).
 */
void codegen_node_single(ParserContext *ctx, ASTNode *node);

/**
 * @brief Walker for list of nodes (calls codegen_node recursively).
 */
void codegen_walker(ParserContext *ctx, ASTNode *node);

/**
 * @brief Generates code for an expression node.
 */
void codegen_expression(ParserContext *ctx, ASTNode *node);

/**
 * @brief Generates code for an expression without outermost parentheses.
 *
 * Used in contexts where extra parentheses break semantics (e.g. OpenMP
 * canonical for-loop form requires bare controlling predicates).
 */
void codegen_expression_bare(ParserContext *ctx, ASTNode *node);

/**
 * @brief Internal handler for match statements.
 */
void codegen_match_internal(ParserContext *ctx, ASTNode *node, int use_result);

// Utility functions (codegen_utils.c + codegen_shared.c).
#include "codegen_shared.h"

void emit_var_decl_type(ParserContext *ctx, const char *type_str, const char *var_name);
void emit_lambda_decl_type(ParserContext *ctx, const char *type_str, const char *lambda_name,
                           int ptrs);
void emit_auto_type(ParserContext *ctx, ASTNode *init_expr, Token t);
void emit_func_signature(ParserContext *ctx, ASTNode *func, const char *name_override);
int emit_move_invalidation(ParserContext *ctx, ASTNode *node);
void codegen_expression_with_move(ParserContext *ctx, ASTNode *node);
void emit_mangled_name(ParserContext *ctx, const char *base, const char *method);
int is_simple_enum(ParserContext *ctx, const char *enum_name);
int is_enum_type_name(ParserContext *ctx, const char *name);
void handle_node_await_internal(ParserContext *ctx, ASTNode *node);

// Declaration emission  (codegen_decl.c).
/**
 * @brief Emits the standard preamble (includes, macros) to the output file.
 */
typedef struct VisitedModules VisitedModules;

void emit_preamble(ParserContext *ctx);
void emit_includes_and_aliases(ParserContext *ctx, ASTNode *node, VisitedModules **visited);
void emit_type_aliases(ParserContext *ctx, ASTNode *node, VisitedModules **visited);
void emit_global_aliases(ParserContext *ctx);
void emit_struct_defs(ParserContext *ctx, ASTNode *node, VisitedModules **visited);
void emit_trait_defs(ParserContext *ctx, ASTNode *node, VisitedModules **visited);
void emit_trait_wrappers(ParserContext *ctx, ASTNode *node, VisitedModules **visited);
void emit_enum_protos(ParserContext *ctx, ASTNode *node);
void emit_globals(ParserContext *ctx, ASTNode *node, VisitedModules **visited);
void emit_lambda_defs(ParserContext *ctx);
void emit_protos(ParserContext *ctx, ASTNode *node, VisitedModules **visited);
void emit_impl_vtables(ParserContext *ctx);

/**
 * @brief Emits test runner and test cases if testing is enabled.
 */
int emit_tests_and_runner(ParserContext *ctx, ASTNode *node);
void print_type_defs(ParserContext *ctx, ASTNode *nodes);

/**
 * @brief Emits C preprocessor directives for source mapping.
 */
void emit_source_mapping(ParserContext *ctx, ASTNode *node);
/**
 * @brief Emits C preprocessor directives for source mapping.
 * Special override for emit_source_mapping that allows duplicate source mappings for 1:N expression
 * mapping. This is a QoL function that improves the debugging experience.
 */
void emit_source_mapping_duplicate(ParserContext *ctx, ASTNode *node);

// Defer stack size limit
#define MAX_DEFER 1024

void emit_pending_closure_frees(ParserContext *ctx);

#endif
