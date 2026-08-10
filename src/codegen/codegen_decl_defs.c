// SPDX-License-Identifier: MIT

#include "../ast/ast.h"
#include "../constants.h"
#include "../parser/parser.h"
#include "../zprep.h"
#include "codegen.h"
#include "compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../platform/misra.h"
#include "codegen_internal.h"

// Emit includes and type aliases (and top-level comments)

int is_module_visited(VisitedModules *visited, const char *path)
{
    while (visited)
    {
        if (strcmp(visited->path, path) == 0)
        {
            return 1;
        }
        visited = visited->next;
    }
    return 0;
}

void mark_module_visited(VisitedModules **visited, const char *path)
{
    VisitedModules *node = xmalloc(sizeof(VisitedModules));
    node->path = path;
    node->next = *visited;
    *visited = node;
}

void free_visited_modules(VisitedModules *visited)
{
    // NO-OP: We use arena allocation (xmalloc) for VisitedModules.
    // Arena is reset globally, single nodes must not be zfree()d.
    (void)visited;
}

static void emit_includes_and_aliases_internal(ParserContext *ctx, ASTNode *node,
                                               VisitedModules **visited, int depth)
{
    if (depth > 1024)
    {
        zfatal("Infinite recursion detected in emit_includes_and_aliases (ctx, circular imports?)");
    }
    while (node)
    {
        if (node->type == NODE_IMPORT)
        {
            if (!is_module_visited(*visited, node->import_stmt.path))
            {
                mark_module_visited(visited, node->import_stmt.path);
                emit_includes_and_aliases_internal(ctx, node->import_stmt.module_root, visited,
                                                   depth + 1);
            }
            node = node->next;
            continue;
        }
        if (node->type == NODE_INCLUDE)
        {
            if (node->include.path)
            {
                if (node->include.is_system)
                {
                    EMIT(ctx, "#include <%s>\n", node->include.path);
                }
                else
                {
                    EMIT(ctx, "#include \"%s\"\n", node->include.path);
                }
            }
        }
        else if (node->type == NODE_AST_COMMENT)
        {
            EMIT(ctx, "%s\n", node->comment.content);
        }
        node = node->next;
    }
}

void emit_includes_and_aliases(ParserContext *ctx, ASTNode *node, VisitedModules **visited)
{
    if (visited)
    {
        emit_includes_and_aliases_internal(ctx, node, visited, 0);
    }
    else
    {
        VisitedModules *local_visited = NULL;
        emit_includes_and_aliases_internal(ctx, node, &local_visited, 0);
    }
}

// Emit type aliases (after struct defs so the aliased types exist)
static void emit_type_aliases_internal(ParserContext *ctx, ASTNode *node, VisitedModules **visited,
                                       int depth)
{
    if (depth > 1024)
    {
        zfatal("Infinite recursion detected in emit_type_aliases (ctx, circular imports?)");
    }
    while (node)
    {
        if (node->type == NODE_IMPORT)
        {
            if (!is_module_visited(*visited, node->import_stmt.path))
            {
                mark_module_visited(visited, node->import_stmt.path);
                emit_type_aliases_internal(ctx, node->import_stmt.module_root, visited, depth + 1);
            }
        }
        else if (node->type == NODE_TYPE_ALIAS)
        {
            if (node->cfg_condition)
            {
                EMIT(ctx, "#if %s\n", node->cfg_condition);
            }
            char *c_type_str = type_to_c_string(node->type_info);
            // Quick fix for raw function pointers and arrays in typedefs:
            // Since type_to_c_string returns `int (*)(int)`, simple replacement isn't valid
            // C. But Zen C doesn't officially support raw function pointer aliases. We'll just
            // print it.
            if (c_type_str)
            {
                if (strstr(c_type_str, "(*)"))
                {
                    char *ptr = (char *)strstr(c_type_str, "(*)");
                    ptrdiff_t prefix_len = ptr - c_type_str;
                    EMIT(ctx, "typedef %.*s (*%s)%s;\n", (int)prefix_len, c_type_str,
                         node->type_alias.alias, ptr + 3);
                }
                else
                {
                    EMIT(ctx, "typedef %s %s;\n", c_type_str, node->type_alias.alias);
                }
                zfree(c_type_str);
            }
            else
            {
                EMIT(ctx, "typedef %s %s;\n", node->type_alias.original_type,
                     node->type_alias.alias);
            }
            if (node->cfg_condition)
            {
                EMIT(ctx, "#endif\n");
            }
        }
        node = node->next;
    }
}

void emit_type_aliases(ParserContext *ctx, ASTNode *node, VisitedModules **visited)
{
    if (visited)
    {
        emit_type_aliases_internal(ctx, node, visited, 0);
    }
    else
    {
        VisitedModules *local_visited = NULL;
        emit_type_aliases_internal(ctx, node, &local_visited, 0);
    }
}

