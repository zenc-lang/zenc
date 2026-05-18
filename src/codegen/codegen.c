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

typedef void (*ExprHandler)(ParserContext *ctx, ASTNode *node);

// Flag to track whether we're emitting a call expression callee.
// When true, codegen_var_expr should not auto-call no-payload enum variants
// because the parent NODE_EXPR_CALL will add the ().
static int g_emitting_callee = 0;

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
static void codegen_literal_expr(ParserContext *ctx, ASTNode *node)
{
    if (node->literal.type_kind == LITERAL_STRING || node->literal.type_kind == LITERAL_RAW_STRING)
    {
        EMIT(ctx, "\"");
        for (int i = 0; node->literal.string_val[i]; i++)
        {
            char c = node->literal.string_val[i];
            if (node->literal.type_kind == LITERAL_RAW_STRING)
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
    else if (node->literal.type_kind == LITERAL_CHAR)
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
    else if (node->literal.type_kind == LITERAL_FLOAT)
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
        for (int i = 0; i < ctx->cg.current_lambda->lambda.num_captures; i++)
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
    char *sep = strstr(node->var_ref.name, "::");
    int sep_len = 2;
    if (!sep)
    {
        sep = strstr(node->var_ref.name, "__");
        sep_len = 2;
    }

    if (sep)
    {
        // Extract type name and method name
        int type_len = sep - node->var_ref.name;
        char *type_name = xmalloc(type_len + 1);
        strncpy(type_name, node->var_ref.name, type_len);
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
    char *underscore = strchr(node->var_ref.name, '_');
    if (underscore && underscore != node->var_ref.name && *(underscore + 1) != '_' &&
        strstr(node->var_ref.name, "__") == NULL)
    {
        char base[MAX_TYPE_NAME_LEN];
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

    if (node->lambda.num_captures > 0)
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
        for (int i = 0; i < node->lambda.num_captures; i++)
        {
            if (node->lambda.capture_modes && node->lambda.capture_modes[i] == 1)
            {
                int found = 0;
                if (ctx->cg.current_lambda)
                {
                    for (int k = 0; k < ctx->cg.current_lambda->lambda.num_captures; k++)
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
                EMIT(ctx, "_z_ctx_%d->%s = ", lid, node->lambda.captured_vars[i]);

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

static void handle_expr_match(ParserContext *ctx, ASTNode *node)
{
    codegen_match_internal(ctx, node, 1);
}

static void handle_expr_var(ParserContext *ctx, ASTNode *node)
{
    codegen_var_expr(ctx, node);
}

static void handle_lambda(ParserContext *ctx, ASTNode *node)
{
    codegen_lambda_expr(ctx, node);
}

static void handle_expr_literal(ParserContext *ctx, ASTNode *node)
{
    codegen_literal_expr(ctx, node);
}

static void handle_raw_stmt(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "%s", node->raw_stmt.content);
}

static void handle_ast_comment(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "%s\n", node->comment.content);
}

static void handle_ternary(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "((");
    codegen_expression(ctx, node->ternary.cond);
    EMIT(ctx, ") ? (");
    codegen_expression(ctx, node->ternary.true_expr);
    EMIT(ctx, ") : (");
    codegen_expression(ctx, node->ternary.false_expr);
    EMIT(ctx, "))");
}

static void handle_await(ParserContext *ctx, ASTNode *node)
{
    handle_node_await_internal(ctx, node);
}

static void handle_va_start(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "va_start(");
    codegen_expression(ctx, node->va_start_args.ap);
    EMIT(ctx, ", ");
    codegen_expression(ctx, node->va_start_args.last_arg);
    EMIT(ctx, ")");
}

static void handle_va_end(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "va_end_args(");
    codegen_expression(ctx, node->va_end_args.ap);
    EMIT(ctx, ")");
}

static void handle_va_copy(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "va_copy(");
    codegen_expression(ctx, node->va_copy_args.dest);
    EMIT(ctx, ", ");
    codegen_expression(ctx, node->va_copy_args.src);
    EMIT(ctx, ")");
}

static void handle_va_arg(ParserContext *ctx, ASTNode *node)
{
    char *type_str = type_to_c_string(node->va_arg_val.type_info);
    EMIT(ctx, "va_arg(");
    codegen_expression(ctx, node->va_arg_val.ap);
    EMIT(ctx, ", %s)", type_str);
    zfree(type_str);
}

static void handle_expr_sizeof(ParserContext *ctx, ASTNode *node)
{
    if (node->size_of.target_type_info)
    {
        char *mapped = type_to_c_string(node->size_of.target_type_info);
        EMIT(ctx, "sizeof(%s)", mapped);
        zfree(mapped);
    }
    else if (node->size_of.target_type)
    {
        const char *t = node->size_of.target_type;
        const char *mapped = map_to_c_type(t);
        EMIT(ctx, "sizeof(%s)", mapped);
    }
    else
    {
        EMIT(ctx, "sizeof(");
        codegen_expression(ctx, node->size_of.expr);
        EMIT(ctx, ")");
    }
}

static void handle_typeof(ParserContext *ctx, ASTNode *node)
{
    if (node->size_of.target_type_info)
    {
        char *mapped = type_to_c_string(node->size_of.target_type_info);
        EMIT(ctx, "typeof(%s)", mapped);
        zfree(mapped);
    }
    else if (node->size_of.target_type)
    {
        EMIT(ctx, "typeof(%s)", node->size_of.target_type);
    }
    else
    {
        EMIT(ctx, "typeof(");
        codegen_expression(ctx, node->size_of.expr);
        EMIT(ctx, ")");
    }
}

static void handle_expr_unary(ParserContext *ctx, ASTNode *node)
{
    if (node->unary.op && strcmp(node->unary.op, "&_rval") == 0)
    {
        if (ctx->config->use_cpp)
        {
            EMIT(ctx, "({ __typeof__((");
            codegen_expression(ctx, node->unary.operand);
            EMIT(ctx, ")) _tmp = ");
            codegen_expression(ctx, node->unary.operand);
            EMIT(ctx, "; &_tmp; })");
        }
        else
        {
            EMIT(ctx, "(__typeof__((");
            codegen_expression(ctx, node->unary.operand);
            EMIT(ctx, "))[]){");
            codegen_expression(ctx, node->unary.operand);
            EMIT(ctx, "}");
        }
    }
    else if (node->unary.op && strcmp(node->unary.op, "?") == 0)
    {
        EMIT(ctx, "({ ");
        emit_auto_type(ctx, node->unary.operand, node->token);
        EMIT(ctx, " _t = (");
        codegen_expression(ctx, node->unary.operand);
        EMIT(ctx, "); if (_t.tag != 0) return _t; _t.data.Ok; })");
    }
    else if (node->unary.op && strcmp(node->unary.op, "_post++") == 0)
    {
        EMIT(ctx, "(");
        codegen_expression(ctx, node->unary.operand);
        EMIT(ctx, "++)");
    }
    else if (node->unary.op && strcmp(node->unary.op, "_post--") == 0)
    {
        EMIT(ctx, "(");
        codegen_expression(ctx, node->unary.operand);
        EMIT(ctx, "--)");
    }
    else
    {
        EMIT(ctx, "(%s", node->unary.op);
        codegen_expression(ctx, node->unary.operand);
        EMIT(ctx, ")");
    }
}

static void handle_expr_array_literal(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "{");
    ASTNode *elem = node->array_literal.elements;
    int first_arr = 1;
    while (elem)
    {
        if (!first_arr)
        {
            EMIT(ctx, ", ");
        }
        codegen_expression(ctx, elem);
        elem = elem->next;
        first_arr = 0;
    }
    EMIT(ctx, "}");
}

static void handle_expr_tuple_literal(ParserContext *ctx, ASTNode *node)
{
    char *type = node->resolved_type ? node->resolved_type
                 : node->type_info   ? type_to_string(node->type_info)
                                     : "unknown";
    EMIT(ctx, "(%s){", type);
    ASTNode *tup_elem = node->tuple_literal.elements;
    int first_tup = 1;
    while (tup_elem)
    {
        if (!first_tup)
        {
            EMIT(ctx, ", ");
        }
        codegen_expression(ctx, tup_elem);
        tup_elem = tup_elem->next;
        first_tup = 0;
    }
    EMIT(ctx, "}");
}

static void handle_expr_member(ParserContext *ctx, ASTNode *node)
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
        if (node->member.target->type == NODE_EXPR_VAR)
        {
            ASTNode *def = find_struct_def(ctx, node->member.target->var_ref.name);
            if (def && def->type == NODE_ENUM)
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

static void handle_expr_index(ParserContext *ctx, ASTNode *node)
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
        if (node->index.array->type == NODE_EXPR_VAR)
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

static void handle_expr_slice(ParserContext *ctx, ASTNode *node)
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

static void handle_block(ParserContext *ctx, ASTNode *node)
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

static void handle_if_expr(ParserContext *ctx, ASTNode *node)
{
    EMIT(ctx, "({ ");

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
    if (node->if_stmt.then_body && node->if_stmt.then_body->type == NODE_BLOCK)
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
    if (node->if_stmt.else_body && node->if_stmt.else_body->type == NODE_BLOCK)
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

static void handle_try_expr(ParserContext *ctx, ASTNode *node)
{
    char *type_name = "Result";
    if (ctx->cg.current_func_ret_type)
    {
        type_name = ctx->cg.current_func_ret_type;
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
        if (er->node && er->node->type == NODE_ENUM && strcmp(er->node->enm.name, search_name) == 0)
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

    EMIT(ctx, "({ ");
    emit_auto_type(ctx, node->try_stmt.expr, node->token);
    EMIT(ctx, " _try = ");
    codegen_expression(ctx, node->try_stmt.expr);

    if (is_enum)
    {
        EMIT(ctx,
             "; if (_try.tag == %s__Err_Tag) return (%s__Err(_try.data.Err)); _try.data.Ok; })",
             search_name, search_name);
    }
    else
    {
        EMIT(ctx, "; if (!_try.is_ok) return %s__Err(_try.err); _try.val; })", search_name);
    }
}

static void handle_plugin(ParserContext *ctx, ASTNode *node)
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

static void handle_expr_cast(ParserContext *ctx, ASTNode *node)
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
        if (def && def->type == NODE_ENUM)
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

static void handle_reflection(ParserContext *ctx, ASTNode *node)
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
            if (f->type == NODE_FIELD)
            {
                EMIT(ctx, "{ \"%s\", \"%s\", __builtin_offsetof(struct %s, %s) }, ", f->field.name,
                     f->field.type, sname, f->field.name);
            }
            f = f->next;
        }
        EMIT(ctx, "{ 0 } }; (void*)_fields_%s; })", sname);
    }
}

static void handle_expr_struct_init(ParserContext *ctx, ASTNode *node)
{
    const char *struct_name = node->struct_init.struct_name;
    if (strcmp(struct_name, "Self") == 0 && ctx->cg.current_impl_type)
    {
        struct_name = ctx->cg.current_impl_type;
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

    int in_func = (ctx->cg.current_func_ret_type != NULL || ctx->cg.current_lambda != NULL);

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

    if (ctx->config->use_cpp)
    {
        if (in_func && !is_vector)
        {
            EMIT(ctx, "({ %s _s = %s; ", struct_name, ctx->config->use_cpp ? "{}" : "{0}");
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
                    if (f->var_decl.init_expr &&
                        f->var_decl.init_expr->type == NODE_EXPR_ARRAY_LITERAL)
                    {
                        ASTNode *elem = f->var_decl.init_expr->array_literal.elements;
                        int idx = 0;
                        while (elem)
                        {
                            EMIT(ctx, "_s.%s[%d] = ", f->var_decl.name, idx++);
                            codegen_expression(ctx, elem);
                            EMIT(ctx, "; ");
                            elem = elem->next;
                        }
                    }
                    else
                    {
                        if (ctx->config->use_cpp)
                        {
                            EMIT(ctx, "_s.%s = (__typeof__(_s.%s))(", f->var_decl.name,
                                 f->var_decl.name);
                            codegen_expression_with_move(ctx, f->var_decl.init_expr);
                            EMIT(ctx, ")");
                        }
                        else
                        {
                            EMIT(ctx, "_s.%s = ", f->var_decl.name);
                            codegen_expression_with_move(ctx, f->var_decl.init_expr);
                        }
                        EMIT(ctx, "; ");
                    }
                }
                f = f->next;
            }
            EMIT(ctx, "_s; })");
        }
        else
        {
            EMIT(ctx, "%s { ", struct_name);
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
                        EMIT(ctx, ", ");
                    }
                    codegen_expression(ctx, f->var_decl.init_expr);
                }
            }
            else
            {
                int first = 1;
                while (f)
                {
                    if (!first)
                    {
                        EMIT(ctx, ", ");
                    }
                    if (is_vector)
                    {
                        codegen_expression(ctx, f->var_decl.init_expr);
                    }
                    else
                    {
                        EMIT(ctx, ".%s = ", f->var_decl.name);
                        codegen_expression_with_move(ctx, f->var_decl.init_expr);
                    }
                    first = 0;
                    f = f->next;
                }
            }
            EMIT(ctx, " }");
        }
    }
    else
    {
        if (is_vector)
        {
            EMIT(ctx, "(%s){", struct_name);
        }
        else if (is_union)
        {
            EMIT(ctx, "(union %s){", struct_name);
        }
        else if (is_zen_struct)
        {
            EMIT(ctx, "(struct %s){", struct_name);
        }
        else
        {
            EMIT(ctx, "(%s){", struct_name);
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
                    EMIT(ctx, ", ");
                }
                codegen_expression(ctx, f->var_decl.init_expr);
            }
        }
        else
        {
            int first = 1;
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
                    if (!first)
                    {
                        EMIT(ctx, ", ");
                    }
                    if (!is_vector)
                    {
                        EMIT(ctx, ".%s = ", f->var_decl.name);
                    }
                    codegen_expression_with_move(ctx, f->var_decl.init_expr);
                    first = 0;
                }
                f = f->next;
            }
            if (first)
            {
                EMIT(ctx, "0");
            }
        }
        EMIT(ctx, "}");
    }
}

