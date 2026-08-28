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

static void emit_single_pattern_cond(ParserContext *ctx, const char *pat, int id, int is_ptr)
{
    // Check for range pattern: "start..end" or "start..=end"
    char *range_incl = (char *)strstr(pat, "..=");
    char *range_excl = (char *)strstr(pat, "..");

    if (range_incl)
    {
        // Inclusive range: start..=end -> _m_id >= start && _m_id <= end
        int start_len = (int)(range_incl - pat);
        char *start = xmalloc((size_t)(start_len + 1));
        strncpy(start, pat, (size_t)(start_len));
        start[start_len] = 0;
        char *end = xstrdup(range_incl + 3);
        if (is_ptr)
        {
            EMIT(ctx, "(*_m_%d >= %s && *_m_%d <= %s)", id, start, id, end);
        }
        else
        {
            EMIT(ctx, "(_m_%d >= %s && _m_%d <= %s)", id, start, id, end);
        }
        zfree(start);
        zfree(end);
    }
    else if (range_excl)
    {
        // Exclusive range: start..end -> _m_id >= start && _m_id < end
        int start_len = (int)(range_excl - pat);
        char *start = xmalloc((size_t)(start_len + 1));
        strncpy(start, pat, (size_t)(start_len));
        start[start_len] = 0;
        char *end = xstrdup(range_excl + 2);
        if (is_ptr)
        {
            EMIT(ctx, "(*_m_%d >= %s && *_m_%d < %s)", id, start, id, end);
        }
        else
        {
            EMIT(ctx, "(_m_%d >= %s && _m_%d < %s)", id, start, id, end);
        }
        zfree(start);
        zfree(end);
    }
    else if (pat[0] == '"')
    {
        // String pattern - string comparison, _m is char* or similar
        if (is_ptr)
        {
            EMIT(ctx, "strcmp(*_m_%d, %s) == 0", id, pat);
        }
        else
        {
            EMIT(ctx, "strcmp(_m_%d, %s) == 0", id, pat);
        }
    }
    else
    {
        // Numeric, Char literal, or simple pattern
        if (is_ptr)
        {
            EMIT(ctx, "*_m_%d == %s", id, pat);
        }
        else
        {
            EMIT(ctx, "_m_%d == %s", id, pat);
        }
    }
}

// Helper: emit condition for a pattern (may contain OR patterns with '|')
static void emit_pattern_condition(ParserContext *ctx, const char *pattern, int id, int is_ptr)
{
    // Check if pattern contains '|' for OR patterns
    if (strchr(pattern, '|'))
    {
        // Split by '|' and emit OR conditions
        char *pattern_copy = xstrdup(pattern);
        char *saveptr;
        char *part = strtok_r(pattern_copy, "|", &saveptr);
        int first = 1;
        EMIT(ctx, "(");
        while (part)
        {
            if (!first)
            {
                EMIT(ctx, " || ");
            }

            // Check if part is an enum variant
            EnumVariantReg *reg = find_enum_variant(ctx, part);
            if (reg)
            {
                int simple = is_simple_enum(ctx, reg->enum_name);
                if (simple)
                {
                    if (is_ptr)
                    {
                        EMIT(ctx, "*_m_%d == %d", id, reg->tag_id);
                    }
                    else
                    {
                        EMIT(ctx, "_m_%d == %d", id, reg->tag_id);
                    }
                }
                else
                {
                    if (is_ptr)
                    {
                        EMIT(ctx, "_m_%d->tag == %d", id, reg->tag_id);
                    }
                    else
                    {
                        EMIT(ctx, "_m_%d.tag == %d", id, reg->tag_id);
                    }
                }
            }
            else
            {
                emit_single_pattern_cond(ctx, part, id, is_ptr);
            }
            first = 0;
            part = strtok_r(NULL, "|", &saveptr);
        }
        EMIT(ctx, ")");
        zfree(pattern_copy);
    }
    else
    {
        // Single pattern (may be a range)
        EnumVariantReg *reg = find_enum_variant(ctx, pattern);
        if (reg)
        {
            int simple = is_simple_enum(ctx, reg->enum_name);
            if (simple)
            {
                if (is_ptr)
                {
                    EMIT(ctx, "*_m_%d == %d", id, reg->tag_id);
                }
                else
                {
                    EMIT(ctx, "_m_%d == %d", id, reg->tag_id);
                }
            }
            else
            {
                if (is_ptr)
                {
                    EMIT(ctx, "_m_%d->tag == %d", id, reg->tag_id);
                }
                else
                {
                    EMIT(ctx, "_m_%d.tag == %d", id, reg->tag_id);
                }
            }
        }
        else
        {
            emit_single_pattern_cond(ctx, pattern, id, is_ptr);
        }
    }
}