void emit_global_aliases(ParserContext *ctx)
{
    TypeAlias *ta = ctx->type_aliases;
    while (ta)
    {
        if (ta->type_info)
        {
            char *c_type_str = type_to_c_string(ta->type_info);
            if (c_type_str)
            {
                if (strstr(c_type_str, "(*)"))
                {
                    char *ptr = (char *)strstr(c_type_str, "(*)");
                    ptrdiff_t prefix_len = ptr - c_type_str;
                    EMIT(ctx, "typedef %.*s (*%s)%s;\n", (int)prefix_len, c_type_str, ta->alias,
                         ptr + 3);
                }
                else
                {
                    EMIT(ctx, "typedef %s %s;\n", c_type_str, ta->alias);
                }
                zfree(c_type_str);
            }
            else
            {
                EMIT(ctx, "typedef %s %s;\n", ta->original_type, ta->alias);
            }
        }
        else
        {
            EMIT(ctx, "typedef %s %s;\n", ta->original_type, ta->alias);
        }
        ta = ta->next;
    }
}

// Emit enum constructor prototypes
void emit_enum_protos(ParserContext *ctx, ASTNode *node)
{
    while (node)
    {
        if (node->type == NODE_ENUM && !node->enm.is_template)
        {
            // Only emit prototypes for ADT-style enums (with payloads)
            int has_payload = 0;
            ASTNode *v_ptr = node->enm.variants;
            while (v_ptr)
            {
                if (v_ptr->variant.payload)
                {
                    has_payload = 1;
                    break;
                }
                v_ptr = v_ptr->next;
            }

            if (has_payload)
            {
                const char *final_name = node->link_name ? node->link_name : node->enm.name;
                if (node->cfg_condition)
                {
                    EMIT(ctx, "#if %s\n", node->cfg_condition);
                }
                ASTNode *v = node->enm.variants;
                while (v)
                {
                    if (v->variant.payload)
                    {
                        Type *pt = v->variant.payload;
                        ASTNode *tuple_def = NULL;
                        if (pt->kind == TYPE_STRUCT && strncmp(pt->name, "Tuple__", 7) == 0)
                        {
                            tuple_def = find_struct_def(ctx, pt->name);
                        }

                        if (tuple_def)
                        {
                            EMIT(ctx, "%s %s__%s(", final_name, final_name, v->variant.name);
                            ASTNode *f = tuple_def->strct.fields;
                            int i = 0;
                            while (f)
                            {
                                char *at = f->field.type;
                                EMIT(ctx, "%s _%d%s", at, i, (f->next) ? ", " : "");
                                f = f->next;
                                i++;
                            }
                            EMIT(ctx, ");\n");
                        }
                        else
                        {
                            char *tstr = type_to_c_string(v->variant.payload);
                            EMIT(ctx, "%s %s__%s(%s v);\n", final_name, final_name, v->variant.name,
                                 tstr);
                            zfree(tstr);
                        }
                    }
                    else
                    {
                        EMIT(ctx, "%s %s__%s();\n", final_name, final_name, v->variant.name);
                    }
                    v = v->next;
                }
                if (node->cfg_condition)
                {
                    EMIT(ctx, "#endif\n");
                }
            }
        }
        node = node->next;
    }
}

// Emit lambda definitions.
// Collect the set of C type strings produced by `return <expr>;` in a lambda body.
static void collect_lambda_return_types(ParserContext *ctx, ASTNode *node, char ***types,
                                        int *count, int *cap)
{
    if (!node)
    {
        return;
    }

    if (node->type == NODE_RETURN && node->ret.value)
    {
        ASTNode *val = node->ret.value;
        char *tstr = NULL;
        if (val->type_info && val->type_info->kind != TYPE_UNKNOWN)
        {
            tstr = type_to_c_string(val->type_info);
        }
        else
        {
            tstr = infer_type(ctx, val);
        }

        if (tstr && strcmp(tstr, "void") != 0 && strcmp(tstr, "unknown") != 0)
        {
            int dup = 0;
            for (int i = 0; i < *count; i++)
            {
                if (strcmp((*types)[i], tstr) == 0)
                {
                    dup = 1;
                    break;
                }
            }
            if (!dup)
            {
                if (*count >= *cap)
                {
                    *cap *= 2;
                    *types = xrealloc(*types, sizeof(char *) * (size_t)(*cap));
                }
                (*types)[*count] = xstrdup(tstr);
                (*count)++;
            }
        }
        if (tstr)
        {
            zfree(tstr);
        }
        return;
    }

    switch (node->type)
    {
    case NODE_BLOCK:
        for (ASTNode *s = node->block.statements; s; s = s->next)
        {
            collect_lambda_return_types(ctx, s, types, count, cap);
        }
        break;
    case NODE_IF:
        collect_lambda_return_types(ctx, node->if_stmt.then_body, types, count, cap);
        collect_lambda_return_types(ctx, node->if_stmt.else_body, types, count, cap);
        break;
    case NODE_WHILE:
        collect_lambda_return_types(ctx, node->while_stmt.body, types, count, cap);
        break;
    case NODE_FOR:
        collect_lambda_return_types(ctx, node->for_stmt.body, types, count, cap);
        break;
    case NODE_FOR_RANGE:
        collect_lambda_return_types(ctx, node->for_range.body, types, count, cap);
        break;
    case NODE_MATCH:
        for (ASTNode *c = node->match_stmt.cases; c; c = c->next)
        {
            collect_lambda_return_types(ctx, c->match_case.body, types, count, cap);
        }
        break;
    default:
        break;
    }
}

