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

void handle_expr_cast(ParserContext *ctx, ASTNode *node)
{
    const char *t = node->cast.target_type;
    const char *mapped = map_to_c_type(t);

    EMIT(ctx, "((%s)(", mapped);
    Type *src_type = node->cast.expr->type_info;
    int cast_tag = 0;
    if (src_type && src_type->kind == TYPE_ENUM)
    {
        const char *clean_name = src_type->name;
        if (strncmp(clean_name, "struct ", 7) == 0)
        {
            clean_name += 7;
        }
        ASTNode *def = find_struct_def(ctx, clean_name);
        if (def && def->kind == NODE_ENUM)
        {
            ASTNode *v = def->enm.variants;
            while (v)
            {
                if (v->variant.payload)
                {
                    cast_tag = 1;
                    break;
                }
                v = v->next;
            }
        }
    }

    if (cast_tag)
    {
        codegen_expression(ctx, node->cast.expr);
        EMIT(ctx, ".tag");
    }
    else
    {
        codegen_expression(ctx, node->cast.expr);
    }
    EMIT(ctx, "))");
}

void handle_reflection(ParserContext *ctx, ASTNode *node)
{
    Type *t = node->reflection.target_type;
    if (node->reflection.kind == 0)
    {
        char *s = type_to_c_string(t);
        EMIT(ctx, "\"%s\"", s);
        zfree(s);
    }
    else
    {
        if (t->kind != TYPE_STRUCT || !t->name)
        {
            EMIT(ctx, "((void*)0)");
            return;
        }
        char *sname = t->name;
        ASTNode *def = find_struct_def(ctx, sname);
        if (!def)
        {
            EMIT(ctx, "((void*)0)");
            return;
        }

        EMIT(ctx,
             "({ static struct { char *name; char *type; unsigned long offset; } _fields_%s[] "
             "= {",
             sname);
        ASTNode *f = def->strct.fields;
        while (f)
        {
            if (f->kind == NODE_FIELD)
            {
                EMIT(ctx, "{ \"%s\", \"%s\", __builtin_offsetof(struct %s, %s) }, ", f->field.name,
                     f->field.type, sname, f->field.name);
            }
            f = f->next;
        }
        EMIT(ctx, "{ 0 } }; (void*)_fields_%s; })", sname);
    }
}
