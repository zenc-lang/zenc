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

const char *get_missing_function_hint(ParserContext *ctx, const char *name)
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
    static char best_buf[MAX_SHORT_MSG_LEN];
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
        if (ref->node && ref->node->kind == NODE_FUNCTION)
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
static void codegen_literal_expr(ParserContext *ctx, ASTNode *node)
{
    if (node->literal.kind == LITERAL_STRING || node->literal.kind == LITERAL_RAW_STRING)
    {
        EMIT(ctx, "\"");
        for (int i = 0; node->literal.string_val[i]; i++)
        {
            char c = node->literal.string_val[i];
            if (node->literal.kind == LITERAL_RAW_STRING)
            {
                if (c == '\\')
                {
                    EMIT(ctx, "\\\\");
                }
                else if (c == '"')
                {
                    EMIT(ctx, "\\\"");
                }
                else if (c == '\n')
                {
                    EMIT(ctx, "\\n");
                }
                else if (c == '\r')
                {
                    EMIT(ctx, "\\r");
                }
                else if (c == '\t')
                {
                    EMIT(ctx, "\\t");
                }
                else
                {
                    EMIT(ctx, "%c", c);
                }
            }
            else
            {
                EMIT(ctx, "%c", c);
            }
        }
        EMIT(ctx, "\"");
    }
    else if (node->literal.kind == LITERAL_CHAR)
    {
        if (node->literal.int_val > 127)
        {
            EMIT(ctx, "%u", (unsigned int)node->literal.int_val);
        }
        else
        {
            EMIT(ctx, "%s", node->literal.string_val);
        }
    }
    else if (node->literal.kind == LITERAL_FLOAT)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.17g", node->literal.float_val);
        if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'E'))
        {
            strcat(buf, ".0");
        }
        EMIT(ctx, "%s", buf);
    }
    else // LITERAL_INT
    {
        if (node->literal.int_val > 9223372036854775807ULL)
        {
            EMIT(ctx, "%lluULL", (unsigned long long)node->literal.int_val);
        }
        else
        {
            EMIT(ctx, "%lld", (long long)node->literal.int_val);
        }
    }
}