static void handle_expr_binary(ParserContext *ctx, ASTNode *node)
{
    if (ctx->config->misra_mode)
    {
        EMIT(ctx, "(");
    }
    if (strncmp(node->binary.op, "??", 2) == 0 && strlen(node->binary.op) == 2)
    {
        EMIT(ctx, "({ ");
        emit_auto_type(ctx, node->binary.left, node->token);
        EMIT(ctx, " _l = (");
        codegen_expression(ctx, node->binary.left);
        EMIT(ctx, "); _l ? _l : (");
        codegen_expression(ctx, node->binary.right);
        EMIT(ctx, "); })");
    }
    else if (strcmp(node->binary.op, "?\?=") == 0)
    {
        EMIT(ctx, "({ if (!(");
        codegen_expression(ctx, node->binary.left);
        EMIT(ctx, ")) ");
        codegen_expression(ctx, node->binary.left);
        EMIT(ctx, " = (");
        codegen_expression(ctx, node->binary.right);
        EMIT(ctx, "); ");
        codegen_expression(ctx, node->binary.left);
        EMIT(ctx, "; })");
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

        int is_simple_enum = 0;
        if (def && def->type == NODE_ENUM)
        {
            is_simple_enum = 1;
            ASTNode *v = def->enm.variants;
            while (v)
            {
                if (v->variant.payload)
                {
                    is_simple_enum = 0;
                    break;
                }
                v = v->next;
            }
        }

        if (t1 && (def || found_opaque) && !is_basic && !is_ptr && !is_simple_enum)
        {
            char *base = mangle_base;
            if (strncmp(base, "struct ", 7) == 0)
            {
                base += 7;
            }

            if (strcmp(node->binary.op, "!=") == 0)
            {
                EMIT(ctx, "(!");
            }
            char meth[MAX_TYPE_NAME_LEN];
            snprintf(meth, sizeof(meth), "%s__Eq__eq", base);
            ZenSymbol *sym = find_symbol_in_all(ctx, meth);
            FuncSig *sig = sym ? sym->data.sig : NULL;
            if (!sig)
            {
                snprintf(meth, sizeof(meth), "%s__eq", base);
                sym = find_symbol_in_all(ctx, meth);
                sig = sym ? sym->data.sig : NULL;
            }

            const char *call_name = sig ? sig->name : NULL;
            if (!call_name)
            {
                snprintf(meth, sizeof(meth), "%s__Eq__eq", base);
                call_name = meth;
            }

            EMIT(ctx, "%s(", (sig && sig->link_name) ? sig->link_name : call_name);

            if (node->binary.left->type == NODE_EXPR_VAR ||
                node->binary.left->type == NODE_EXPR_INDEX ||
                node->binary.left->type == NODE_EXPR_MEMBER)
            {
                EMIT(ctx, "&");
                codegen_expression(ctx, node->binary.left);
            }
            else if (ctx->config->use_cpp)
            {
                EMIT(ctx, "({ __typeof__((");
                codegen_expression(ctx, node->binary.left);
                EMIT(ctx, ")) _tmp = ");
                codegen_expression(ctx, node->binary.left);
                EMIT(ctx, "; &_tmp; })");
            }
            else
            {
                EMIT(ctx, "(__typeof__((");
                codegen_expression(ctx, node->binary.left);
                EMIT(ctx, "))[]){");
                codegen_expression(ctx, node->binary.left);
                EMIT(ctx, "}");
            }

            EMIT(ctx, ", ");

            int needs_ptr = 1;
            if (sig)
            {
                needs_ptr = (sig->total_args > 1 && sig->arg_types[1]->kind == TYPE_POINTER);
            }

            if (needs_ptr && (node->binary.right->type == NODE_EXPR_VAR ||
                              node->binary.right->type == NODE_EXPR_INDEX ||
                              node->binary.right->type == NODE_EXPR_MEMBER))
            {
                EMIT(ctx, "&");
                codegen_expression(ctx, node->binary.right);
            }
            else if (needs_ptr && ctx->config->use_cpp)
            {
                EMIT(ctx, "({ __typeof__((");
                codegen_expression(ctx, node->binary.right);
                EMIT(ctx, ")) _tmp = ");
                codegen_expression(ctx, node->binary.right);
                EMIT(ctx, "; &_tmp; })");
            }
            else if (needs_ptr)
            {
                EMIT(ctx, "(__typeof__((");
                codegen_expression(ctx, node->binary.right);
                EMIT(ctx, "))[]){");
                codegen_expression(ctx, node->binary.right);
                EMIT(ctx, "}");
            }
            else
            {
                codegen_expression(ctx, node->binary.right);
            }

            EMIT(ctx, ")");
            if (strcmp(node->binary.op, "!=") == 0)
            {
                EMIT(ctx, ")");
            }
        }
        else if (t1 && (strcmp(t1, "string") == 0))
        {
            char *t2 = infer_type(ctx, node->binary.right);
            if (t2 && (strcmp(t2, "string") == 0))
            {
                EMIT(ctx, "(strcmp(");
                codegen_expression(ctx, node->binary.left);
                EMIT(ctx, ", ");
                codegen_expression(ctx, node->binary.right);
                if (strcmp(node->binary.op, "==") == 0)
                {
                    EMIT(ctx, ") == 0)");
                }
                else
                {
                    EMIT(ctx, ") != 0)");
                }
            }
            else
            {
                EMIT(ctx, "(");
                codegen_expression(ctx, node->binary.left);
                EMIT(ctx, " %s ", node->binary.op);
                codegen_expression(ctx, node->binary.right);
                EMIT(ctx, ")");
            }
        }
        else
        {
            EMIT(ctx, "(");
            codegen_expression(ctx, node->binary.left);
            EMIT(ctx, " %s ", node->binary.op);
            codegen_expression(ctx, node->binary.right);
            EMIT(ctx, ")");
        }
        if (t1)
        {
            zfree(t1);
        }
    }
    else if (strcmp(node->binary.op, "**") == 0)
    {
        EMIT(ctx, "(_zc_pow((double)(");
        codegen_expression(ctx, node->binary.left);
        EMIT(ctx, "), (double)(");
        codegen_expression(ctx, node->binary.right);
        EMIT(ctx, ")))");
    }
    else if (strcmp(node->binary.op, "**=") == 0)
    {
        EMIT(ctx, "({ ");
        codegen_expression(ctx, node->binary.left);
        EMIT(ctx, " = _zc_pow((double)(");
        codegen_expression(ctx, node->binary.left);
        EMIT(ctx, "), (double)(");
        codegen_expression(ctx, node->binary.right);
        EMIT(ctx, ")); ");
        codegen_expression(ctx, node->binary.left);
        EMIT(ctx, "; })");
    }
    else
    {
        int is_assignment =
            (node->binary.op[strlen(node->binary.op) - 1] == '=' &&
             strcmp(node->binary.op, "==") != 0 && strcmp(node->binary.op, "!=") != 0 &&
             strcmp(node->binary.op, "<=") != 0 && strcmp(node->binary.op, ">=") != 0);

        int is_drop_assignment = 0;
        char *clean_type = NULL;
        if (is_assignment && strcmp(node->binary.op, "=") == 0 && ctx->config->use_cpp &&
            node->binary.left->type == NODE_EXPR_VAR)
        {
            char *type_name = infer_type(ctx, node->binary.left);
            if (type_name)
            {
                if (strchr(type_name, '*') == NULL)
                {
                    clean_type = xstrdup(type_name);
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
                }
                zfree(type_name);
            }
        }

        if (is_drop_assignment)
        {
            EMIT(ctx, "({ ");
            EMIT(ctx, "ZC_AUTO _z_tmp = (");
            codegen_expression_with_move(ctx, node->binary.right);
            EMIT(ctx, "); ");

            EMIT(ctx, "__typeof__((");
            codegen_expression(ctx, node->binary.left);
            EMIT(ctx, "))* _z_dest = &(");
            codegen_expression(ctx, node->binary.left);
            EMIT(ctx, "); ");

            if (node->binary.left->type == NODE_EXPR_VAR)
            {
                EMIT(ctx, "if (__z_drop_flag_%s) %s__Drop__glue(_z_dest); ",
                     node->binary.left->var_ref.name, clean_type);
            }
            else
            {
                EMIT(ctx, "%s__Drop__glue(_z_dest); ", clean_type);
            }

            EMIT(ctx, "*_z_dest = _z_tmp; ");

            if (node->binary.left->type == NODE_EXPR_VAR)
            {
                EMIT(ctx, "__z_drop_flag_%s = 1; ", node->binary.left->var_ref.name);
            }

            EMIT(ctx, "*_z_dest; })");
        }
        else
        {
            EMIT(ctx, "(");
            if (is_assignment)
            {
                codegen_expression(ctx, node->binary.left);
            }
            else
            {
                codegen_expression_with_move(ctx, node->binary.left);
            }

            EMIT(ctx, " %s ", node->binary.op);
            if (ctx->config->misra_mode ||
                (ctx->config->use_cpp && is_assignment && strcmp(node->binary.op, "=") == 0))
            {
                int should_cast = ctx->config->misra_mode;
                if (!should_cast && node->binary.left->type_info)
                {
                    TypeKind k = node->binary.left->type_info->kind;
                    if (k == TYPE_POINTER || k == TYPE_ENUM ||
                        is_enum_type_name(ctx, node->binary.left->type_info->name))
                    {
                        should_cast = 1;
                    }
                }

                if (should_cast)
                {
                    char *c_type = NULL;
                    if (node->binary.left->type_info)
                    {
                        c_type = type_to_c_string(node->binary.left->type_info);
                    }
                    if (c_type && strcmp(c_type, "unknown") != 0 && strcmp(c_type, "void") != 0)
                    {
                        EMIT(ctx, "(%s)(", c_type);
                        codegen_expression_with_move(ctx, node->binary.right);
                        EMIT(ctx, ")");
                        zfree(c_type);
                    }
                    else
                    {
                        if (c_type)
                        {
                            zfree(c_type);
                        }
                        codegen_expression_with_move(ctx, node->binary.right);
                    }
                }
                else
                {
                    codegen_expression_with_move(ctx, node->binary.right);
                }
            }
            else
            {
                codegen_expression_with_move(ctx, node->binary.right);
            }
            EMIT(ctx, ")");
        }

        if (clean_type)
        {
            zfree(clean_type);
        }
    }
    if (ctx->config->misra_mode)
    {
        EMIT(ctx, ")");
    }
}

