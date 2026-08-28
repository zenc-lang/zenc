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

typedef void (*CodegenHandler)(ParserContext *ctx, ASTNode *node);

void handle_node_ast_comment(ParserContext *ctx, ASTNode *node)
{
    (void)ctx;
    EMIT(ctx, "%s\n", node->comment.content);
}

void handle_node_match(ParserContext *ctx, ASTNode *node)
{
    codegen_match_internal(ctx, node, 0);
    EMIT(ctx, ";\n");
}

void handle_node_assert(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "__zenc_assert(");
    codegen_expression(ctx, node->assert_stmt.condition);
    if (node->assert_stmt.message)
    {
        if (node->assert_stmt.message_is_literal)
        {
            EMIT(ctx, ", %s", node->assert_stmt.message);
        }
        else
        {
            EMIT(ctx, ", \"%%s\", %s", node->assert_stmt.message);
        }
    }
    else
    {
        EMIT(ctx, ", \"Assertion failed\"");
    }
    EMIT(ctx, ");\n");
}

void handle_node_expect(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "__zenc_expect(");
    codegen_expression(ctx, node->assert_stmt.condition);
    if (node->assert_stmt.message)
    {
        if (node->assert_stmt.message_is_literal)
        {
            EMIT(ctx, ", %s", node->assert_stmt.message);
        }
        else
        {
            EMIT(ctx, ", \"%%s\", %s", node->assert_stmt.message);
        }
    }
    else
    {
        EMIT(ctx, ", \"Expectation failed\"");
    }
    EMIT(ctx, ");\n");
}

void handle_node_defer(ParserContext *ctx, ASTNode *node)
{
    (void)ctx;
    if (ctx->cg.defer_count < MAX_DEFER)
    {
        ctx->cg.defer_stack[ctx->cg.defer_count++] = node->defer_stmt.stmt;
    }
}

void handle_node_comptime(ParserContext *ctx, ASTNode *node)
{
    if (node->comptime.generated)
    {
        codegen_walker(ctx, node->comptime.generated);
    }
}

void handle_node_block(ParserContext *ctx, ASTNode *node)
{
    int saved = ctx->cg.defer_count;
    EMIT(ctx, "{\n");
    emitter_indent(&ctx->cg.emitter);
    codegen_walker(ctx, node->block.statements);
    for (int i = ctx->cg.defer_count - 1; i >= saved; i--)
    {
        emit_source_mapping_duplicate(ctx, ctx->cg.defer_stack[i]);
        codegen_node_single(ctx, ctx->cg.defer_stack[i]);
    }
    ctx->cg.defer_count = saved;
    emitter_dedent(&ctx->cg.emitter);
    EMIT(ctx, "}\n");
}

void handle_node_impl(ParserContext *ctx, ASTNode *node)
{
    char *sname = node->impl.struct_name;
    TypeAlias *ta = find_type_alias_node(ctx, sname);
    const char *resolved = (ta && !ta->is_opaque) ? ta->original_type : NULL;

    if (resolved)
    {
        size_t slen = strlen(sname);
        ASTNode *m = node->impl.methods;
        while (m)
        {
            if (m->kind == NODE_FUNCTION && m->func.name &&
                strncmp(m->func.name, sname, (size_t)(slen)) == 0 && m->func.name[slen] == '_' &&
                m->func.name[slen + 1] == '_')
            {
                const char *method_part = m->func.name + slen;
                size_t new_name_sz = strlen(resolved) + strlen(method_part) + 1;
                char *new_name = xmalloc(new_name_sz);
                snprintf(new_name, new_name_sz, "%s%s", resolved, method_part);
                zfree(m->func.name);
                m->func.name = new_name;
            }
            m = m->next;
        }
        ctx->cg.current_impl_type = (char *)resolved;
    }
    else
    {
        ctx->cg.current_impl_type = sname;
    }

    codegen_walker(ctx, node->impl.methods);
    ctx->cg.current_impl_type = NULL;
}

