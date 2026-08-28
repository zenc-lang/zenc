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

// Emit struct and enum definitions.
static void emit_struct_defs_internal(ParserContext *ctx, ASTNode *node, VisitedModules **visited,
                                      int depth, int filter_type)
{
    if (depth > 1024)
    {
        zfatal("Infinite recursion detected in emit_struct_defs (ctx, circular imports?)");
    }
    while (node)
    {
        if (node->kind == NODE_IMPORT)
        {
            if (!is_module_visited(*visited, node->import_stmt.path))
            {
                mark_module_visited(visited, node->import_stmt.path);
                emit_struct_defs_internal(ctx, node->import_stmt.module_root, visited, depth + 1,
                                          filter_type);
            }
            node = node->next;
            continue;
        }

        if (node->kind == NODE_ROOT)
        {
            if (node->root.children != node)
            { // Basic cycle check
                emit_struct_defs_internal(ctx, node->root.children, visited, depth + 1,
                                          filter_type);
            }
            node = node->next;
            continue;
        }
        ASTNode *v;
        if (node->kind == NODE_STRUCT && node->strct.is_template)
        {
            node = node->next;
            continue;
        }
        if (node->kind == NODE_ENUM && node->enm.is_template)
        {
            node = node->next;
            continue;
        }
        if (filter_type == NODE_ENUM && node->kind != NODE_ENUM)
        {
            node = node->next;
            continue;
        }
        if (filter_type == NODE_STRUCT && node->kind != NODE_STRUCT)
        {
            node = node->next;
            continue;
        }
        if (node->kind == NODE_STRUCT)
        {
            if (node->strct.is_incomplete)
            {
                // Forward declaration - no body needed (typedef handles it)
                node = node->next;
                continue;
            }

            if (node->strct.crepr_c_type)
            {
                // @crepr("C.kind.name") — use the C type directly via typedef.
                // Fields are documentation only; the C compiler resolves field access
                // against the actual C struct through the typedef.
                if (node->cfg_condition)
                {
                    EMIT(ctx, "#if %s\n", node->cfg_condition);
                }
                EMIT(ctx, "typedef %s %s;\n\n", node->strct.crepr_c_type, node->strct.name);
                if (node->cfg_condition)
                {
                    EMIT(ctx, "#endif\n");
                }
                node = node->next;
                continue;
            }

            if (node->cfg_condition)
            {
                EMIT(ctx, "#if %s\n", node->cfg_condition);
            }

            if (node->type_info && node->type_info->kind == TYPE_VECTOR)
            {
                char *inner_c = type_to_c_string(node->type_info->inner);
                EMIT(ctx, "typedef ZC_SIMD(%s, %d) %s;\n", inner_c, node->type_info->array_size,
                     node->strct.name);
                zfree(inner_c);
                if (node->cfg_condition)
                {
                    EMIT(ctx, "#endif\n");
                }
                node = node->next;
                continue;
            }

            if (node->strct.is_union)
            {
                EMIT(ctx, "union");
            }
            else
            {
                EMIT(ctx, "struct");
            }

            int has_any_attr = node->strct.is_packed || node->strct.align ||
                               node->strct.is_export || node->strct.attributes;
            if (has_any_attr)
            {
                EMIT(ctx, " __attribute__((");
                int first_attr = 1;
                if (node->strct.is_packed)
                {
                    EMIT(ctx, "packed");
                    first_attr = 0;
                }
                if (node->strct.align)
                {
                    if (!first_attr)
                    {
                        EMIT(ctx, ", ");
                    }
                    EMIT(ctx, "aligned(%d)", node->strct.align);
                    first_attr = 0;
                }
                if (node->strct.is_export)
                {
                    if (!first_attr)
                    {
                        EMIT(ctx, ", ");
                    }
                    EMIT(ctx, "visibility(\"default\")");
                    first_attr = 0;
                }
                if (node->strct.attributes)
                {
                    Attribute *custom = node->strct.attributes;
                    while (custom)
                    {
                        if (!first_attr)
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
                        first_attr = 0;
                        custom = custom->next;
                    }
                }
                EMIT(ctx, "))");
            }

            if (node->strct.name)
            {
                EMIT(ctx, " %s", node->link_name ? node->link_name : node->strct.name);
            }

            EMIT(ctx, " {");
            EMIT(ctx, "\n");
            emitter_indent(&ctx->cg.emitter);
            if (node->strct.fields)
            {
                codegen_walker(ctx, node->strct.fields);
            }
            else
            {
                // C requires at least one member in a struct.
                EMIT(ctx, "char _placeholder;\n");
            }
            emitter_dedent(&ctx->cg.emitter);
            EMIT(ctx, "}");

            EMIT(ctx, ";\n\n");
            if (node->cfg_condition)
            {
                EMIT(ctx, "#endif\n");
            }
        }
        else if (node->kind == NODE_ENUM)
        {
            const char *final_name = node->link_name ? node->link_name : node->enm.name;
            if (node->cfg_condition)
            {
                EMIT(ctx, "#if %s\n", node->cfg_condition);
            }

            int has_payload = 0;
            v = node->enm.variants;
            while (v)
            {
                if (v->variant.payload)
                {
                    has_payload = 1;
                    break;
                }
                v = v->next;
            }

            if (!has_payload)
            {
                EMIT(ctx, "typedef enum { ");
                v = node->enm.variants;
                while (v)
                {
                    EMIT(ctx, "%s__%s_Tag, ", final_name, v->variant.name);
                    v = v->next;
                }
                EMIT(ctx, "} %s;\n\n", final_name);

                v = node->enm.variants;
                while (v)
                {
                    EMIT(ctx, "static inline %s %s__%s() { return %s__%s_Tag; }\n", final_name,
                         final_name, v->variant.name, final_name, v->variant.name);
                    v = v->next;
                }
                EMIT(ctx, "\n");
            }

            else
            {
                EMIT(ctx, "typedef enum { ");
                v = node->enm.variants;
                while (v)
                {
                    EMIT(ctx, "%s__%s_Tag, ", final_name, v->variant.name);
                    v = v->next;
                }
                EMIT(ctx, "} %s_Tag;\n", final_name);
                EMIT(ctx, "struct %s { %s_Tag tag; union { ", final_name, final_name);
                v = node->enm.variants;
                while (v)
                {
                    if (v->variant.payload)
                    {
                        char *tstr = type_to_c_string(v->variant.payload);
                        EMIT(ctx, "%s %s; ", tstr, v->variant.name);
                        zfree(tstr);
                    }
                    v = v->next;
                }
                EMIT(ctx, "} data; };\n\n");

                v = node->enm.variants;
                while (v)
                {
                    if (v->variant.payload)
                    {
                        Type *pt = v->variant.payload;
                        char *tstr = type_to_c_string(pt);
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
                            EMIT(ctx, ") {\n");
                            emitter_indent(&ctx->cg.emitter);
                            if (ctx->config->use_cpp)
                            {
                                EMIT(ctx, "%s _res = {}; _res.tag = %s__%s_Tag; ", final_name,
                                     final_name, v->variant.name);
                                for (int j = 0; j < i; j++)
                                {
                                    EMIT(ctx, "_res.data.%s.v%d = _%d; ", v->variant.name, j, j);
                                }
                                emitter_dedent(&ctx->cg.emitter);
                                EMIT(ctx, "return _res; }\n");
                            }
                            else
                            {
                                EMIT(ctx, "return (%s){.tag=%s__%s_Tag, .data.%s={", final_name,
                                     final_name, v->variant.name, v->variant.name);
                                for (int j = 0; j < i; j++)
                                {
                                    EMIT(ctx, ".v%d=_%d%s", j, j, (j == i - 1) ? "" : ", ");
                                }
                                emitter_dedent(&ctx->cg.emitter);
                                EMIT(ctx, "}}; }\n");
                            }
                        }
                        else
                        {
                            if (ctx->config->use_cpp)
                            {
                                EMIT(ctx,
                                     "%s %s__%s(%s v) { %s _res = {}; _res.tag=%s__%s_Tag; "
                                     "_res.data.%s=v; return _res; }\n",
                                     final_name, final_name, v->variant.name, tstr, final_name,
                                     final_name, v->variant.name, v->variant.name);
                            }
                            else
                            {
                                EMIT(ctx,
                                     "%s %s__%s(%s v) { return (%s){.tag=%s__%s_Tag, .data.%s=v}; "
                                     "}\n",
                                     final_name, final_name, v->variant.name, tstr, final_name,
                                     final_name, v->variant.name, v->variant.name);
                            }
                        }
                        zfree(tstr);
                    }
                    else
                    {
                        if (ctx->config->use_cpp)
                        {
                            EMIT(
                                ctx,
                                "%s %s__%s() { %s _res = {}; _res.tag=%s__%s_Tag; return _res; }\n",
                                final_name, final_name, v->variant.name, final_name, final_name,
                                v->variant.name);
                        }
                        else
                        {
                            EMIT(ctx, "%s %s__%s() { return (%s){.tag=%s__%s_Tag}; }\n", final_name,
                                 final_name, v->variant.name, final_name, final_name,
                                 v->variant.name);
                        }
                    }
                    v = v->next;
                }
            }
            if (node->cfg_condition)
            {
                EMIT(ctx, "#endif\n");
            }
        }

        node = node->next;
    }
}