static void handle_expr_call(ParserContext *ctx, ASTNode *node)
{
    emit_source_mapping(ctx, node);

    if (node->call.callee->type == NODE_EXPR_MEMBER)
    {
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
                    EMIT(ctx, "%d", target->type_info->array_size);
                }
                else
                {
                    codegen_expression(ctx, target);
                    EMIT(ctx, ".len");
                }
                return;
            }
        }

        if (target->type == NODE_EXPR_VAR)
        {
            char type_name[MAX_TYPE_NAME_LEN];
            strncpy(type_name, target->var_ref.name, sizeof(type_name));
            type_name[sizeof(type_name) - 1] = 0;

            char *mangled_type = type_name;

            ASTNode *def = find_struct_def(ctx, mangled_type);
            if (def && def->type == NODE_ENUM)
            {
                char mangled[MAX_MANGLED_NAME_LEN];
                const char *ename_for_mangling = (def->link_name) ? def->link_name : mangled_type;
                snprintf(mangled, sizeof(mangled), "%s__%s", ename_for_mangling, method);
                FuncSig *sig = find_func(ctx, mangled);
                if (sig)
                {
                    const char *emit_name = (sig->link_name) ? sig->link_name : mangled;
                    EMIT(ctx, "%s(", emit_name);
                    ASTNode *arg = node->call.args;
                    int arg_idx = 0;
                    while (arg)
                    {
                        if (arg_idx > 0)
                        {
                            EMIT(ctx, ", ");
                        }

                        Type *param_t =
                            (arg_idx < sig->total_args) ? sig->arg_types[arg_idx] : NULL;

                        if (param_t && param_t->kind == TYPE_STRUCT &&
                            strncmp(param_t->name, "Tuple__", 7) == 0 && sig->total_args == 1 &&
                            node->call.arg_count > 1)
                        {
                            EMIT(ctx, "(%s){", param_t->name);
                            int first = 1;
                            while (arg)
                            {
                                if (!first)
                                {
                                    EMIT(ctx, ", ");
                                }
                                first = 0;
                                codegen_expression(ctx, arg);
                                arg = arg->next;
                            }
                            EMIT(ctx, "}");
                            break;
                        }

                        codegen_expression(ctx, arg);
                        arg = arg->next;
                        arg_idx++;
                    }
                    EMIT(ctx, ")");
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
            char base_buf[MAX_ERROR_MSG_LEN];

            char *lt = strchr(base, '<');
            if (lt)
            {
                char *gt = strrchr(base, '>');
                if (gt)
                {
                    int prefix_len = lt - base;
                    char prefix[MAX_TYPE_NAME_LEN];
                    if (prefix_len >= 255)
                    {
                        prefix_len = 255;
                    }
                    strncpy(prefix, base, prefix_len);
                    prefix[prefix_len] = 0;

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

                    zfree(args_ptr);
                    zfree(clean_arg);
                }
            }

            if (!strchr(type, '*') &&
                (target->type == NODE_EXPR_CALL || target->type == NODE_EXPR_LITERAL ||
                 target->type == NODE_EXPR_BINARY || target->type == NODE_EXPR_UNARY ||
                 target->type == NODE_EXPR_CAST || target->type == NODE_EXPR_STRUCT_INIT))
            {
                char *type_mangled = (char *)normalize_type_name(type);
                if (type_mangled != type)
                {
                    mangled_base = type_mangled;
                }

                char type_buf[MAX_ERROR_MSG_LEN];
                char *t_lt = strchr(type, '<');

                if (t_lt)
                {
                    char *t_gt = strrchr(type, '>');
                    if (t_gt)
                    {
                        int p_len = t_lt - type;
                        char prefix[MAX_TYPE_NAME_LEN];
                        if (p_len >= 255)
                        {
                            p_len = 255;
                        }
                        strncpy(prefix, type, p_len);
                        prefix[p_len] = 0;

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

                        zfree(args_ptr);
                        zfree(clean_arg);
                    }
                }

                emit_mangled_name(ctx, mangled_base, method);
                EMIT(ctx, "(");
                if (ctx->config->use_cpp)
                {
                    EMIT(ctx, "({ __typeof__((");
                    codegen_expression(ctx, target);
                    EMIT(ctx, ")) _tmp = ");
                    codegen_expression(ctx, target);
                    EMIT(ctx, "; &_tmp; })");
                }
                else
                {
                    EMIT(ctx, "((%s[]){", type_mangled);
                    codegen_expression(ctx, target);
                    EMIT(ctx, "})");
                }
                ASTNode *arg = node->call.args;
                while (arg)
                {
                    EMIT(ctx, ", ");
                    codegen_expression_with_move(ctx, arg);
                    arg = arg->next;
                }
                EMIT(ctx, ")");
            }
            else
            {
                char *call_base = mangled_base;

                int need_cast = 0;
                char mixin_func_base[MAX_MANGLED_NAME_LEN * 2];
                snprintf(mixin_func_base, sizeof(mixin_func_base), "%s__%s", call_base, method);
                char *mixin_func_name_ptr = merge_underscores(mixin_func_base);
                char mixin_func_name[MAX_MANGLED_NAME_LEN * 2];
                strncpy(mixin_func_name, mixin_func_name_ptr, sizeof(mixin_func_name) - 1);
                mixin_func_name[sizeof(mixin_func_name) - 1] = 0;
                zfree(mixin_func_name_ptr);

                char *resolved_method_suffix = NULL;

                if (!find_func(ctx, mixin_func_name))
                {
                    TypeAlias *ta = ctx ? ctx->type_aliases : NULL;
                    while (ta)
                    {
                        if (strcmp(ta->original_type, call_base) == 0)
                        {
                            char alias_func_base[MAX_ERROR_MSG_LEN];
                            snprintf(alias_func_base, sizeof(alias_func_base), "%s__%s", ta->alias,
                                     method);
                            char *alias_func_name = merge_underscores(alias_func_base);
                            if (find_func(ctx, alias_func_name))
                            {
                                zfree(alias_func_name);
                                break;
                            }
                            zfree(alias_func_name);
                        }
                        ta = ta->next;
                    }
                    StructRef *ref = ctx->parsed_impls_list;
                    while (ref)
                    {
                        if (ref->node && ref->node->type == NODE_IMPL_TRAIT &&
                            strcmp(ref->node->impl_trait.target_type, base) == 0)
                        {
                            char trait_base[MAX_MANGLED_NAME_LEN];
                            snprintf(trait_base, sizeof(trait_base), "%s__%s__%s", base,
                                     ref->node->impl_trait.trait_name, method);
                            char *trait_mangled = merge_underscores(trait_base);
                            if (find_func(ctx, trait_mangled))
                            {
                                char suffix_base[MAX_MANGLED_NAME_LEN];
                                snprintf(suffix_base, sizeof(suffix_base), "%s__%s",
                                         ref->node->impl_trait.trait_name, method);
                                resolved_method_suffix = merge_underscores(suffix_base);
                                zfree(trait_mangled);
                                break;
                            }
                            zfree(trait_mangled);
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
                                char trait_base[MAX_ERROR_MSG_LEN];
                                snprintf(trait_base, sizeof(trait_base), "%s__%s__%s", base, tname,
                                         method);
                                char *trait_mangled = merge_underscores(trait_base);
                                if (find_func(ctx, trait_mangled))
                                {
                                    char suffix_base[MAX_ERROR_MSG_LEN];
                                    snprintf(suffix_base, sizeof(suffix_base), "%s__%s", tname,
                                             method);
                                    resolved_method_suffix = merge_underscores(suffix_base);
                                    zfree(trait_mangled);
                                    break;
                                }
                                zfree(trait_mangled);
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
                                char mixin_base[MAX_ERROR_MSG_LEN];
                                snprintf(mixin_base, sizeof(mixin_base), "%s__%s",
                                         def->strct.used_structs[k], method);
                                char *mixin_check = merge_underscores(mixin_base);
                                if (find_func(ctx, mixin_check))
                                {
                                    call_base = def->strct.used_structs[k];
                                    need_cast = 1;
                                    zfree(mixin_check);
                                    break;
                                }
                                zfree(mixin_check);
                            }
                        }
                    }
                }

                emit_mangled_name(ctx, call_base, method);
                EMIT(ctx, "(");
                if (need_cast)
                {
                    EMIT(ctx, "(%s*)%s", call_base, strchr(type, '*') ? "" : "&");
                }
                else if (!strchr(type, '*'))
                {
                    EMIT(ctx, "&");
                }
                codegen_expression(ctx, target);
                ASTNode *arg = node->call.args;
                while (arg)
                {
                    EMIT(ctx, ", ");
                    codegen_expression_with_move(ctx, arg);
                    arg = arg->next;
                }
                EMIT(ctx, ")");

                if (resolved_method_suffix)
                {
                    zfree(resolved_method_suffix);
                }
            }
            zfree(clean);
            zfree(type);
            return;
        }
        if (type)
        {
            zfree(type);
        }
    }

