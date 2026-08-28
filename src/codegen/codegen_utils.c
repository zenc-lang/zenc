// SPDX-License-Identifier: MIT

#include "../ast/ast.h"
#include "../constants.h"
#include "../parser/parser.h"
#include "../zprep.h"
#include "codegen.h"
#include "../ast/primitives.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void emit_pending_closure_frees(ParserContext *ctx)
{
    for (int i = 0; i < ctx->cg.pending_closure_free_count; i++)
    {
        EMIT(ctx, "free(_z_closure_ctx_stash[%d]); _z_closure_ctx_stash[%d] = NULL;\n",
             ctx->cg.pending_closure_frees[i], ctx->cg.pending_closure_frees[i]);
    }
    ctx->cg.pending_closure_free_count = 0;
}

// Strip template suffix from a type name (for example, "MyStruct<T>" -> "MyStruct")
// Returns newly allocated string, caller must free.

// Helper to emit a mangled name (Type__Method) with standardized underscores.
// Uses the canonical mangle_method_symbol so call sites always emit the same
// name the parser used for the definition (e.g. `_method` keeps its triple
// underscore).
void emit_mangled_name(ParserContext *ctx, const char *base, const char *method)
{
    if (!base || !method)
    {
        return;
    }
    char *merged = mangle_method_symbol(base, NULL, method);

    ZenSymbol *sym = ctx ? find_symbol_in_all(ctx, merged) : NULL;
    if (sym && sym->link_name)
    {
        EMIT(ctx, "%s", sym->link_name);
    }
    else
    {
        EMIT(ctx, "%s", merged);
    }
    zfree(merged);
}

int is_enum_type_name(ParserContext *ctx, const char *name)
{
    if (!name || !ctx)
    {
        return 0;
    }
    const char *clean = name;
    if (strncmp(clean, "struct ", 7) == 0)
    {
        clean += 7;
    }
    ASTNode *def = find_struct_def(ctx, clean);
    return (def && def->kind == NODE_ENUM);
}

// Helper to emit C declaration (handle arrays, function pointers correctly)
static void emit_c_decl(ParserContext *ctx, const char *type_str, const char *name)
{
    char *bracket = (char *)strchr(type_str, '[');
    char *generic = (char *)strchr(type_str, '<');
    char *fn_ptr = (char *)strstr(type_str, "(*");

    if (fn_ptr)
    {
        char *end_paren = (char *)strchr(fn_ptr, ')');
        if (end_paren)
        {
            ptrdiff_t prefix_len = end_paren - type_str;
            EMIT(ctx, "%.*s%s%s", (int)prefix_len, type_str, name, end_paren);
        }
        else
        {
            // Fallback if malformed (shouldn't happen)
            ptrdiff_t prefix_len = fn_ptr - type_str + 2;
            EMIT(ctx, "%.*s%s%s", (int)prefix_len, type_str, name, fn_ptr + 2);
        }
    }
    else if (generic && (!bracket || generic < bracket))
    {
        char mangled_candidate[MAX_MANGLED_NAME_LEN];
        char *gt = (char *)strchr(generic, '>');
        int success = 0;

        if (gt)
        {
            ptrdiff_t base_len = generic - type_str;
            ptrdiff_t arg_len = gt - generic - 1;

            // Limit check
            if (base_len + arg_len + 2 < 256)
            {
                snprintf(mangled_candidate, 256, "%.*s__%.*s", (int)base_len, type_str,
                         (int)arg_len, generic + 1);

                if (find_struct_def(ctx, mangled_candidate))
                {
                    EMIT(ctx, "%s %s", mangled_candidate, name);
                    success = 1;
                }
            }
        }

        if (!success)
        {
            ptrdiff_t base_len = generic - type_str;
            EMIT(ctx, "%.*s %s", (int)base_len, type_str, name);
        }
        else if (gt[1] == '*')
        {
            EMIT(ctx, "*");
        }

        if (bracket)
        {
            EMIT(ctx, "%s", bracket);
        }
    }
    else if (bracket)
    {
        ptrdiff_t base_len = bracket - type_str;
        EMIT(ctx, "%.*s %s%s", (int)base_len, type_str, name, bracket);
    }
    else
    {
        EMIT(ctx, "%s %s", type_str, name);
    }
}