void handle_node_function(ParserContext *ctx, ASTNode *node)
{
    if (!node->func.body || node->func.generic_params)
    {
        return;
    }
    if (node->cfg_condition)
    {
        EMIT(ctx, "#if %s\n", node->cfg_condition);
    }

    emit_source_mapping(ctx, node);

    if (node->func.is_async)
    {
        const char *final_name = (node->link_name) ? node->link_name : node->func.name;
        int has_ret = node->func.ret_type && strcmp(node->func.ret_type, "void") != 0;

        // Parse args
        char *args_copy = xstrdup(node->func.args);
        char *token = strtok(args_copy, ",");
        int count = 0;
        char **arg_names = xmalloc(32 * sizeof(char *));
        char **arg_types = xmalloc(32 * sizeof(char *));
        while (token)
        {
            while (*token == ' ')
            {
                token++;
            }
            char *last_space = (char *)strrchr(token, ' ');
            if (last_space)
            {
                *last_space = 0;
                arg_types[count] = xstrdup(token);
                arg_names[count] = xstrdup(last_space + 1);
                count++;
            }
            token = strtok(NULL, ",");
        }
        zfree(args_copy);

        // 1. Init function (struct definition emitted in protos)
        EMIT(ctx, "void %s_init(struct %s_Future *f", final_name, final_name);
        for (int i = 0; i < count; i++)
        {
            EMIT(ctx, ", %s %s", arg_types[i], arg_names[i]);
        }
        EMIT(ctx, ")\n{\n");
        emitter_indent(&ctx->cg.emitter);
        EMIT(ctx, "f->_state = 0;\n");
        for (int i = 0; i < count; i++)
        {
            EMIT(ctx, "f->%s = %s;\n", arg_names[i], arg_names[i]);
        }
        emitter_dedent(&ctx->cg.emitter);
        EMIT(ctx, "}\n");

        // 3. Emit the actual function body as _impl_%s (regular C function with normal returns)
        EMIT(ctx, "%s _impl_%s(", has_ret ? node->func.ret_type : "void", final_name);
        for (int i = 0; i < count; i++)
        {
            EMIT(ctx, "%s%s %s", i > 0 ? ", " : "", arg_types[i], arg_names[i]);
        }
        EMIT(ctx, ")\n{\n");
        emitter_indent(&ctx->cg.emitter);
        ctx->cg.defer_count = 0;

        // Set up drop flags for parameters with destructors (e.g. String, Vec)
        for (int ai = 0; ai < count && ai < node->func.count; ai++)
        {
            Type *arg_type = node->func.arg_types[ai];
            if (!arg_type)
            {
                continue;
            }
            int has_drop = 0;
            char *drop_type_name = NULL;
            if (arg_type->kind == TYPE_STRUCT && arg_type->name)
            {
                ASTNode *def = find_struct_def(ctx, arg_type->name);
                if (def && def->kind == NODE_STRUCT && def->type_info &&
                    def->type_info->traits.has_drop)
                {
                    has_drop = 1;
                    drop_type_name = arg_type->name;
                }
            }
            if (has_drop && ai < 32 && arg_names[ai])
            {
                EMIT(ctx, "int __z_drop_flag_%s = 1;\n", arg_names[ai]);
                ASTNode *defer_node = xmalloc(sizeof(ASTNode));
                defer_node->token = node->token;
                defer_node->kind = NODE_RAW_STMT;
                size_t stmt_sz = 256 + strlen(arg_names[ai]) * 2 + strlen(drop_type_name);
                char *stmt_str = xmalloc(stmt_sz);
                if (strcmp(arg_names[ai], "self") == 0)
                {
                    snprintf(stmt_str, stmt_sz, "if (__z_drop_flag_%s) %s__Drop__glue(%s);",
                             arg_names[ai], drop_type_name, arg_names[ai]);
                }
                else
                {
                    snprintf(stmt_str, stmt_sz, "if (__z_drop_flag_%s) %s__Drop__glue(&%s);",
                             arg_names[ai], drop_type_name, arg_names[ai]);
                }
                defer_node->raw_stmt.content = stmt_str;
                defer_node->line = node->line;
                if (ctx->cg.defer_count < MAX_DEFER)
                {
                    ctx->cg.defer_stack[ctx->cg.defer_count++] = defer_node;
                }
            }
        }

        char *prev_ret = ctx->cg.current_func_ret_type;
        Type *prev_ret_info = ctx->cg.current_func_ret_type_info;
        ctx->cg.current_func_ret_type = node->func.ret_type;
        ctx->cg.current_func_ret_type_info = node->func.ret_type_info;

        codegen_walker(ctx, node->func.body);

        ctx->cg.current_func_ret_type = prev_ret;
        ctx->cg.current_func_ret_type_info = prev_ret_info;
        for (int i = ctx->cg.defer_count - 1; i >= 0; i--)
        {
            emit_source_mapping_duplicate(ctx, ctx->cg.defer_stack[i]);
            codegen_node_single(ctx, ctx->cg.defer_stack[i]);
        }
        emitter_dedent(&ctx->cg.emitter);
        EMIT(ctx, "}\n");

        // 4. Poll function — calls _impl_, stores result, zeroes moved params
        EMIT(ctx, "int %s_poll(void *ctx)\n", final_name);
        EMIT(ctx, "{\n");
        emitter_indent(&ctx->cg.emitter);
        EMIT(ctx, "struct %s_Future *f = ctx;\n", final_name);
        EMIT(ctx, "if (f->_state > 0) return 1;\n");
        EMIT(ctx, "f->_state = 1;\n");
        if (has_ret)
        {
            EMIT(ctx, "f->_result = _impl_%s(", final_name);
            for (int i = 0; i < count; i++)
            {
                EMIT(ctx, "%sf->%s", i > 0 ? ", " : "", arg_names[i]);
            }
            EMIT(ctx, ");\n");
        }
        else
        {
            EMIT(ctx, "_impl_%s(", final_name);
            for (int i = 0; i < count; i++)
            {
                EMIT(ctx, "%sf->%s", i > 0 ? ", " : "", arg_names[i]);
            }
            EMIT(ctx, ");\n");
            // Also drop the params that were passed by value
        }
        // Zero out future fields for types with destructors (ownership moved to _impl_)
        for (int ai = 0; ai < count && ai < node->func.count; ai++)
        {
            Type *arg_type = node->func.arg_types[ai];
            if (!arg_type)
            {
                continue;
            }
            if (arg_type->kind == TYPE_STRUCT && arg_type->name)
            {
                ASTNode *def = find_struct_def(ctx, arg_type->name);
                if (def && def->kind == NODE_STRUCT && def->type_info &&
                    def->type_info->traits.has_drop && ai < 32 && arg_names[ai])
                {
                    EMIT(ctx, "memset(&f->%s, 0, sizeof(f->%s));\n", arg_names[ai], arg_names[ai]);
                }
            }
        }
        EMIT(ctx, "return 1;\n");
        emitter_dedent(&ctx->cg.emitter);
        EMIT(ctx, "}\n");

        // 5. Get function
        if (has_ret)
        {
            EMIT(ctx, "%s %s_get(struct %s_Future *f) { return f->_result; }\n",
                 node->func.ret_type, final_name, final_name);
        }

        for (int i = 0; i < count; i++)
        {
            zfree(arg_names[i]);
            zfree(arg_types[i]);
        }
        zfree(arg_names);
        zfree(arg_types);

        if (node->cfg_condition)
        {
            EMIT(ctx, "#endif\n");
        }
        return;
    }

    ctx->cg.defer_count = 0;
    EMIT(ctx, "\n");

    // Emit GCC attributes before function
    {
        int has_attrs = node->func.constructor || node->func.destructor || node->func.noinline ||
                        node->func.unused || node->func.weak || node->func.cold || node->func.hot ||
                        node->func.noreturn || node->func.pure || node->func.section ||
                        node->func.is_export;
        if (has_attrs)
        {
            EMIT(ctx, "__attribute__((");
            int first = 1;
#define EMIT_ATTR(cond, name)                                                                      \
    if (cond)                                                                                      \
    {                                                                                              \
        if (!first)                                                                                \
            EMIT(ctx, ", ");                                                                       \
        EMIT(ctx, name);                                                                           \
        first = 0;                                                                                 \
    }
            EMIT_ATTR(node->func.constructor, "constructor");
            EMIT_ATTR(node->func.destructor, "destructor");
            EMIT_ATTR(node->func.noinline, "noinline");
            EMIT_ATTR(node->func.unused, "unused");
            EMIT_ATTR(node->func.weak, "weak");
            EMIT_ATTR(node->func.cold, "cold");
            EMIT_ATTR(node->func.hot, "hot");
            EMIT_ATTR(node->func.noreturn, "noreturn");
            EMIT_ATTR(node->func.pure, "pure");
            EMIT_ATTR(node->func.is_export, "visibility(\"default\")");
            if (node->func.section)
            {
                if (!first)
                {
                    EMIT(ctx, ", ");
                }
                EMIT(ctx, "section(\"%s\")", node->func.section);
            }

            Attribute *custom = node->func.attributes;
            while (custom)
            {
                if (!first)
                {
                    EMIT(ctx, ", ");
                }
                EMIT(ctx, "%s", custom->name);
                if (custom->count > 0)
                {
                    EMIT(ctx, "(");
                    for (int i = 0; i < custom->count; i++)
                    {
                        if (i > 0)
                        {
                            EMIT(ctx, ", ");
                        }
                        EMIT(ctx, "%s", custom->args[i]);
                    }
                    EMIT(ctx, ")");
                }
                first = 0;
                custom = custom->next;
            }

#undef EMIT_ATTR
            EMIT(ctx, ")) ");
        }
        else if (node->func.attributes)
        {
            // Handle case where specific attributes are missing but custom ones exist
            EMIT(ctx, "__attribute__((");
            int first = 1;
            Attribute *custom = node->func.attributes;
            while (custom)
            {
                if (!first)
                {
                    EMIT(ctx, ", ");
                }
                EMIT(ctx, "%s", custom->name);
                if (custom->count > 0)
                {
                    EMIT(ctx, "(");
                    for (int i = 0; i < custom->count; i++)
                    {
                        if (i > 0)
                        {
                            EMIT(ctx, ", ");
                        }
                        EMIT(ctx, "%s", custom->args[i]);
                    }
                    EMIT(ctx, ")");
                }
                first = 0;
                custom = custom->next;
            }
            EMIT(ctx, ")) ");
        }
    }

    if (node->func.is_inline)
    {
        EMIT(ctx, "inline ");
    }
    emit_func_signature(ctx, node, NULL);
    EMIT(ctx, "\n{\n");
    emitter_indent(&ctx->cg.emitter);
    if (ctx->config->misra_mode && node->func.ret_type && strcmp(node->func.ret_type, "void") != 0)
    {
        char *safe_ret_type = type_to_c_string(node->func.ret_type_info);
        EMIT(ctx, "%s _misra_ret = 0;\n", safe_ret_type);
        zfree(safe_ret_type);
    }
    char *prev_ret = ctx->cg.current_func_ret_type;
    Type *prev_ret_info = ctx->cg.current_func_ret_type_info;
    ctx->cg.current_func_ret_type = node->func.ret_type;
    ctx->cg.current_func_ret_type_info = node->func.ret_type_info;

    // Set self_is_pointer flag for codegen of the body
    int prev_self_is_ptr = ctx->self_is_pointer;
    ctx->self_is_pointer = 0;
    if (node->func.count > 0 && node->func.param_names && node->func.param_names[0] &&
        strcmp(node->func.param_names[0], "self") == 0)
    {
        ctx->self_is_pointer = 1;
    }

    // Initialize drop flags for arguments that implement Drop
    for (int i = 0; i < node->func.count; i++)
    {
        Type *arg_type = node->func.arg_types[i];
        char *arg_name = node->func.param_names ? node->func.param_names[i] : NULL;
        if (arg_type && arg_name)
        {
            // Check if type implements Drop
            int has_drop = 0;
            char *drop_type_name = NULL;

            if (arg_type->kind == TYPE_FUNCTION ||
                (arg_type->kind == TYPE_STRUCT && arg_type->name))
            {
                if (arg_type->kind == TYPE_FUNCTION)
                {
                    if (arg_type->traits.has_drop)
                    {
                        has_drop = 1;
                    }
                }
                else
                {
                    ASTNode *def = find_struct_def(ctx, arg_type->name);
                    if (def && def->kind == NODE_STRUCT && def->type_info &&
                        def->type_info->traits.has_drop)
                    {
                        has_drop = 1;
                        drop_type_name = arg_type->name;
                    }
                }
            }

            if (has_drop)
            {
                emit_source_mapping_duplicate(ctx, node);
                EMIT(ctx, "int __z_drop_flag_%s = 1;\n", arg_name);

                ASTNode *defer_node = xmalloc(sizeof(ASTNode));
                defer_node->token = node->token;
                defer_node->kind = NODE_RAW_STMT;
                char *stmt_str = NULL;
                if (arg_type->kind == TYPE_FUNCTION)
                {
                    size_t stmt_sz = 256 + strlen(arg_name) * 4;
                    stmt_str = xmalloc(stmt_sz);
                    snprintf(stmt_str, stmt_sz, "if (__z_drop_flag_%s && %s.drop) %s.drop(%s.ctx);",
                             arg_name, arg_name, arg_name, arg_name);
                }
                else
                {
                    size_t stmt_sz = 256 + strlen(arg_name) * 2 + strlen(drop_type_name);
                    stmt_str = xmalloc(stmt_sz);
                    // If it's self, it's already a pointer in C
                    if (strcmp(arg_name, "self") == 0)
                    {
                        snprintf(stmt_str, stmt_sz, "if (__z_drop_flag_%s) %s__Drop__glue(%s);",
                                 arg_name, drop_type_name, arg_name);
                    }
                    else
                    {
                        snprintf(stmt_str, stmt_sz, "if (__z_drop_flag_%s) %s__Drop__glue(&%s);",
                                 arg_name, drop_type_name, arg_name);
                    }
                }
                defer_node->raw_stmt.content = stmt_str;

                if (ctx->cg.defer_count < MAX_DEFER)
                {
                    ctx->cg.defer_stack[ctx->cg.defer_count++] = defer_node;
                }
            }
        }
    }

    codegen_walker(ctx, node->func.body);
    for (int i = ctx->cg.defer_count - 1; i >= 0; i--)
    {
        emit_source_mapping_duplicate(ctx, ctx->cg.defer_stack[i]);
        codegen_node_single(ctx, ctx->cg.defer_stack[i]);
    }
    ctx->cg.current_func_ret_type = prev_ret;
    ctx->cg.current_func_ret_type_info = prev_ret_info;
    ctx->self_is_pointer = prev_self_is_ptr;

    if (ctx->config->misra_mode)
    {
        EMIT(ctx, "goto _misra_end_of_func;\n");
        EMIT(ctx, "_misra_end_of_func:\n");
        if (node->func.ret_type && strcmp(node->func.ret_type, "void") != 0)
        {
            EMIT(ctx, "return _misra_ret;\n");
        }
        else
        {
            EMIT(ctx, "return;\n");
        }
    }
    emitter_dedent(&ctx->cg.emitter);
    EMIT(ctx, "}\n");
    if (node->cfg_condition)
    {
        EMIT(ctx, "#endif\n");
    }
}