// When the lambda's return type was not propagated from a call signature (e.g.
// lambdas inside f-string interpolations, which skip the normal type-check pass),
// infer it from the body's return statements.
static void infer_lambda_return_type(ParserContext *ctx, ASTNode *node)
{
    if (node->type_info && node->type_info->inner && node->type_info->inner->kind != TYPE_UNKNOWN)
    {
        return;
    }
    if (node->lambda.return_type && strcmp(node->lambda.return_type, "void") != 0 &&
        strcmp(node->lambda.return_type, "unknown") != 0)
    {
        return;
    }

    char **types = NULL;
    int count = 0;
    int cap = 8;
    types = xmalloc(sizeof(char *) * (size_t)cap);

    collect_lambda_return_types(ctx, node->lambda.body, &types, &count, &cap);

    if (count == 1)
    {
        zfree(node->lambda.return_type);
        node->lambda.return_type = xstrdup(types[0]);
    }

    for (int i = 0; i < count; i++)
    {
        zfree(types[i]);
    }
    zfree(types);
}

void emit_lambda_defs(ParserContext *ctx)
{
    LambdaRef *cur = ctx->global_lambdas;
    while (cur)
    {
        ASTNode *node = cur->node;
        int saved_defer = ctx->cg.defer_count;
        ctx->cg.defer_count = 0;

        infer_lambda_return_type(ctx, node);

        if (node->lambda.num_captures > 0)
        {
            EMIT(ctx, "struct Lambda_%d_Ctx {\n", node->lambda.lambda_id);
            emitter_indent(&ctx->cg.emitter);
            for (int i = 0; i < node->lambda.num_captures; i++)
            {
                if (node->lambda.capture_modes && node->lambda.capture_modes[i] == 1)
                {
                    char *tstr = NULL;
                    if (node->lambda.captured_types_info && node->lambda.captured_types_info[i])
                    {
                        tstr = type_to_c_string(node->lambda.captured_types_info[i]);
                    }
                    else
                    {
                        tstr = xstrdup(node->lambda.captured_types[i]);
                    }
                    EMIT(ctx, "%s* %s;\n", tstr, node->lambda.captured_vars[i]);
                    zfree(tstr);
                }
                else
                {
                    char *tstr = NULL;
                    if (node->lambda.captured_types_info && node->lambda.captured_types_info[i])
                    {
                        tstr = type_to_c_string(node->lambda.captured_types_info[i]);
                    }
                    else
                    {
                        tstr = xstrdup(node->lambda.captured_types[i]);
                    }
                    EMIT(ctx, "%s %s;\n", tstr, node->lambda.captured_vars[i]);
                    zfree(tstr);

                    char *tname = node->lambda.captured_types[i];
                    const char *clean = tname;
                    if (strncmp(clean, "struct ", 7) == 0)
                    {
                        clean += 7;
                    }

                    ASTNode *fdef = find_struct_def(ctx, clean);
                    if (fdef && fdef->type_info && fdef->type_info->traits.has_drop)
                    {
                        EMIT(ctx, "int __z_drop_flag_%s;\n", node->lambda.captured_vars[i]);
                    }
                }
            }
            emitter_dedent(&ctx->cg.emitter);
            EMIT(ctx, "};\n\n");

            // Generate Drop function for the closure context
            EMIT(ctx, "static void _lambda_%d_drop(void* _ctx) {\n", node->lambda.lambda_id);
            emitter_indent(&ctx->cg.emitter);
            EMIT(ctx, "struct Lambda_%d_Ctx* ctx = (struct Lambda_%d_Ctx*)_ctx;\n",
                 node->lambda.lambda_id, node->lambda.lambda_id);

            for (int i = 0; i < node->lambda.num_captures; i++)
            {
                if (node->lambda.capture_modes && node->lambda.capture_modes[i] == 0)
                {
                    char *tname = node->lambda.captured_types[i];
                    const char *clean = tname;
                    if (strncmp(clean, "struct ", 7) == 0)
                    {
                        clean += 7;
                    }

                    ASTNode *fdef = find_struct_def(ctx, clean);
                    if (fdef && fdef->type_info && fdef->type_info->traits.has_drop)
                    {
                        EMIT(ctx, "if (ctx->__z_drop_flag_%s) %s__Drop__glue(&ctx->%s);\n",
                             node->lambda.captured_vars[i], clean, node->lambda.captured_vars[i]);
                    }
                }
            }

            EMIT(ctx, "free(_ctx);\n");
            emitter_dedent(&ctx->cg.emitter);
            EMIT(ctx, "}\n\n");
        }

        char *ret_type_str = node->lambda.return_type;
        if (node->type_info && node->type_info->inner &&
            node->type_info->inner->kind != TYPE_UNKNOWN)
        {
            ret_type_str = type_to_c_string(node->type_info->inner);
        }

        if (strcmp(ret_type_str, "unknown") == 0)
        {
            EMIT(ctx, "void* _lambda_%d(", node->lambda.lambda_id);
        }
        else
        {
            EMIT(ctx, "%s _lambda_%d(", ret_type_str, node->lambda.lambda_id);
        }

        if (!node->lambda.is_bare)
        {
            EMIT(ctx, "void* _ctx");
        }

        if (node->type_info && node->type_info->inner &&
            node->type_info->inner->kind != TYPE_UNKNOWN)
        {
            zfree(ret_type_str);
        }

        for (int i = 0; i < node->lambda.num_params; i++)
        {
            char *param_type_str = node->lambda.param_types[i];
            if (node->type_info && node->type_info->args && node->type_info->args[i] &&
                node->type_info->args[i]->kind != TYPE_UNKNOWN)
            {
                param_type_str = type_to_c_string(node->type_info->args[i]);
            }

            if (!node->lambda.is_bare || i > 0)
            {
                EMIT(ctx, ", ");
            }

            if (strcmp(param_type_str, "unknown") == 0)
            {
                EMIT(ctx, "void* %s", node->lambda.param_names[i]);
            }
            else
            {
                EMIT(ctx, "%s %s", param_type_str, node->lambda.param_names[i]);
            }
            if (node->type_info && node->type_info->args && node->type_info->args[i] &&
                node->type_info->args[i]->kind != TYPE_UNKNOWN)
            {
                zfree(param_type_str);
            }
        }
        EMIT(ctx, ") {\n");
        emitter_indent(&ctx->cg.emitter);

        if (node->lambda.num_captures > 0)
        {
            EMIT(ctx, "struct Lambda_%d_Ctx* ctx = (struct Lambda_%d_Ctx*)_ctx;\n",
                 node->lambda.lambda_id, node->lambda.lambda_id);
        }

        ctx->cg.current_lambda = node;
        if (node->lambda.body && node->lambda.body->type == NODE_BLOCK)
        {
            if (node->lambda.is_expression && node->type_info && node->type_info->inner &&
                node->type_info->inner->kind != TYPE_VOID)
            {
                ASTNode *stmt = node->lambda.body->block.statements;
                while (stmt)
                {
                    if (stmt->next == NULL)
                    {
                        if (stmt->type != NODE_RETURN)
                        {
                            EMIT(ctx, "return ");
                        }
                        codegen_node_single(ctx, stmt);
                    }
                    else
                    {
                        codegen_node_single(ctx, stmt);
                    }
                    stmt = stmt->next;
                }
            }
            else
            {
                codegen_walker(ctx, node->lambda.body->block.statements);
            }
        }
        else if (node->lambda.body)
        {
            if (node->type_info && node->type_info->inner &&
                node->type_info->inner->kind != TYPE_VOID && node->lambda.body->type != NODE_RETURN)
            {
                EMIT(ctx, "return ");
            }
            codegen_node_single(ctx, node->lambda.body);
            EMIT(ctx, ";\n");
        }
        ctx->cg.current_lambda = NULL;

        for (int i = ctx->cg.defer_count - 1; i >= 0; i--)
        {
            emit_source_mapping_duplicate(ctx, ctx->cg.defer_stack[i]);
            codegen_node_single(ctx, ctx->cg.defer_stack[i]);
        }

        emitter_dedent(&ctx->cg.emitter);
        EMIT(ctx, "}\n\n");

        ctx->cg.defer_count = saved_defer;
        cur = cur->next;
    }
}