// Helper to emit variable declarations with array types.
void emit_var_decl_type(ParserContext *ctx, const char *type_str, const char *var_name)
{
    emit_c_decl(ctx, type_str, var_name);
}

// Get field type from struct.
void emit_auto_type(ParserContext *ctx, ASTNode *init_expr, Token t)
{
    (void)t;
    char *inferred = NULL;
    if (init_expr)
    {
        inferred = infer_type(ctx, init_expr);
    }

    if (inferred && strcmp(inferred, "__auto_type") != 0 && strcmp(inferred, "unknown") != 0)
    {
        EMIT(ctx, "%s", inferred);
    }
    else
    {
        if (z_path_match_compiler(ctx->config->cc, "tcc") && init_expr)
        {
            EMIT(ctx, "__typeof__((");
            codegen_expression(ctx, init_expr);
            EMIT(ctx, "))");
        }
        else
        {
            EMIT(ctx, "ZC_AUTO");
        }
    }
}

// Emit function signature using Type info for correct C codegen
void emit_func_signature(ParserContext *ctx, ASTNode *func, const char *name_override)
{
    if (!func || func->kind != NODE_FUNCTION)
    {
        return;
    }

    // Emit MISRA static linkage
    if (ctx->config->misra_mode && !func->func.is_export && strcmp(func->func.name, "main") != 0)
    {
        EMIT(ctx, "static ");
    }

    // Emit CUDA qualifiers (for both forward declarations and definitions)
    if (ctx->config->use_cuda)
    {
        if (func->func.cuda_global)
        {
            EMIT(ctx, "__global__ ");
        }
        if (func->func.cuda_device)
        {
            EMIT(ctx, "__device__ ");
        }
        if (func->func.cuda_host)
        {
            EMIT(ctx, "__host__ ");
        }
    }

    char *ret_suffix = NULL;

    const char *final_name =
        (func->link_name) ? func->link_name : (name_override ? name_override : func->func.name);

    if (strcmp(final_name, "z_plugin_init") != 0)
    {
        EMIT(ctx, "ZC_FUNC ");
    }

    // Return type
    if (func->func.is_async)
    {
        // Async functions use _impl_ for the actual implementation;
        // this signature is for prototype declarations.
        if (func->func.ret_type)
        {
            EMIT(ctx, "%s ", func->func.ret_type);
        }
        else
        {
            EMIT(ctx, "void ");
        }
    }
    else
    {
        char *ret_str;
        if (func->func.ret_type_info)
        {
            ret_str = type_to_c_string(func->func.ret_type_info);
        }
        else if (func->func.ret_type)
        {
            ret_str = xstrdup(func->func.ret_type);
        }
        else
        {
            ret_str = xstrdup("void");
        }

        char *fn_ptr = (char *)strstr(ret_str, "(*)");

        if (fn_ptr)
        {
            ptrdiff_t prefix_len = fn_ptr - ret_str + 2; // Include "(*"
            EMIT(ctx, "%.*s%s(", (int)prefix_len, ret_str, final_name);
            ret_suffix = xstrdup(fn_ptr + 2);
        }
        else
        {
            EMIT(ctx, "%s %s(", ret_str, final_name);
        }
        zfree(ret_str);
    }

    if (func->func.is_async)
    {
        EMIT(ctx, "%s(", final_name);
    }

    // Args
    if (func->func.count == 0 && !func->func.is_varargs)
    {
        EMIT(ctx, "void");
    }
    else
    {
        for (int i = 0; i < func->func.count; i++)
        {
            if (i > 0)
            {
                EMIT(ctx, ", ");
            }

            char *type_str = NULL;
            // Check for @ctype override first
            if (func->func.c_type_overrides && func->func.c_type_overrides[i])
            {
                type_str = xstrdup(func->func.c_type_overrides[i]);
            }
            else if (func->func.arg_types && func->func.arg_types[i])
            {
                type_str = type_to_c_string(func->func.arg_types[i]);
            }
            else
            {
                type_str = xstrdup("void*"); // Fallback
            }

            const char *name = "";

            if (func->func.param_names && func->func.param_names[i])
            {
                name = func->func.param_names[i];
            }

            // check if array type
            emit_c_decl(ctx, type_str, name);
            zfree(type_str);
        }
        if (func->func.is_varargs)
        {
            if (func->func.count > 0)
            {
                EMIT(ctx, ", ");
            }
            EMIT(ctx, "...");
        }
    }
    EMIT(ctx, ")");

    if (ret_suffix)
    {
        EMIT(ctx, "%s", ret_suffix);
        zfree(ret_suffix);
    }
}