// Helper
ZEN_CONST bool is_int_type(TypeKind k)
{
    static const bool is_int[] = {
        [TYPE_CHAR] = true,    [TYPE_I8] = true,         [TYPE_U8] = true,
        [TYPE_I16] = true,     [TYPE_U16] = true,        [TYPE_I32] = true,
        [TYPE_U32] = true,     [TYPE_I64] = true,        [TYPE_U64] = true,
        [TYPE_I128] = true,    [TYPE_U128] = true,       [TYPE_INT] = true,
        [TYPE_UINT] = true,    [TYPE_USIZE] = true,      [TYPE_ISIZE] = true,
        [TYPE_BYTE] = true,    [TYPE_RUNE] = true,       [TYPE_ENUM] = true,
        [TYPE_C_INT] = true,   [TYPE_C_UINT] = true,     [TYPE_C_LONG] = true,
        [TYPE_C_ULONG] = true, [TYPE_C_LONGLONG] = true, [TYPE_C_ULONGLONG] = true,
        [TYPE_C_SHORT] = true, [TYPE_C_USHORT] = true,   [TYPE_C_CHAR] = true,
        [TYPE_C_UCHAR] = true, [TYPE_BITINT] = true,     [TYPE_UBITINT] = true};
    if (k < (int)(sizeof(is_int) / sizeof(is_int[0])))
    {
        return is_int[k];
    }
    return false;
}