void handle_node_impl_trait(ParserContext *ctx, ASTNode *node)
{
    char *sname = node->impl_trait.target_type;
    TypeAlias *ta = find_type_alias_node(ctx, sname);
    const char *resolved = (ta && !ta->is_opaque) ? ta->original_type : NULL;

    if (resolved)
    {
        size_t slen = strlen(sname);
        ASTNode *m = node->impl_trait.methods;
        while (m)
        {
            if (m->kind == NODE_FUNCTION && m->func.name &&
                strncmp(m->func.name, sname, (size_t)(slen)) == 0 && m->func.name[slen] == '_' &&
                m->func.name[slen + 1] == '_')
            {
                const char *method_part = m->func.name + slen;
                size_t new_name_sz = strlen(resolved) + strlen(method_part) + 1;
                char *new_name = xmalloc(new_name_sz);
                snprintf(new_name, new_name_sz, "%s%s", resolved, method_part);
                zfree(m->func.name);
                m->func.name = new_name;
            }
            m = m->next;
        }
        ctx->cg.current_impl_type = (char *)resolved;
    }
    else
    {
        ctx->cg.current_impl_type = sname;
    }

    codegen_walker(ctx, node->impl_trait.methods);
    ctx->cg.current_impl_type = NULL;
}

void handle_node_destruct_var(ParserContext *ctx, ASTNode *node)
{
    int id = ctx->cg.tmp_counter++;
    emit_auto_type(ctx, node->destruct.init_expr, node->token);
    EMIT(ctx, " _tmp_%d = ", id);
    codegen_expression(ctx, node->destruct.init_expr);
    EMIT(ctx, ";\n");

    if (node->destruct.is_guard)
    {
        char *variant = node->destruct.guard_variant;
        char *check = "val";

        emit_source_mapping_duplicate(ctx, node);
        if (strcmp(variant, "Some") == 0)
        {
            EMIT(ctx, "if (!_tmp_%d.is_some) {\n", id);
            emitter_indent(&ctx->cg.emitter);
        }
        else if (strcmp(variant, "Ok") == 0)
        {
            EMIT(ctx, "if (!_tmp_%d.is_ok) {\n", id);
            emitter_indent(&ctx->cg.emitter);
        }
        else if (strcmp(variant, "Err") == 0)
        {
            EMIT(ctx, "if (_tmp_%d.is_ok) {\n", id);
            emitter_indent(&ctx->cg.emitter);
            check = "err";
        }
        else
        {
            EMIT(ctx, "if (!_tmp_%d.is_%s) {\n", id, variant);
            emitter_indent(&ctx->cg.emitter);
        }

        codegen_walker(ctx, node->destruct.else_block->block.statements);
        emitter_dedent(&ctx->cg.emitter);
        EMIT(ctx, "}\n");

        emit_source_mapping_duplicate(ctx, node);
        if (z_path_match_compiler(ctx->config->cc, "tcc"))
        {
            EMIT(ctx, "__typeof__(_tmp_%d.%s) %s = _tmp_%d.%s;\n", id, check,
                 node->destruct.names[0], id, check);
        }
        else
        {
            EMIT(ctx, "ZC_AUTO %s = _tmp_%d.%s;\n", node->destruct.names[0], id, check);
        }
    }
    else
    {
        for (int i = 0; i < node->destruct.count; i++)
        {
            emit_source_mapping_duplicate(ctx, node);
            if (node->destruct.is_struct_destruct)
            {
                char *field = node->destruct.field_names ? node->destruct.field_names[i]
                                                         : node->destruct.names[i];
                if (z_path_match_compiler(ctx->config->cc, "tcc"))
                {
                    EMIT(ctx, "__typeof__(_tmp_%d.%s) %s = _tmp_%d.%s;\n", id, field,
                         node->destruct.names[i], id, field);
                }
                else
                {
                    EMIT(ctx, "ZC_AUTO %s = _tmp_%d.%s;\n", node->destruct.names[i], id, field);
                }
            }
            else
            {
                char *explicit_type = node->destruct.types ? node->destruct.types[i] : NULL;
                if (explicit_type)
                {
                    EMIT(ctx, "%s %s = _tmp_%d.v%d;\n", explicit_type, node->destruct.names[i], id,
                         i);
                }
                else if (z_path_match_compiler(ctx->config->cc, "tcc"))
                {
                    EMIT(ctx, "__typeof__(_tmp_%d.v%d) %s = _tmp_%d.v%d;\n", id, i,
                         node->destruct.names[i], id, i);
                }
                else
                {
                    EMIT(ctx, "ZC_AUTO %s = _tmp_%d.v%d;\n", node->destruct.names[i], id, i);
                }
            }
        }
    }
}