int emit_move_invalidation(ParserContext *ctx, ASTNode *node)
{
    if (!node)
    {
        return 0;
    }

    // Check if it's a valid l-value we can memset
    if (node->kind != NODE_EXPR_VAR && node->kind != NODE_EXPR_MEMBER)
    {
        return 0;
    }

    // Common logic to find type and check Drop
    char *type_name = infer_type(ctx, node);
    ASTNode *def = NULL;
    if (type_name)
    {
        char *clean_type = type_name;
        if (strncmp(clean_type, "struct ", 7) == 0)
        {
            clean_type += 7;
        }
        def = find_struct_def(ctx, clean_type);
    }

    Type *t = node->type_info;
    int has_drop = 0;
    if (t)
    {
        if (t->kind == TYPE_FUNCTION)
        {
            has_drop = t->traits.has_drop && !t->is_raw;
        }
        else if (t->kind == TYPE_STRUCT || t->kind == TYPE_ENUM)
        {
            if (def && def->type_info)
            {
                has_drop = def->type_info->traits.has_drop;
            }
            else
            {
                has_drop = t->traits.has_drop;
            }
        }
    }
    else if (def && def->type_info)
    {
        has_drop = def->type_info->traits.has_drop;
    }

    if (has_drop)
    {
        if (node->kind == NODE_EXPR_VAR)
        {
            char *df_prefix = "";
            if (ctx->cg.current_lambda)
            {
                for (int i = 0; i < ctx->cg.current_lambda->lambda.capture_count; i++)
                {
                    if (strcmp(node->var_ref.name,
                               ctx->cg.current_lambda->lambda.captured_vars[i]) == 0)
                    {
                        if (ctx->cg.current_lambda->lambda.capture_modes &&
                            ctx->cg.current_lambda->lambda.capture_modes[i] == 0)
                        {
                            df_prefix = "ctx->";
                        }
                        break;
                    }
                }
            }

            if (strcmp(node->var_ref.name, "self") != 0)
            {
                EMIT(ctx, "%s__z_drop_flag_%s = 0", df_prefix, node->var_ref.name);
                return 1;
            }
            return 0;
        }
        else if (node->kind == NODE_EXPR_MEMBER)
        {
            // For members: memset(&foo.bar, 0, sizeof(foo.bar))
            EMIT(ctx, "memset(&");
            codegen_expression(ctx, node);
            EMIT(ctx, ", 0, sizeof(");
            codegen_expression(ctx, node);
            EMIT(ctx, "))");
            return 1;
        }
    }
    return 0;
}

