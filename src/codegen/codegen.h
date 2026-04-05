
#ifndef CODEGEN_H
#define CODEGEN_H

#include "../ast/ast.h"
#include "../parser/parser.h"
#include "../zprep.h"
#include <stdio.h>

// Main codegen entry points.

/**
 * @brief Generates code for a given AST node.
 *
 * @param ctx Parser context.
 * @param node The AST node to generate code for.
 * @param out Output file stream.
 */
void codegen_node(ParserContext *ctx, ASTNode *node, FILE *out);

/**
 * @brief Generates code for a single AST node (non-recursive for siblings).
 */
void codegen_node_single(ParserContext *ctx, ASTNode *node, FILE *out);

/**
 * @brief Walker for list of nodes (calls codegen_node recursively).
 */
void codegen_walker(ParserContext *ctx, ASTNode *node, FILE *out);

/**
 * @brief Generates code for an expression node.
 */
void codegen_expression(ParserContext *ctx, ASTNode *node, FILE *out);

/**
 * @brief Generates code for an expression without outermost parentheses.
 *
 * Used in contexts where extra parentheses break semantics (e.g. OpenMP
 * canonical for-loop form requires bare controlling predicates).
 */
void codegen_expression_bare(ParserContext *ctx, ASTNode *node, FILE *out);

/**
 * @brief Internal handler for match statements.
 */
void codegen_match_internal(ParserContext *ctx, ASTNode *node, FILE *out, int use_result);

// Utility functions (codegen_utils.c).
char *infer_type(ParserContext *ctx, ASTNode *node);
char *get_field_type_str(ParserContext *ctx, const char *struct_name, const char *field_name);
char *extract_call_args(const char *args);
void emit_var_decl_type(ParserContext *ctx, FILE *out, const char *type_str, const char *var_name);
char *replace_string_type(const char *args);
const char *parse_original_method_name(const char *mangled);
void emit_auto_type(ParserContext *ctx, ASTNode *init_expr, Token t, FILE *out);
void emit_func_signature(ParserContext *ctx, FILE *out, ASTNode *func, const char *name_override);
char *strip_template_suffix(const char *name);
int emit_move_invalidation(ParserContext *ctx, ASTNode *node, FILE *out);
void codegen_expression_with_move(ParserContext *ctx, ASTNode *node, FILE *out);
int is_struct_return_type(const char *ret_type);
int z_is_struct_type(Type *t);
void emit_mangled_name(FILE *out, const char *base, const char *method);

// Declaration emission  (codegen_decl.c).
/**
 * @brief Emits the standard preamble (includes, macros) to the output file.
 */
void emit_preamble(ParserContext *ctx, FILE *out);
void emit_includes_and_aliases(ASTNode *node, FILE *out);
void emit_type_aliases(ASTNode *node, FILE *out);
void emit_global_aliases(ParserContext *ctx, FILE *out);
void emit_struct_defs(ParserContext *ctx, ASTNode *node, FILE *out);
void emit_trait_defs(ASTNode *node, FILE *out);
void emit_trait_wrappers(ASTNode *node, FILE *out);
void emit_enum_protos(ParserContext *ctx, ASTNode *node, FILE *out);
void emit_globals(ParserContext *ctx, ASTNode *node, FILE *out);
void emit_lambda_defs(ParserContext *ctx, FILE *out);
void emit_protos(ParserContext *ctx, ASTNode *node, FILE *out);
void emit_impl_vtables(ParserContext *ctx, FILE *out);

/**
 * @brief Emits test runner and test cases if testing is enabled.
 */
int emit_tests_and_runner(ParserContext *ctx, ASTNode *node, FILE *out);
void print_type_defs(ParserContext *ctx, FILE *out, ASTNode *nodes);

/**
 * @brief Emits C preprocessor directives for source mapping.
 */
void emit_source_mapping(ASTNode *node, FILE *out);
/**
 * @brief Emits C preprocessor directives for source mapping.
 * Special override for emit_source_mapping that allows duplicate source mappings for 1:N expression
 * mapping. This is a QoL function that improves the debugging experience.
 */
void emit_source_mapping_duplicate(ASTNode *node, FILE *out);

// Global state (shared across modules).
extern ASTNode *global_user_structs;  ///< List of user defined structs.
extern char *g_current_impl_type;     ///< Type currently being implemented (in impl block).
extern int tmp_counter;               ///< Counter for temporary variables.
extern int defer_count;               ///< Counter for defer statements in current scope.
extern ASTNode *defer_stack[];        ///< Stack of deferred nodes.
extern ASTNode *g_current_lambda;     ///< Current lambda being generated.
extern char *g_current_func_ret_type; ///< Return type of current function.
extern Type *g_current_func_ret_type_info;

// Defer boundary tracking for proper defer execution on break/continue/return
#define MAX_DEFER 1024
#define MAX_LOOP_DEPTH 64
extern int loop_defer_boundary[]; ///< Defer stack index at start of each loop.
extern int loop_depth;            ///< Current loop nesting depth.
extern int func_defer_boundary;   ///< Defer stack index at function entry.

// Closure context free tracking
#define MAX_PENDING_CLOSURE_FREES 64
extern int pending_closure_frees[]; ///< Lambda IDs whose ctx needs freeing.
extern int pending_closure_free_count;
void emit_pending_closure_frees(FILE *out);

#endif
