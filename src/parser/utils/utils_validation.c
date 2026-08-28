// SPDX-License-Identifier: MIT
#include "plugins/plugin_manager.h"
#include "parser.h"
#include "utils/format_expr.h"
#include "utils/colors.h"
#include "utils/utils.h"
#include "constants.h"
#include "ast/primitives.h"
#include <ctype.h>
#include "analysis/const_fold.h"

void register_type_usage(ParserContext *ctx, const char *name, Token t)
{
    if (ctx->is_speculative)
    {
        return;
    }

    TypeUsage *u = xmalloc(sizeof(TypeUsage));
    u->name = xstrdup(name);
    u->location = t;
    u->next = ctx->pending_type_validations;
    ctx->pending_type_validations = u;
}

int validate_types(ParserContext *ctx)
{
    int errors = 0;
    TypeUsage *u = ctx->pending_type_validations;
    while (u)
    {
        ASTNode *def = find_struct_def(ctx, u->name);
        if (!def)
        {
            const char *alias = find_type_alias(ctx, u->name);
            if (!alias)
            {
                SelectiveImport *si = find_selective_import(ctx, u->name);
                if (!si && !is_extern_symbol(ctx, u->name))
                {
                    if (!is_trait(u->name) && TYPE_UNKNOWN == find_primitive_kind(u->name))
                    {
                        int whitelisted = 0;
                        if (ctx->config->c_type_whitelist)
                        {
                            char **ptr = ctx->config->c_type_whitelist;
                            while (*ptr)
                            {
                                if (strcmp(u->name, *ptr) == 0)
                                {
                                    whitelisted = 1;
                                    break;
                                }
                                ptr++;
                            }
                        }

                        if (whitelisted)
                        {
                            u = u->next;
                            continue;
                        }

                        if (!ctx->config->mode_lsp && !ctx->config->mode_doc)
                        {
                            char msg[MAX_SHORT_MSG_LEN];
                            snprintf(msg, sizeof(msg),
                                     "Unknown type '%s' (assuming external C struct)", u->name);
                            const char *hint = get_closest_type_hint(ctx, u->name);
                            if (hint)
                            {
                                char help[MAX_MANGLED_NAME_LEN];
                                snprintf(help, sizeof(help), "Did you mean '%s'?", hint);
                                zwarn_with_suggestion(u->location, msg, help);
                            }
                            else
                            {
                                zwarn_at(u->location, "%s", msg);
                            }
                        }
                    }
                }
            }
        }
        u = u->next;
    }
    return errors == 0;
}

void propagate_vector_inner_types(ParserContext *ctx)
{
    StructRef *ref = ctx->parsed_structs_list;
    while (ref)
    {
        ASTNode *strct = ref->node;
        if (strct && strct->kind == NODE_STRUCT && strct->type_info &&
            strct->type_info->kind == TYPE_VECTOR && !strct->type_info->inner)
        {
            if (strct->strct.fields && strct->strct.fields->type_info)
            {
                strct->type_info->inner = strct->strct.fields->type_info;
            }
        }
        ref = ref->next;
    }
}

