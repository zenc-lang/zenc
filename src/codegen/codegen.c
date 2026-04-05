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

// Helper to suggest standard library imports for common missing functions
static const char *get_missing_function_hint(ParserContext *ctx, const char *name)
{
    if (strcmp(name, "malloc") == 0 || strcmp(name, "free") == 0 || strcmp(name, "calloc") == 0 ||
        strcmp(name, "realloc") == 0)
    {
        return "Include <stdlib.h> or use 'use std::mem'";
    }
    if (strcmp(name, "printf") == 0 || strcmp(name, "scanf") == 0 || strcmp(name, "fprintf") == 0 ||
        strcmp(name, "sprintf") == 0 || strcmp(name, "snprintf") == 0)
    {
        return "Include <stdio.h> or use 'use std::io'";
    }
    if (strcmp(name, "memset") == 0 || strcmp(name, "memcpy") == 0 || strcmp(name, "strlen") == 0 ||
        strcmp(name, "strcpy") == 0 || strcmp(name, "strcmp") == 0 || strcmp(name, "strncmp") == 0)
    {
        return "Include <string.h>";
    }

    int best_dist = 4;
    static char best_buf[256];
    const char *best = NULL;

    FuncSig *sig = ctx->func_registry;
    while (sig)
    {
        int dist = levenshtein(name, sig->name);
        if (dist < best_dist)
        {
            best_dist = dist;
            best = sig->name;
        }
        sig = sig->next;
    }

    StructRef *ref = ctx->parsed_funcs_list;
    while (ref)
    {
        if (ref->node && ref->node->type == NODE_FUNCTION)
        {
            int dist = levenshtein(name, ref->node->func.name);
            if (dist < best_dist)
            {
                best_dist = dist;
                best = ref->node->func.name;
            }
        }
        ref = ref->next;
    }

    if (best)
    {
        snprintf(best_buf, sizeof(best_buf), "Did you mean '%s'?", best);
        return best_buf;
    }

    return NULL;
}

// Emit literal expression (int, float, string, char)
static void codegen_literal_expr(ASTNode *node, FILE *out)
{
    if (node->literal.type_kind == LITERAL_STRING || node->literal.type_kind == LITERAL_RAW_STRING)
    {
        fprintf(out, "\"");
        for (int i = 0; node->literal.string_val[i]; i++)
        {
            if (node->literal.type_kind == LITERAL_RAW_STRING &&
                node->literal.string_val[i] == '\\')
            {
                fprintf(out, "\\\\");
            }
            else
            {
                fprintf(out, "%c", node->literal.string_val[i]);
            }
        }
        fprintf(out, "\"");
    }
    else if (node->literal.type_kind == LITERAL_CHAR)
    {
        if (node->literal.int_val > 127)
        {
            fprintf(out, "%u", (unsigned int)node->literal.int_val);
        }
        else
        {
            fprintf(out, "%s", node->literal.string_val);
        }
    }
    else if (node->literal.type_kind == LITERAL_FLOAT)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.17g", node->literal.float_val);
        if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'E'))
        {
            strcat(buf, ".0");
        }
        fprintf(out, "%s", buf);
    }
    else // LITERAL_INT
    {
        if (node->literal.int_val > 9223372036854775807ULL)
        {
            fprintf(out, "%lluULL", (unsigned long long)node->literal.int_val);
        }
        else
        {
            fprintf(out, "%lld", (long long)node->literal.int_val);
        }
    }
}

// Emit variable reference expression
static void codegen_var_expr(ParserContext *ctx, ASTNode *node, FILE *out)
{
    if (g_current_lambda)
    {
        for (int i = 0; i < g_current_lambda->lambda.num_captures; i++)
        {
            if (strcmp(node->var_ref.name, g_current_lambda->lambda.captured_vars[i]) == 0)
            {
                if (g_current_lambda->lambda.capture_modes &&
                    g_current_lambda->lambda.capture_modes[i] == 1)
                {
                    fprintf(out, "(*ctx->%s)", node->var_ref.name);
                }
                else
                {
                    fprintf(out, "ctx->%s", node->var_ref.name);
                }
                return;
            }
        }
    }

    if (node->resolved_type && strcmp(node->resolved_type, "unknown") == 0)
    {
        if (node->var_ref.suggestion && !ctx->silent_warnings &&
            !find_func(ctx, node->var_ref.name))
        {
            char msg[256];
            char help[256];
            snprintf(msg, sizeof(msg), "Undefined variable '%s'", node->var_ref.name);
            snprintf(help, sizeof(help), "Did you mean '%s'?", node->var_ref.suggestion);
            zwarn_at(node->token, "%s\n   = help: %s", msg, help);
        }
    }

    // Check for static method call pattern: Type::method or Slice<T>::method
    char *double_colon = strstr(node->var_ref.name, "::");
    if (double_colon)
    {
        // Extract type name and method name
        int type_len = double_colon - node->var_ref.name;
        char *type_name = xmalloc(type_len + 1);
        strncpy(type_name, node->var_ref.name, type_len);
        type_name[type_len] = 0;

        char *method_name = double_colon + 2; // Skip ::

        // Handle generic types: Slice<int> -> Slice_int
        char *mangled_type;
        mangled_type = xstrdup(type_name);

        // Output as Type__method
        if (ctx)
        {
            TypeAlias *ta = find_type_alias_node(ctx, mangled_type);
            const char *alias = (ta && !ta->is_opaque) ? ta->original_type : NULL;
            if (alias)
            {
                emit_mangled_name(out, alias, method_name);
                free(type_name);
                free(mangled_type);
                return;
            }
        }
        emit_mangled_name(out, mangled_type, method_name);
        free(type_name);
        free(mangled_type);
        return;
    }

    if (strcmp(node->var_ref.name, "self") == 0)
    {
        if (node->type_info && node->type_info->kind == TYPE_STRUCT)
        {
            fprintf(out, "(*self)");
            return;
        }
    }

    // Check for legacy Enum_Variant patterns (single underscore)
    // Avoid double-mangling if it already has double underscores (generics)
    char *underscore = strchr(node->var_ref.name, '_');
    if (underscore && underscore != node->var_ref.name && *(underscore + 1) != '_' &&
        strstr(node->var_ref.name, "__") == NULL)
    {
        char base[256];
        size_t len = underscore - node->var_ref.name;
        if (len < sizeof(base))
        {
            strncpy(base, node->var_ref.name, len);
            base[len] = 0;

            ASTNode *def = find_struct_def(ctx, base);
            int is_common_enum =
                (strncmp(base, "Result", 6) == 0 || strncmp(base, "Option", 6) == 0 ||
                 strncmp(base, "JsonType", 8) == 0);
            if (is_common_enum || (def && def->type == NODE_ENUM))
            {
                emit_mangled_name(out, base, underscore + 1);
                return;
            }
        }
    }

    fprintf(out, "%s", node->var_ref.name);
}

