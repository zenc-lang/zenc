// SPDX-License-Identifier: MIT
#include "parser.h"
#include "constants.h"
#include "expr_internal.h"
#include "analysis/move_check.h"
#include "utils/utils.h"
#include "ast/ast.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int token_is_field_name(Token t)
{
    if (t.kind == TOK_IDENT || t.kind == TOK_INT)
    {
        return 1;
    }
    if (!t.start || t.len == 0)
    {
        return 0;
    }
    char c = t.start[0];
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

int check_opaque_alias_compat(ParserContext *ctx, Type *a, Type *b)
{
    if (!a || !b)
    {
        return 0;
    }

    RECURSION_GUARD(ctx, NULL, 0); // null lexer, return 0 on fail

    int a_is_opaque = (a->kind == TYPE_ALIAS && a->alias.is_opaque_alias);
    int b_is_opaque = (b->kind == TYPE_ALIAS && b->alias.is_opaque_alias);

    if (!a_is_opaque && !b_is_opaque)
    {
        RECURSION_EXIT(ctx);
        return 1;
    }

    if (a_is_opaque)
    {
        if (a->alias.alias_defined_in_file && ctx->current_filename &&
            strcmp(a->alias.alias_defined_in_file, ctx->current_filename) == 0)
        {
            int res = check_opaque_alias_compat(ctx, a->inner, b);
            RECURSION_EXIT(ctx);
            return res;
        }
        RECURSION_EXIT(ctx);
        return 0;
    }

    if (b_is_opaque)
    {
        if (b->alias.alias_defined_in_file && ctx->current_filename &&
            strcmp(b->alias.alias_defined_in_file, ctx->current_filename) == 0)
        {
            int res = check_opaque_alias_compat(ctx, a, b->inner);
            RECURSION_EXIT(ctx);
            return res;
        }
        RECURSION_EXIT(ctx);
        return 0;
    }

    RECURSION_EXIT(ctx);
    return 0;
}

#include "../zen/zen_facts.h"
#include "constants.h"
#include "expr_internal.h"
#include "analysis/move_check.h"
#include "utils/utils.h"
#include "../constants.h"
#include "constants.h"
#include "expr_internal.h"
#include "analysis/move_check.h"
#include "utils/utils.h"
#include "parser.h"
#include "constants.h"
#include "expr_internal.h"
#include "analysis/move_check.h"
#include "utils/utils.h"
#include "analysis/move_check.h"
#include "utils/utils.h"
#include "constants.h"
#include "expr_internal.h"
#include "analysis/move_check.h"
#include "utils/utils.h"
#include <ctype.h>
#include "constants.h"
#include "expr_internal.h"
#include "analysis/move_check.h"
#include "utils/utils.h"
#include <libgen.h>
#include "constants.h"
#include "expr_internal.h"
#include "analysis/move_check.h"
#include "utils/utils.h"
#include <stdio.h>
#include "constants.h"
#include "expr_internal.h"
#include "analysis/move_check.h"
#include "utils/utils.h"

#include <stdlib.h>
#include "constants.h"
#include "expr_internal.h"
#include "analysis/move_check.h"
#include "utils/utils.h"
#include <string.h>
#include "constants.h"
#include "expr_internal.h"
#include "analysis/move_check.h"
#include "utils/utils.h"

ASTNode *find_function_definition(ParserContext *ctx, const char *name)
{
    StructRef *curr = ctx->parsed_funcs_list;
    while (curr)
    {
        if (curr->node && curr->node->func.name && strcmp(curr->node->func.name, name) == 0)
        {
            return curr->node;
        }
        curr = curr->next;
    }
    return NULL;
}

void get_struct_name(ParserContext *ctx, ASTNode *node, char **out_struct_name,
                     char **out_var_ref_name)
{
    if (node->kind == NODE_EXPR_UNARY && strcmp(node->unary.op, "&") == 0 &&
        node->unary.operand->kind == NODE_EXPR_VAR)
    {
        *out_var_ref_name = node->unary.operand->var_ref.name;
        *out_struct_name = find_symbol_type(ctx, *out_var_ref_name);
    }
    else if (node->kind == NODE_EXPR_VAR)
    {
        Type *rhs_t = find_symbol_type_info(ctx, node->var_ref.name);
        if (rhs_t && rhs_t->kind == TYPE_POINTER && rhs_t->inner &&
            rhs_t->inner->kind == TYPE_STRUCT)
        {
            *out_struct_name = rhs_t->inner->name;
            *out_var_ref_name = node->var_ref.name;
        }
        else
        {
            char *rhs_type_str = find_symbol_type(ctx, node->var_ref.name);
            if (rhs_type_str)
            {
                char *clean_rhs = rhs_type_str;
                if (strncmp(clean_rhs, "const ", 6) == 0)
                {
                    clean_rhs += 6;
                }
                size_t len = strlen(clean_rhs);
                if (len > 0 && clean_rhs[len - 1] == '*')
                {
                    size_t struct_type_len = len - 1;
                    char *st = xmalloc(struct_type_len + 1);
                    strncpy(st, clean_rhs, (size_t)(struct_type_len));
                    st[struct_type_len] = '\0';
                    *out_struct_name = st;
                    *out_var_ref_name = node->var_ref.name;
                    // Note: 'st' might leak if not handled, but we follow the existing pattern
                }
            }
        }
    }
}

CallArgs parse_call_args(ParserContext *ctx, Lexer *l, FuncSig *sig)
{
    CallArgs res = {NULL, NULL, NULL, 0, 0};
    (void)ctx;

    if (lexer_peek(l).kind != TOK_RPAREN)
    {
        while (1)
        {
            if (lexer_peek(l).kind == TOK_EOF)
            {
                break;
            }
            char *arg_name = NULL;
            Token t1 = lexer_peek(l);
            if (t1.kind == TOK_IDENT)
            {
                Token t2 = lexer_peek2(l);
                if (t2.kind == TOK_COLON)
                {
                    arg_name = token_strdup(t1);
                    res.has_named = 1;
                    lexer_next(l); // eat IDENT
                    lexer_next(l); // eat :
                }
            }

            ASTNode *arg = parse_expression(ctx, l);
            check_move_usage(ctx, arg, arg ? arg->token : t1);

            if (arg && arg->kind == NODE_EXPR_VAR)
            {
                Type *inner_t = find_symbol_type_info(ctx, arg->var_ref.name);
                if (!inner_t)
                {
                    ZenSymbol *s = find_symbol_entry(ctx, arg->var_ref.name);
                    if (s)
                    {
                        inner_t = s->type_info;
                    }
                }

                if (!is_type_copy(ctx, inner_t))
                {
                    ZenSymbol *s = find_symbol_entry(ctx, arg->var_ref.name);
                    if (s)
                    {
                        s->is_moved = 1;
                    }
                }
            }

            // Implicit trait cast logic
            if (sig && sig->arg_types && res.count < sig->total_args && arg)
            {
                Type *expected = sig->arg_types[res.count];
                if (expected && expected->name && is_trait(expected->name))
                {
                    arg = transform_to_trait_object(ctx, expected->name, arg);
                }
            }

            if (!res.head)
            {
                res.head = arg;
                res.tail = arg;
            }
            else
            {
                res.tail->next = arg;
                res.tail = arg;
            }

            res.arg_names = xrealloc(res.arg_names, (size_t)(res.count + 1) * sizeof(char *));
            res.arg_names[res.count] = arg_name;
            res.count++;

            if (lexer_peek(l).kind == TOK_COMMA)
            {
                lexer_next(l);
            }
            else
            {
                break;
            }
        }
    }

    return res;
}

ASTNode *transform_to_trait_object(ParserContext *ctx, const char *target_trait,
                                   ASTNode *source_expr)
{
    if (!target_trait || !source_expr)
    {
        return source_expr;
    }

    char *clean_trait = xstrdup(target_trait);
    char *p = (char *)strchr(clean_trait, '*');
    if (p)
    {
        *p = '\0';
    }

    if (!is_trait(clean_trait))
    {
        zfree(clean_trait);
        return source_expr;
    }

    char *struct_type = NULL;
    char *var_ref_name = NULL;

    get_struct_name(ctx, source_expr, &struct_type, &var_ref_name);

    if (struct_type && var_ref_name)
    {
        char *clean_struct_type = struct_type;
        if (strncmp(clean_struct_type, "const ", 6) == 0)
        {
            clean_struct_type += 6;
        }

        // Check if the struct actually implements the trait
        if (check_impl(ctx, clean_trait, clean_struct_type))
        {
            char *code = xmalloc(512);
            char v_buf[MAX_MANGLED_NAME_LEN];
            snprintf(v_buf, sizeof(v_buf), "%s__%s__VTable", clean_struct_type, clean_trait);
            char *v_mangled = merge_underscores(v_buf);

            if (source_expr->kind == NODE_EXPR_UNARY && strcmp(source_expr->unary.op, "&") == 0)
            {
                snprintf(code, 512, "(%s){.self=(void*)&%s, .vtable=&%s}", clean_trait,
                         var_ref_name, v_mangled);
            }
            else
            {
                snprintf(code, 512, "(%s){.self=(void*)%s, .vtable=&%s}", clean_trait, var_ref_name,
                         v_mangled);
            }

            ASTNode *wrapper = ast_create(NODE_RAW_STMT);
            wrapper->token = source_expr->token;
            wrapper->raw_stmt.content = code;

            // Set Type Info so subsequent method calls work
            Type *trait_type = type_new(TYPE_STRUCT);
            trait_type->name = xstrdup(clean_trait);
            wrapper->type_info = trait_type;

            zfree(clean_trait);
            // If target_trait had a *, we might need to wrap in &addr?
            if (strchr(target_trait, '*'))
            {
                if (ctx->config->use_cpp)
                {
                    // C++ does not allow taking the address of a compound literal.
                    // Use a statement expression with a local variable instead.
                    char *ptr_code = xmalloc(1024);
                    snprintf(ptr_code, 1024, "({ static %s _ztrait = %s; &_ztrait; })",
                             wrapper->type_info->name, code);
                    ASTNode *raw_ptr = ast_create(NODE_RAW_STMT);
                    raw_ptr->token = source_expr->token;
                    raw_ptr->raw_stmt.content = ptr_code;

                    Type *ptr_type = type_new(TYPE_POINTER);
                    ptr_type->inner = trait_type;
                    raw_ptr->type_info = ptr_type;
                    return raw_ptr;
                }

                ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                addr->unary.op = xstrdup("&");
                addr->unary.operand = wrapper;
                addr->token = source_expr->token;

                Type *ptr_type = type_new(TYPE_POINTER);
                ptr_type->inner = trait_type;
                addr->type_info = ptr_type;
                return addr;
            }

            return wrapper;
        }
    }

    zfree(clean_trait);
    return source_expr;
}

void validate_named_arguments(Token call_token, const char *func_name, char **arg_names,
                              int args_count, ASTNode *func_def)
{
    if (!func_def || !arg_names)
    {
        (void)func_name;
        return;
    }

    for (int i = 0; i < args_count; i++)
    {
        // Skip positional arguments (NULL name)
        if (!arg_names[i])
        {
            continue;
        }

        // Check bounds
        if (i >= func_def->func.count)
        {
            continue;
        }

        // Check parameter name match
        const char *expected_name = func_def->func.param_names[i];
        if (!expected_name)
        {
            continue;
        }

        if (strcmp(arg_names[i], expected_name) != 0)
        {
            char msg[MAX_SHORT_MSG_LEN];
            snprintf(
                msg, sizeof(msg),
                "Named arguments must follow function parameter order. Expected '%s' but got '%s'",
                expected_name, arg_names[i]);
            zpanic_at(call_token, "%s", msg);
            return;
        }
    }
}

// Helper to check if a type is a struct type

void check_move_usage(ParserContext *ctx, ASTNode *node, Token t)
{
    (void)t;
    (void)ctx;
    if (!node)
    {
        return;
    }
    if (node->kind == NODE_EXPR_VAR)
    {
        // Move check placeholder: find_symbol_entry(ctx, node->var_ref.name);
    }
}

int type_is_unsigned(Type *t)
{
    if (!t)
    {
        return 0;
    }

    return (t->kind == TYPE_U8 || t->kind == TYPE_U16 || t->kind == TYPE_U32 ||
            t->kind == TYPE_U64 || t->kind == TYPE_USIZE || t->kind == TYPE_BYTE ||
            t->kind == TYPE_U128 || t->kind == TYPE_UINT ||
            (t->kind == TYPE_STRUCT && t->name &&
             (0 == strcmp(t->name, "uint8_t") || 0 == strcmp(t->name, "uint16_t") ||
              0 == strcmp(t->name, "uint32_t") || 0 == strcmp(t->name, "uint64_t") ||
              0 == strcmp(t->name, "size_t"))));
}

char *infer_printf_format(ParserContext *ctx, ASTNode **args, int ac)
{
    char *fmt = xmalloc(MAX_SHORT_MSG_LEN);
    fmt[0] = 0;
    for (int i = 0; i < ac; i++)
    {
        Type *inner_t = args[i]->type_info;
        if (!inner_t && args[i]->kind == NODE_EXPR_VAR)
        {
            inner_t = find_symbol_type_info(ctx, args[i]->var_ref.name);
        }

        if (!inner_t)
        {
            strcat(fmt, "%d"); // Fallback
        }
        else
        {
            if (inner_t->kind == TYPE_INT || inner_t->kind == TYPE_I32 ||
                inner_t->kind == TYPE_BOOL)
            {
                strcat(fmt, "%d");
            }
            else if (inner_t->kind == TYPE_F64)
            {
                strcat(fmt, "%lf");
            }
            else if (inner_t->kind == TYPE_F32 || inner_t->kind == TYPE_FLOAT)
            {
                strcat(fmt, "%f");
            }
            else if (inner_t->kind == TYPE_STRING ||
                     (inner_t->kind == TYPE_ARRAY && inner_t->inner &&
                      (inner_t->inner->kind == TYPE_CHAR || inner_t->inner->kind == TYPE_U8 ||
                       inner_t->inner->kind == TYPE_I8)))
            {
                strcat(fmt, "%s");
            }
            else if (inner_t->kind == TYPE_CHAR || inner_t->kind == TYPE_I8 ||
                     inner_t->kind == TYPE_U8 || inner_t->kind == TYPE_BYTE)
            {
                strcat(fmt, " %c"); // Space skip whitespace
            }
            else
            {
                strcat(fmt, "%d");
            }
        }
        if (i < ac - 1)
        {
            strcat(fmt, " ");
        }
    }
    return fmt;
}

void check_format_string(ASTNode *call, Token t)
{
    if (call->kind != NODE_EXPR_CALL)
    {
        return;
    }
    ASTNode *callee = call->call.callee;
    if (callee->kind != NODE_EXPR_VAR)
    {
        return;
    }

    char *fname = callee->var_ref.name;
    if (!fname)
    {
        return;
    }

    if (strcmp(fname, "printf") != 0 && strcmp(fname, "sprintf") != 0 &&
        strcmp(fname, "fprintf") != 0 && strcmp(fname, "dprintf") != 0)
    {
        return;
    }

    int fmt_idx = 0;
    if (strcmp(fname, "fprintf") == 0 || strcmp(fname, "sprintf") == 0 ||
        strcmp(fname, "dprintf") == 0)
    {
        fmt_idx = 1;
    }

    ASTNode *args = call->call.args;
    ASTNode *fmt_arg = args;
    for (int i = 0; i < fmt_idx; i++)
    {
        if (!fmt_arg)
        {
            return;
        }
        fmt_arg = fmt_arg->next;
    }
    if (!fmt_arg)
    {
        return;
    }

    if (fmt_arg->kind != NODE_EXPR_LITERAL || fmt_arg->literal.kind != 2)
    {
        return;
    }

    const char *fmt = fmt_arg->literal.string_val;

    ASTNode *curr_arg = fmt_arg->next;
    int arg_num = fmt_idx + 2;

    for (int i = 0; fmt[i]; i++)
    {
        if (fmt[i] == '%')
        {
            i++;
            if (fmt[i] == 0)
            {
                break;
            }
            if (fmt[i] == '%')
            {
                continue;
            }

            // Flags.
            while (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '#' ||
                   fmt[i] == '0')
            {
                i++;
            }

            // Width.
            while (isdigit(fmt[i]))
            {
                i++;
            }

            if (fmt[i] == '*')
            {
                i++;
                if (!curr_arg)
                {
                    warn_format_string(t, arg_num, "width(int)", "missing");
                }
                else
                {
                    /* check int */
                    curr_arg = curr_arg->next;
                    arg_num++;
                }
            }

            // Precision.
            if (fmt[i] == '.')
            {
                i++;
                while (isdigit(fmt[i]))
                {
                    i++;
                }

                if (fmt[i] == '*')
                {
                    i++;
                    if (!curr_arg)
                    {
                        warn_format_string(t, arg_num, "precision(int)", "missing");
                    }
                    else
                    {
                        /* check int */
                        curr_arg = curr_arg->next;
                        arg_num++;
                    }
                }
            }

            // Length.
            if (fmt[i] == 'h' || fmt[i] == 'l' || fmt[i] == 'L' || fmt[i] == 'z' || fmt[i] == 'j' ||
                fmt[i] == 't')
            {
                if (fmt[i] == 'h' && fmt[i + 1] == 'h')
                {
                    i++;
                }
                else if (fmt[i] == 'l' && fmt[i + 1] == 'l')
                {
                    i++;
                }
                i++;
            }

            char spec = fmt[i];

            if (!curr_arg)
            {
                warn_format_string(t, arg_num, "argument", "missing");
                continue;
            }

            Type *vt = curr_arg->type_info;
            char *got_type = vt ? type_to_string(vt) : "?";

            if (spec == 'd' || spec == 'i' || spec == 'u' || spec == 'x' || spec == 'X' ||
                spec == 'o')
            {
                if (vt && !is_integer_type(vt))
                {
                    warn_format_string(t, arg_num, "integer", got_type);
                }
            }
            else if (spec == 's')
            {
                if (vt && vt->kind != TYPE_STRING && vt->kind != TYPE_POINTER &&
                    vt->kind != TYPE_ARRAY)
                {
                    warn_format_string(t, arg_num, "string", got_type);
                }
            }
            else if (spec == 'f' || spec == 'F' || spec == 'e' || spec == 'E' || spec == 'g' ||
                     spec == 'G')
            {
                if (vt && vt->kind != TYPE_FLOAT && vt->kind != TYPE_F64)
                {
                    warn_format_string(t, arg_num, "float", got_type);
                }
            }
            else if (spec == 'p')
            {
                if (vt && vt->kind != TYPE_POINTER && vt->kind != TYPE_ARRAY)
                {
                    warn_format_string(t, arg_num, "pointer", got_type);
                }
            }

            curr_arg = curr_arg->next;
            arg_num++;
        }
    }
}