void handle_node_var_decl(ParserContext *ctx, ASTNode *node)
{
    int saved_closure_frees = ctx->cg.pending_closure_free_count;

    if (strcmp(node->var_decl.name, "_") == 0 && node->var_decl.init_expr)
    {
        int is_void = 0;
        if (node->type_info && node->type_info->kind == TYPE_VOID)
        {
            is_void = 1;
        }
        else if (node->var_decl.type_str && strcmp(node->var_decl.type_str, "void") == 0)
        {
            is_void = 1;
        }
        else if (!node->type_info && !node->var_decl.type_str)
        {
            char *ret_type = infer_type(ctx, node->var_decl.init_expr);
            if (!ret_type || strcmp(ret_type, "void") == 0)
            {
                is_void = 1;
            }
        }
        if (is_void)
        {
            codegen_expression(ctx, node->var_decl.init_expr);
            EMIT(ctx, ";\n");
            return;
        }
    }
    if (node->var_decl.is_thread_local)
    {
        EMIT(ctx, "_Thread_local ");
    }
    if (node->var_decl.is_static)
    {
        EMIT(ctx, "static ");
    }
    if (node->var_decl.is_autofree)
    {
        EMIT(ctx, "__attribute__((cleanup(_z_autofree_impl))) ");
    }
    {
        char *tname = NULL;
        if (node->type_info &&
            (!node->var_decl.init_expr || node->var_decl.init_expr->kind != NODE_AWAIT))
        {
            tname = type_to_c_string(node->type_info);
            // Async functions now return Async*; correct the type name
            // so the emitted C uses "Async*" instead of "Async" or "Async<...>".
            if (tname && strncmp(tname, "Async", 5) == 0 && node->var_decl.init_expr &&
                node->var_decl.init_expr->resolved_type &&
                strncmp(node->var_decl.init_expr->resolved_type, "Async", 5) == 0)
            {
                zfree(tname);
                tname = xstrdup("Async*");
            }
        }
        else if (node->var_decl.type_str && strcmp(node->var_decl.type_str, "__auto_type") != 0)
        {
            tname = node->var_decl.type_str;
        }

        if (tname && strcmp(tname, "void*") != 0 && strcmp(tname, "unknown") != 0)
        {
            char *clean_type = tname;
            if (strncmp(clean_type, "struct ", 7) == 0)
            {
                clean_type += 7;
            }

            ASTNode *def = find_struct_def(ctx, clean_type);
            int has_drop = (def && def->type_info && def->type_info->traits.has_drop);

            if (has_drop)
            {
                EMIT(ctx, "int __z_drop_flag_%s = 1; ", node->var_decl.name);

                ASTNode *defer_node = xmalloc(sizeof(ASTNode));
                defer_node->kind = NODE_RAW_STMT;
                defer_node->token = node->token;
                size_t stmt_sz = 256 + strlen(node->var_decl.name) * 2 + strlen(clean_type);
                char *stmt_str = xmalloc(stmt_sz);
                snprintf(stmt_str, stmt_sz, "if (__z_drop_flag_%s) %s__Drop__glue(&%s);",
                         node->var_decl.name, clean_type, node->var_decl.name);
                defer_node->raw_stmt.content = stmt_str;
                defer_node->line = node->line;

                if (ctx->cg.defer_count < MAX_DEFER)
                {
                    ctx->cg.defer_stack[ctx->cg.defer_count++] = defer_node;
                }
            }

            emit_var_decl_type(ctx, tname, node->var_decl.name);
            add_symbol(ctx, node->var_decl.name, tname, node->type_info, 0);

            if (node->var_decl.init_expr)
            {
                EMIT(ctx, " = ");
                if (ctx->config->use_cpp && node->type_info &&
                    (node->type_info->kind == TYPE_POINTER || node->type_info->kind == TYPE_ENUM))
                {
                    char *ct = type_to_c_string(node->type_info);
                    EMIT(ctx, "(%s)(", ct);
                    codegen_expression(ctx, node->var_decl.init_expr);
                    EMIT(ctx, ")");
                    zfree(ct);
                }
                else
                {
                    codegen_expression(ctx, node->var_decl.init_expr);
                }
            }
            else if (node->type_info)
            {
                TypeKind k = node->type_info->kind;
                if (k == TYPE_ARRAY || k == TYPE_STRUCT)
                {
                    EMIT(ctx, " = %s", ctx->config->use_cpp ? "{}" : "{0}");
                }
                else if (is_int_type(k))
                {
                    EMIT(ctx, " = 0");
                }
                else if (k == TYPE_F32 || k == TYPE_FLOAT)
                {
                    EMIT(ctx, " = 0.0f");
                }
                else if (k == TYPE_F64)
                {
                    EMIT(ctx, " = 0.0");
                }
                else if (k == TYPE_BOOL)
                {
                    EMIT(ctx, " = false");
                }
            }
            EMIT(ctx, ";\n");
            if (node->var_decl.init_expr && emit_move_invalidation(ctx, node->var_decl.init_expr))
            {
                EMIT(ctx, ";\n");
            }

            if (node->type_info)
            {
                zfree(tname);
            }
        }
        else
        {
            char *inferred = NULL;
            if (node->var_decl.init_expr)
            {
                inferred = infer_type(ctx, node->var_decl.init_expr);
            }

            if (inferred && strcmp(inferred, "__auto_type") != 0)
            {
                char *clean_type = inferred;
                if (strncmp(clean_type, "struct ", 7) == 0)
                {
                    clean_type += 7;
                }

                ASTNode *def = find_struct_def(ctx, clean_type);
                int has_drop = (def && def->type_info && def->type_info->traits.has_drop);

                if (has_drop)
                {
                    EMIT(ctx, "int __z_drop_flag_%s = 1; ", node->var_decl.name);

                    ASTNode *defer_node = xmalloc(sizeof(ASTNode));
                    defer_node->kind = NODE_RAW_STMT;
                    defer_node->token = node->token;
                    char *stmt_str = NULL;
                    if (node->var_decl.init_expr && node->var_decl.init_expr->type_info &&
                        node->var_decl.init_expr->type_info->kind == TYPE_FUNCTION)
                    {
                        size_t stmt_sz = 256 + strlen(node->var_decl.name) * 4;
                        stmt_str = xmalloc(stmt_sz);
                        snprintf(stmt_str, stmt_sz,
                                 "if (__z_drop_flag_%s && %s.drop) %s.drop(%s.ctx);",
                                 node->var_decl.name, node->var_decl.name, node->var_decl.name,
                                 node->var_decl.name);
                    }
                    else
                    {
                        size_t stmt_sz = 256 + strlen(node->var_decl.name) * 2 + strlen(clean_type);
                        stmt_str = xmalloc(stmt_sz);
                        snprintf(stmt_str, stmt_sz, "if (__z_drop_flag_%s) %s__Drop__glue(&%s);",
                                 node->var_decl.name, clean_type, node->var_decl.name);
                    }
                    defer_node->raw_stmt.content = stmt_str;
                    defer_node->line = node->line;

                    if (ctx->cg.defer_count < MAX_DEFER)
                    {
                        ctx->cg.defer_stack[ctx->cg.defer_count++] = defer_node;
                    }
                }

                emit_var_decl_type(ctx, inferred, node->var_decl.name);
                add_symbol(ctx, node->var_decl.name, inferred, NULL, 0);
                EMIT(ctx, " = ");
                if (ctx->config->use_cpp && inferred &&
                    (strchr(inferred, '*') || is_enum_type_name(ctx, inferred)))
                {
                    EMIT(ctx, "(%s)(", inferred);
                    codegen_expression(ctx, node->var_decl.init_expr);
                    EMIT(ctx, ")");
                }
                else
                {
                    codegen_expression(ctx, node->var_decl.init_expr);
                }
                EMIT(ctx, ";\n");

                if (node->var_decl.init_expr &&
                    emit_move_invalidation(ctx, node->var_decl.init_expr))
                {
                    EMIT(ctx, ";\n");
                }
            }
            else
            {
                emit_auto_type(ctx, node->var_decl.init_expr, node->token);
                EMIT(ctx, " %s", node->var_decl.name);

                if (inferred)
                {
                    add_symbol(ctx, node->var_decl.name, inferred, NULL, 0);
                }

                EMIT(ctx, " = ");
                codegen_expression(ctx, node->var_decl.init_expr);
                EMIT(ctx, ";\n");
                if (node->var_decl.init_expr &&
                    emit_move_invalidation(ctx, node->var_decl.init_expr))
                {
                    EMIT(ctx, ";\n");
                }
            }
        }
    }

    ctx->cg.pending_closure_free_count = saved_closure_frees;
}