// Emit variable reference expression
static void codegen_var_expr(ParserContext *ctx, ASTNode *node)
{
    if (ctx->cg.current_lambda)
    {
        for (int i = 0; i < ctx->cg.current_lambda->lambda.capture_count; i++)
        {
            if (strcmp(node->var_ref.name, ctx->cg.current_lambda->lambda.captured_vars[i]) == 0)
            {
                if (ctx->cg.current_lambda->lambda.capture_modes &&
                    ctx->cg.current_lambda->lambda.capture_modes[i] == 1)
                {
                    EMIT(ctx, "(*ctx->%s)", node->var_ref.name);
                }
                else
                {
                    EMIT(ctx, "ctx->%s", node->var_ref.name);
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
            char msg[MAX_SHORT_MSG_LEN];
            char help[MAX_SHORT_MSG_LEN];
            snprintf(msg, sizeof(msg), "Undefined variable '%s'", node->var_ref.name);
            snprintf(help, sizeof(help), "Did you mean '%s'?", node->var_ref.suggestion);
            zwarn_at(node->token, "%s\n   = help: %s", msg, help);
        }
    }

    // Check for static method call pattern: Type::method or Type__method
    char *sep = (char *)strstr(node->var_ref.name, "::");
    int sep_len = 2;
    if (!sep)
    {
        sep = strstr(node->var_ref.name, "__");
        sep_len = 2;
    }

    if (sep)
    {
        // Extract type name and method name
        ptrdiff_t type_len = sep - node->var_ref.name;
        char *type_name = xmalloc((size_t)(type_len + 1));
        strncpy(type_name, node->var_ref.name, (size_t)(type_len));
        type_name[type_len] = 0;

        char *method_name = sep + sep_len;

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
                emit_mangled_name(ctx, alias, method_name);
                zfree(type_name);
                zfree(mangled_type);
                return;
            }
        }
        emit_mangled_name(ctx, mangled_type, method_name);

        // If it's a no-payload enum variant and we're NOT inside a call expression callee,
        // auto-call the constructor. When g_emitting_callee is set, the parent
        // NODE_EXPR_CALL will add the ().
        if (!g_emitting_callee)
        {
            EnumVariantReg *ev = find_enum_variant(ctx, method_name);
            if (ev)
            {
                const char *clean_ev = ev->enum_name;
                if (strncmp(clean_ev, "struct ", 7) == 0)
                {
                    clean_ev += 7;
                }
                const char *clean_mangled = mangled_type;
                if (strncmp(clean_mangled, "struct ", 7) == 0)
                {
                    clean_mangled += 7;
                }

                if (strcmp(clean_ev, clean_mangled) == 0)
                {
                    EMIT(ctx, "()");
                }
            }
        }

        zfree(type_name);
        zfree(mangled_type);
        return;
    }

    if (strcmp(node->var_ref.name, "self") == 0)
    {
        if (node->type_info && node->type_info->kind == TYPE_STRUCT)
        {
            EMIT(ctx, "(*self)");
            return;
        }
    }

    // Check for legacy Enum_Variant patterns (single underscore)
    // Avoid double-mangling if it already has double underscores (generics)
    char *underscore = (char *)strchr(node->var_ref.name, '_');
    if (underscore && underscore != node->var_ref.name && *(underscore + 1) != '_' &&
        strstr(node->var_ref.name, "__") == NULL)
    {
        char base[MAX_TYPE_NAME_LEN];
        size_t len = (size_t)(underscore - node->var_ref.name);
        if (len < sizeof(base))
        {
            strncpy(base, node->var_ref.name, (size_t)(len));
            base[len] = 0;

            ASTNode *def = find_struct_def(ctx, base);
            int is_common_enum =
                (strncmp(base, "Result", 6) == 0 || strncmp(base, "Option", 6) == 0 ||
                 strncmp(base, "JsonType", 8) == 0);
            if (is_common_enum || (def && def->kind == NODE_ENUM))
            {
                emit_mangled_name(ctx, base, underscore + 1);
                return;
            }
        }
    }
    ZenSymbol *sym = find_symbol_in_all(ctx, node->var_ref.name);
    if (sym && sym->link_name)
    {
        EMIT(ctx, "%s", sym->link_name);
    }
    else
    {
        EMIT(ctx, "%s", node->var_ref.name);
    }
}

// Emit lambda expression
static void codegen_lambda_expr(ParserContext *ctx, ASTNode *node)
{
    if (node->lambda.is_bare)
    {
        EMIT(ctx, "_lambda_%d", node->lambda.lambda_id);
        return;
    }

    if (node->lambda.capture_count > 0)
    {
        int lid = node->lambda.lambda_id;
        if (ctx->config->use_cpp)
        {
            EMIT(ctx,
                 "({ struct Lambda_%d_Ctx *_z_ctx_%d = (struct Lambda_%d_Ctx*)malloc(sizeof(struct "
                 "Lambda_%d_Ctx));\n",
                 lid, lid, lid, lid);
        }
        else
        {
            EMIT(ctx,
                 "({ struct Lambda_%d_Ctx *_z_ctx_%d = malloc(sizeof(struct Lambda_%d_Ctx));\n",
                 lid, lid, lid);
        }
        for (int i = 0; i < node->lambda.capture_count; i++)
        {
            if (node->lambda.capture_modes && node->lambda.capture_modes[i] == 1)
            {
                int found = 0;
                if (ctx->cg.current_lambda)
                {
                    for (int k = 0; k < ctx->cg.current_lambda->lambda.capture_count; k++)
                    {
                        if (strcmp(node->lambda.captured_vars[i],
                                   ctx->cg.current_lambda->lambda.captured_vars[k]) == 0)
                        {
                            if (ctx->cg.current_lambda->lambda.capture_modes &&
                                ctx->cg.current_lambda->lambda.capture_modes[k] == 1)
                            {
                                EMIT(ctx, "_z_ctx_%d->%s = ctx->%s;\n", lid,
                                     node->lambda.captured_vars[i], node->lambda.captured_vars[i]);
                            }
                            else
                            {
                                EMIT(ctx, "_z_ctx_%d->%s = &ctx->%s;\n", lid,
                                     node->lambda.captured_vars[i], node->lambda.captured_vars[i]);
                            }
                            found = 1;
                            break;
                        }
                    }
                }
                if (!found)
                {
                    EMIT(ctx, "_z_ctx_%d->%s = &%s;\n", lid, node->lambda.captured_vars[i],
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

                EMIT(ctx, "*(%s*)(&_z_ctx_%d->%s) = ", tstr, lid, node->lambda.captured_vars[i]);
                zfree(tstr);

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

                codegen_expression_with_move(ctx, var_node);

                ast_free(var_node);

                EMIT(ctx, ";\n");

                if (node->lambda.captured_types && node->lambda.captured_types[i])
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
                        EMIT(ctx, "_z_ctx_%d->__z_drop_flag_%s = 1;\n", lid,
                             node->lambda.captured_vars[i]);
                    }
                }
            }
        }
        if (ctx->config->use_cpp)
        {
            EMIT(ctx, "z_closure_T _cl = {(void*)_lambda_%d, _z_ctx_%d, _lambda_%d_drop}; _cl; })",
                 lid, lid, lid);
        }
        else
        {
            EMIT(ctx,
                 "(z_closure_T){.func = _lambda_%d, .ctx = _z_ctx_%d, .drop = _lambda_%d_drop}; })",
                 lid, lid, lid);
        }
    }
    else
    {
        if (ctx->config->use_cpp)
        {
            EMIT(ctx, "(z_closure_T){ (void*)_lambda_%d, NULL, NULL }", node->lambda.lambda_id);
        }
        else
        {
            EMIT(ctx, "((z_closure_T){.func = (void*)_lambda_%d, .ctx = NULL, .drop = NULL})",
                 node->lambda.lambda_id);
        }
    }
}