void codegen_match_internal(ParserContext *ctx, ASTNode *node, int use_result)
{
    int id = ctx->cg.tmp_counter++;
    int is_self = (node->match_stmt.expr->kind == NODE_EXPR_VAR &&
                   strcmp(node->match_stmt.expr->var_ref.name, "self") == 0);

    char *ret_type = infer_type(ctx, node);
    int is_expr = (use_result && ret_type && strcmp(ret_type, "void") != 0);

    if (is_expr)
    {
        EMIT(ctx, "({ ");
    }
    else
    {
        EMIT(ctx, "{ ");
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

    int is_lvalue_opt = (node->match_stmt.expr->kind == NODE_EXPR_VAR ||
                         node->match_stmt.expr->kind == NODE_EXPR_MEMBER ||
                         node->match_stmt.expr->kind == NODE_EXPR_INDEX);

    emit_source_mapping(ctx, node); // Step through match statements elegantly

    if (is_self)
    {
        emit_auto_type(ctx, node->match_stmt.expr, node->token);
        EMIT(ctx, " _m_%d = ", id);
        codegen_expression(ctx, node->match_stmt.expr);
        EMIT(ctx, "; ");
    }
    else if (has_ref_binding && is_lvalue_opt)
    {
        // Take address for ref bindings
        EMIT(ctx, "ZC_AUTO_INIT(_m_%d, &", id);
        codegen_expression(ctx, node->match_stmt.expr);
        EMIT(ctx, "); ");
    }
    else if (has_ref_binding)
    {
        // Non-lvalue with ref binding: create temporary
        emit_auto_type(ctx, node->match_stmt.expr, node->token);
        EMIT(ctx, " _temp_%d = ", id);
        codegen_expression(ctx, node->match_stmt.expr);
        EMIT(ctx, "; ");

        EMIT(ctx, "ZC_AUTO_INIT(_m_%d, &_temp_%d); ", id, id);
    }
    else
    {
        // No ref bindings: store value directly (not pointer)
        emit_auto_type(ctx, node->match_stmt.expr, node->token);
        EMIT(ctx, " _m_%d = ", id);
        codegen_expression(ctx, node->match_stmt.expr);
        EMIT(ctx, "; ");
    }

    if (is_expr)
    {
        EMIT(ctx, "%s _r_%d; ", ret_type, id);
    }

    char *expr_type = infer_type(ctx, node->match_stmt.expr);
    int is_option = str_is_option_type(expr_type);
    int is_result = str_is_result_type(expr_type);

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
            if (v->enum_name && strcmp(v->enum_name, enum_name) == 0)
            {
                int covered = 0;
                ASTNode *c2 = node->match_stmt.cases;
                while (c2)
                {
                    char mangled_v[512];
                    snprintf(mangled_v, sizeof(mangled_v), "%s__%s", v->enum_name, v->variant_name);

                    if (strcmp(c2->match_case.pattern, v->variant_name) == 0 ||
                        strcmp(c2->match_case.pattern, mangled_v) == 0)
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
        int is_wildcard = (strcmp(c->match_case.pattern, "_") == 0);
        int is_final_wildcard = (is_wildcard && c->next == NULL);

        if (!first)
        {
            EMIT(ctx, " else ");
        }

        emit_source_mapping(ctx, c); // Step through match cases elegantly

        if (!is_final_wildcard || first)
        {
            EMIT(ctx, "if (");
            if (is_wildcard)
            {
                EMIT(ctx, "1");
            }
            else if (is_option)
            {
                int m_is_ptr = has_ref_binding || (expr_type && strchr(expr_type, '*'));
                const char *acc = m_is_ptr ? "->" : ".";

                if (c->match_case.pattern && strcmp(c->match_case.pattern, "Some") == 0)
                {
                    EMIT(ctx, "_m_%d%sis_some", id, acc);
                }
                else if (c->match_case.pattern && strcmp(c->match_case.pattern, "None") == 0)
                {
                    EMIT(ctx, "!_m_%d%sis_some", id, acc);
                }
                else
                {
                    EMIT(ctx, "1");
                }
            }
            else if (is_result)
            {
                int m_is_ptr = has_ref_binding || (expr_type && strchr(expr_type, '*'));
                const char *acc = m_is_ptr ? "->" : ".";

                if (c->match_case.pattern && strcmp(c->match_case.pattern, "Ok") == 0)
                {
                    EMIT(ctx, "_m_%d%sis_ok", id, acc);
                }
                else if (c->match_case.pattern && strcmp(c->match_case.pattern, "Err") == 0)
                {
                    EMIT(ctx, "!_m_%d%sis_ok", id, acc);
                }
                else
                {
                    EMIT(ctx, "1");
                }
            }
            else
            {
                // Use helper for OR patterns, range patterns, and simple patterns
                if (c->match_case.pattern)
                {
                    emit_pattern_condition(ctx, c->match_case.pattern, id, has_ref_binding);
                }
                else
                {
                    EMIT(ctx, "1");
                }
            }
        }

        if (!is_final_wildcard || first)
        {
            EMIT(ctx, ") ");
        }
        EMIT(ctx, "{ ");
        if (c->match_case.binding_count > 0)
        {
            for (int i = 0; i < c->match_case.binding_count; i++)
            {
                char *bname = c->match_case.binding_names[i];
                if (!bname)
                {
                    continue;
                }
                int is_r = c->match_case.binding_refs ? c->match_case.binding_refs[i] : 0;

                if (is_option)
                {
                    if (is_r)
                    {
                        EMIT(ctx, "ZC_AUTO_INIT(%s, &_m_%d->val); ", bname, id);
                    }
                    else if (has_ref_binding)
                    {
                        EMIT(ctx, "ZC_AUTO_INIT(%s, _m_%d->val); ", bname, id);
                    }
                    else
                    {
                        EMIT(ctx, "ZC_AUTO_INIT(%s, _m_%d.val); ", bname, id);
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
                        EMIT(ctx, "ZC_AUTO_INIT(%s, &_m_%d->%s); ", bname, id, field);
                    }
                    else if (has_ref_binding)
                    {
                        EMIT(ctx, "ZC_AUTO_INIT(%s, _m_%d->%s); ", bname, id, field);
                    }
                    else
                    {
                        EMIT(ctx, "ZC_AUTO_INIT(%s, _m_%d.%s); ", bname, id, field);
                    }
                }
                else
                {
                    char *v = (char *)strstr(c->match_case.pattern, "::");
                    if (v)
                    {
                        v += 2;
                    }
                    else
                    {
                        v = strrchr(c->match_case.pattern, '_');
                        if (v)
                        {
                            v++;
                        }
                        else
                        {
                            v = (char *)c->match_case.pattern;
                        }
                    }

                    if (c->match_case.binding_count > 1)
                    {
                        // Tuple destructuring: data.Variant.vI
                        if (is_r)
                        {
                            EMIT(ctx, "ZC_AUTO_INIT(%s, &_m_%d->data.%s.v%d); ", bname, id, v, i);
                        }
                        else if (has_ref_binding)
                        {
                            EMIT(ctx, "ZC_AUTO_INIT(%s, _m_%d->data.%s.v%d); ", bname, id, v, i);
                        }
                        else
                        {
                            EMIT(ctx, "ZC_AUTO_INIT(%s, _m_%d.data.%s.v%d); ", bname, id, v, i);
                        }
                    }
                    else
                    {
                        // Single destructuring: data.Variant
                        if (is_r)
                        {
                            EMIT(ctx, "ZC_AUTO_INIT(%s, &_m_%d->data.%s); ", bname, id, v);
                        }
                        else if (has_ref_binding)
                        {
                            EMIT(ctx, "ZC_AUTO_INIT(%s, _m_%d->data.%s); ", bname, id, v);
                        }
                        else
                        {
                            EMIT(ctx, "ZC_AUTO_INIT(%s, _m_%d.data.%s); ", bname, id, v);
                        }
                    }
                }
            }
        }

        // Check if body is a string literal (should auto-print).
        ASTNode *body = c->match_case.body;
        int is_string_literal =
            (body->kind == NODE_EXPR_LITERAL && body->literal.kind == LITERAL_STRING);

        if (is_expr)
        {
            EMIT(ctx, "_r_%d = ", id);
            if (is_string_literal)
            {
                codegen_node_single(ctx, body);
            }
            else
            {
                if (body->kind == NODE_BLOCK)
                {
                    int saved = ctx->cg.defer_count;
                    EMIT(ctx, "({ ");
                    ASTNode *stmt = body->block.statements;
                    while (stmt)
                    {
                        emit_source_mapping(ctx, stmt);
                        codegen_node_single(ctx, stmt);
                        stmt = stmt->next;
                    }
                    for (int i = ctx->cg.defer_count - 1; i >= saved; i--)
                    {
                        emit_source_mapping_duplicate(ctx, ctx->cg.defer_stack[i]);
                        codegen_node_single(ctx, ctx->cg.defer_stack[i]);
                    }
                    ctx->cg.defer_count = saved;
                    EMIT(ctx, " })");
                }
                else
                {
                    codegen_node_single(ctx, body);
                }
            }
            EMIT(ctx, ";");
        }
        else
        {
            if (is_string_literal)
            {
                char *inner = body->literal.string_val;
                char *code =
                    process_printf_sugar(ctx, body->token, inner, 1, "stdout", NULL, NULL, 0, 0, 0);

                EMIT(ctx, "%s;", code);
                zfree(code);
            }
            else
            {
                codegen_node_single(ctx, body);
            }
        }

        EMIT(ctx, " }");
        first = 0;
        c = c->next;
    }

    if (is_expr)
    {
        if (ctx->config->misra_mode && !has_wildcard)
        {
            EMIT(ctx, " else { } /* MISRA 15.7 */ ");
        }
        EMIT(ctx, " _r_%d; })", id);
    }
    else
    {
        if (ctx->config->misra_mode && !has_wildcard)
        {
            EMIT(ctx, " else { } /* MISRA 15.7 */ ");
        }
        EMIT(ctx, " }");
    }
}