// Emit lambda expression
static void codegen_lambda_expr(ParserContext *ctx, ASTNode *node, FILE *out)
{
    if (node->lambda.is_bare)
    {
        fprintf(out, "((void*)_lambda_%d)", node->lambda.lambda_id);
        return;
    }

    if (node->lambda.num_captures > 0)
    {
        int lid = node->lambda.lambda_id;
        if (g_config.use_cpp)
        {
            fprintf(
                out,
                "({ struct Lambda_%d_Ctx *_z_ctx_%d = (struct Lambda_%d_Ctx*)malloc(sizeof(struct "
                "Lambda_%d_Ctx));\n",
                lid, lid, lid, lid);
        }
        else
        {
            fprintf(out,
                    "({ struct Lambda_%d_Ctx *_z_ctx_%d = malloc(sizeof(struct "
                    "Lambda_%d_Ctx));\n",
                    lid, lid, lid);
        }
        for (int i = 0; i < node->lambda.num_captures; i++)
        {
            if (node->lambda.capture_modes && node->lambda.capture_modes[i] == 1)
            {
                int found = 0;
                if (g_current_lambda)
                {
                    for (int k = 0; k < g_current_lambda->lambda.num_captures; k++)
                    {
                        if (strcmp(node->lambda.captured_vars[i],
                                   g_current_lambda->lambda.captured_vars[k]) == 0)
                        {
                            if (g_current_lambda->lambda.capture_modes &&
                                g_current_lambda->lambda.capture_modes[k] == 1)
                            {
                                fprintf(out, "_z_ctx_%d->%s = ctx->%s;\n", lid,
                                        node->lambda.captured_vars[i],
                                        node->lambda.captured_vars[i]);
                            }
                            else
                            {
                                fprintf(out, "_z_ctx_%d->%s = &ctx->%s;\n", lid,
                                        node->lambda.captured_vars[i],
                                        node->lambda.captured_vars[i]);
                            }
                            found = 1;
                            break;
                        }
                    }
                }
                if (!found)
                {
                    fprintf(out, "_z_ctx_%d->%s = &%s;\n", lid, node->lambda.captured_vars[i],
                            node->lambda.captured_vars[i]);
                }
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

                fprintf(out, "*(%s*)(&_z_ctx_%d->%s) = ", tstr, lid, node->lambda.captured_vars[i]);
                free(tstr);

                ASTNode *var_node = ast_create(NODE_EXPR_VAR);
                var_node->var_ref.name = xstrdup(node->lambda.captured_vars[i]);
                var_node->token = node->token;

                if (node->lambda.captured_types && node->lambda.captured_types[i])
                {
                    var_node->resolved_type = xstrdup(node->lambda.captured_types[i]);
                }
                else
                {
                    // Should rely on analysis, but fallback just in case.
                    var_node->resolved_type = xstrdup("int");
                }

                codegen_expression_with_move(ctx, var_node, out);

                ast_free(var_node);

                fprintf(out, ";\n");

                char *tname = node->lambda.captured_types[i];
                const char *clean = tname;
                if (strncmp(clean, "struct ", 7) == 0)
                {
                    clean += 7;
                }

                ASTNode *fdef = find_struct_def(ctx, clean);
                if (fdef && fdef->type_info && fdef->type_info->traits.has_drop)
                {
                    fprintf(out, "_z_ctx_%d->__z_drop_flag_%s = 1;\n", lid,
                            node->lambda.captured_vars[i]);
                }
            }
        }
        if (g_config.use_cpp)
        {
            fprintf(out,
                    "z_closure_T _cl = {(void*)_lambda_%d, _z_ctx_%d, _lambda_%d_drop}; _cl; })",
                    lid, lid, lid);
        }
        else
        {
            fprintf(
                out,
                "(z_closure_T){.func = _lambda_%d, .ctx = _z_ctx_%d, .drop = _lambda_%d_drop}; })",
                lid, lid, lid);
        }
    }
    else
    {
        if (g_config.use_cpp)
        {
            fprintf(out, "(z_closure_T){ (void*)_lambda_%d, NULL, NULL }", node->lambda.lambda_id);
        }
        else
        {
            fprintf(out, "((z_closure_T){.func = (void*)_lambda_%d, .ctx = NULL, .drop = NULL})",
                    node->lambda.lambda_id);
        }
    }
}