void emit_struct_defs(ParserContext *ctx, ASTNode *node, VisitedModules **visited)
{
    if (visited)
    {
        emit_struct_defs_internal(ctx, node, visited, 0, 0);
    }
    else
    {
        VisitedModules *local_visited = NULL;
        emit_struct_defs_internal(ctx, node, &local_visited, 0, 0);
    }
}

// Helper to substitute 'Self' with replacement string
static char *substitute_proto_self(const char *type_str, const char *replacement)
{
    if (!type_str)
    {
        return NULL;
    }
    if (strcasecmp(type_str, "Self") == 0)
    {
        return xstrdup(replacement);
    }
    // Handle pointers (Self* -> replacement*)
    if (strncasecmp(type_str, "Self", 4) == 0)
    {
        const char *rest = type_str + 4;
        char *buf = xmalloc(strlen(replacement) + strlen(rest) + 1);
        sprintf(buf, "%s%s", replacement, rest);
        return buf;
    }
    return xstrdup(type_str);
}

// Emit trait definitions.
static void emit_trait_defs_internal(ParserContext *ctx, ASTNode *node, VisitedModules **visited,
                                     int depth)
{
    if (depth > 1024)
    {
        zfatal("Infinite recursion detected in emit_trait_defs (ctx, circular imports?)");
    }
    while (node)
    {
        if (node->kind == NODE_IMPORT)
        {
            if (!is_module_visited(*visited, node->import_stmt.path))
            {
                mark_module_visited(visited, node->import_stmt.path);
                emit_trait_defs_internal(ctx, node->import_stmt.module_root, visited, depth + 1);
            }
            node = node->next;
            continue;
        }
        if (node->kind == NODE_TRAIT)
        {
            if (node->trait.generic_param_count > 0)
            {
                node = node->next;
                continue;
            }
            if (node->cfg_condition)
            {
                EMIT(ctx, "#if %s\n", node->cfg_condition);
            }
            EMIT(ctx, "typedef struct %s_VTable {\n", node->trait.name);
            emitter_indent(&ctx->cg.emitter);
            ASTNode *m = node->trait.methods;
            while (m)
            {
                char *ret_safe = substitute_proto_self(m->func.ret_type, "void*");
                const char *orig = parse_original_method_name(m->func.name);
                EMIT(ctx, "%s (*%s)(", ret_safe, orig);
                zfree(ret_safe);

                int has_self = (m->func.args && strstr(m->func.args, "self"));
                if (!has_self)
                {
                    EMIT(ctx, "void* self");
                }

                if (m->func.args && strlen(m->func.args) > 0)
                {
                    char *args_safe = replace_type_str(m->func.args, "Self", "void*", NULL, NULL);
                    // Filter out "void* self" or "const void* self" if it's already there to avoid
                    // duplication
                    if (strstr(args_safe, "void* self") == args_safe ||
                        strstr(args_safe, "const void* self") == args_safe)
                    {
                        EMIT(ctx, "%s", args_safe);
                    }
                    else if (strlen(args_safe) > 0)
                    {
                        if (!has_self)
                        {
                            EMIT(ctx, ", ");
                        }
                        EMIT(ctx, "%s", args_safe);
                    }
                    zfree(args_safe);
                }
                EMIT(ctx, ");\n");
                m = m->next;
            }
            emitter_dedent(&ctx->cg.emitter);
            EMIT(ctx, "} %s_VTable;\n", node->trait.name);
            EMIT(ctx, "typedef struct %s { void *self; %s_VTable *vtable; } %s;\n",
                 node->trait.name, node->trait.name, node->trait.name);

            if (node->cfg_condition)
            {
                EMIT(ctx, "#endif\n");
            }
            EMIT(ctx, "\n");
        }
        node = node->next;
    }
}