void propagate_drop_traits(ParserContext *ctx)
{
    int changed = 1;
    while (changed)
    {
        changed = 0;

        StructRef *ref = ctx->parsed_structs_list;
        while (ref)
        {
            ASTNode *strct = ref->node;
            if (strct && strct->kind == NODE_STRUCT && strct->type_info &&
                !strct->type_info->traits.has_drop)
            {
                ASTNode *field = strct->strct.fields;
                while (field)
                {
                    Type *ft = field->type_info;
                    if (ft)
                    {
                        if (ft->kind == TYPE_VECTOR)
                        {
                            strct->type_info->traits.has_drop = 1;
                            ft->traits.has_drop = 1;
                            changed = 1;
                            break;
                        }
                        if (ft->kind == TYPE_FUNCTION && ft->traits.has_drop && !ft->is_raw)
                        {
                            strct->type_info->traits.has_drop = 1;
                            ft->traits.has_drop = 1;
                            changed = 1;
                            break;
                        }
                        if (ft->kind == TYPE_STRUCT && ft->name)
                        {
                            ASTNode *fdef = find_struct_def(ctx, ft->name);
                            if (fdef && fdef->type_info && fdef->type_info->traits.has_drop)
                            {
                                strct->type_info->traits.has_drop = 1;
                                ft->traits.has_drop = 1;
                                changed = 1;
                                break;
                            }
                        }
                    }
                    field = field->next;
                }
            }
            ref = ref->next;
        }

        ASTNode *ins = ctx->instantiated_structs;
        while (ins)
        {
            if (ins->kind == NODE_STRUCT && ins->type_info && !ins->type_info->traits.has_drop)
            {
                ASTNode *field = ins->strct.fields;
                while (field)
                {
                    Type *ft = field->type_info;
                    if (ft)
                    {
                        if (ft->kind == TYPE_VECTOR)
                        {
                            ins->type_info->traits.has_drop = 1;
                            ft->traits.has_drop = 1;
                            changed = 1;
                            break;
                        }
                        if (ft->kind == TYPE_FUNCTION && ft->traits.has_drop && !ft->is_raw)
                        {
                            ins->type_info->traits.has_drop = 1;
                            ft->traits.has_drop = 1;
                            changed = 1;
                            break;
                        }
                        if (ft->kind == TYPE_STRUCT && ft->name)
                        {
                            ASTNode *fdef = find_struct_def(ctx, ft->name);
                            if (fdef && fdef->type_info && fdef->type_info->traits.has_drop)
                            {
                                ins->type_info->traits.has_drop = 1;
                                ft->traits.has_drop = 1;
                                changed = 1;
                                break;
                            }
                        }
                    }
                    field = field->next;
                }
            }
            ins = ins->next;
        }
    }
}

void fix_type_refs_has_drop(ParserContext *ctx)
{
    if (!ctx)
    {
        return;
    }

    for (StructRef *ref = ctx->parsed_funcs_list; ref; ref = ref->next)
    {
        ASTNode *fn = ref->node;
        if (!fn || fn->kind != NODE_FUNCTION)
        {
            continue;
        }

        for (int i = 0; i < fn->func.count; i++)
        {
            Type *at = fn->func.arg_types[i];
            if (at && at->kind == TYPE_STRUCT && at->name && !at->traits.has_drop)
            {
                ASTNode *def = find_struct_def(ctx, at->name);
                if (def && def->type_info && def->type_info->traits.has_drop)
                {
                    at->traits.has_drop = 1;
                }
            }
        }

        if (fn->func.ret_type_info && fn->func.ret_type_info->kind == TYPE_STRUCT &&
            fn->func.ret_type_info->name && !fn->func.ret_type_info->traits.has_drop)
        {
            ASTNode *def = find_struct_def(ctx, fn->func.ret_type_info->name);
            if (def && def->type_info && def->type_info->traits.has_drop)
            {
                fn->func.ret_type_info->traits.has_drop = 1;
            }
        }
    }

    for (StructRef *ref = ctx->parsed_structs_list; ref; ref = ref->next)
    {
        ASTNode *strct = ref->node;
        if (!strct || strct->kind != NODE_STRUCT)
        {
            continue;
        }
        for (ASTNode *field = strct->strct.fields; field; field = field->next)
        {
            Type *ft = field->type_info;
            if (ft && ft->kind == TYPE_STRUCT && ft->name && !ft->traits.has_drop)
            {
                ASTNode *def = find_struct_def(ctx, ft->name);
                if (def && def->type_info && def->type_info->traits.has_drop)
                {
                    ft->traits.has_drop = 1;
                }
            }
        }
    }

    for (ASTNode *ins = ctx->instantiated_structs; ins; ins = ins->next)
    {
        if (ins->kind != NODE_STRUCT)
        {
            continue;
        }
        for (ASTNode *field = ins->strct.fields; field; field = field->next)
        {
            Type *ft = field->type_info;
            if (ft && ft->kind == TYPE_STRUCT && ft->name && !ft->traits.has_drop)
            {
                ASTNode *def = find_struct_def(ctx, ft->name);
                if (def && def->type_info && def->type_info->traits.has_drop)
                {
                    ft->traits.has_drop = 1;
                }
            }
        }
    }
}