void handle_expr_match(ParserContext *ctx, ASTNode *node)
{
    codegen_match_internal(ctx, node, 1);
}

void handle_expr_var(ParserContext *ctx, ASTNode *node)
{
    codegen_var_expr(ctx, node);
}

void handle_lambda(ParserContext *ctx, ASTNode *node)
{
    codegen_lambda_expr(ctx, node);
}

void handle_expr_literal(ParserContext *ctx, ASTNode *node)
{
    codegen_literal_expr(ctx, node);
}

void handle_raw_stmt(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "%s", node->raw_stmt.content);
}

void handle_ast_comment(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "%s\n", node->comment.content);
}

// Error-recovered node from fault-tolerant (LSP/REPL) parsing: emit a C
// comment so the recovered source is visibly incomplete rather than silently
// producing wrong output.
void handle_erroneous(ParserContext *ctx, ASTNode *node)
{
    (void)node;
    EMIT(ctx, "/* <error-recovered> */\n");
}

void handle_ternary(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "((");
    codegen_expression(ctx, node->ternary.cond);
    EMIT(ctx, ") ? (");
    codegen_expression(ctx, node->ternary.true_expr);
    EMIT(ctx, ") : (");
    codegen_expression(ctx, node->ternary.false_expr);
    EMIT(ctx, "))");
}

void handle_await(ParserContext *ctx, ASTNode *node)
{
    handle_node_await_internal(ctx, node);
}

void handle_va_start(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "va_start(");
    codegen_expression(ctx, node->va_start_args.ap);
    EMIT(ctx, ", ");
    codegen_expression(ctx, node->va_start_args.last_arg);
    EMIT(ctx, ")");
}

void handle_va_end(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "va_end_args(");
    codegen_expression(ctx, node->va_end_args.ap);
    EMIT(ctx, ")");
}

void handle_va_copy(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "va_copy(");
    codegen_expression(ctx, node->va_copy_args.dest);
    EMIT(ctx, ", ");
    codegen_expression(ctx, node->va_copy_args.src);
    EMIT(ctx, ")");
}

void handle_va_arg(ParserContext *ctx, ASTNode *node)
{
    char *type_str = type_to_c_string(node->va_arg_val.type_info);
    EMIT(ctx, "va_arg(");
    codegen_expression(ctx, node->va_arg_val.ap);
    EMIT(ctx, ", %s)", type_str);
    zfree(type_str);
}
