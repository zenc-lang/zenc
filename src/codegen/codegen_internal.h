// SPDX-License-Identifier: MIT
#ifndef CODEGEN_INTERNAL_H
#define CODEGEN_INTERNAL_H

// Cross-file declarations for codegen/*.c

// VisitedModules for tracking module cycles during emit
struct VisitedModules
{
    const char *path;
    struct VisitedModules *next;
};
int is_module_visited(struct VisitedModules *visited, const char *path);
void mark_module_visited(struct VisitedModules **visited, const char *path);
void free_visited_modules(struct VisitedModules *visited);

// Typedefs for dispatch tables
typedef void (*CodegenHandler)(ParserContext *ctx, ASTNode *node);
typedef void (*ExprHandler)(ParserContext *ctx, ASTNode *node);

// From codegen_stmt_handlers.c
void handle_node_ast_comment(ParserContext *ctx, ASTNode *node);
void handle_node_match(ParserContext *ctx, ASTNode *node);
void handle_node_assert(ParserContext *ctx, ASTNode *node);
void handle_node_expect(ParserContext *ctx, ASTNode *node);
void handle_node_defer(ParserContext *ctx, ASTNode *node);
void handle_node_comptime(ParserContext *ctx, ASTNode *node);
void handle_node_block(ParserContext *ctx, ASTNode *node);
void handle_node_impl(ParserContext *ctx, ASTNode *node);
void handle_node_function(ParserContext *ctx, ASTNode *node);
void handle_node_impl_trait(ParserContext *ctx, ASTNode *node);
void handle_node_destruct_var(ParserContext *ctx, ASTNode *node);
void handle_node_var_decl(ParserContext *ctx, ASTNode *node);
void handle_node_const(ParserContext *ctx, ASTNode *node);
void handle_node_field(ParserContext *ctx, ASTNode *node);
void handle_node_if(ParserContext *ctx, ASTNode *node);
void handle_node_unless(ParserContext *ctx, ASTNode *node);
void handle_node_guard(ParserContext *ctx, ASTNode *node);
void handle_node_while(ParserContext *ctx, ASTNode *node);

// From codegen_expr_core.c
void handle_expr_match(ParserContext *ctx, ASTNode *node);
void handle_expr_var(ParserContext *ctx, ASTNode *node);
void handle_lambda(ParserContext *ctx, ASTNode *node);
void handle_expr_literal(ParserContext *ctx, ASTNode *node);
void handle_raw_stmt(ParserContext *ctx, ASTNode *node);
void handle_ast_comment(ParserContext *ctx, ASTNode *node);
void handle_erroneous(ParserContext *ctx, ASTNode *node);
void handle_ternary(ParserContext *ctx, ASTNode *node);
void handle_await(ParserContext *ctx, ASTNode *node);
void handle_va_start(ParserContext *ctx, ASTNode *node);
void handle_va_end(ParserContext *ctx, ASTNode *node);
void handle_va_copy(ParserContext *ctx, ASTNode *node);
void handle_va_arg(ParserContext *ctx, ASTNode *node);

// From codegen_expr_handlers.c
void handle_expr_sizeof(ParserContext *ctx, ASTNode *node);
void handle_typeof(ParserContext *ctx, ASTNode *node);
void handle_expr_cast(ParserContext *ctx, ASTNode *node);
void handle_expr_unary(ParserContext *ctx, ASTNode *node);
void handle_expr_array_literal(ParserContext *ctx, ASTNode *node);
void handle_expr_tuple_literal(ParserContext *ctx, ASTNode *node);
void handle_expr_member(ParserContext *ctx, ASTNode *node);
void handle_expr_index(ParserContext *ctx, ASTNode *node);
void handle_expr_slice(ParserContext *ctx, ASTNode *node);
void handle_if_expr(ParserContext *ctx, ASTNode *node);
void handle_try_expr(ParserContext *ctx, ASTNode *node);
void handle_plugin(ParserContext *ctx, ASTNode *node);
void handle_expr_struct_init(ParserContext *ctx, ASTNode *node);
void handle_expr_binary(ParserContext *ctx, ASTNode *node);
void handle_expr_call(ParserContext *ctx, ASTNode *node);
void handle_block(ParserContext *ctx, ASTNode *node);
void handle_reflection(ParserContext *ctx, ASTNode *node);

// From codegen_stmt_match.c

// From codegen.c
extern int g_emitting_callee;

const char *get_missing_function_hint(ParserContext *ctx, const char *name);
#endif