void handle_node_const(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "__attribute__((unused)) const ");
    if (node->var_decl.type_str)
    {
        EMIT(ctx, "%s %s", node->var_decl.type_str, node->var_decl.name);
    }
    else
    {
        emit_auto_type(ctx, node->var_decl.init_expr, node->token);
        EMIT(ctx, " %s", node->var_decl.name);
    }
    EMIT(ctx, " = ");
    codegen_expression(ctx, node->var_decl.init_expr);
    EMIT(ctx, ";\n");
}

void handle_node_field(ParserContext *ctx, ASTNode *node)
{
    if (node->field.bit_width > 0)
    {
        EMIT(ctx, "%s %s : %d;\n", node->field.type, node->field.name, node->field.bit_width);
    }
    else
    {
        emit_var_decl_type(ctx, node->field.type, node->field.name);
        EMIT(ctx, ";\n");
    }
}

void handle_node_if(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "if (");
    codegen_expression(ctx, node->if_stmt.condition);
    EMIT(ctx, ") ");
    if (ctx->config->misra_mode && node->if_stmt.then_body->kind != NODE_BLOCK)
    {
        EMIT(ctx, "{\n");
        emitter_indent(&ctx->cg.emitter);
    }
    codegen_node_single(ctx, node->if_stmt.then_body);
    if (ctx->config->misra_mode && node->if_stmt.then_body->kind != NODE_BLOCK)
    {
        emitter_dedent(&ctx->cg.emitter);
        EMIT(ctx, "\n}");
    }
    if (node->if_stmt.else_body)
    {
        emit_source_mapping(ctx, node->if_stmt.else_body);
        EMIT(ctx, " else ");
        if (ctx->config->misra_mode && node->if_stmt.else_body->kind != NODE_BLOCK)
        {
            EMIT(ctx, "{\n");
            emitter_indent(&ctx->cg.emitter);
        }
        codegen_node_single(ctx, node->if_stmt.else_body);
        if (ctx->config->misra_mode && node->if_stmt.else_body->kind != NODE_BLOCK)
        {
            emitter_dedent(&ctx->cg.emitter);
            EMIT(ctx, "\n}");
        }
    }
    else if (ctx->config->misra_mode)
    {
        EMIT(ctx, " else { } /* MISRA */ ");
    }
}

