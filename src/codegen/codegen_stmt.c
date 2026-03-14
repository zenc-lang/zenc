
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

char *g_current_func_ret_type = NULL;

// Helper: emit a single pattern condition (either a value, or a range)
static void emit_single_pattern_cond(const char *pat, int id, int is_ptr, FILE *out)
{
    // Check for range pattern: "start..end" or "start..=end"
    char *range_incl = strstr(pat, "..=");
    char *range_excl = strstr(pat, "..");

    if (range_incl)
    {
        // Inclusive range: start..=end -> _m_id >= start && _m_id <= end
        int start_len = (int)(range_incl - pat);
        char *start = xmalloc(start_len + 1);
        strncpy(start, pat, start_len);
        start[start_len] = 0;
        char *end = xstrdup(range_incl + 3);
        if (is_ptr)
        {
            fprintf(out, "(*_m_%d >= %s && *_m_%d <= %s)", id, start, id, end);
        }
        else
        {
            fprintf(out, "(_m_%d >= %s && _m_%d <= %s)", id, start, id, end);
        }
        free(start);
        free(end);
    }
    else if (range_excl)
    {
        // Exclusive range: start..end -> _m_id >= start && _m_id < end
        int start_len = (int)(range_excl - pat);
        char *start = xmalloc(start_len + 1);
        strncpy(start, pat, start_len);
        start[start_len] = 0;
        char *end = xstrdup(range_excl + 2);
        if (is_ptr)
        {
            fprintf(out, "(*_m_%d >= %s && *_m_%d < %s)", id, start, id, end);
        }
        else
        {
            fprintf(out, "(_m_%d >= %s && _m_%d < %s)", id, start, id, end);
        }
        free(start);
        free(end);
    }
    else if (pat[0] == '"')
    {
        // String pattern - string comparison, _m is char* or similar
        if (is_ptr)
        {
            fprintf(out, "strcmp(*_m_%d, %s) == 0", id, pat);
        }
        else
        {
            fprintf(out, "strcmp(_m_%d, %s) == 0", id, pat);
        }
    }
    else
    {
        // Numeric, Char literal, or simple pattern
        if (is_ptr)
        {
            fprintf(out, "*_m_%d == %s", id, pat);
        }
        else
        {
            fprintf(out, "_m_%d == %s", id, pat);
        }
    }
}

// Helper: emit condition for a pattern (may contain OR patterns with '|')
static void emit_pattern_condition(ParserContext *ctx, const char *pattern, int id, int is_ptr,
                                   FILE *out)
{
    // Check if pattern contains '|' for OR patterns
    if (strchr(pattern, '|'))
    {
        // Split by '|' and emit OR conditions
        char *pattern_copy = xstrdup(pattern);
        char *saveptr;
        char *part = strtok_r(pattern_copy, "|", &saveptr);
        int first = 1;
        fprintf(out, "(");
        while (part)
        {
            if (!first)
            {
                fprintf(out, " || ");
            }

            // Check if part is an enum variant
            EnumVariantReg *reg = find_enum_variant(ctx, part);
            if (reg)
            {
                if (is_ptr)
                {
                    fprintf(out, "_m_%d->tag == %d", id, reg->tag_id);
                }
                else
                {
                    fprintf(out, "_m_%d.tag == %d", id, reg->tag_id);
                }
            }
            else
            {
                emit_single_pattern_cond(part, id, is_ptr, out);
            }
            first = 0;
            part = strtok_r(NULL, "|", &saveptr);
        }
        fprintf(out, ")");
        free(pattern_copy);
    }
    else
    {
        // Single pattern (may be a range)
        EnumVariantReg *reg = find_enum_variant(ctx, pattern);
        if (reg)
        {
            if (is_ptr)
            {
                fprintf(out, "_m_%d->tag == %d", id, reg->tag_id);
            }
            else
            {
                fprintf(out, "_m_%d.tag == %d", id, reg->tag_id);
            }
        }
        else
        {
            emit_single_pattern_cond(pattern, id, is_ptr, out);
        }
    }
}

// Helper
static bool is_int_type(TypeKind k)
{
    switch (k)
    {
    case TYPE_CHAR:
    case TYPE_I8:
    case TYPE_U8:
    case TYPE_I16:
    case TYPE_U16:
    case TYPE_I32:
    case TYPE_U32:
    case TYPE_I64:
    case TYPE_U64:
    case TYPE_I128:
    case TYPE_U128:
    case TYPE_INT:
    case TYPE_UINT:
    case TYPE_USIZE:
    case TYPE_ISIZE:
    case TYPE_BYTE:
    case TYPE_RUNE:
    case TYPE_ENUM:
    case TYPE_C_INT:
    case TYPE_C_UINT:
    case TYPE_C_LONG:
    case TYPE_C_ULONG:
    case TYPE_C_LONG_LONG:
    case TYPE_C_ULONG_LONG:
    case TYPE_C_SHORT:
    case TYPE_C_USHORT:
    case TYPE_C_CHAR:
    case TYPE_C_UCHAR:
    case TYPE_BITINT:
    case TYPE_UBITINT:
        return true;
    default:
        return false;
    }
}