void emit_trait_defs(ParserContext *ctx, ASTNode *node, VisitedModules **visited)
{
    if (visited)
    {
        emit_trait_defs_internal(ctx, node, visited, 0);
    }
    else
    {
        VisitedModules *local_visited = NULL;
        emit_trait_defs_internal(ctx, node, &local_visited, 0);
    }
}

// Emit trait wrapper functions.
static void emit_trait_wrappers_internal(ParserContext *ctx, ASTNode *node,
                                         VisitedModules **visited, int depth)
{
    if (depth > 1024)
    {
        zfatal("Infinite recursion detected in emit_trait_wrappers (ctx, circular imports?)");
    }
    while (node)
    {
        if (node->kind == NODE_IMPORT)
        {
            if (!is_module_visited(*visited, node->import_stmt.path))
            {
                mark_module_visited(visited, node->import_stmt.path);
                emit_trait_wrappers_internal(ctx, node->import_stmt.module_root, visited,
                                             depth + 1);
            }
            node = node->next;
            continue;
        }
        if (node->kind == NODE_TRAIT)
        {
            if (node->trait.generic_param_count > 0)
            {
                node = node->next;
                continue;
            }
            if (node->cfg_condition)
            {
                EMIT(ctx, "#if %s\n", node->cfg_condition);
            }
            ASTNode *m = node->trait.methods;
            while (m)
            {
                char *ret_sub = substitute_proto_self(m->func.ret_type, node->trait.name);
                const char *orig = parse_original_method_name(m->func.name);
                int is_const_self = (m->func.count > 0 && m->func.arg_types &&
                                     m->func.arg_types[0] && m->func.arg_types[0]->is_const);
                EMIT(ctx, "%s %s__%s(%s%s* self", ret_sub, node->trait.name, orig,
                     is_const_self ? "const " : "", node->trait.name);

                if (m->func.args && strlen(m->func.args) > 0)
                {
                    char *sa = replace_type_str(m->func.args, "Self", node->trait.name, NULL, NULL);
                    if (strstr(sa, "void* self") == sa || strstr(sa, "const void* self") == sa)
                    {
                        char *comma = (char *)strchr(sa, ',');
                        if (comma)
                        {
                            EMIT(ctx, ", %s", comma + 1);
                        }
                    }
                    else if (strlen(sa) > 0)
                    {
                        EMIT(ctx, ", %s", sa);
                    }
                    zfree(sa);
                }
                EMIT(ctx, ") {\n");
                emitter_indent(&ctx->cg.emitter);

                int ret_is_self = (m->func.ret_type && strcasecmp(m->func.ret_type, "Self") == 0);
                if (ret_is_self)
                {
                    EMIT(ctx, "void* res = self->vtable->%s(self->self", orig);
                }
                else
                {
                    EMIT(ctx, "return self->vtable->%s(self->self", orig);
                }

                if (m->func.args && strlen(m->func.args) > 0)
                {
                    char *call_args = extract_call_args(m->func.args);
                    if (call_args && strlen(call_args) > 0)
                    {
                        if (strcmp(call_args, "self") != 0)
                        {
                            if (strstr(call_args, "self") == call_args)
                            {
                                char *comma = (char *)strchr(call_args, ',');
                                if (comma)
                                {
                                    EMIT(ctx, ", %s", comma + 1);
                                }
                            }
                            else
                            {
                                EMIT(ctx, ", %s", call_args);
                            }
                        }
                    }
                    zfree(call_args);
                }
                EMIT(ctx, ");\n");

                if (ret_is_self)
                {
                    EMIT(ctx, "return (%s){.self = res, .vtable = self->vtable};\n",
                         node->trait.name);
                }
                emitter_dedent(&ctx->cg.emitter);
                EMIT(ctx, "}\n\n");
                zfree(ret_sub);
                m = m->next;
            }
            if (node->cfg_condition)
            {
                EMIT(ctx, "#endif\n");
            }
            EMIT(ctx, "\n");
        }
        node = node->next;
    }
}

void emit_trait_wrappers(ParserContext *ctx, ASTNode *node, VisitedModules **visited)
{
    if (visited)
    {
        emit_trait_wrappers_internal(ctx, node, visited, 0);
    }
    else
    {
        VisitedModules *local_visited = NULL;
        emit_trait_wrappers_internal(ctx, node, &local_visited, 0);
    }
}

// Emit global variables
