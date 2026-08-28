// SPDX-License-Identifier: MIT
#include "../parser/parser.h"

#include "codegen.h"
#include "zprep.h"
#include "../constants.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../plugins/plugin_manager.h"
#include "ast.h"
#include "zprep_plugin.h"
#include "codegen_internal.h"

void handle_block(ParserContext *ctx, ASTNode *node)
{
    int saved = ctx->cg.defer_count;
    EMIT(ctx, "({ ");
    codegen_walker(ctx, node->block.statements);
    for (int i = ctx->cg.defer_count - 1; i >= saved; i--)
    {
        emit_source_mapping_duplicate(ctx, ctx->cg.defer_stack[i]);
        codegen_node_single(ctx, ctx->cg.defer_stack[i]);
    }
    ctx->cg.defer_count = saved;
    EMIT(ctx, " })");
}

void handle_if_expr(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "({ ");

    ASTNode *then_result = NULL;
    if (node->if_stmt.then_body && node->if_stmt.then_body->kind == NODE_BLOCK)
    {
        ASTNode *stmt = node->if_stmt.then_body->block.statements;
        while (stmt && stmt->next)
        {
            stmt = stmt->next;
        }
        then_result = stmt;
    }
    else
    {
        then_result = node->if_stmt.then_body;
    }

    if (then_result)
    {
        EMIT(ctx, "__typeof__(");
        codegen_expression(ctx, then_result);
        EMIT(ctx, ") _ifval; ");
    }
    else
    {
        EMIT(ctx, "int _ifval; ");
    }

    EMIT(ctx, "if (");
    codegen_expression(ctx, node->if_stmt.condition);
    EMIT(ctx, ") { ");
    if (node->if_stmt.then_body && node->if_stmt.then_body->kind == NODE_BLOCK)
    {
        ASTNode *stmt = node->if_stmt.then_body->block.statements;
        while (stmt && stmt->next)
        {
            codegen_node_single(ctx, stmt);
            stmt = stmt->next;
        }
        if (stmt)
        {
            EMIT(ctx, "_ifval = ");
            codegen_expression(ctx, stmt);
            EMIT(ctx, "; ");
        }
    }
    else if (node->if_stmt.then_body)
    {
        EMIT(ctx, "_ifval = ");
        codegen_expression(ctx, node->if_stmt.then_body);
        EMIT(ctx, "; ");
    }
    EMIT(ctx, "} else { ");
    if (node->if_stmt.else_body && node->if_stmt.else_body->kind == NODE_BLOCK)
    {
        ASTNode *stmt = node->if_stmt.else_body->block.statements;
        while (stmt && stmt->next)
        {
            codegen_node_single(ctx, stmt);
            stmt = stmt->next;
        }
        if (stmt)
        {
            EMIT(ctx, "_ifval = ");
            codegen_expression(ctx, stmt);
            EMIT(ctx, "; ");
        }
    }
    else if (node->if_stmt.else_body)
    {
        EMIT(ctx, "_ifval = ");
        codegen_expression(ctx, node->if_stmt.else_body);
        EMIT(ctx, "; ");
    }
    EMIT(ctx, "} _ifval; })");
}

static int type_is_enum(ParserContext *ctx, const char *name)
{
    if (!name)
    {
        return 0;
    }
    StructRef *er = ctx->parsed_enums_list;
    while (er)
    {
        if (er->node && er->node->kind == NODE_ENUM && strcmp(er->node->enm.name, name) == 0)
        {
            return 1;
        }
        er = er->next;
    }
    ASTNode *ins = ctx->instantiated_structs;
    while (ins)
    {
        if (ins->kind == NODE_ENUM && strcmp(ins->enm.name, name) == 0)
        {
            return 1;
        }
        ins = ins->next;
    }
    return 0;
}

void handle_try_expr(ParserContext *ctx, ASTNode *node)
{
    char *type_name = "Result";
    Type *expr_type = node->try_stmt.expr->type_info;

    if (expr_type && expr_type->name)
    {
        type_name = expr_type->name;
    }
    else if (ctx->cg.current_func_ret_type)
    {
        type_name = ctx->cg.current_func_ret_type;
    }

    if (strcmp(type_name, "__auto_type") == 0 || strcmp(type_name, "unknown") == 0)
    {
        type_name = "Result";
    }

    const char *search_name = str_strip_struct_prefix(type_name);

    // The unwrapped value (`_try`) has the expression's type, so its field
    // accessors (`.tag`, `.data`, `.is_ok`, `.err`, ...) follow that type.
    // But `?` returns from the enclosing function, so the Err/None value we
    // construct and return must belong to the FUNCTION's return type, which
    // may differ from the expression's type, e.g.
    //   fn f() -> Result<int> { let s = read_all()?; ... }  // read_all -> Result<string>
    // The constructor name (Result__int32_t__Err, Option__X__None, ...) is
    // therefore derived from current_func_ret_type, falling back to the
    // expression's type when the function does not return a Result/Option.
    const char *ret_name = ctx->cg.current_func_ret_type
                               ? str_strip_struct_prefix(ctx->cg.current_func_ret_type)
                               : NULL;
    if (!ret_name || !(str_is_result_type(ret_name) || str_is_option_type(ret_name)))
    {
        ret_name = search_name;
    }

    int is_enum = type_is_enum(ctx, search_name);
    int is_option = str_is_option_type(search_name);

    EMIT(ctx, "({ ");
    emit_auto_type(ctx, node->try_stmt.expr, node->token);
    EMIT(ctx, " _try = ");
    codegen_expression(ctx, node->try_stmt.expr);

    if (is_option)
    {
        if (is_enum)
        {
            EMIT(ctx, "; if (_try.tag == %s__None_Tag) return (%s__None()); _try.data.Some; })",
                 ret_name, ret_name);
        }
        else
        {
            EMIT(ctx, "; if (!_try.is_some) return %s__None(); _try.val; })", ret_name);
        }
    }
    else if (is_enum)
    {
        EMIT(ctx,
             "; if (_try.tag == %s__Err_Tag) return (%s__Err(_try.data.Err)); _try.data.Ok; })",
             ret_name, ret_name);
    }
    else
    {
        EMIT(ctx, "; if (!_try.is_ok) return %s__Err(_try.err); _try.val; })", ret_name);
    }
}

void handle_plugin(ParserContext *ctx, ASTNode *node)
{
    ZPlugin *found = ctx->hook_find_plugin
                         ? (ZPlugin *)ctx->hook_find_plugin(node->plugin_stmt.plugin_name)
                         : NULL;
    if (found)
    {
        ZApi api;
        if (ctx->hook_plugin_init_api)
        {
            ctx->hook_plugin_init_api(&api, ctx->current_filename, node->line, ctx->config);
        }
        api.out = ctx->cg.emitter.file;
        api.hoist_out = ctx->cg.hoist_out;
        found->fn(node->plugin_stmt.body, &api);
    }
    else
    {
        EMIT(ctx, "/* Unknown plugin: %s */\n", node->plugin_stmt.plugin_name);
    }
}