void codegen_expression(ParserContext *ctx, ASTNode *node, FILE *out)
{
    if (!node)
    {
        return;
    }

    switch (node->type)
    {
    case NODE_MATCH:
        codegen_match_internal(ctx, node, out, 1);
        break;
    case NODE_EXPR_BINARY:
        if (strncmp(node->binary.op, "??", 2) == 0 && strlen(node->binary.op) == 2)
        {
            fprintf(out, "({ ");
            emit_auto_type(ctx, node->binary.left, node->token, out);
            fprintf(out, " _l = (");
            codegen_expression(ctx, node->binary.left, out);
            fprintf(out, "); _l ? _l : (");
            codegen_expression(ctx, node->binary.right, out);
            fprintf(out, "); })");
        }
        else if (strcmp(node->binary.op, "?\?=") == 0)
        {
            fprintf(out, "({ if (!(");
            codegen_expression(ctx, node->binary.left, out);
            fprintf(out, ")) ");
            codegen_expression(ctx, node->binary.left, out);
            fprintf(out, " = (");
            codegen_expression(ctx, node->binary.right, out);
            fprintf(out, "); ");
            codegen_expression(ctx, node->binary.left, out);
            fprintf(out, "; })");
        }
        else if ((strcmp(node->binary.op, "==") == 0 || strcmp(node->binary.op, "!=") == 0))
        {
            char *t1 = infer_type(ctx, node->binary.left);
            int is_ptr = 0;
            char *fully_resolved = t1;
            char *mangle_base = t1;
            int found_opaque = 0;

            if (t1)
            {
                char *curr = t1;
                int depth = 0;
                while (depth++ < 20)
                {
                    if (strchr(curr, '*'))
                    {
                        is_ptr = 1;
                        break;
                    }

                    int resolved = 0;
                    TypeAlias *ta = find_type_alias_node(ctx, curr);
                    if (ta)
                    {
                        if (ta->is_opaque)
                        {
                            if (!found_opaque)
                            {
                                mangle_base = ta->alias;
                                found_opaque = 1;
                            }
                        }
                        else if (!found_opaque)
                        {
                            mangle_base = ta->original_type;
                        }

                        curr = ta->original_type;
                        resolved = 1;
                    }

                    if (!resolved)
                    {
                        break;
                    }
                }
                fully_resolved = curr;
            }

            int is_basic = IS_BASIC_TYPE(fully_resolved);
            ASTNode *def = t1 ? find_struct_def(ctx, t1) : NULL;

            if (t1 && (def || found_opaque) && !is_basic && !is_ptr)
            {
                char *base = mangle_base;
                if (strncmp(base, "struct ", 7) == 0)
                {
                    base += 7;
                }

                if (strcmp(node->binary.op, "!=") == 0)
                {
                    fprintf(out, "(!");
                }
                char meth[256];
                snprintf(meth, sizeof(meth), "%s__Eq__eq", base);
                ZenSymbol *sym = find_symbol_in_all(ctx, meth);
                FuncSig *sig = sym ? sym->data.sig : NULL;
                if (!sig)
                {
                    snprintf(meth, sizeof(meth), "%s__eq", base);
                    sym = find_symbol_in_all(ctx, meth);
                    sig = sym ? sym->data.sig : NULL;
                }

                // If specialized Eq fails, try constructed name directly
                const char *call_name = sig ? sig->name : NULL;
                if (!call_name)
                {
                    // If no signature found, default to Type__Eq__eq and assume it's there
                    // (The typechecker should ensure this where appropriate, or it's a legacy __eq)
                    snprintf(meth, sizeof(meth), "%s__Eq__eq", base);
                    call_name = meth;
                }

                fprintf(out, "%s(", call_name);

                if (node->binary.left->type == NODE_EXPR_VAR ||
                    node->binary.left->type == NODE_EXPR_INDEX ||
                    node->binary.left->type == NODE_EXPR_MEMBER)
                {
                    fprintf(out, "&");
                    codegen_expression(ctx, node->binary.left, out);
                }
                else if (g_config.use_cpp)
                {
                    fprintf(out, "({ __typeof__((");
                    codegen_expression(ctx, node->binary.left, out);
                    fprintf(out, ")) _tmp = ");
                    codegen_expression(ctx, node->binary.left, out);
                    fprintf(out, "; &_tmp; })");
                }
                else
                {
                    fprintf(out, "(__typeof__((");
                    codegen_expression(ctx, node->binary.left, out);
                    fprintf(out, "))[]){");
                    codegen_expression(ctx, node->binary.left, out);
                    fprintf(out, "}");
                }

                fprintf(out, ", ");

                int needs_ptr = 1; // Default for Eq on structs
                if (sig)
                {
                    needs_ptr = (sig->total_args > 1 && sig->arg_types[1]->kind == TYPE_POINTER);
                }

                if (needs_ptr && (node->binary.right->type == NODE_EXPR_VAR ||
                                  node->binary.right->type == NODE_EXPR_INDEX ||
                                  node->binary.right->type == NODE_EXPR_MEMBER))
                {
                    fprintf(out, "&");
                    codegen_expression(ctx, node->binary.right, out);
                }
                else if (needs_ptr && g_config.use_cpp)
                {
                    fprintf(out, "({ __typeof__((");
                    codegen_expression(ctx, node->binary.right, out);
                    fprintf(out, ")) _tmp = ");
                    codegen_expression(ctx, node->binary.right, out);
                    fprintf(out, "; &_tmp; })");
                }
                else if (needs_ptr)
                {
                    fprintf(out, "(__typeof__((");
                    codegen_expression(ctx, node->binary.right, out);
                    fprintf(out, "))[]){");
                    codegen_expression(ctx, node->binary.right, out);
                    fprintf(out, "}");
                }
                else
                {
                    codegen_expression(ctx, node->binary.right, out);
                }

                fprintf(out, ")");
                if (strcmp(node->binary.op, "!=") == 0)
                {
                    fprintf(out, ")");
                }
            }
            else if (t1 && (strcmp(t1, "string") == 0))
            {
                char *t2 = infer_type(ctx, node->binary.right);
                if (t2 && (strcmp(t2, "string") == 0))
                {
                    fprintf(out, "(strcmp(");
                    codegen_expression(ctx, node->binary.left, out);
                    fprintf(out, ", ");
                    codegen_expression(ctx, node->binary.right, out);
                    if (strcmp(node->binary.op, "==") == 0)
                    {
                        fprintf(out, ") == 0)");
                    }
                    else
                    {
                        fprintf(out, ") != 0)");
                    }
                }
                else
                {
                    fprintf(out, "(");
                    codegen_expression(ctx, node->binary.left, out);
                    fprintf(out, " %s ", node->binary.op);
                    codegen_expression(ctx, node->binary.right, out);
                    fprintf(out, ")");
                }
            }
            else
            {
                fprintf(out, "(");
                codegen_expression(ctx, node->binary.left, out);
                fprintf(out, " %s ", node->binary.op);
                codegen_expression(ctx, node->binary.right, out);
                fprintf(out, ")");
            }
            if (t1)
            {
                free(t1);
            }
        }
        else if (strcmp(node->binary.op, "**") == 0)
        {
            fprintf(out, "(_zc_pow((double)(");
            codegen_expression(ctx, node->binary.left, out);
            fprintf(out, "), (double)(");
            codegen_expression(ctx, node->binary.right, out);
            fprintf(out, ")))");
        }
        else if (strcmp(node->binary.op, "**=") == 0)
        {
            fprintf(out, "({ ");
            codegen_expression(ctx, node->binary.left, out);
            fprintf(out, " = _zc_pow((double)(");
            codegen_expression(ctx, node->binary.left, out);
            fprintf(out, "), (double)(");
            codegen_expression(ctx, node->binary.right, out);
            fprintf(out, ")); ");
            codegen_expression(ctx, node->binary.left, out);
            fprintf(out, "; })");
        }
        else
        {
            int is_assignment =
                (node->binary.op[strlen(node->binary.op) - 1] == '=' &&
                 strcmp(node->binary.op, "==") != 0 && strcmp(node->binary.op, "!=") != 0 &&
                 strcmp(node->binary.op, "<=") != 0 && strcmp(node->binary.op, ">=") != 0);

            int is_drop_assignment = 0;
            char *clean_type = NULL;
            if (is_assignment && strcmp(node->binary.op, "=") == 0 && g_config.use_cpp)
            {
                char *type_name = infer_type(ctx, node->binary.left);
                if (type_name)
                {
                    clean_type = xstrdup(type_name);
                    char *ptr = strchr(clean_type, '*');
                    if (ptr)
                    {
                        *ptr = '\0';
                    }
                    char *base = clean_type;
                    if (strncmp(base, "struct ", 7) == 0)
                    {
                        base += 7;
                    }
                    ASTNode *def = find_struct_def(ctx, base);
                    if (def && def->type_info && def->type_info->traits.has_drop)
                    {
                        is_drop_assignment = 1;
                        memmove(clean_type, base, strlen(base) + 1);
                    }
                    free(type_name);
                }
            }

            if (is_drop_assignment)
            {
                fprintf(out, "({ ");
                fprintf(out, "__typeof__((");
                codegen_expression(ctx, node->binary.left, out);
                fprintf(out, "))* _z_dest = &(");
                codegen_expression(ctx, node->binary.left, out);
                fprintf(out, "); ");

                if (node->binary.left->type == NODE_EXPR_VAR)
                {
                    fprintf(out, "if (__z_drop_flag_%s) %s__Drop__glue(_z_dest); ",
                            node->binary.left->var_ref.name, clean_type);
                }
                else
                {
                    fprintf(out, "%s__Drop__glue(_z_dest); ", clean_type);
                }

                fprintf(out, "*_z_dest = (");
                codegen_expression_with_move(ctx, node->binary.right, out);
                fprintf(out, "); ");

                if (node->binary.left->type == NODE_EXPR_VAR)
                {
                    fprintf(out, "__z_drop_flag_%s = 1; ", node->binary.left->var_ref.name);
                }

                fprintf(out, "*_z_dest; })");
            }
            else
            {
                fprintf(out, "(");
                if (is_assignment)
                {
                    codegen_expression(ctx, node->binary.left, out);
                }
                else
                {
                    codegen_expression_with_move(ctx, node->binary.left, out);
                }

                fprintf(out, " %s ", node->binary.op);
                codegen_expression_with_move(ctx, node->binary.right, out);
                fprintf(out, ")");
            }

            if (clean_type)
            {
                free(clean_type);
            }
        }
        break;
    case NODE_EXPR_VAR:
        codegen_var_expr(ctx, node, out);
        break;
    case NODE_LAMBDA:
        codegen_lambda_expr(ctx, node, out);
        break;
    case NODE_EXPR_LITERAL:
        codegen_literal_expr(node, out);
        break;
    case NODE_EXPR_CALL:
    {
        // Always give the ability to step into a function call
        emit_source_mapping(node, out);

        if (node->call.callee->type == NODE_EXPR_MEMBER)
        {
            // If the member is a function pointer, don't mangle it as a method call
            Type *callee_ti = get_inner_type(node->call.callee->type_info);
            if (callee_ti && callee_ti->kind == TYPE_FUNCTION)
            {
                goto skip_method_mangling;
            }

            ASTNode *target = node->call.callee->member.target;
            char *method = node->call.callee->member.field;

            if (strcmp(method, "len") == 0)
            {
                if (target->type_info && target->type_info->kind == TYPE_ARRAY)
                {
                    if (target->type_info->array_size > 0)
                    {
                        fprintf(out, "%d", target->type_info->array_size);
                    }
                    else
                    {
                        codegen_expression(ctx, target, out);
                        fprintf(out, ".len");
                    }
                    return;
                }
            }

            // Check for Static Enum Variant Call: Enum.Variant(...) or Enum<T>.Variant(...)
            if (target->type == NODE_EXPR_VAR)
            {
                char type_name[256];
                strncpy(type_name, target->var_ref.name, sizeof(type_name));
                type_name[sizeof(type_name) - 1] = 0;

                char *mangled_type = type_name;

                ASTNode *def = find_struct_def(ctx, mangled_type);
                if (def && def->type == NODE_ENUM)
                {
                    char mangled[512];
                    snprintf(mangled, sizeof(mangled), "%s__%s", mangled_type, method);
                    FuncSig *sig = find_func(ctx, mangled);
                    if (sig)
                    {
                        fprintf(out, "%s(", mangled);
                        ASTNode *arg = node->call.args;
                        int arg_idx = 0;
                        while (arg)
                        {
                            if (arg_idx > 0)
                            {
                                fprintf(out, ", ");
                            }

                            Type *param_t =
                                (arg_idx < sig->total_args) ? sig->arg_types[arg_idx] : NULL;

                            if (param_t && param_t->kind == TYPE_STRUCT &&
                                strncmp(param_t->name, "Tuple__", 7) == 0 && sig->total_args == 1 &&
                                node->call.arg_count > 1)
                            {
                                fprintf(out, "(%s){", param_t->name);
                                int first = 1;
                                while (arg)
                                {
                                    if (!first)
                                    {
                                        fprintf(out, ", ");
                                    }
                                    first = 0;
                                    codegen_expression(ctx, arg, out);
                                    arg = arg->next;
                                }
                                fprintf(out, "}");
                                break;
                            }

                            codegen_expression(ctx, arg, out);
                            arg = arg->next;
                            arg_idx++;
                        }
                        fprintf(out, ")");
                        return;
                    }
                }
            }

            char *type = infer_type(ctx, target);
            if (type)
            {
                char *clean = xstrdup(type);
                char *ptr = strchr(clean, '*');
                if (ptr)
                {
                    *ptr = '\0';
                }

                char *base = clean;
                if (strncmp(base, "struct ", 7) == 0)
                {
                    base += 7;
                }

                if (ctx)
                {
                    TypeAlias *ta = find_type_alias_node(ctx, base);
                    const char *alias = (ta && !ta->is_opaque) ? ta->original_type : NULL;
                    if (alias)
                    {
                        base = (char *)alias;
                    }
                }

                const char *normalized = normalize_type_name(base);
                char *mangled_base = (char *)normalized;
                char base_buf[1024];

                // Mangle generic types: Slice<int> -> Slice__int
                char *lt = strchr(base, '<');
                if (lt)
                {
                    char *gt = strrchr(base, '>');
                    if (gt)
                    {
                        int prefix_len = lt - base;
                        char prefix[256];
                        if (prefix_len >= 255)
                        {
                            prefix_len = 255;
                        }
                        strncpy(prefix, base, prefix_len);
                        prefix[prefix_len] = 0;

                        // Strip trailing underscores from prefix
                        char *p_end = prefix + strlen(prefix);
                        while (p_end > prefix && *(p_end - 1) == '_')
                        {
                            *(--p_end) = '\0';
                        }

                        char *args_ptr = xstrdup(lt + 1);
                        char *args_end = strrchr(args_ptr, '>');
                        if (args_end)
                        {
                            *args_end = 0;
                        }

                        char *clean_arg = sanitize_mangled_name(args_ptr);
                        snprintf(base_buf, sizeof(base_buf), "%s__%s", prefix, clean_arg);
                        mangled_base = base_buf;

                        free(args_ptr);
                        free(clean_arg);
                    }
                }

                if (!strchr(type, '*') &&
                    (target->type == NODE_EXPR_CALL || target->type == NODE_EXPR_LITERAL ||
                     target->type == NODE_EXPR_BINARY || target->type == NODE_EXPR_UNARY ||
                     target->type == NODE_EXPR_CAST))
                {
                    char *type_mangled = (char *)normalize_type_name(type);
                    if (type_mangled != type)
                    {
                        mangled_base = type_mangled;
                    }

                    char type_buf[1024];
                    char *t_lt = strchr(type, '<');
                    if (t_lt)
                    {
                        char *t_gt = strrchr(type, '>');
                        if (t_gt)
                        {
                            int p_len = t_lt - type;
                            char prefix[256];
                            if (p_len >= 255)
                            {
                                p_len = 255;
                            }
                            strncpy(prefix, type, p_len);
                            prefix[p_len] = 0;

                            // Strip trailing underscores from prefix
                            char *p_end = prefix + strlen(prefix);
                            while (p_end > prefix && *(p_end - 1) == '_')
                            {
                                *(--p_end) = '\0';
                            }

                            char *args_ptr = xstrdup(t_lt + 1);
                            char *args_end = strrchr(args_ptr, '>');
                            if (args_end)
                            {
                                *args_end = 0;
                            }

                            char *clean_arg = sanitize_mangled_name(args_ptr);
                            snprintf(type_buf, sizeof(type_buf), "%s__%s", prefix, clean_arg);
                            type_mangled = type_buf;

                            free(args_ptr);
                            free(clean_arg);
                        }
                    }

                    emit_mangled_name(out, mangled_base, method);
                    fprintf(out, "((%s[]){", type_mangled);
                    codegen_expression(ctx, target, out);
                    fprintf(out, "}");
                    ASTNode *arg = node->call.args;
                    while (arg)
                    {
                        fprintf(out, ", ");
                        codegen_expression_with_move(ctx, arg, out);
                        arg = arg->next;
                    }
                    fprintf(out, ")");
                }
                else
                {
                    // Mixin Lookup Logic
                    char *call_base = mangled_base;

                    int need_cast = 0;
                    char mixin_func_base[1024];
                    snprintf(mixin_func_base, sizeof(mixin_func_base), "%s__%s", call_base, method);
                    char *mixin_func_name_ptr = merge_underscores(mixin_func_base);
                    char mixin_func_name[1024];
                    strncpy(mixin_func_name, mixin_func_name_ptr, sizeof(mixin_func_name) - 1);
                    mixin_func_name[sizeof(mixin_func_name) - 1] = 0;
                    free(mixin_func_name_ptr);

                    char *resolved_method_suffix = NULL;

                    if (!find_func(ctx, mixin_func_name))
                    {
                        // Reverse alias lookup: if base is a resolved alias (e.g. Slice_char),
                        // try finding the method under the alias name (e.g. StringView__println)
                        TypeAlias *ta = ctx->type_aliases;
                        while (ta)
                        {
                            if (strcmp(ta->original_type, call_base) == 0)
                            {
                                char alias_func_base[1024];
                                snprintf(alias_func_base, sizeof(alias_func_base), "%s__%s",
                                         ta->alias, method);
                                char *alias_func_name = merge_underscores(alias_func_base);
                                if (find_func(ctx, alias_func_name))
                                {
                                    free(alias_func_name);
                                    break;
                                }
                                free(alias_func_name);
                            }
                            ta = ta->next;
                        }
                        StructRef *ref = ctx->parsed_impls_list;
                        while (ref)
                        {
                            if (ref->node && ref->node->type == NODE_IMPL_TRAIT &&
                                strcmp(ref->node->impl_trait.target_type, base) == 0)
                            {
                                char trait_base[512];
                                snprintf(trait_base, sizeof(trait_base), "%s__%s__%s", base,
                                         ref->node->impl_trait.trait_name, method);
                                char *trait_mangled = merge_underscores(trait_base);
                                if (find_func(ctx, trait_mangled))
                                {
                                    char suffix_base[512];
                                    snprintf(suffix_base, sizeof(suffix_base), "%s__%s",
                                             ref->node->impl_trait.trait_name, method);
                                    resolved_method_suffix = merge_underscores(suffix_base);
                                    free(trait_mangled);
                                    break;
                                }
                                free(trait_mangled);
                            }
                            ref = ref->next;
                        }

                        if (!resolved_method_suffix)
                        {
                            GenericImplTemplate *it = ctx->impl_templates;
                            while (it)
                            {
                                char *tname = NULL;
                                if (it->impl_node && it->impl_node->type == NODE_IMPL_TRAIT)
                                {
                                    tname = it->impl_node->impl_trait.trait_name;
                                    char trait_base[1024];
                                    snprintf(trait_base, sizeof(trait_base), "%s__%s__%s", base,
                                             tname, method);
                                    char *trait_mangled = merge_underscores(trait_base);
                                    if (find_func(ctx, trait_mangled))
                                    {
                                        char suffix_base[1024];
                                        snprintf(suffix_base, sizeof(suffix_base), "%s__%s", tname,
                                                 method);
                                        resolved_method_suffix = merge_underscores(suffix_base);
                                        free(trait_mangled);
                                        break;
                                    }
                                    free(trait_mangled);
                                }
                                it = it->next;
                            }
                        }

                        if (resolved_method_suffix)
                        {
                            method = resolved_method_suffix;
                        }
                        else
                        {
                            ASTNode *def = find_struct_def(ctx, base);
                            if (def && def->type == NODE_STRUCT && def->strct.used_structs)
                            {
                                for (int k = 0; k < def->strct.used_struct_count; k++)
                                {
                                    char mixin_base[1024];
                                    snprintf(mixin_base, sizeof(mixin_base), "%s__%s",
                                             def->strct.used_structs[k], method);
                                    char *mixin_check = merge_underscores(mixin_base);
                                    if (find_func(ctx, mixin_check))
                                    {
                                        call_base = def->strct.used_structs[k];
                                        need_cast = 1;
                                        free(mixin_check);
                                        break;
                                    }
                                    free(mixin_check);
                                }
                            }
                        }
                    }

                    emit_mangled_name(out, call_base, method);
                    fprintf(out, "(");
                    if (need_cast)
                    {
                        fprintf(out, "(%s*)%s", call_base, strchr(type, '*') ? "" : "&");
                    }
                    else if (!strchr(type, '*'))
                    {
                        fprintf(out, "&");
                    }
                    codegen_expression(ctx, target, out);
                    ASTNode *arg = node->call.args;
                    while (arg)
                    {
                        fprintf(out, ", ");
                        codegen_expression_with_move(ctx, arg, out);
                        arg = arg->next;
                    }
                    fprintf(out, ")");

                    if (resolved_method_suffix)
                    {
                        free(resolved_method_suffix);
                    }
                }
                free(clean);
                free(type);
                return;
            }
            if (type)
            {
                free(type);
            }
        }

    skip_method_mangling:;

        if (node->call.callee->type == NODE_EXPR_VAR)
        {
            ASTNode *def = find_struct_def(ctx, node->call.callee->var_ref.name);
            if (def && def->type == NODE_STRUCT)
            {
                fprintf(out, "(struct %s){0}", node->call.callee->var_ref.name);
                return;
            }
        }

        Type *callee_ti = get_inner_type(node->call.callee->type_info);
        if (callee_ti && callee_ti->kind == TYPE_FUNCTION && !callee_ti->is_raw)
        {
            fprintf(out, "({ z_closure_T _c = ");
            codegen_expression(ctx, node->call.callee, out);
            fprintf(out, "; ");

            Type *ft = callee_ti;
            char *ret = type_to_c_string(ft->inner);
            if (strcmp(ret, "string") == 0)
            {
                free(ret);
                ret = xstrdup("char*");
            }
            if (strcmp(ret, "unknown") == 0)
            {
                free(ret);
                ret = xstrdup("void*");
            }

            fprintf(out, "((%s (*)(void*", ret);
            for (int i = 0; i < ft->arg_count; i++)
            {
                char *as = type_to_c_string(ft->args[i]);
                if (strcmp(as, "unknown") == 0)
                {
                    free(as);
                    as = xstrdup("void*");
                }
                fprintf(out, ", %s", as);
                free(as);
            }
            if (ft->is_varargs)
            {
                fprintf(out, ", ...");
            }
            fprintf(out, "))_c.func)(_c.ctx");

            ASTNode *arg = node->call.args;
            while (arg)
            {
                fprintf(out, ", ");
                codegen_expression_with_move(ctx, arg, out);
                arg = arg->next;
            }
            fprintf(out, "); })");
            free(ret);
            break;
        }

        if (node->call.callee->type == NODE_EXPR_VAR &&
            strcmp(node->call.callee->var_ref.name, "panic") == 0)
        {
            fprintf(out, "__zenc_panic");
            goto skip_callee_gen;
        }
        else if (node->call.callee->type == NODE_EXPR_VAR)
        {
            char *name = node->call.callee->var_ref.name;
            char *underscore = strchr(name, '_');
            if (underscore && underscore != name && *(underscore + 1) != '_' &&
                strstr(name, "__") == NULL)
            {
                // Potential Enum_Variant patterns (single underscore)
                char base[256];
                size_t len = underscore - name;
                if (len < sizeof(base))
                {
                    strncpy(base, name, len);
                    base[len] = 0;
                    ASTNode *def = find_struct_def(ctx, base);
                    // Special case for common enum variants
                    int is_common_enum =
                        (strncmp(base, "Result", 6) == 0 || strncmp(base, "Option", 6) == 0 ||
                         strncmp(base, "JsonType", 8) == 0);
                    if (is_common_enum || (def && def->type == NODE_ENUM))
                    {
                        emit_mangled_name(out, base, underscore + 1);
                        goto skip_callee_gen;
                    }
                }
            }
        }

        codegen_expression(ctx, node->call.callee, out);
    skip_callee_gen:
        fprintf(out, "(");

        if (node->call.arg_names && node->call.callee->type == NODE_EXPR_VAR)
        {
            ASTNode *arg = node->call.args;
            int first = 1;
            while (arg)
            {
                if (!first)
                {
                    fprintf(out, ", ");
                }
                first = 0;
                codegen_expression_with_move(ctx, arg, out);
                arg = arg->next;
            }
        }
        else
        {
            FuncSig *sig = NULL;
            if (node->call.callee->type == NODE_EXPR_VAR)
            {
                sig = find_func(ctx, node->call.callee->var_ref.name);
                // Warn about undefined functions (only if no C header imports)
                if (!sig && !find_struct_def(ctx, node->call.callee->var_ref.name))
                {
                    const char *name = node->call.callee->var_ref.name;

                    int has_c_interop = ctx->has_external_includes;

                    // Check modules for C header imports
                    Module *mod = ctx->modules;
                    while (mod && !has_c_interop)
                    {
                        if (mod->is_c_header)
                        {
                            has_c_interop = 1;
                        }
                        mod = mod->next;
                    }

                    // Check imported_files for stdlib imports (absolute paths)
                    ImportedFile *file = ctx->imported_files;
                    while (file && !has_c_interop)
                    {
                        if (file->path && strstr(file->path, "/std/"))
                        {
                            has_c_interop = 1;
                        }
                        file = file->next;
                    }

                    // Skip internal runtime functions
                    int is_internal = strncmp(name, "_z_", 3) == 0 || strncmp(name, "_Z", 2) == 0;

                    // Check if explicitly declared as extern (via `extern` or header scanning)
                    int is_extern = is_extern_symbol(ctx, name);

                    // Check whitelist
                    int is_whitelisted = 0;
                    if (g_config.c_function_whitelist)
                    {
                        char **w = g_config.c_function_whitelist;
                        while (*w)
                        {
                            if (strcmp(*w, name) == 0)
                            {
                                is_whitelisted = 1;
                                break;
                            }
                            w++;
                        }
                    }

                    if (!has_c_interop && !is_internal && !is_extern && !is_whitelisted &&
                        !(node->call.callee->type_info &&
                          get_inner_type(node->call.callee->type_info)->kind == TYPE_FUNCTION))
                    {
                        Token t = node->call.callee->token;
                        char msg[256];
                        snprintf(msg, sizeof(msg), "Undefined function '%s'", name);
                        const char *hint = get_missing_function_hint(ctx, name);

                        if (hint)
                        {
                            zwarn_with_suggestion(t, msg, hint);
                        }
                        else
                        {
                            zwarn_at(t, "%s", msg);
                        }
                    }
                }
            }

            ASTNode *arg = node->call.args;
            int arg_idx = 0;
            while (arg)
            {
                int handled = 0;
                if (sig && arg_idx < sig->total_args)
                {
                    Type *param_t = sig->arg_types[arg_idx];
                    Type *arg_t = arg->type_info;

                    if (param_t && param_t->kind == TYPE_ARRAY && param_t->array_size == 0 &&
                        arg_t && arg_t->kind == TYPE_ARRAY && arg_t->array_size > 0)
                    {
                        char *inner = type_to_c_string(param_t->inner);
                        fprintf(out, "(Slice__%s){.data = ", inner);
                        codegen_expression(ctx, arg, out);
                        fprintf(out, ", .len = %d, .cap = %d}", arg_t->array_size,
                                arg_t->array_size);
                        free(inner);
                        handled = 1;
                    }
                    else if (param_t && param_t->kind == TYPE_STRUCT &&
                             strncmp(param_t->name, "Tuple__", 7) == 0 && sig->total_args == 1 &&
                             node->call.arg_count > 1)
                    {
                        fprintf(out, "(%s){", param_t->name);
                        ASTNode *curr = arg;
                        int first_field = 1;
                        while (curr)
                        {
                            if (!first_field)
                            {
                                fprintf(out, ", ");
                            }
                            first_field = 0;
                            codegen_expression_with_move(ctx, curr, out);
                            curr = curr->next;
                        }
                        fprintf(out, "}");
                        handled = 1;
                        arg = NULL;
                    }
                }

                if (handled)
                {
                    if (arg == NULL)
                    {
                        break;
                    }
                }
                else
                {
                    codegen_expression_with_move(ctx, arg, out);
                }

                if (arg && arg->next)
                {
                    fprintf(out, ", ");
                }
                if (arg)
                {
                    arg = arg->next;
                }
                arg_idx++;
            }
        }
        fprintf(out, ")");
        break;
    }
    case NODE_EXPR_MEMBER:
        if (strcmp(node->member.field, "len") == 0)
        {
            if (node->member.target->type_info &&
                node->member.target->type_info->kind == TYPE_ARRAY &&
                node->member.target->type_info->array_size > 0)
            {
                fprintf(out, "%d", node->member.target->type_info->array_size);
                break;
            }
        }

        if (node->member.target->type_info && node->member.target->type_info->kind == TYPE_VECTOR)
        {
            codegen_expression(ctx, node->member.target, out);
            return;
        }

        if (node->member.is_pointer_access == 2)
        {
            fprintf(out, "({ ");
            emit_auto_type(ctx, node->member.target, node->token, out);
            fprintf(out, " _t = (");
            codegen_expression(ctx, node->member.target, out);
            char *field = node->member.field;
            if (field && field[0] >= '0' && field[0] <= '9')
            {
                fprintf(out, "); _t ? _t->v%s : 0; })", field);
            }
            else
            {
                fprintf(out, "); _t ? _t->%s : 0; })", field);
            }
        }
        else
        {
            // Static enum member access: Enum.Variant
            if (node->member.target->type == NODE_EXPR_VAR)
            {
                ASTNode *def = find_struct_def(ctx, node->member.target->var_ref.name);
                if (def && def->type == NODE_ENUM)
                {
                    fprintf(out, "%s__%s", node->member.target->var_ref.name, node->member.field);
                    break;
                }
            }

            codegen_expression(ctx, node->member.target, out);
            char *lt = infer_type(ctx, node->member.target);
            int actually_ptr = 0;
            if (lt && (lt[strlen(lt) - 1] == '*' || strstr(lt, "*")))
            {
                actually_ptr = 1;
            }
            if (lt)
            {
                free(lt);
            }

            char *field = node->member.field;
            if (field && field[0] >= '0' && field[0] <= '9')
            {
                fprintf(out, "%sv%s", actually_ptr ? "->" : ".", field);
            }
            else
            {
                fprintf(out, "%s%s", actually_ptr ? "->" : ".", field);
            }
        }
        break;
    case NODE_EXPR_INDEX:
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
                free(inferred);
            }
        }

        if (is_slice_struct)
        {
            if (node->index.array->type == NODE_EXPR_VAR)
            {
                codegen_expression(ctx, node->index.array, out);
                fprintf(out, ".data[_z_check_bounds(");
                codegen_expression(ctx, node->index.index, out);
                fprintf(out, ", ");
                codegen_expression(ctx, node->index.array, out);
                fprintf(out, ".len)]");
            }
            else
            {
                codegen_expression(ctx, node->index.array, out);
                fprintf(out, ".data[");
                codegen_expression(ctx, node->index.index, out);
                fprintf(out, "]");
            }
        }
        else
        {
            char *base_type = infer_type(ctx, node->index.array);
            char *struct_name = NULL;
            char method_name[512] = {0};

            if (base_type && !strchr(base_type, '*'))
            {
                char clean[256];
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

                fprintf(out, "%s(", method_name);
                if (needs_addr)
                {
                    fprintf(out, "&");
                }
                codegen_expression(ctx, node->index.array, out);
                fprintf(out, ", ");
                codegen_expression(ctx, node->index.index, out);
                // Emit extra indices for multi-index (v[i, j, k])
                ASTNode *extra = node->index.extra_indices;
                while (extra)
                {
                    fprintf(out, ", ");
                    codegen_expression(ctx, extra, out);
                    extra = extra->next;
                }
                fprintf(out, ")");
                free(struct_name);
            }
            else
            {
                int fixed_size = -1;
                if (node->index.array->type_info &&
                    (node->index.array->type_info->kind == TYPE_ARRAY ||
                     node->index.array->type_info->kind == TYPE_VECTOR))
                {
                    fixed_size = node->index.array->type_info->array_size;
                }

                codegen_expression(ctx, node->index.array, out);
                fprintf(out, "[");
                if (fixed_size > 0)
                {
                    fprintf(out, "_z_check_bounds(");
                }
                codegen_expression(ctx, node->index.index, out);
                if (fixed_size > 0)
                {
                    fprintf(out, ", %d)", fixed_size);
                }
                fprintf(out, "]");
            }
        }
    }
    break;
    case NODE_EXPR_SLICE:
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

        fprintf(out, "({ ");
        emit_auto_type(ctx, node->slice.array, node->token, out);
        fprintf(out, " _arr = ");
        codegen_expression(ctx, node->slice.array, out);
        fprintf(out, "; int _start = ");
        if (node->slice.start)
        {
            codegen_expression(ctx, node->slice.start, out);
        }
        else
        {
            fprintf(out, "0");
        }
        fprintf(out, "; int _len = ");

        if (node->slice.end)
        {
            codegen_expression(ctx, node->slice.end, out);
            fprintf(out, " - _start; ");
        }
        else
        {
            if (known_size > 0)
            {
                fprintf(out, "%d - _start; ", known_size);
            }
            else if (is_slice_struct)
            {
                fprintf(out, "_arr.len - _start; ");
            }
            else
            {
                fprintf(out, "0; ");
            }
        }

        if (is_slice_struct)
        {
            fprintf(out,
                    "(Slice__%s){ .data = _arr.data + _start, .len = _len, .cap = "
                    "_len }; })",
                    tname);
        }
        else
        {
            fprintf(out, "(Slice__%s){ .data = _arr + _start, .len = _len, .cap = _len }; })",
                    tname);
        }
        if (tname && strcmp(tname, "unknown") != 0)
        {
            free(tname);
        }
        break;
    }
    case NODE_BLOCK:
    {
        int saved = defer_count;
        fprintf(out, "({ ");
        codegen_walker(ctx, node->block.statements, out);
        for (int i = defer_count - 1; i >= saved; i--)
        {
            emit_source_mapping_duplicate(defer_stack[i], out);
            codegen_node_single(ctx, defer_stack[i], out);
        }
        defer_count = saved;
        fprintf(out, " })");
        break;
    }
    case NODE_IF:
    {
        // If-expression: emit as GCC statement expression
        // Use typeof on first branch's last expression to declare result type
        fprintf(out, "({ ");

        // Find the result expression from then_body for typeof
        ASTNode *then_result = NULL;
        if (node->if_stmt.then_body && node->if_stmt.then_body->type == NODE_BLOCK)
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

        // Declare result variable using typeof
        if (then_result)
        {
            fprintf(out, "__typeof__(");
            codegen_expression(ctx, then_result, out);
            fprintf(out, ") _ifval; ");
        }
        else
        {
            fprintf(out, "int _ifval; "); // fallback
        }

        fprintf(out, "if (");
        codegen_expression(ctx, node->if_stmt.condition, out);
        fprintf(out, ") { ");
        if (node->if_stmt.then_body && node->if_stmt.then_body->type == NODE_BLOCK)
        {
            ASTNode *stmt = node->if_stmt.then_body->block.statements;
            while (stmt && stmt->next)
            {
                codegen_node_single(ctx, stmt, out);
                stmt = stmt->next;
            }
            if (stmt)
            {
                fprintf(out, "_ifval = ");
                codegen_expression(ctx, stmt, out);
                fprintf(out, "; ");
            }
        }
        else if (node->if_stmt.then_body)
        {
            fprintf(out, "_ifval = ");
            codegen_expression(ctx, node->if_stmt.then_body, out);
            fprintf(out, "; ");
        }
        fprintf(out, "} else { ");
        if (node->if_stmt.else_body && node->if_stmt.else_body->type == NODE_BLOCK)
        {
            ASTNode *stmt = node->if_stmt.else_body->block.statements;
            while (stmt && stmt->next)
            {
                codegen_node_single(ctx, stmt, out);
                stmt = stmt->next;
            }
            if (stmt)
            {
                fprintf(out, "_ifval = ");
                codegen_expression(ctx, stmt, out);
                fprintf(out, "; ");
            }
        }
        else if (node->if_stmt.else_body)
        {
            fprintf(out, "_ifval = ");
            codegen_expression(ctx, node->if_stmt.else_body, out);
            fprintf(out, "; ");
        }
        fprintf(out, "} _ifval; })");
        break;
    }
    case NODE_TRY:
    {
        char *type_name = "Result";
        if (g_current_func_ret_type)
        {
            type_name = g_current_func_ret_type;
        }
        else if (node->try_stmt.expr->type_info && node->try_stmt.expr->type_info->name)
        {
            type_name = node->try_stmt.expr->type_info->name;
        }

        if (strcmp(type_name, "__auto_type") == 0 || strcmp(type_name, "unknown") == 0)
        {
            type_name = "Result";
        }

        char *search_name = type_name;
        if (strncmp(search_name, "struct ", 7) == 0)
        {
            search_name += 7;
        }

        int is_enum = 0;
        StructRef *er = ctx->parsed_enums_list;
        while (er)
        {
            if (er->node && er->node->type == NODE_ENUM &&
                strcmp(er->node->enm.name, search_name) == 0)
            {
                is_enum = 1;
                break;
            }
            er = er->next;
        }
        if (!is_enum)
        {
            ASTNode *ins = ctx->instantiated_structs;
            while (ins)
            {
                if (ins->type == NODE_ENUM && strcmp(ins->enm.name, search_name) == 0)
                {
                    is_enum = 1;
                    break;
                }
                ins = ins->next;
            }
        }

        fprintf(out, "({ ");
        emit_auto_type(ctx, node->try_stmt.expr, node->token, out);
        fprintf(out, " _try = ");
        codegen_expression(ctx, node->try_stmt.expr, out);

        if (is_enum)
        {
            fprintf(out,
                    "; if (_try.tag == %s__Err_Tag) return (%s__Err(_try.data.Err)); "
                    "_try.data.Ok; })",
                    search_name, search_name);
        }
        else
        {
            fprintf(out,
                    "; if (!_try.is_ok) return %s__Err(_try.err); "
                    "_try.val; })",
                    search_name);
        }
        break;
    }
    case NODE_RAW_STMT:
        fprintf(out, "%s", node->raw_stmt.content);
        break;
    case NODE_PLUGIN:
    {
        ZPlugin *found = zptr_find_plugin(node->plugin_stmt.plugin_name);
        if (found)
        {
            ZApi api = {.filename = g_current_filename ? g_current_filename : "input.zc",
                        .current_line = node->line,
                        .out = out,
                        .hoist_out = ctx->hoist_out};
            found->fn(node->plugin_stmt.body, &api);
        }
        else
        {
            fprintf(out, "/* Unknown plugin: %s */\n", node->plugin_stmt.plugin_name);
        }
        break;
    }
    case NODE_EXPR_UNARY:
        if (node->unary.op && strcmp(node->unary.op, "&_rval") == 0)
        {
            if (g_config.use_cpp)
            {
                fprintf(out, "({ __typeof__((");
                codegen_expression(ctx, node->unary.operand, out);
                fprintf(out, ")) _tmp = ");
                codegen_expression(ctx, node->unary.operand, out);
                fprintf(out, "; &_tmp; })");
            }
            else
            {
                fprintf(out, "(__typeof__((");
                codegen_expression(ctx, node->unary.operand, out);
                fprintf(out, "))[]){");
                codegen_expression(ctx, node->unary.operand, out);
                fprintf(out, "}");
            }
        }
        else if (node->unary.op && strcmp(node->unary.op, "?") == 0)
        {
            fprintf(out, "({ ");
            emit_auto_type(ctx, node->unary.operand, node->token, out);
            fprintf(out, " _t = (");
            codegen_expression(ctx, node->unary.operand, out);
            fprintf(out, "); if (_t.tag != 0) return _t; _t.data.Ok; })");
        }
        else if (node->unary.op && strcmp(node->unary.op, "_post++") == 0)
        {
            fprintf(out, "(");
            codegen_expression(ctx, node->unary.operand, out);
            fprintf(out, "++)");
        }
        else if (node->unary.op && strcmp(node->unary.op, "_post--") == 0)
        {
            fprintf(out, "(");
            codegen_expression(ctx, node->unary.operand, out);
            fprintf(out, "--)");
        }
        else
        {
            fprintf(out, "(%s", node->unary.op);
            codegen_expression(ctx, node->unary.operand, out);
            fprintf(out, ")");
        }
        break;
    case NODE_VA_START:
        fprintf(out, "va_start(");
        codegen_expression(ctx, node->va_start.ap, out);
        fprintf(out, ", ");
        codegen_expression(ctx, node->va_start.last_arg, out);
        fprintf(out, ")");
        break;
    case NODE_VA_END:
        fprintf(out, "va_end(");
        codegen_expression(ctx, node->va_end.ap, out);
        fprintf(out, ")");
        break;
    case NODE_VA_COPY:
        fprintf(out, "va_copy(");
        codegen_expression(ctx, node->va_copy.dest, out);
        fprintf(out, ", ");
        codegen_expression(ctx, node->va_copy.src, out);
        fprintf(out, ")");
        break;
    case NODE_AST_COMMENT:
        fprintf(out, "%s\n", node->comment.content);
        break;
    case NODE_VA_ARG:
    {
        char *type_str = type_to_c_string(node->va_arg.type_info);
        fprintf(out, "va_arg(");
        codegen_expression(ctx, node->va_arg.ap, out);
        fprintf(out, ", %s)", type_str);
        free(type_str);
        break;
    }
    case NODE_EXPR_CAST:
    {
        const char *t = node->cast.target_type;
        const char *mapped = t;
        if (strcmp(t, "c_int") == 0)
        {
            mapped = "int";
        }
        else if (strcmp(t, "c_uint") == 0)
        {
            mapped = "unsigned int";
        }
        else if (strcmp(t, "c_long") == 0)
        {
            mapped = "long";
        }
        else if (strcmp(t, "c_ulong") == 0)
        {
            mapped = "unsigned long";
        }
        else if (strcmp(t, "c_long_long") == 0)
        {
            mapped = "long long";
        }
        else if (strcmp(t, "c_ulong_long") == 0)
        {
            mapped = "unsigned long long";
        }
        else if (strcmp(t, "c_short") == 0)
        {
            mapped = "short";
        }
        else if (strcmp(t, "c_ushort") == 0)
        {
            mapped = "unsigned short";
        }
        else if (strcmp(t, "c_char") == 0)
        {
            mapped = "char";
        }
        else if (strcmp(t, "c_uchar") == 0)
        {
            mapped = "unsigned char";
        }
        const char *norm = normalize_type_name(t);
        if (norm != t)
        {
            mapped = norm;
        }
        else if (strcmp(t, "uint") == 0)
        {
            mapped = "unsigned int";
        }

        fprintf(out, "((%s)(", mapped);
        codegen_expression(ctx, node->cast.expr, out);
        fprintf(out, "))");
        break;
    }
    case NODE_EXPR_SIZEOF:
        if (node->size_of.target_type_info)
        {
            char *mapped = type_to_c_string(node->size_of.target_type_info);
            fprintf(out, "sizeof(%s)", mapped);
            free(mapped);
        }
        else if (node->size_of.target_type)
        {
            const char *t = node->size_of.target_type;
            const char *mapped = t;
            if (strcmp(t, "c_int") == 0)
            {
                mapped = "int";
            }
            else if (strcmp(t, "c_uint") == 0)
            {
                mapped = "unsigned int";
            }
            else if (strcmp(t, "c_long") == 0)
            {
                mapped = "long";
            }
            else if (strcmp(t, "c_ulong") == 0)
            {
                mapped = "unsigned long";
            }
            else if (strcmp(t, "c_long_long") == 0)
            {
                mapped = "long long";
            }
            else if (strcmp(t, "c_ulong_long") == 0)
            {
                mapped = "unsigned long long";
            }
            else if (strcmp(t, "c_short") == 0)
            {
                mapped = "short";
            }
            else if (strcmp(t, "c_ushort") == 0)
            {
                mapped = "unsigned short";
            }
            else if (strcmp(t, "c_char") == 0)
            {
                mapped = "char";
            }
            else if (strcmp(t, "c_uchar") == 0)
            {
                mapped = "unsigned char";
            }
            else
            {
                const char *norm = normalize_type_name(t);
                if (norm != t)
                {
                    mapped = norm;
                }
                else if (strcmp(t, "uint") == 0)
                {
                    mapped = "unsigned int";
                }
            }

            fprintf(out, "sizeof(%s)", mapped);
        }
        else
        {
            fprintf(out, "sizeof(");
            codegen_expression(ctx, node->size_of.expr, out);
            fprintf(out, ")");
        }
        break;
    case NODE_TYPEOF:
        if (node->size_of.target_type_info)
        {
            char *mapped = type_to_c_string(node->size_of.target_type_info);
            fprintf(out, "typeof(%s)", mapped);
            free(mapped);
        }
        else if (node->size_of.target_type)
        {
            fprintf(out, "typeof(%s)", node->size_of.target_type);
        }
        else
        {
            fprintf(out, "typeof(");
            codegen_expression(ctx, node->size_of.expr, out);
            fprintf(out, ")");
        }
        break;

    case NODE_REFLECTION:
    {
        Type *t = node->reflection.target_type;
        if (node->reflection.kind == 0)
        {
            char *s = type_to_c_string(t);
            fprintf(out, "\"%s\"", s);
            free(s);
        }
        else
        {
            if (t->kind != TYPE_STRUCT || !t->name)
            {
                fprintf(out, "((void*)0)");
                break;
            }
            char *sname = t->name;
            ASTNode *def = find_struct_def(ctx, sname);
            if (!def)
            {
                fprintf(out, "((void*)0)");
                break;
            }

            fprintf(out,
                    "({ static struct { char *name; char *type; unsigned long offset; } "
                    "_fields_%s[] = {",
                    sname);
            ASTNode *f = def->strct.fields;
            while (f)
            {
                if (f->type == NODE_FIELD)
                {
                    fprintf(out, "{ \"%s\", \"%s\", __builtin_offsetof(struct %s, %s) }, ",
                            f->field.name, f->field.type, sname, f->field.name);
                }
                f = f->next;
            }
            fprintf(out, "{ 0 } }; (void*)_fields_%s; })", sname);
        }
        break;
    }
    case NODE_EXPR_STRUCT_INIT:
    {
        const char *struct_name = node->struct_init.struct_name;
        if (strcmp(struct_name, "Self") == 0 && g_current_impl_type)
        {
            struct_name = g_current_impl_type;
        }

        int is_zen_struct = 0;
        int is_union = 0;
        StructRef *sr = ctx->parsed_structs_list;
        int is_vector = 0;
        while (sr)
        {
            if (sr->node && sr->node->type == NODE_STRUCT &&
                strcmp(sr->node->strct.name, struct_name) == 0)
            {
                is_zen_struct = 1;
                if (sr->node->strct.is_union)
                {
                    is_union = 1;
                }
                if (sr->node->type_info && sr->node->type_info->kind == TYPE_VECTOR)
                {
                    is_vector = 1;
                }
                break;
            }
            sr = sr->next;
        }

        // Determine if we are inside a function/lambda to allow statement expressions
        int in_func = (g_current_func_ret_type != NULL || g_current_lambda != NULL);

        // Find vector size if applicable
        int vec_size = 0;
        if (is_vector)
        {
            StructRef *v_chk = ctx->parsed_structs_list;
            while (v_chk)
            {
                if (v_chk->node && v_chk->node->type == NODE_STRUCT &&
                    strcmp(v_chk->node->strct.name, struct_name) == 0)
                {
                    if (v_chk->node->type_info)
                    {
                        vec_size = v_chk->node->type_info->array_size;
                    }
                    break;
                }
                v_chk = v_chk->next;
            }
        }

        if (g_config.use_cpp)
        {
            if (in_func && !is_vector)
            {
                fprintf(out, "({ %s _s = {}; ", struct_name);
                ASTNode *f = node->struct_init.fields;
                while (f)
                {
                    int skip = 0;
                    if (f->var_decl.init_expr && f->var_decl.init_expr->type == NODE_EXPR_LITERAL &&
                        f->var_decl.init_expr->literal.type_kind == LITERAL_INT &&
                        f->var_decl.init_expr->literal.int_val == 0)
                    {
                        skip = 1;
                    }
                    if (!skip)
                    {
                        fprintf(out, "_s.%s = ", f->var_decl.name);
                        codegen_expression_with_move(ctx, f->var_decl.init_expr, out);
                        fprintf(out, "; ");
                    }
                    f = f->next;
                }
                fprintf(out, "_s; })");
            }
            else
            {
                fprintf(out, "%s { ", struct_name);
                ASTNode *f = node->struct_init.fields;
                int field_count = 0;
                ASTNode *tmp = f;
                while (tmp)
                {
                    field_count++;
                    tmp = tmp->next;
                }

                if (is_vector && field_count == 1 && vec_size > 1)
                {
                    for (int i = 0; i < vec_size; i++)
                    {
                        if (i > 0)
                        {
                            fprintf(out, ", ");
                        }
                        codegen_expression(ctx, f->var_decl.init_expr, out);
                    }
                }
                else
                {
                    int first = 1;
                    while (f)
                    {
                        if (!first)
                        {
                            fprintf(out, ", ");
                        }
                        if (is_vector)
                        {
                            codegen_expression(ctx, f->var_decl.init_expr, out);
                        }
                        else
                        {
                            fprintf(out, ".%s = ", f->var_decl.name);
                            codegen_expression_with_move(ctx, f->var_decl.init_expr, out);
                        }
                        first = 0;
                        f = f->next;
                    }
                }
                fprintf(out, " }");
            }
        }
        else
        {
            if (is_vector)
            {
                fprintf(out, "(%s){", struct_name);
            }
            else if (is_union)
            {
                fprintf(out, "(union %s){", struct_name);
            }
            else if (is_zen_struct)
            {
                fprintf(out, "(struct %s){", struct_name);
            }
            else
            {
                fprintf(out, "(%s){", struct_name);
            }

            ASTNode *f = node->struct_init.fields;
            int field_count = 0;
            ASTNode *tmp = f;
            while (tmp)
            {
                field_count++;
                tmp = tmp->next;
            }

            if (is_vector && field_count == 1 && vec_size > 1)
            {
                for (int i = 0; i < vec_size; i++)
                {
                    if (i > 0)
                    {
                        fprintf(out, ", ");
                    }
                    codegen_expression(ctx, f->var_decl.init_expr, out);
                }
            }
            else
            {
                int first = 1;
                while (f)
                {
                    // Skip fields with literal 0 init — compound literals
                    // zero-initialize unset fields per C99. Also works around
                    // a TCC bug where .val = 0 for struct-typed fields breaks
                    // subsequent designated initializers.
                    int skip = 0;
                    if (f->var_decl.init_expr && f->var_decl.init_expr->type == NODE_EXPR_LITERAL &&
                        f->var_decl.init_expr->literal.type_kind == LITERAL_INT &&
                        f->var_decl.init_expr->literal.int_val == 0)
                    {
                        skip = 1;
                    }
                    if (!skip)
                    {
                        if (!first)
                        {
                            fprintf(out, ", ");
                        }
                        if (!is_vector)
                        {
                            fprintf(out, ".%s = ", f->var_decl.name);
                        }
                        codegen_expression_with_move(ctx, f->var_decl.init_expr, out);
                        first = 0;
                    }
                    f = f->next;
                }
            }
            fprintf(out, "}");
        }
        break;
    }
    case NODE_EXPR_ARRAY_LITERAL:
    {
        fprintf(out, "{");
        ASTNode *elem = node->array_literal.elements;
        int first_arr = 1;
        while (elem)
        {
            if (!first_arr)
            {
                fprintf(out, ", ");
            }
            codegen_expression(ctx, elem, out);
            elem = elem->next;
            first_arr = 0;
        }
        fprintf(out, "}");
        break;
    }
    case NODE_EXPR_TUPLE_LITERAL:
    {
        char *type = node->resolved_type ? node->resolved_type
                     : node->type_info   ? type_to_string(node->type_info)
                                         : "unknown";
        fprintf(out, "(%s){", type);
        ASTNode *tup_elem = node->tuple_literal.elements;
        int first_tup = 1;
        while (tup_elem)
        {
            if (!first_tup)
            {
                fprintf(out, ", ");
            }
            codegen_expression(ctx, tup_elem, out);
            tup_elem = tup_elem->next;
            first_tup = 0;
        }
        fprintf(out, "}");
        break;
    }
    case NODE_TERNARY:
        fprintf(out, "((");
        codegen_expression(ctx, node->ternary.cond, out);
        fprintf(out, ") ? (");
        codegen_expression(ctx, node->ternary.true_expr, out);
        fprintf(out, ") : (");
        codegen_expression(ctx, node->ternary.false_expr, out);
        fprintf(out, "))");
        break;
    case NODE_AWAIT:
    {
        char *ret_type = "void*";
        int free_ret = 0;
        if (node->type_info)
        {
            char *t = type_to_c_string(node->type_info);
            if (t)
            {
                ret_type = t;
                free_ret = 1;
            }
        }
        else if (node->resolved_type)
        {
            ret_type = node->resolved_type;
        }

        if (strcmp(ret_type, "Async") == 0 || strcmp(ret_type, "void*") == 0)
        {
            char *inf = infer_type(ctx, node);
            if (inf && strcmp(inf, "Async") != 0 && strcmp(inf, "void*") != 0)
            {
                if (free_ret)
                {
                    free(ret_type);
                }
                ret_type = inf;
                free_ret = 0;
            }
        }

        int needs_long_cast = 0;
        int returns_struct = 0;
        if (strstr(ret_type, "*") == NULL && strcmp(ret_type, "string") != 0 &&
            strcmp(ret_type, "void") != 0 && strcmp(ret_type, "Async") != 0)
        {
            if (is_struct_return_type(ret_type))
            {
                returns_struct = 1;
            }
            else
            {
                needs_long_cast = 1;
            }
            if (strncmp(ret_type, "struct", 6) == 0)
            {
                returns_struct = 1;
            }
        }

        fprintf(out, "({ Async _a = ");
        codegen_expression(ctx, node->unary.operand, out);
        fprintf(out, "; void* _r; pthread_join(_a.thread, &_r); ");
        if (strcmp(ret_type, "void") == 0)
        {
            fprintf(out, "})");
        }
        else
        {
            if (returns_struct)
            {
                fprintf(out, "%s _val = *(%s*)_r; free(_r); _val; })", ret_type, ret_type);
            }
            else
            {
                if (needs_long_cast)
                {
                    fprintf(out, "(%s)(long)_r; })", ret_type);
                }
                else
                {
                    fprintf(out, "(%s)_r; })", ret_type);
                }
            }
        }
        if (free_ret)
        {
            free(ret_type);
        }
        break;
    }
    default:
        break;
    }
}