void codegen_match_internal(ParserContext *ctx, ASTNode *node, FILE *out, int use_result)
{
    int id = tmp_counter++;
    int is_self = (node->match_stmt.expr->type == NODE_EXPR_VAR &&
                   strcmp(node->match_stmt.expr->var_ref.name, "self") == 0);

    char *ret_type = infer_type(ctx, node);
    int is_expr = (use_result && ret_type && strcmp(ret_type, "void") != 0);

    if (is_expr)
    {
        fprintf(out, "({ ");
    }
    else
    {
        fprintf(out, "{ ");
    }

    // Check if any case uses ref binding - only take address if needed
    int has_ref_binding = 0;
    ASTNode *ref_check = node->match_stmt.cases;
    while (ref_check)
    {
        if (ref_check->match_case.binding_refs)
        {
            for (int i = 0; i < ref_check->match_case.binding_count; i++)
            {
                if (ref_check->match_case.binding_refs[i])
                {
                    has_ref_binding = 1;
                    break;
                }
            }
        }
        if (has_ref_binding)
        {
            break;
        }
        ref_check = ref_check->next;
    }

    int is_lvalue_opt = (node->match_stmt.expr->type == NODE_EXPR_VAR ||
                         node->match_stmt.expr->type == NODE_EXPR_MEMBER ||
                         node->match_stmt.expr->type == NODE_EXPR_INDEX);

    emit_source_mapping(node, out); // Step through match statements elegantly

    if (is_self)
    {
        emit_auto_type(ctx, node->match_stmt.expr, node->token, out);
        fprintf(out, " _m_%d = ", id);
        codegen_expression(ctx, node->match_stmt.expr, out);
        fprintf(out, "; ");
    }
    else if (has_ref_binding && is_lvalue_opt)
    {
        // Take address for ref bindings
        fprintf(out, "ZC_AUTO_INIT(_m_%d, &", id);
        codegen_expression(ctx, node->match_stmt.expr, out);
        fprintf(out, "); ");
    }
    else if (has_ref_binding)
    {
        // Non-lvalue with ref binding: create temporary
        emit_auto_type(ctx, node->match_stmt.expr, node->token, out);
        fprintf(out, " _temp_%d = ", id);
        codegen_expression(ctx, node->match_stmt.expr, out);
        fprintf(out, "; ");

        fprintf(out, "ZC_AUTO_INIT(_m_%d, &_temp_%d); ", id, id);
    }
    else
    {
        // No ref bindings: store value directly (not pointer)
        emit_auto_type(ctx, node->match_stmt.expr, node->token, out);
        fprintf(out, " _m_%d = ", id);
        codegen_expression(ctx, node->match_stmt.expr, out);
        fprintf(out, "; ");
    }

    if (is_expr)
    {
        fprintf(out, "%s _r_%d; ", ret_type, id);
    }

    char *expr_type = infer_type(ctx, node->match_stmt.expr);
    int is_option = (expr_type && strncmp(expr_type, "Option_", 7) == 0);
    int is_result = (expr_type && strncmp(expr_type, "Result_", 7) == 0);

    char *enum_name = NULL;
    ASTNode *chk = node->match_stmt.cases;
    int has_wildcard = 0;
    while (chk)
    {
        if (strcmp(chk->match_case.pattern, "_") == 0)
        {
            has_wildcard = 1;
        }
        else if (!enum_name)
        {
            EnumVariantReg *reg = find_enum_variant(ctx, chk->match_case.pattern);
            if (reg)
            {
                enum_name = reg->enum_name;
            }
        }
        chk = chk->next;
    }

    if (enum_name && !has_wildcard)
    {
        // Iterate through all registered variants for this enum
        EnumVariantReg *v = ctx->enum_variants;
        while (v)
        {
            if (strcmp(v->enum_name, enum_name) == 0)
            {
                int covered = 0;
                ASTNode *c2 = node->match_stmt.cases;
                while (c2)
                {
                    if (strcmp(c2->match_case.pattern, v->variant_name) == 0)
                    {
                        covered = 1;
                        break;
                    }
                    c2 = c2->next;
                }
                if (!covered)
                {
                    zwarn_at(node->token, "Non-exhaustive match: Missing variant '%s'",
                             v->variant_name);
                }
            }
            v = v->next;
        }
    }

    ASTNode *c = node->match_stmt.cases;
    int first = 1;
    while (c)
    {
        if (!first)
        {
            fprintf(out, " else ");
        }
        emit_source_mapping(c, out); // Step through match cases elegantly
        fprintf(out, "if (");
        if (strcmp(c->match_case.pattern, "_") == 0)
        {
            fprintf(out, "1");
        }
        else if (is_option)
        {
            int m_is_ptr = has_ref_binding || (expr_type && strchr(expr_type, '*'));
            const char *acc = m_is_ptr ? "->" : ".";

            if (strcmp(c->match_case.pattern, "Some") == 0)
            {
                fprintf(out, "_m_%d%sis_some", id, acc);
            }
            else if (strcmp(c->match_case.pattern, "None") == 0)
            {
                fprintf(out, "!_m_%d%sis_some", id, acc);
            }
            else
            {
                fprintf(out, "1");
            }
        }
        else if (is_result)
        {
            int m_is_ptr = has_ref_binding || (expr_type && strchr(expr_type, '*'));
            const char *acc = m_is_ptr ? "->" : ".";

            if (strcmp(c->match_case.pattern, "Ok") == 0)
            {
                fprintf(out, "_m_%d%sis_ok", id, acc);
            }
            else if (strcmp(c->match_case.pattern, "Err") == 0)
            {
                fprintf(out, "!_m_%d%sis_ok", id, acc);
            }
            else
            {
                fprintf(out, "1");
            }
        }
        else
        {
            // Use helper for OR patterns, range patterns, and simple patterns
            emit_pattern_condition(ctx, c->match_case.pattern, id, has_ref_binding, out);
        }
        fprintf(out, ") { ");
        if (c->match_case.binding_count > 0)
        {
            for (int i = 0; i < c->match_case.binding_count; i++)
            {
                char *bname = c->match_case.binding_names[i];
                int is_r = c->match_case.binding_refs ? c->match_case.binding_refs[i] : 0;

                if (is_option)
                {
                    if (is_r)
                    {
                        fprintf(out, "ZC_AUTO_INIT(%s, &_m_%d->val); ", bname, id);
                    }
                    else if (has_ref_binding)
                    {
                        fprintf(out, "ZC_AUTO_INIT(%s, _m_%d->val); ", bname, id);
                    }
                    else
                    {
                        fprintf(out, "ZC_AUTO_INIT(%s, _m_%d.val); ", bname, id);
                    }
                }
                else if (is_result)
                {
                    char *field = "val";
                    if (strcmp(c->match_case.pattern, "Err") == 0)
                    {
                        field = "err";
                    }

                    if (is_r)
                    {
                        fprintf(out, "ZC_AUTO_INIT(%s, &_m_%d->%s); ", bname, id, field);
                    }
                    else if (has_ref_binding)
                    {
                        fprintf(out, "ZC_AUTO_INIT(%s, _m_%d->%s); ", bname, id, field);
                    }
                    else
                    {
                        fprintf(out, "ZC_AUTO_INIT(%s, _m_%d.%s); ", bname, id, field);
                    }
                }
                else
                {
                    char *v = strrchr(c->match_case.pattern, '_');
                    if (v)
                    {
                        v++;
                    }
                    else
                    {
                        v = c->match_case.pattern;
                    }

                    if (c->match_case.binding_count > 1)
                    {
                        // Tuple destructuring: data.Variant.vI
                        if (is_r)
                        {
                            fprintf(out, "ZC_AUTO_INIT(%s, &_m_%d->data.%s.v%d); ", bname, id, v,
                                    i);
                        }
                        else if (has_ref_binding)
                        {
                            fprintf(out, "ZC_AUTO_INIT(%s, _m_%d->data.%s.v%d); ", bname, id, v, i);
                        }
                        else
                        {
                            fprintf(out, "ZC_AUTO_INIT(%s, _m_%d.data.%s.v%d); ", bname, id, v, i);
                        }
                    }
                    else
                    {
                        // Single destructuring: data.Variant
                        if (is_r)
                        {
                            fprintf(out, "ZC_AUTO_INIT(%s, &_m_%d->data.%s); ", bname, id, v);
                        }
                        else if (has_ref_binding)
                        {
                            fprintf(out, "ZC_AUTO_INIT(%s, _m_%d->data.%s); ", bname, id, v);
                        }
                        else
                        {
                            fprintf(out, "ZC_AUTO_INIT(%s, _m_%d.data.%s); ", bname, id, v);
                        }
                    }
                }
            }
        }

        // Check if body is a string literal (should auto-print).
        ASTNode *body = c->match_case.body;
        int is_string_literal =
            (body->type == NODE_EXPR_LITERAL && body->literal.type_kind == LITERAL_STRING);

        if (is_expr)
        {
            fprintf(out, "_r_%d = ", id);
            if (is_string_literal)
            {
                codegen_node_single(ctx, body, out);
            }
            else
            {
                if (body->type == NODE_BLOCK)
                {
                    int saved = defer_count;
                    fprintf(out, "({ ");
                    ASTNode *stmt = body->block.statements;
                    while (stmt)
                    {
                        codegen_node_single(ctx, stmt, out);
                        stmt = stmt->next;
                    }
                    for (int i = defer_count - 1; i >= saved; i--)
                    {
                        codegen_node_single(ctx, defer_stack[i], out);
                    }
                    defer_count = saved;
                    fprintf(out, " })");
                }
                else
                {
                    codegen_node_single(ctx, body, out);
                }
            }
            fprintf(out, ";");
        }
        else
        {
            if (is_string_literal)
            {
                char *inner = body->literal.string_val;
                char *code = process_printf_sugar(ctx, body->token, inner, 1, "stdout", NULL, NULL, 0);
                fprintf(out, "%s;", code);
                free(code);
            }
            else
            {
                codegen_node_single(ctx, body, out);
            }
        }

        fprintf(out, " }");
        first = 0;
        c = c->next;
    }

    if (is_expr)
    {
        fprintf(out, " _r_%d; })", id);
    }
    else
    {
        fprintf(out, " }");
    }
}
void codegen_node_single(ParserContext *ctx, ASTNode *node, FILE *out)
{
    if (!node)
    {
        return;
    }

    switch (node->type)
    {
    case NODE_AST_COMMENT:
        fprintf(out, "%s\n", node->comment.content);
        break;
    case NODE_MATCH:
        codegen_match_internal(ctx, node, out, 0); // 0 = statement context
        fprintf(out, ";\n");
        break;
    case NODE_FUNCTION:
        if (!node->func.body || node->func.generic_params)
        {
            break;
        }
        if (node->cfg_condition)
        {
            fprintf(out, "#if %s\n", node->cfg_condition);
        }

        emit_source_mapping(node, out);

        if (node->func.is_async)
        {
            fprintf(out, "struct %s_Args {\n", node->func.name);
            char *args_copy = xstrdup(node->func.args);
            char *token = strtok(args_copy, ",");
            int arg_count = 0;
            char **arg_names = xmalloc(32 * sizeof(char *));

            while (token)
            {
                while (*token == ' ')
                {
                    token++; // trim leading
                }
                char *last_space = strrchr(token, ' ');
                if (last_space)
                {
                    *last_space = 0;
                    char *type = token;
                    char *name = last_space + 1;
                    fprintf(out, "%s %s;\n", type, name);

                    arg_names[arg_count++] = xstrdup(name);
                }
                token = strtok(NULL, ",");
            }
            free(args_copy);
            fprintf(out, "};\n");

            fprintf(out, "void* _runner_%s(void* _args)\n", node->func.name);
            fprintf(out, "{\n");
            fprintf(out, "    struct %s_Args* args = (struct %s_Args*)_args;\n", node->func.name,
                    node->func.name);

            // Determine mechanism: struct/large-type? -> malloc; primitive -> cast
            int returns_struct = 0;
            char *rt = node->func.ret_type;
            if (strstr(rt, "*") == NULL && strcmp(rt, "string") != 0 && strcmp(rt, "void") != 0 &&
                strcmp(rt, "Async") != 0)
            {
                if (is_struct_return_type(rt))
                {
                    returns_struct = 1;
                }
            }

            // Call Impl
            if (returns_struct)
            {
                fprintf(out, "    %s *res_ptr = malloc(sizeof(%s));\n", rt, rt);
                fprintf(out, "    *res_ptr = ");
            }
            else if (strcmp(rt, "void") != 0 && strcmp(rt, "Async") != 0)
            {
                fprintf(out, "    %s res = ", rt);
            }
            else
            {
                fprintf(out, "    ");
            }

            fprintf(out, "_impl_%s(", node->func.name);
            for (int i = 0; i < arg_count; i++)
            {
                fprintf(out, "%sargs->%s", i > 0 ? ", " : "", arg_names[i]);
            }
            fprintf(out, ");\n");
            fprintf(out, "    free(args);\n");

            if (returns_struct)
            {
                fprintf(out, "    return (void*)res_ptr;\n");
            }
            else if (strcmp(rt, "void") != 0)
            {
                fprintf(out, "    return (void*)(uintptr_t)res;\n");
            }
            else
            {
                fprintf(out, "    return NULL;\n");
            }
            fprintf(out, "}\n");

            fprintf(out, "%s _impl_%s(%s)\n", node->func.ret_type, node->func.name,
                    node->func.args);
            fprintf(out, "{\n");
            defer_count = 0;
            codegen_walker(ctx, node->func.body, out);
            for (int i = defer_count - 1; i >= 0; i--)
            {
                codegen_node_single(ctx, defer_stack[i], out);
            }
            fprintf(out, "}\n");

            // 4. Define Public Wrapper (Spawns Thread)
            fprintf(out, "Async %s(%s)\n", node->func.name, node->func.args);
            fprintf(out, "{\n");
            fprintf(out, "    struct %s_Args* args = malloc(sizeof(struct %s_Args));\n",
                    node->func.name, node->func.name);
            for (int i = 0; i < arg_count; i++)
            {
                fprintf(out, "    args->%s = %s;\n", arg_names[i], arg_names[i]);
            }

            fprintf(out, "    pthread_t th;\n");
            fprintf(out, "    pthread_create(&th, NULL, _runner_%s, args);\n", node->func.name);
            fprintf(out, "    return (Async){.thread=th, .result=NULL};\n");
            fprintf(out, "}\n");

            if (node->cfg_condition)
            {
                fprintf(out, "#endif\n");
            }
            break;
        }

        defer_count = 0;
        fprintf(out, "\n");

        // Emit GCC attributes before function
        {
            int has_attrs = node->func.constructor || node->func.destructor ||
                            node->func.noinline || node->func.unused || node->func.weak ||
                            node->func.cold || node->func.hot || node->func.noreturn ||
                            node->func.pure || node->func.section || node->func.is_export;
            if (has_attrs)
            {
                fprintf(out, "__attribute__((");
                int first = 1;
#define EMIT_ATTR(cond, name)                                                                      \
    if (cond)                                                                                      \
    {                                                                                              \
        if (!first)                                                                                \
            fprintf(out, ", ");                                                                    \
        fprintf(out, name);                                                                        \
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
                        fprintf(out, ", ");
                    }
                    fprintf(out, "section(\"%s\")", node->func.section);
                }

                Attribute *custom = node->func.attributes;
                while (custom)
                {
                    if (!first)
                    {
                        fprintf(out, ", ");
                    }
                    fprintf(out, "%s", custom->name);
                    if (custom->arg_count > 0)
                    {
                        fprintf(out, "(");
                        for (int i = 0; i < custom->arg_count; i++)
                        {
                            if (i > 0)
                            {
                                fprintf(out, ", ");
                            }
                            fprintf(out, "%s", custom->args[i]);
                        }
                        fprintf(out, ")");
                    }
                    first = 0;
                    custom = custom->next;
                }

#undef EMIT_ATTR
                fprintf(out, ")) ");
            }
            else if (node->func.attributes)
            {
                // Handle case where specific attributes are missing but custom ones exist
                fprintf(out, "__attribute__((");
                int first = 1;
                Attribute *custom = node->func.attributes;
                while (custom)
                {
                    if (!first)
                    {
                        fprintf(out, ", ");
                    }
                    fprintf(out, "%s", custom->name);
                    if (custom->arg_count > 0)
                    {
                        fprintf(out, "(");
                        for (int i = 0; i < custom->arg_count; i++)
                        {
                            if (i > 0)
                            {
                                fprintf(out, ", ");
                            }
                            fprintf(out, "%s", custom->args[i]);
                        }
                        fprintf(out, ")");
                    }
                    first = 0;
                    custom = custom->next;
                }
                fprintf(out, ")) ");
            }
        }

        if (node->func.is_inline)
        {
            fprintf(out, "inline ");
        }
        emit_func_signature(ctx, out, node, NULL);
        fprintf(out, "\n{\n");
        char *prev_ret = g_current_func_ret_type;
        g_current_func_ret_type = node->func.ret_type;

        // Initialize drop flags for arguments that implement Drop
        for (int i = 0; i < node->func.arg_count; i++)
        {
            Type *arg_type = node->func.arg_types[i];
            char *arg_name = node->func.param_names[i];
            if (arg_type && arg_name)
            {
                // Check if type implements Drop
                int has_drop = 0;
                char *drop_type_name = NULL;
                Token drop_token;

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
                        if (def && def->type == NODE_STRUCT && def->type_info &&
                            def->type_info->traits.has_drop)
                        {
                            has_drop = 1;
                            drop_type_name = arg_type->name;
                        }
                    }
                }

                if (has_drop)
                {
                    emit_source_mapping_duplicate(node, out);
                    fprintf(out, "    int __z_drop_flag_%s = 1;\n", arg_name);

                    ASTNode *defer_node = xmalloc(sizeof(ASTNode));
                    defer_node->token = node->token;
                    defer_node->type = NODE_RAW_STMT;
                    char *stmt_str = NULL;
                    if (arg_type->kind == TYPE_FUNCTION)
                    {
                        stmt_str = xmalloc(256 + strlen(arg_name) * 2);
                        sprintf(stmt_str, "if (__z_drop_flag_%s && %s.drop) %s.drop(%s.ctx);",
                                arg_name, arg_name, arg_name, arg_name);
                    }
                    else
                    {
                        stmt_str = xmalloc(256 + strlen(arg_name) * 2 + strlen(drop_type_name));
                        sprintf(stmt_str, "if (__z_drop_flag_%s) %s__Drop_glue(&%s);", arg_name,
                                drop_type_name, arg_name);
                    }
                    defer_node->raw_stmt.content = stmt_str;

                    if (defer_count < MAX_DEFER)
                    {
                        defer_stack[defer_count++] = defer_node;
                    }
                }
            }
        }

        codegen_walker(ctx, node->func.body, out);
        for (int i = defer_count - 1; i >= 0; i--)
        {
            codegen_node_single(ctx, defer_stack[i], out);
        }
        g_current_func_ret_type = prev_ret;

        fprintf(out, "}\n");
        if (node->cfg_condition)
        {
            fprintf(out, "#endif\n");
        }
        break;

    case NODE_ASSERT:
        fprintf(out, "assert(");
        codegen_expression(ctx, node->assert_stmt.condition, out);
        if (node->assert_stmt.message)
        {
            fprintf(out, ", %s", node->assert_stmt.message);
        }
        else
        {
            fprintf(out, ", \"Assertion failed\"");
        }
        fprintf(out, ");\n");
        break;

    case NODE_DEFER:
        if (defer_count < MAX_DEFER)
        {
            defer_stack[defer_count++] = node->defer_stmt.stmt;
        }
        break;
    case NODE_IMPL:
    {
        char *sname = node->impl.struct_name;
        TypeAlias *ta = find_type_alias_node(ctx, sname);
        const char *resolved = (ta && !ta->is_opaque) ? ta->original_type : NULL;

        // If this is an opaque alias, rename method func.name prefixes before codegen
        if (resolved)
        {
            int slen = strlen(sname);
            ASTNode *m = node->impl.methods;
            while (m)
            {
                if (m->type == NODE_FUNCTION && m->func.name &&
                    strncmp(m->func.name, sname, slen) == 0 && m->func.name[slen] == '_' &&
                    m->func.name[slen + 1] == '_')
                {
                    const char *method_part = m->func.name + slen;
                    char *new_name = xmalloc(strlen(resolved) + strlen(method_part) + 1);
                    sprintf(new_name, "%s%s", resolved, method_part);
                    free(m->func.name);
                    m->func.name = new_name;
                }
                m = m->next;
            }
            g_current_impl_type = (char *)resolved;
        }
        else
        {
            g_current_impl_type = sname;
        }

        codegen_walker(ctx, node->impl.methods, out);
        g_current_impl_type = NULL;
        break;
    }
    case NODE_IMPL_TRAIT:
    {
        char *sname = node->impl_trait.target_type;
        TypeAlias *ta = find_type_alias_node(ctx, sname);
        const char *resolved = (ta && !ta->is_opaque) ? ta->original_type : NULL;

        if (resolved)
        {
            int slen = strlen(sname);
            ASTNode *m = node->impl_trait.methods;
            while (m)
            {
                if (m->type == NODE_FUNCTION && m->func.name &&
                    strncmp(m->func.name, sname, slen) == 0 && m->func.name[slen] == '_' &&
                    m->func.name[slen + 1] == '_')
                {
                    const char *method_part = m->func.name + slen;
                    char *new_name = xmalloc(strlen(resolved) + strlen(method_part) + 1);
                    sprintf(new_name, "%s%s", resolved, method_part);
                    free(m->func.name);
                    m->func.name = new_name;
                }
                m = m->next;
            }
            g_current_impl_type = (char *)resolved;
        }
        else
        {
            g_current_impl_type = sname;
        }

        codegen_walker(ctx, node->impl_trait.methods, out);
        g_current_impl_type = NULL;
        break;
    }
    case NODE_DESTRUCT_VAR:
    {
        int id = tmp_counter++;
        fprintf(out, "    ");
        emit_auto_type(ctx, node->destruct.init_expr, node->token, out);
        fprintf(out, " _tmp_%d = ", id);
        codegen_expression(ctx, node->destruct.init_expr, out);
        fprintf(out, ";\n");

        if (node->destruct.is_guard)
        {
            // var Some(val) = opt else ...
            char *variant = node->destruct.guard_variant;
            char *check = "val"; // field to access

            emit_source_mapping_duplicate(node, out);
            if (strcmp(variant, "Some") == 0)
            {
                fprintf(out, "    if (!_tmp_%d.is_some) {\n", id);
            }
            else if (strcmp(variant, "Ok") == 0)
            {
                fprintf(out, "    if (!_tmp_%d.is_ok) {\n", id);
            }
            else if (strcmp(variant, "Err") == 0)
            {
                fprintf(out, "    if (_tmp_%d.is_ok) {\n", id); // Err if NOT ok
                check = "err";
            }
            else
            {
                // Generic guard? Assume .is_variant present?
                fprintf(out, "    if (!_tmp_%d.is_%s) {\n", id, variant);
            }

            // Else block
            codegen_walker(ctx, node->destruct.else_block->block.statements, out);
            fprintf(out, "    }\n");

            // Bind value
            emit_source_mapping_duplicate(node, out);
            if (strstr(g_config.cc, "tcc"))
            {
                fprintf(out, "    __typeof__(_tmp_%d.%s) %s = _tmp_%d.%s;\n", id, check,
                        node->destruct.names[0], id, check);
            }
            else
            {
                fprintf(out, "    ZC_AUTO %s = _tmp_%d.%s;\n", node->destruct.names[0], id, check);
            }
        }
        else
        {
            for (int i = 0; i < node->destruct.count; i++)
            {
                emit_source_mapping_duplicate(node, out);
                if (node->destruct.is_struct_destruct)
                {
                    char *field = node->destruct.field_names ? node->destruct.field_names[i]
                                                             : node->destruct.names[i];
                    if (strstr(g_config.cc, "tcc"))
                    {
                        fprintf(out, "    __typeof__(_tmp_%d.%s) %s = _tmp_%d.%s;\n", id, field,
                                node->destruct.names[i], id, field);
                    }
                    else
                    {
                        fprintf(out, "    ZC_AUTO %s = _tmp_%d.%s;\n", node->destruct.names[i], id,
                                field);
                    }
                }
                else
                {
                    // Tuple destructuring: access .v0, .v1, etc.
                    char *explicit_type = node->destruct.types ? node->destruct.types[i] : NULL;
                    if (explicit_type)
                    {
                        // Use explicit type annotation
                        fprintf(out, "    %s %s = _tmp_%d.v%d;\n", explicit_type,
                                node->destruct.names[i], id, i);
                    }
                    else if (strstr(g_config.cc, "tcc"))
                    {
                        fprintf(out, "    __typeof__(_tmp_%d.v%d) %s = _tmp_%d.v%d;\n", id, i,
                                node->destruct.names[i], id, i);
                    }
                    else
                    {
                        fprintf(out, "    ZC_AUTO %s = _tmp_%d.v%d;\n", node->destruct.names[i], id,
                                i);
                    }
                }
            }
        }
        break;
    }
    case NODE_BLOCK:
    {
        int saved = defer_count;
        fprintf(out, "    {\n");
        codegen_walker(ctx, node->block.statements, out);
        for (int i = defer_count - 1; i >= saved; i--)
        {
            codegen_node_single(ctx, defer_stack[i], out);
        }
        defer_count = saved;
        fprintf(out, "    }\n");
        break;
    }
    case NODE_VAR_DECL:
    {
        // Save pending closure free count — if the init expr creates a closure,
        // the variable takes ownership and we should NOT free the context.
        int saved_closure_frees = pending_closure_free_count;

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
                fprintf(out, "    ");
                codegen_expression(ctx, node->var_decl.init_expr, out);
                fprintf(out, ";\n");
                break;
            }
        }
        fprintf(out, "    ");
        if (node->var_decl.is_static)
        {
            fprintf(out, "static ");
        }
        if (node->var_decl.is_autofree)
        {
            fprintf(out, "__attribute__((cleanup(_z_autofree_impl))) ");
        }
        {
            char *tname = NULL;
            if (node->type_info &&
                (!node->var_decl.init_expr || node->var_decl.init_expr->type != NODE_AWAIT))
            {
                tname = codegen_type_to_string(node->type_info);
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
                    // Drop Flag: int __z_drop_flag_name = 1;
                    fprintf(out, "int __z_drop_flag_%s = 1; ", node->var_decl.name);

                    // Synthesize Defer: if (__z_drop_flag_name) Name__Drop_drop(&name);
                    ASTNode *defer_node = xmalloc(sizeof(ASTNode));
                    defer_node->type = NODE_RAW_STMT;
                    defer_node->token = node->token;
                    char *stmt_str =
                        xmalloc(256 + strlen(node->var_decl.name) * 2 + strlen(clean_type));
                    sprintf(stmt_str, "if (__z_drop_flag_%s) %s__Drop_glue(&%s);",
                            node->var_decl.name, clean_type, node->var_decl.name);
                    defer_node->raw_stmt.content = stmt_str;
                    defer_node->line = node->line;

                    if (defer_count < MAX_DEFER)
                    {
                        defer_stack[defer_count++] = defer_node;
                    }
                }

                // Emit Variable with Type
                emit_var_decl_type(ctx, out, tname, node->var_decl.name);
                add_symbol(ctx, node->var_decl.name, tname, node->type_info);

                if (node->var_decl.init_expr)
                {
                    fprintf(out, " = ");
                    codegen_expression(ctx, node->var_decl.init_expr, out);
                }
                else if (node->type_info)
                {
                    TypeKind k = node->type_info->kind;
                    if (k == TYPE_ARRAY || k == TYPE_STRUCT)
                    {
                        fprintf(out, " = %s", g_config.use_cpp ? "{}" : "{0}");
                    }
                    else if (is_int_type(k))
                    {
                        fprintf(out, " = 0");
                    }
                    else if (k == TYPE_F32 || k == TYPE_FLOAT)
                    {
                        fprintf(out, " = 0.0f");
                    }
                    else if (k == TYPE_F64)
                    {
                        fprintf(out, " = 0.0");
                    }
                    else if (k == TYPE_BOOL)
                    {
                        fprintf(out, " = false");
                    }
                }
                fprintf(out, ";\n");
                if (node->var_decl.init_expr &&
                    emit_move_invalidation(ctx, node->var_decl.init_expr, out))
                {
                    fprintf(out, ";\n");
                }

                if (node->type_info)
                {
                    free(tname); // Free if allocated by codegen_type_to_string
                }
            }
            else
            {
                // Inference Fallback
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
                        // Drop Flag: int __z_drop_flag_name = 1;
                        fprintf(out, "int __z_drop_flag_%s = 1; ", node->var_decl.name);

                        // Synthesize Defer: if (__z_drop_flag_name) Name__Drop_drop(&name);
                        ASTNode *defer_node = xmalloc(sizeof(ASTNode));
                        defer_node->type = NODE_RAW_STMT;
                        defer_node->token = node->token;
                        // Build string
                        char *stmt_str = NULL;
                        if (node->var_decl.init_expr && node->var_decl.init_expr->type_info &&
                            node->var_decl.init_expr->type_info->kind == TYPE_FUNCTION)
                        {
                            stmt_str = xmalloc(256 + strlen(node->var_decl.name) * 2);
                            sprintf(stmt_str, "if (__z_drop_flag_%s && %s.drop) %s.drop(%s.ctx);",
                                    node->var_decl.name, node->var_decl.name, node->var_decl.name,
                                    node->var_decl.name);
                        }
                        else
                        {
                            char *clean_name = xstrdup(clean_type);
                            if (strncmp(clean_name, "struct ", 7) == 0)
                            {
                                memmove(clean_name, clean_name + 7, strlen(clean_name) - 6);
                            }
                            stmt_str =
                                xmalloc(256 + strlen(node->var_decl.name) * 2 + strlen(clean_name));
                            sprintf(stmt_str, "if (__z_drop_flag_%s) %s__Drop_glue(&%s);",
                                    node->var_decl.name, clean_name, node->var_decl.name);
                            free(clean_name);
                        }
                        defer_node->raw_stmt.content = stmt_str;
                        defer_node->line = node->line;

                        // Push to defer stack
                        if (defer_count < MAX_DEFER)
                        {
                            defer_stack[defer_count++] = defer_node;
                        }
                    }

                    emit_var_decl_type(ctx, out, inferred, node->var_decl.name);
                    add_symbol(ctx, node->var_decl.name, inferred, NULL);
                    fprintf(out, " = ");
                    codegen_expression(ctx, node->var_decl.init_expr, out);
                    fprintf(out, ";\n");

                    if (node->var_decl.init_expr &&
                        emit_move_invalidation(ctx, node->var_decl.init_expr, out))
                    {
                        fprintf(out, ";\n");
                    }
                }
                else
                {
                    emit_auto_type(ctx, node->var_decl.init_expr, node->token, out);
                    fprintf(out, " %s", node->var_decl.name);

                    if (inferred)
                    {
                        add_symbol(ctx, node->var_decl.name, inferred, NULL);
                    }

                    fprintf(out, " = ");
                    codegen_expression(ctx, node->var_decl.init_expr, out);
                    fprintf(out, ";\n");
                    if (node->var_decl.init_expr &&
                        emit_move_invalidation(ctx, node->var_decl.init_expr, out))
                    {
                        fprintf(out, ";\n");
                    }
                }
            }
        }

        // Suppress pending frees - variable owns the closure context
        pending_closure_free_count = saved_closure_frees;
        break;
    }
    case NODE_CONST:
        fprintf(out, "    const ");
        if (node->var_decl.type_str)
        {
            fprintf(out, "%s %s", node->var_decl.type_str, node->var_decl.name);
        }
        else
        {
            emit_auto_type(ctx, node->var_decl.init_expr, node->token, out);
            fprintf(out, " %s", node->var_decl.name);
        }
        fprintf(out, " = ");
        codegen_expression(ctx, node->var_decl.init_expr, out);
        fprintf(out, ";\n");
        break;
    case NODE_FIELD:
        if (node->field.bit_width > 0)
        {
            fprintf(out, "    %s %s : %d;\n", node->field.type, node->field.name,
                    node->field.bit_width);
        }
        else
        {
            fprintf(out, "    ");
            emit_var_decl_type(ctx, out, node->field.type, node->field.name);
            fprintf(out, ";\n");
        }
        break;
    case NODE_IF:
        fprintf(out, "if (");
        codegen_expression(ctx, node->if_stmt.condition, out);
        fprintf(out, ") ");
        codegen_node_single(ctx, node->if_stmt.then_body, out);
        if (node->if_stmt.else_body)
        {
            fprintf(out, " else ");
            codegen_node_single(ctx, node->if_stmt.else_body, out);
        }
        break;
    case NODE_UNLESS:
        fprintf(out, "if (!(");
        codegen_expression(ctx, node->unless_stmt.condition, out);
        fprintf(out, ")) ");
        codegen_node_single(ctx, node->unless_stmt.body, out);
        break;
    case NODE_GUARD:
        fprintf(out, "if (!(");
        codegen_expression(ctx, node->guard_stmt.condition, out);
        fprintf(out, ")) ");
        codegen_node_single(ctx, node->guard_stmt.body, out);
        break;
    case NODE_WHILE:
    {
        loop_defer_boundary[loop_depth++] = defer_count;
        fprintf(out, "while (");
        codegen_expression(ctx, node->while_stmt.condition, out);
        fprintf(out, ") ");
        codegen_node_single(ctx, node->while_stmt.body, out);
        loop_depth--;
        break;
    }
    case NODE_FOR:
    {
        loop_defer_boundary[loop_depth++] = defer_count;
        fprintf(out, "for (");
        if (node->for_stmt.init)
        {
            if (node->for_stmt.init->type == NODE_VAR_DECL)
            {
                ASTNode *v = node->for_stmt.init;
                if (v->var_decl.type_str && strcmp(v->var_decl.type_str, "__auto_type") != 0)
                {
                    fprintf(out, "%s %s = (%s)(", v->var_decl.type_str, v->var_decl.name,
                            v->var_decl.type_str);
                    codegen_expression(ctx, v->var_decl.init_expr, out);
                    fprintf(out, ")");
                }
                else
                {
                    emit_auto_type(ctx, v->var_decl.init_expr, v->token, out);
                    fprintf(out, " %s = ", v->var_decl.name);
                    codegen_expression(ctx, v->var_decl.init_expr, out);
                }
            }
            else
            {
                codegen_expression(ctx, node->for_stmt.init, out);
            }
        }
        fprintf(out, "; ");
        if (node->for_stmt.condition)
        {
            codegen_expression_bare(ctx, node->for_stmt.condition, out);
        }
        fprintf(out, "; ");
        if (node->for_stmt.step)
        {
            codegen_expression_bare(ctx, node->for_stmt.step, out);
        }
        fprintf(out, ") ");
        codegen_node_single(ctx, node->for_stmt.body, out);
        loop_depth--;
        break;
    }
    case NODE_BREAK:
        // Run defers from current scope down to loop boundary before breaking
        if (loop_depth > 0)
        {
            int boundary = loop_defer_boundary[loop_depth - 1];
            for (int i = defer_count - 1; i >= boundary; i--)
            {
                codegen_node_single(ctx, defer_stack[i], out);
            }
        }
        if (node->break_stmt.target_label)
        {
            fprintf(out, "goto __break_%s;\n", node->break_stmt.target_label);
        }
        else
        {
            fprintf(out, "break;\n");
        }
        break;
    case NODE_CONTINUE:
        // Run defers from current scope down to loop boundary before continuing
        if (loop_depth > 0)
        {
            int boundary = loop_defer_boundary[loop_depth - 1];
            for (int i = defer_count - 1; i >= boundary; i--)
            {
                codegen_node_single(ctx, defer_stack[i], out);
            }
        }
        if (node->continue_stmt.target_label)
        {
            fprintf(out, "goto __continue_%s;\n", node->continue_stmt.target_label);
        }
        else
        {
            fprintf(out, "continue;\n");
        }
        break;
    case NODE_GOTO:
        if (node->goto_stmt.goto_expr)
        {
            // Computed goto: goto *expr;
            fprintf(out, "goto *(");
            codegen_expression(ctx, node->goto_stmt.goto_expr, out);
            fprintf(out, ");\n");
        }
        else
        {
            fprintf(out, "goto %s;\n", node->goto_stmt.label_name);
        }
        break;
    case NODE_LABEL:
        fprintf(out, "%s:;\n", node->label_stmt.label_name);
        break;
    case NODE_DO_WHILE:
    {
        loop_defer_boundary[loop_depth++] = defer_count;
        fprintf(out, "do ");
        codegen_node_single(ctx, node->do_while_stmt.body, out);
        fprintf(out, " while (");
        codegen_expression(ctx, node->do_while_stmt.condition, out);
        fprintf(out, ");\n");
        loop_depth--;
        break;
    }
    // Loop constructs: loop, repeat, for-in
    case NODE_LOOP:
    {
        // loop { ... } => while (1) { ... }
        loop_defer_boundary[loop_depth++] = defer_count;
        fprintf(out, "while (1) ");
        codegen_node_single(ctx, node->loop_stmt.body, out);
        loop_depth--;
        break;
    }
    case NODE_REPEAT:
    {
        loop_defer_boundary[loop_depth++] = defer_count;
        fprintf(out, "for (int _rpt_i = 0; _rpt_i < (%s); _rpt_i++) ", node->repeat_stmt.count);
        codegen_node_single(ctx, node->repeat_stmt.body, out);
        loop_depth--;
        break;
    }
    case NODE_FOR_RANGE:
    {
        // Track loop entry for defer boundary
        loop_defer_boundary[loop_depth++] = defer_count;

        fprintf(out, "for (");
        if (strstr(g_config.cc, "tcc"))
        {
            fprintf(out, "__typeof__((");
            codegen_expression(ctx, node->for_range.start, out);
            fprintf(out, ")) %s = ", node->for_range.var_name);
        }
        else
        {
            fprintf(out, "ZC_AUTO %s = ", node->for_range.var_name);
        }
        codegen_expression(ctx, node->for_range.start, out);
        if (node->for_range.step && node->for_range.step[0] == '-')
        {
            if (node->for_range.is_inclusive)
            {
                fprintf(out, "; %s >= ", node->for_range.var_name);
            }
            else
            {
                fprintf(out, "; %s > ", node->for_range.var_name);
            }
        }
        else
        {
            if (node->for_range.is_inclusive)
            {
                fprintf(out, "; %s <= ", node->for_range.var_name);
            }
            else
            {
                fprintf(out, "; %s < ", node->for_range.var_name);
            }
        }
        codegen_expression(ctx, node->for_range.end, out);
        fprintf(out, "; %s", node->for_range.var_name);
        if (node->for_range.step)
        {
            fprintf(out, " += %s) ", node->for_range.step);
        }
        else
        {
            fprintf(out, "++) ");
        }
        codegen_node_single(ctx, node->for_range.body, out);

        loop_depth--;
        break;
    }
    case NODE_ASM:
    {
        int is_extended = (node->asm_stmt.num_outputs > 0 || node->asm_stmt.num_inputs > 0 ||
                           node->asm_stmt.num_clobbers > 0);

        if (node->asm_stmt.is_volatile)
        {
            fprintf(out, "    __asm__ __volatile__(");
        }
        else
        {
            fprintf(out, "    __asm__(");
        }

        char *code = node->asm_stmt.code;
        char *transformed = xmalloc(strlen(code) * 3); // Generous buffer
        char *dst = transformed;

        for (char *p = code; *p; p++)
        {
            if (*p == '{')
            {
                // Find matching }
                char *end = strchr(p + 1, '}');
                if (end)
                {
                    // Extract variable name
                    int var_len = end - p - 1;
                    char var_name[64];
                    strncpy(var_name, p + 1, var_len);
                    var_name[var_len] = 0;

                    // Find variable index
                    int idx = -1;

                    // Check outputs first
                    for (int i = 0; i < node->asm_stmt.num_outputs; i++)
                    {
                        if (strcmp(node->asm_stmt.outputs[i], var_name) == 0)
                        {
                            idx = i;
                            break;
                        }
                    }

                    // Then check inputs
                    if (idx == -1)
                    {
                        for (int i = 0; i < node->asm_stmt.num_inputs; i++)
                        {
                            if (strcmp(node->asm_stmt.inputs[i], var_name) == 0)
                            {
                                idx = node->asm_stmt.num_outputs + i;
                                break;
                            }
                        }
                    }

                    if (idx >= 0)
                    {
                        // Replace with %N
#if defined(ZC_ARCH_ARM64)
                        // Use most optimal register size on arm architectures
                        if (node->asm_stmt.register_size <= 32)
                        {
                            dst += sprintf(dst, "%%w%d", idx);
                        }
                        else
#endif
                        {
                            dst += sprintf(dst, "%%%d", idx);
                        }
                    }
                    else
                    {
                        // Variable not found - error or keep as-is?
                        dst += sprintf(dst, "{%s}", var_name);
                    }

                    p = end; // Skip past }
                }
                else
                {
                    *dst++ = *p;
                }
            }
            else if (*p == '%')
            {
                if (is_extended)
                {
                    *dst++ = '%';
                    *dst++ = '%';
                }
                else
                {
                    *dst++ = '%';
                }
            }
            else
            {
                *dst++ = *p;
            }
        }
        *dst = 0;

        fprintf(out, "\"");
        for (char *p = transformed; *p; p++)
        {
            if (*p == '\n')
            {
                fprintf(out, "\\n\"\n        \"");
            }
            else if (*p == '"')
            {
                fprintf(out, "\\\"");
            }
            else if (*p == '\\')
            {
                fprintf(out, "\\\\");
            }
            else
            {
                fputc(*p, out);
            }
        }
        fprintf(out, "\\n\"");

        if (node->asm_stmt.num_outputs > 0)
        {
            fprintf(out, "\n        : ");
            for (int i = 0; i < node->asm_stmt.num_outputs; i++)
            {
                if (i > 0)
                {
                    fprintf(out, ", ");
                }

                // Determine constraint
                char *mode = node->asm_stmt.output_modes[i];
                if (strcmp(mode, "out") == 0)
                {
                    fprintf(out, "\"=r\"(%s)", node->asm_stmt.outputs[i]);
                }
                else if (strcmp(mode, "inout") == 0)
                {
                    fprintf(out, "\"+r\"(%s)", node->asm_stmt.outputs[i]);
                }
                else
                {
                    fprintf(out, "\"=r\"(%s)", node->asm_stmt.outputs[i]);
                }
            }
        }

        if (node->asm_stmt.num_inputs > 0)
        {
            fprintf(out, "\n        : ");
            for (int i = 0; i < node->asm_stmt.num_inputs; i++)
            {
                if (i > 0)
                {
                    fprintf(out, ", ");
                }
                fprintf(out, "\"r\"(%s)", node->asm_stmt.inputs[i]);
            }
        }
        else if (node->asm_stmt.num_outputs > 0)
        {
            fprintf(out, "\n        : ");
        }

        if (node->asm_stmt.num_clobbers > 0)
        {
            fprintf(out, "\n        : ");
            for (int i = 0; i < node->asm_stmt.num_clobbers; i++)
            {
                if (i > 0)
                {
                    fprintf(out, ", ");
                }
                fprintf(out, "\"%s\"", node->asm_stmt.clobbers[i]);
            }
        }

        fprintf(out, ");\n");
        break;
    }
    case NODE_RETURN:
    {
        // Suppress pending closure frees — returned closures escape the scope
        pending_closure_free_count = 0;
        int has_defers = (defer_count > func_defer_boundary);
        int handled = 0;

        if (node->ret.value && node->ret.value->type == NODE_EXPR_ARRAY_LITERAL &&
            g_current_func_ret_type && strncmp(g_current_func_ret_type, "Slice_", 6) == 0)
        {
            // Heap allocation for slice literals to prevent use-after-return
            ASTNode *arr = node->ret.value;
            int count = arr->array_literal.count;
            char *elem_type = "void*"; // fallback

            // Prioritize the function return type (Slice_T) to determine the pointer type
            // This prevents "incompatible pointer type" errors in C when returning literals of
            // different types
            if (g_current_func_ret_type && strncmp(g_current_func_ret_type, "Slice_", 6) == 0)
            {
                elem_type = xstrdup(g_current_func_ret_type + 6);
            }
            else if (arr->array_literal.elements && arr->array_literal.elements->type_info)
            {
                elem_type = codegen_type_to_string(arr->array_literal.elements->type_info);
            }
            else if (arr->type_info && arr->type_info->inner)
            {
                elem_type = codegen_type_to_string(arr->type_info->inner);
            }
            else
            {
                elem_type = xstrdup("void*");
            }

            fprintf(out, "    { %s *_tmp_arr = malloc(%d * sizeof(%s));\n", elem_type, count,
                    elem_type);

            ASTNode *elem = arr->array_literal.elements;
            int idx = 0;
            while (elem)
            {
                fprintf(out, "    _tmp_arr[%d] = ", idx++);
                codegen_expression(ctx, elem, out);
                fprintf(out, ";\n");
                elem = elem->next;
            }

            fprintf(out, "    return (%s){.data = _tmp_arr, .len = %d, .cap = %d};\n",
                    g_current_func_ret_type, count, count);
            fprintf(out, "    }\n");
            handled = 1;
        }

        if (node->ret.value && node->ret.value->type == NODE_EXPR_VAR)
        {
            char *tname = infer_type(ctx, node->ret.value);
            if (tname)
            {
                char *clean = tname;
                if (strncmp(clean, "struct ", 7) == 0)
                {
                    clean += 7;
                }

                ASTNode *def = find_struct_def(ctx, clean);
                if (def && def->type_info && def->type_info->traits.has_drop)
                {
                    fprintf(out, "    return ({ ");
                    if (strstr(g_config.cc, "tcc"))
                    {
                        fprintf(out, "__typeof__(");
                        codegen_expression(ctx, node->ret.value, out);
                        fprintf(out, ")");
                    }
                    else
                    {
                        fprintf(out, "ZC_AUTO");
                    }
                    fprintf(out, " _z_ret_mv = ");
                    codegen_expression(ctx, node->ret.value, out);
                    fprintf(out, "; memset(&");
                    codegen_expression(ctx, node->ret.value, out);
                    fprintf(out, ", 0, sizeof(_z_ret_mv)); ");
                    fprintf(out, "__z_drop_flag_%s = 0; ", node->ret.value->var_ref.name);
                    for (int i = defer_count - 1; i >= func_defer_boundary; i--)
                    {
                        codegen_node_single(ctx, defer_stack[i], out);
                    }
                    fprintf(out, "_z_ret_mv; });\n");
                    handled = 1;
                }
                free(tname);
            }
        }

        if (!handled)
        {
            if (has_defers && node->ret.value)
            {
                fprintf(out, "    { ");
                emit_auto_type(ctx, node->ret.value, node->token, out);
                fprintf(out, " _z_ret = ");
                codegen_expression(ctx, node->ret.value, out);
                fprintf(out, "; ");
                for (int i = defer_count - 1; i >= func_defer_boundary; i--)
                {
                    codegen_node_single(ctx, defer_stack[i], out);
                }
                fprintf(out, "return _z_ret; }\n");
            }
            else if (has_defers)
            {
                for (int i = defer_count - 1; i >= func_defer_boundary; i--)
                {
                    codegen_node_single(ctx, defer_stack[i], out);
                }
                fprintf(out, "    return;\n");
            }
            else
            {
                fprintf(out, "    return ");
                codegen_expression(ctx, node->ret.value, out);
                fprintf(out, ";\n");
            }
        }
        break;
    }
    case NODE_EXPR_MEMBER:
    {
        codegen_expression(ctx, node->member.target, out);
        char *lt = infer_type(ctx, node->member.target);
        if (lt && (lt[strlen(lt) - 1] == '*' || strstr(lt, "*")))
        {
            fprintf(out, "->%s", node->member.field);
        }
        else
        {
            fprintf(out, ".%s", node->member.field);
        }
        if (lt)
        {
            free(lt);
        }
        break;
    }
    case NODE_REPL_PRINT:
    {
        fprintf(out, "{ ");
        emit_auto_type(ctx, node->repl_print.expr, node->token, out);
        fprintf(out, " _zval = (");
        codegen_expression(ctx, node->repl_print.expr, out);
        fprintf(out, "); fprintf(stdout, _z_str(_zval), _z_arg(_zval)); fprintf(stdout, "
                     "\"\\n\"); }\n");
        break;
    }
    case NODE_AWAIT:
    {
        char *ret_type = "void*";
        int free_ret = 0;
        if (node->type_info)
        {
            char *t = codegen_type_to_string(node->type_info);
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
                free_ret = 0; // infer_type ownership ambiguous, avoid double free
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
            fprintf(out, "})"); // result unused
        }
        else
        {
            if (returns_struct)
            {
                // Dereference and free
                fprintf(out, "%s _val = *(%s*)_r; free(_r); _val; })", ret_type, ret_type);
            }
            else
            {
                if (needs_long_cast)
                {
                    fprintf(out, "(%s)(uintptr_t)_r; })", ret_type);
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
        fprintf(out, ";\n"); // Statement terminator
        break;
    }
    case NODE_EXPR_LITERAL:
        // String literal statement should auto-print
        if (node->literal.type_kind == LITERAL_STRING)
        {
            fprintf(out, "    printf(\"%%s\\n\", ");
            codegen_expression(ctx, node, out);
            fprintf(out, ");\n");
        }
        else
        {
            // Non-string literals as statements - just evaluate
            codegen_expression(ctx, node, out);
            fprintf(out, ";\n");
        }
        break;
    case NODE_CUDA_LAUNCH:
    {
        // Emit CUDA kernel launch: kernel<<<grid, block, shared, stream>>>(args);
        ASTNode *call = node->cuda_launch.call;

        // Get kernel name from callee
        if (call->call.callee->type == NODE_EXPR_VAR)
        {
            fprintf(out, "    %s<<<", call->call.callee->var_ref.name);
        }
        else
        {
            fprintf(out, "    ");
            codegen_expression(ctx, call->call.callee, out);
            fprintf(out, "<<<");
        }

        // Grid dimension
        codegen_expression(ctx, node->cuda_launch.grid, out);
        fprintf(out, ", ");

        // Block dimension
        codegen_expression(ctx, node->cuda_launch.block, out);

        // Optional shared memory size
        if (node->cuda_launch.shared_mem || node->cuda_launch.stream)
        {
            fprintf(out, ", ");
            if (node->cuda_launch.shared_mem)
            {
                codegen_expression(ctx, node->cuda_launch.shared_mem, out);
            }
            else
            {
                fprintf(out, "0");
            }
        }

        // Optional CUDA stream
        if (node->cuda_launch.stream)
        {
            fprintf(out, ", ");
            codegen_expression(ctx, node->cuda_launch.stream, out);
        }

        fprintf(out, ">>>(");

        // Arguments
        ASTNode *arg = call->call.args;
        int first = 1;
        while (arg)
        {
            if (!first)
            {
                fprintf(out, ", ");
            }
            codegen_expression(ctx, arg, out);
            first = 0;
            arg = arg->next;
        }

        fprintf(out, ");\n");
        break;
    }
    case NODE_RAW_STMT:
    {
        if (g_current_lambda)
        {
            Lexer l;
            lexer_init(&l, node->raw_stmt.content);
            Token t;
            int last_pos = 0;
            while ((t = lexer_next(&l)).type != TOK_EOF)
            {
                int current_tok_start = (int)(t.start - node->raw_stmt.content);
                for (int i = last_pos; i < current_tok_start; i++)
                {
                    fputc(node->raw_stmt.content[i], out);
                }

                if (t.type == TOK_IDENT)
                {
                    char *name = token_strdup(t);
                    int captured = -1;
                    if (g_current_lambda->lambda.captured_vars)
                    {
                        for (int i = 0; i < g_current_lambda->lambda.num_captures; i++)
                        {
                            if (strcmp(name, g_current_lambda->lambda.captured_vars[i]) == 0)
                            {
                                captured = i;
                                break;
                            }
                        }
                    }

                    if (captured != -1)
                    {
                        if (g_current_lambda->lambda.capture_modes &&
                            g_current_lambda->lambda.capture_modes[captured] == 1)
                        {
                            fprintf(out, "(*ctx->%s)", name);
                        }
                        else
                        {
                            fprintf(out, "ctx->%s", name);
                        }
                    }
                    else
                    {
                        fprintf(out, "%.*s", t.len, t.start);
                    }
                    free(name);
                }
                else
                {
                    fprintf(out, "%.*s", t.len, t.start);
                }
                last_pos = current_tok_start + t.len;
            }
            fprintf(out, "%s\n", node->raw_stmt.content + last_pos);
        }
        else
        {
            emit_source_mapping_duplicate(node, out);
            fprintf(out, "    %s\n", node->raw_stmt.content);
        }
        break;
    }
    default:
        codegen_expression(ctx, node, out);
        fprintf(out, ";\n");
        if (node->type == NODE_EXPR_CALL && node->call.callee && pending_closure_free_count > 0)
        {
            int is_thread_spawn = 0;
            if (node->call.callee->type == NODE_EXPR_VAR && node->call.callee->var_ref.name &&
                strstr(node->call.callee->var_ref.name, "Thread::spawn"))
            {
                is_thread_spawn = 1;
            }
            else if (node->call.callee->type == NODE_EXPR_MEMBER &&
                     node->call.callee->member.target &&
                     node->call.callee->member.target->type == NODE_EXPR_VAR &&
                     strcmp(node->call.callee->member.target->var_ref.name, "Thread") == 0 &&
                     strcmp(node->call.callee->member.field, "spawn") == 0)
            {
                is_thread_spawn = 1;
            }
            if (is_thread_spawn)
            {
                pending_closure_free_count = 0;
            }
        }
        emit_pending_closure_frees(out);
        break;
    }
}

// Walks AST nodes and generates code.
void codegen_walker(ParserContext *ctx, ASTNode *node, FILE *out)
{
    while (node)
    {
        emit_source_mapping(node, out); // Step to this expression
        codegen_node_single(ctx, node, out);
        node = node->next;
    }
}