skip_method_mangling:

    if (node->call.callee->type == NODE_EXPR_VAR)
    {
        ASTNode *def = find_struct_def(ctx, node->call.callee->var_ref.name);
        if (def && def->type == NODE_STRUCT)
        {
            EMIT(ctx, "(struct %s){0}", node->call.callee->var_ref.name);
            return;
        }
    }

    Type *callee_ti = get_inner_type(node->call.callee->type_info);
    if (callee_ti && callee_ti->kind == TYPE_FUNCTION && !callee_ti->is_raw)
    {
        EMIT(ctx, "({ z_closure_T _c = ");
        codegen_expression(ctx, node->call.callee);
        EMIT(ctx, "; ");

        Type *ft = callee_ti;
        char *ret = type_to_c_string(ft->inner);
        if (strcmp(ret, "string") == 0)
        {
            zfree(ret);
            ret = xstrdup("char*");
        }
        if (strcmp(ret, "unknown") == 0)
        {
            zfree(ret);
            ret = xstrdup("void*");
        }

        EMIT(ctx, "((%s (*)(void*", ret);
        for (int i = 0; i < ft->arg_count; i++)
        {
            char *as = type_to_c_string(ft->args[i]);
            if (strcmp(as, "unknown") == 0)
            {
                zfree(as);
                as = xstrdup("void*");
            }
            EMIT(ctx, ", %s", as);
            zfree(as);
        }
        if (ft->is_varargs)
        {
            EMIT(ctx, ", ...");
        }
        EMIT(ctx, "))_c.func)(_c.ctx");

        ASTNode *arg = node->call.args;
        while (arg)
        {
            EMIT(ctx, ", ");
            codegen_expression_with_move(ctx, arg);
            arg = arg->next;
        }
        EMIT(ctx, "); })");
        zfree(ret);
        return;
    }

    if (node->call.callee->type == NODE_EXPR_VAR &&
        strcmp(node->call.callee->var_ref.name, "panic") == 0)
    {
        EMIT(ctx, "__zenc_panic");
        goto skip_callee_gen;
    }
    else if (node->call.callee->type == NODE_EXPR_VAR)
    {
        char *name = node->call.callee->var_ref.name;
        char *underscore = strchr(name, '_');
        if (underscore && underscore != name && *(underscore + 1) != '_' &&
            strstr(name, "__") == NULL)
        {
            char base[MAX_TYPE_NAME_LEN];
            size_t len = underscore - name;
            if (len < sizeof(base))
            {
                strncpy(base, name, len);
                base[len] = 0;
                ASTNode *def = find_struct_def(ctx, base);
                int is_common_enum =
                    (strncmp(base, "Result", 6) == 0 || strncmp(base, "Option", 6) == 0 ||
                     strncmp(base, "JsonType", 8) == 0);
                if (is_common_enum || (def && def->type == NODE_ENUM))
                {
                    emit_mangled_name(ctx, base, underscore + 1);
                    goto skip_callee_gen;
                }
            }
        }
    }

    g_emitting_callee = 1;
    codegen_expression(ctx, node->call.callee);
    g_emitting_callee = 0;