void handle_node_unless(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "if (!(");
    codegen_expression(ctx, node->unless_stmt.condition);
    EMIT(ctx, ")) ");
    if (ctx->config->misra_mode && node->unless_stmt.body->kind != NODE_BLOCK)
    {
        EMIT(ctx, "{\n");
        emitter_indent(&ctx->cg.emitter);
    }
    codegen_node_single(ctx, node->unless_stmt.body);
    if (ctx->config->misra_mode && node->unless_stmt.body->kind != NODE_BLOCK)
    {
        emitter_dedent(&ctx->cg.emitter);
        EMIT(ctx, "\n}");
    }
}

void handle_node_guard(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "if (!(");
    codegen_expression(ctx, node->guard_stmt.condition);
    EMIT(ctx, ")) ");
    if (ctx->config->misra_mode && node->guard_stmt.body->kind != NODE_BLOCK)
    {
        EMIT(ctx, "{\n");
        emitter_indent(&ctx->cg.emitter);
    }
    codegen_node_single(ctx, node->guard_stmt.body);
    if (ctx->config->misra_mode && node->guard_stmt.body->kind != NODE_BLOCK)
    {
        emitter_dedent(&ctx->cg.emitter);
        EMIT(ctx, "\n}");
    }
}

void handle_node_while(ParserContext *ctx, ASTNode *node)
{
    if (node->while_stmt.loop_label)
    {
        EMIT(ctx, "%s:;\n", node->while_stmt.loop_label);
    }
    if (ctx->cg.loop_depth < 64)
    {
        ctx->cg.loop_defer_boundary[ctx->cg.loop_depth] = ctx->cg.defer_count;
    }
    ctx->cg.loop_depth++;
    EMIT(ctx, "while (");
    codegen_expression(ctx, node->while_stmt.condition);
    EMIT(ctx, ") ");
    if (node->while_stmt.loop_label)
    {
        EMIT(ctx, "{\n");
        emitter_indent(&ctx->cg.emitter);
        codegen_node_single(ctx, node->while_stmt.body);
        emitter_dedent(&ctx->cg.emitter);
        EMIT(ctx, "__continue_%s:;\n", node->while_stmt.loop_label);
        EMIT(ctx, "}\n");
    }
    else
    {
        if (ctx->config->misra_mode && node->while_stmt.body->kind != NODE_BLOCK)
        {
            EMIT(ctx, "{\n");
            emitter_indent(&ctx->cg.emitter);
        }
        codegen_node_single(ctx, node->while_stmt.body);
        if (ctx->config->misra_mode && node->while_stmt.body->kind != NODE_BLOCK)
        {
            emitter_dedent(&ctx->cg.emitter);
            EMIT(ctx, "\n}");
        }
    }
    ctx->cg.loop_depth--;
    if (node->while_stmt.loop_label)
    {
        EMIT(ctx, "__break_%s:;\n", node->while_stmt.loop_label);
    }
}
