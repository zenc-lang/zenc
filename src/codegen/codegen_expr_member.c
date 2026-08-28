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

void handle_expr_member(ParserContext *ctx, ASTNode *node)
{
    if (strcmp(node->member.field, "len") == 0)
    {
        if (node->member.target->type_info && node->member.target->type_info->kind == TYPE_ARRAY &&
            node->member.target->type_info->array_size > 0)
        {
            EMIT(ctx, "%d", node->member.target->type_info->array_size);
            return;
        }
    }

    if (node->member.target->type_info && node->member.target->type_info->kind == TYPE_VECTOR)
    {
        codegen_expression(ctx, node->member.target);
        return;
    }

    if (strcmp(node->member.field, "tag") == 0)
    {
        char *tname = infer_type(ctx, node->member.target);
        if (tname)
        {
            if (is_simple_enum(ctx, tname))
            {
                codegen_expression(ctx, node->member.target);
                zfree(tname);
                return;
            }
            zfree(tname);
        }
    }

    if (node->member.is_pointer_access == 2)
    {
        EMIT(ctx, "({ ");
        emit_auto_type(ctx, node->member.target, node->token);
        EMIT(ctx, " _t = (");
        codegen_expression(ctx, node->member.target);
        char *field = node->member.field;
        if (field && field[0] >= '0' && field[0] <= '9')
        {
            EMIT(ctx, "); _t ? _t->v%s : 0; })", field);
        }
        else
        {
            EMIT(ctx, "); _t ? _t->%s : 0; })", field);
        }
    }
    else
    {
        if (node->member.target->kind == NODE_EXPR_VAR)
        {
            ASTNode *def = find_struct_def(ctx, node->member.target->var_ref.name);
            if (def && def->kind == NODE_ENUM)
            {
                EMIT(ctx, "%s__%s", node->member.target->var_ref.name, node->member.field);
                return;
            }
        }

        codegen_expression(ctx, node->member.target);
        char *lt = infer_type(ctx, node->member.target);
        int actually_ptr = 0;
        if (lt && (lt[strlen(lt) - 1] == '*' || strstr(lt, "*")))
        {
            actually_ptr = 1;
        }
        if (lt)
        {
            zfree(lt);
        }

        char *field = node->member.field;
        if (field && field[0] >= '0' && field[0] <= '9')
        {
            EMIT(ctx, "%sv%s", actually_ptr ? "->" : ".", field);
        }
        else
        {
            EMIT(ctx, "%s%s", actually_ptr ? "->" : ".", field);
        }
    }
}