skip_callee_gen:
    EMIT(ctx, "(");

    if (node->call.arg_names && node->call.callee->type == NODE_EXPR_VAR)
    {
        ASTNode *arg = node->call.args;
        int first = 1;
        while (arg)
        {
            if (!first)
            {
                EMIT(ctx, ", ");
            }
            first = 0;
            codegen_expression_with_move(ctx, arg);
            arg = arg->next;
        }
    }
    else
    {
        FuncSig *sig = NULL;
        if (node->call.callee->type == NODE_EXPR_VAR)
        {
            sig = find_func(ctx, node->call.callee->var_ref.name);
            if (!sig && !find_struct_def(ctx, node->call.callee->var_ref.name))
            {
                const char *name = node->call.callee->var_ref.name;

                int has_c_interop = ctx->has_external_includes;

                if (!has_c_interop)
                {
                    zmap_iter_ModMap it = zmap_iter_init(ModMap, &ctx->imports.modules);
                    const char *alias;
                    Module *mod;
                    while (zmap_iter_next(&it, &alias, &mod))
                    {
                        if (mod->is_c_header)
                        {
                            has_c_interop = 1;
                            break;
                        }
                    }
                }

                if (!has_c_interop)
                {
                    zmap_iter_FileSet it = zmap_iter_init(FileSet, &ctx->imports.imported_files);
                    const char *key;
                    const char *val;
                    while (zmap_iter_next(&it, &key, &val))
                    {
                        if (key && strstr(key, "/std/"))
                        {
                            has_c_interop = 1;
                            break;
                        }
                    }
                }

                int is_internal = strncmp(name, "_z_", 3) == 0 || strncmp(name, "_Z", 2) == 0;
                int is_extern = is_extern_symbol(ctx, name);
                int is_whitelisted = 0;
                if (ctx->config->c_function_whitelist)
                {
                    char **w = ctx->config->c_function_whitelist;
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
                    char msg[MAX_SHORT_MSG_LEN];
                    snprintf(msg, sizeof(msg), "Undefined function '%s'", name);
                    const char *hint = get_missing_function_hint(ctx, name);
                    zwarn_diag(DIAG_INTEROP_UNDEF_FUNC, t, msg, hint);
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

                if (param_t && param_t->kind == TYPE_ARRAY && param_t->array_size == 0 && arg_t &&
                    arg_t->kind == TYPE_ARRAY && arg_t->array_size > 0)
                {
                    char *inner = type_to_c_string(param_t->inner);
                    EMIT(ctx, "(Slice__%s){.data = ", inner);
                    codegen_expression(ctx, arg);
                    EMIT(ctx, ", .len = %d, .cap = %d}", arg_t->array_size, arg_t->array_size);
                    zfree(inner);
                    handled = 1;
                }
                else if (param_t && param_t->kind == TYPE_STRUCT &&
                         strncmp(param_t->name, "Tuple__", 7) == 0 && sig->total_args == 1 &&
                         node->call.arg_count > 1)
                {
                    EMIT(ctx, "(%s){", param_t->name);
                    ASTNode *curr = arg;
                    int first_field = 1;
                    while (curr)
                    {
                        if (!first_field)
                        {
                            EMIT(ctx, ", ");
                        }
                        first_field = 0;
                        codegen_expression_with_move(ctx, curr);
                        curr = curr->next;
                    }
                    EMIT(ctx, "}");
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
                if (ctx->config->use_cpp && sig && arg_idx < sig->total_args)
                {
                    Type *param_t = sig->arg_types[arg_idx];
                    if (param_t && (param_t->kind == TYPE_POINTER || param_t->kind == TYPE_ENUM))
                    {
                        char *c_type = type_to_c_string(param_t);
                        EMIT(ctx, "(%s)(", c_type);
                        codegen_expression_with_move(ctx, arg);
                        EMIT(ctx, ")");
                        zfree(c_type);
                    }
                    else
                    {
                        codegen_expression_with_move(ctx, arg);
                    }
                }
                else
                {
                    codegen_expression_with_move(ctx, arg);
                }
            }

            if (arg && arg->next)
            {
                EMIT(ctx, ", ");
            }
            if (arg)
            {
                arg = arg->next;
            }
            arg_idx++;
        }
    }
    EMIT(ctx, ")");
}

void codegen_expression(ParserContext *ctx, ASTNode *node)
{
    if (!node)
    {
        return;
    }

    RECURSION_GUARD_TOKEN(ctx, node->token, );

    static const ExprHandler handlers[256] = {
        [NODE_MATCH] = handle_expr_match,
        [NODE_EXPR_BINARY] = handle_expr_binary,
        [NODE_EXPR_VAR] = handle_expr_var,
        [NODE_LAMBDA] = handle_lambda,
        [NODE_EXPR_LITERAL] = handle_expr_literal,
        [NODE_EXPR_CALL] = handle_expr_call,
        [NODE_EXPR_MEMBER] = handle_expr_member,
        [NODE_EXPR_INDEX] = handle_expr_index,
        [NODE_EXPR_SLICE] = handle_expr_slice,
        [NODE_BLOCK] = handle_block,
        [NODE_IF] = handle_if_expr,
        [NODE_TRY] = handle_try_expr,
        [NODE_RAW_STMT] = handle_raw_stmt,
        [NODE_PLUGIN] = handle_plugin,
        [NODE_EXPR_UNARY] = handle_expr_unary,
        [NODE_VA_START] = handle_va_start,
        [NODE_VA_END] = handle_va_end,
        [NODE_VA_COPY] = handle_va_copy,
        [NODE_AST_COMMENT] = handle_ast_comment,
        [NODE_VA_ARG] = handle_va_arg,
        [NODE_EXPR_CAST] = handle_expr_cast,
        [NODE_EXPR_SIZEOF] = handle_expr_sizeof,
        [NODE_TYPEOF] = handle_typeof,
        [NODE_REFLECTION] = handle_reflection,
        [NODE_EXPR_STRUCT_INIT] = handle_expr_struct_init,
        [NODE_EXPR_ARRAY_LITERAL] = handle_expr_array_literal,
        [NODE_EXPR_TUPLE_LITERAL] = handle_expr_tuple_literal,
        [NODE_TERNARY] = handle_ternary,
        [NODE_AWAIT] = handle_await,
    };

    if (node->type >= 0 && node->type < 256 && handlers[node->type])
    {
        handlers[node->type](ctx, node);
        RECURSION_EXIT(ctx);
        return;
    }

    RECURSION_EXIT(ctx);
}

void codegen_expression_bare(ParserContext *ctx, ASTNode *node)
{
    if (!node)
    {
        return;
    }

    RECURSION_GUARD_TOKEN(ctx, node->token, );

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
            codegen_expression(ctx, node->binary.left);
            EMIT(ctx, " %s ", op);
            codegen_expression(ctx, node->binary.right);
            RECURSION_EXIT(ctx);
            return;
        }
    }

    if (node->type == NODE_EXPR_UNARY && node->unary.op)
    {
        if (strcmp(node->unary.op, "_post++") == 0)
        {
            codegen_expression(ctx, node->unary.operand);
            EMIT(ctx, "++");
            RECURSION_EXIT(ctx);
            return;
        }
        if (strcmp(node->unary.op, "_post--") == 0)
        {
            codegen_expression(ctx, node->unary.operand);
            EMIT(ctx, "--");
            RECURSION_EXIT(ctx);
            return;
        }
    }

    codegen_expression(ctx, node);
    RECURSION_EXIT(ctx);
}