// Emits expression, wrapping it in a move-invalidation block if it's a consuming variable usage
void codegen_expression_with_move(ParserContext *ctx, ASTNode *node)
{
    if (!node)
    {
        return;
    }

    if (node && (node->kind == NODE_EXPR_VAR || node->kind == NODE_EXPR_MEMBER))
    {
        // Re-use infer logic to see if we need invalidation
        char *type_name = infer_type(ctx, node);
        ASTNode *def = NULL;
        if (type_name)
        {
            char *clean_type = type_name;
            if (strncmp(clean_type, "struct ", 7) == 0)
            {
                clean_type += 7;
            }
            def = find_struct_def(ctx, clean_type);
        }

        Type *t = node->type_info;
        int has_drop = 0;
        if (t)
        {
            if (t->kind == TYPE_FUNCTION)
            {
                has_drop = t->traits.has_drop && !t->is_raw;
            }
            else if (t->kind == TYPE_STRUCT || t->kind == TYPE_ENUM)
            {
                if (def && def->type_info)
                {
                    has_drop = def->type_info->traits.has_drop;
                }
                else
                {
                    has_drop = t->traits.has_drop;
                }
            }
        }
        else if (def && def->type_info)
        {
            has_drop = def->type_info->traits.has_drop;
        }

        if (has_drop)
        {
            if (node->kind == NODE_EXPR_VAR)
            {
                EMIT(ctx, "({ ");
                emit_move_invalidation(ctx, node);
                EMIT(ctx, "; ");
                codegen_expression(ctx, node);
                EMIT(ctx, "; })");
            }
            else
            {
                EMIT(ctx, "({ __typeof__(");
                codegen_expression(ctx, node);
                EMIT(ctx, ") _mv = ");
                codegen_expression(ctx, node);
                EMIT(ctx, "; ");
                emit_move_invalidation(ctx, node);
                EMIT(ctx, "; _mv; })");
            }
            return;
        }
    }
    codegen_expression(ctx, node);
}

// Helper: check if an enum is "simple" (no variant has a payload).
// Simple enums are emitted as plain C enums, not struct+tag+union.
int is_simple_enum(ParserContext *ctx, const char *enum_name)
{
    if (!ctx || !enum_name)
    {
        return 0;
    }
    const char *clean = enum_name;
    if (strncmp(clean, "struct ", 7) == 0)
    {
        clean += 7;
    }
    ASTNode *def = find_struct_def(ctx, clean);
    if (!def || def->kind != NODE_ENUM)
    {
        return 0;
    }
    ASTNode *v = def->enm.variants;
    while (v)
    {
        if (v->variant.payload)
        {
            return 0;
        }
        v = v->next;
    }
    return 1;
}

void handle_node_await_internal(ParserContext *ctx, ASTNode *node)
{
    // Determine the function name from the awaited expression
    ASTNode *operand = node->unary.operand;
    const char *fname = NULL;
    if (operand && operand->kind == NODE_EXPR_CALL && operand->call.callee)
    {
        if (operand->call.callee->kind == NODE_EXPR_VAR)
        {
            fname = operand->call.callee->var_ref.name;
        }
    }

    if (!fname)
    {
        // Fallback: use infer_type result as the return type
        char *inf = infer_type(ctx, node);
        EMIT(ctx, "({ %s _r = ", inf ? inf : "void");
        codegen_expression(ctx, operand);
        EMIT(ctx, "; _r; })");
        zfree(inf);
        return;
    }

    // Get the return type for the get function
    char *ret_type = "void";
    if (node->type_info)
    {
        char *t = type_to_c_string(node->type_info);
        if (t && strcmp(t, "void") != 0)
        {
            ret_type = t;
        }
    }
    else if (node->resolved_type)
    {
        ret_type = node->resolved_type;
    }

    // Generate: ({ struct fname_Future _f; fname_init(&_f, args...);
    //             while (!fname_poll(&_f)); fname_get(&_f); })
    EMIT(ctx, "({ struct %s_Future _f; ", fname);
    EMIT(ctx, "%s_init(&_f", fname);

    // Extract arguments from the call expression
    if (operand->kind == NODE_EXPR_CALL)
    {
        ASTNode *arg = operand->call.args;
        while (arg)
        {
            EMIT(ctx, ", ");
            codegen_expression(ctx, arg);
            arg = arg->next;
        }
    }

    EMIT(ctx, "); while (!%s_poll(&_f)); ", fname);
    if (strcmp(ret_type, "void") != 0 && strcmp(ret_type, "void*") != 0)
    {
        EMIT(ctx, "%s_get(&_f); })", fname);
    }
    else
    {
        EMIT(ctx, " })");
    }
}