void handle_expr_index(ParserContext *ctx, ASTNode *node)
{
    int is_slice_struct = 0;
    if (node->index.array->type_info)
    {
        if (node->index.array->type_info->kind == TYPE_ARRAY &&
            node->index.array->type_info->array_size == 0)
        {
            is_slice_struct = 1;
        }
    }

    if (!is_slice_struct && node->index.array->resolved_type)
    {
        if (strncmp(node->index.array->resolved_type, "Slice__", 7) == 0)
        {
            is_slice_struct = 1;
        }
    }

    if (!is_slice_struct && !node->index.array->type_info && !node->index.array->resolved_type)
    {
        char *inferred = infer_type(ctx, node->index.array);
        if (inferred && strncmp(inferred, "Slice__", 7) == 0)
        {
            is_slice_struct = 1;
        }
        if (inferred)
        {
            zfree(inferred);
        }
    }

    if (is_slice_struct)
    {
        if (node->index.array->kind == NODE_EXPR_VAR)
        {
            codegen_expression(ctx, node->index.array);
            EMIT(ctx, ".data[_z_check_bounds(");
            codegen_expression(ctx, node->index.index);
            EMIT(ctx, ", ");
            codegen_expression(ctx, node->index.array);
            EMIT(ctx, ".len)]");
        }
        else
        {
            codegen_expression(ctx, node->index.array);
            EMIT(ctx, ".data[");
            codegen_expression(ctx, node->index.index);
            EMIT(ctx, "]");
        }
    }
    else
    {
        char *base_type = infer_type(ctx, node->index.array);
        char *struct_name = NULL;
        char method_name[MAX_MANGLED_NAME_LEN] = {0};

        if (base_type && !strchr(base_type, '*'))
        {
            char clean[MAX_TYPE_NAME_LEN];
            strncpy(clean, base_type, sizeof(clean) - 1);
            clean[sizeof(clean) - 1] = '\0';
            if (strncmp(clean, "struct ", 7) == 0)
            {
                memmove(clean, clean + 7, strlen(clean + 7) + 1);
            }

            snprintf(method_name, sizeof(method_name), "%s__index", clean);
            if (find_func(ctx, method_name))
            {
                struct_name = xstrdup(clean);
            }
            else
            {
                snprintf(method_name, sizeof(method_name), "%s__get", clean);
                if (find_func(ctx, method_name))
                {
                    struct_name = xstrdup(clean);
                }
            }
        }

        if (struct_name)
        {
            FuncSig *sig = find_func(ctx, method_name);
            int needs_addr =
                (sig && sig->total_args > 0 && sig->arg_types[0]->kind == TYPE_POINTER);

            EMIT(ctx, "%s(", method_name);
            if (needs_addr)
            {
                EMIT(ctx, "&");
            }
            codegen_expression(ctx, node->index.array);
            EMIT(ctx, ", ");
            codegen_expression(ctx, node->index.index);
            ASTNode *extra = node->index.extra_indices;
            while (extra)
            {
                EMIT(ctx, ", ");
                codegen_expression(ctx, extra);
                extra = extra->next;
            }
            EMIT(ctx, ")");
            zfree(struct_name);
        }
        else
        {
            int fixed_size = -1;
            if (node->index.array->type_info && (node->index.array->type_info->kind == TYPE_ARRAY ||
                                                 node->index.array->type_info->kind == TYPE_VECTOR))
            {
                fixed_size = node->index.array->type_info->array_size;
            }

            codegen_expression(ctx, node->index.array);
            EMIT(ctx, "[");
            if (fixed_size > 0)
            {
                EMIT(ctx, "_z_check_bounds(");
            }
            codegen_expression(ctx, node->index.index);
            if (fixed_size > 0)
            {
                EMIT(ctx, ", %d)", fixed_size);
            }
            EMIT(ctx, "]");
        }
    }
}

void handle_expr_slice(ParserContext *ctx, ASTNode *node)
{
    int known_size = -1;
    int is_slice_struct = 0;
    if (node->slice.array->type_info)
    {
        if (node->slice.array->type_info->kind == TYPE_ARRAY)
        {
            known_size = node->slice.array->type_info->array_size;
            if (known_size == 0)
            {
                is_slice_struct = 1;
            }
        }
    }

    char *tname = "unknown";
    if (node->type_info && node->type_info->inner)
    {
        tname = type_to_c_string(node->type_info->inner);
    }

    EMIT(ctx, "({ ");
    emit_auto_type(ctx, node->slice.array, node->token);
    EMIT(ctx, " _arr = ");
    codegen_expression(ctx, node->slice.array);
    EMIT(ctx, "; int _start = ");
    if (node->slice.start)
    {
        codegen_expression(ctx, node->slice.start);
    }
    else
    {
        EMIT(ctx, "0");
    }
    EMIT(ctx, "; int _len = ");

    if (node->slice.end)
    {
        codegen_expression(ctx, node->slice.end);
        EMIT(ctx, " - _start; ");
    }
    else
    {
        if (known_size > 0)
        {
            EMIT(ctx, "%d - _start; ", known_size);
        }
        else if (is_slice_struct)
        {
            EMIT(ctx, "_arr.len - _start; ");
        }
        else
        {
            EMIT(ctx, "0; ");
        }
    }

    if (is_slice_struct)
    {
        EMIT(ctx, "(Slice__%s){ .data = _arr.data + _start, .len = _len, .cap = _len }; })", tname);
    }
    else
    {
        EMIT(ctx, "(Slice__%s){ .data = _arr + _start, .len = _len, .cap = _len }; })", tname);
    }
    if (tname && strcmp(tname, "unknown") != 0)
    {
        zfree(tname);
    }
}