void codegen_expression_bare(ParserContext *ctx, ASTNode *node, FILE *out)
{
    if (!node)
    {
        return;
    }

    if (node->type == NODE_EXPR_BINARY)
    {
        const char *op = node->binary.op;
        int is_simple = (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 ||
                         strcmp(op, ">=") == 0 || strcmp(op, "+") == 0 || strcmp(op, "-") == 0 ||
                         strcmp(op, "*") == 0 || strcmp(op, "/") == 0 || strcmp(op, "%") == 0 ||
                         strcmp(op, "+=") == 0 || strcmp(op, "-=") == 0 || strcmp(op, "*=") == 0 ||
                         strcmp(op, "/=") == 0 || strcmp(op, "=") == 0);

        if (is_simple)
        {
            codegen_expression(ctx, node->binary.left, out);
            fprintf(out, " %s ", op);
            codegen_expression(ctx, node->binary.right, out);
            return;
        }
    }

    if (node->type == NODE_EXPR_UNARY && node->unary.op)
    {
        if (strcmp(node->unary.op, "_post++") == 0)
        {
            codegen_expression(ctx, node->unary.operand, out);
            fprintf(out, "++");
            return;
        }
        if (strcmp(node->unary.op, "_post--") == 0)
        {
            codegen_expression(ctx, node->unary.operand, out);
            fprintf(out, "--");
            return;
        }
    }

    codegen_expression(ctx, node, out);
}
