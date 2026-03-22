#include "../codegen/codegen.h"

int check_opaque_alias_compat(ParserContext *ctx, Type *a, Type *b)
{
    if (!a || !b)
    {
        return 0;
    }

    int a_is_opaque = (a->kind == TYPE_ALIAS && a->alias.is_opaque_alias);
    int b_is_opaque = (b->kind == TYPE_ALIAS && b->alias.is_opaque_alias);

    if (!a_is_opaque && !b_is_opaque)
    {
        return 1;
    }

    if (a_is_opaque)
    {
        if (a->alias.alias_defined_in_file && g_current_filename &&
            strcmp(a->alias.alias_defined_in_file, g_current_filename) == 0)
        {
            return check_opaque_alias_compat(ctx, a->inner, b);
        }
        return 0;
    }

    if (b_is_opaque)
    {
        if (b->alias.alias_defined_in_file && g_current_filename &&
            strcmp(b->alias.alias_defined_in_file, g_current_filename) == 0)
        {
            return check_opaque_alias_compat(ctx, a, b->inner);
        }
        return 0;
    }

    return 0;
}

#include "../zen/zen_facts.h"
#include "../constants.h"
#include "parser.h"
#include "analysis/move_check.h"
#include <ctype.h>
#include <libgen.h>
#include <stdio.h>

#include <stdlib.h>
#include <string.h>

static ASTNode *find_function_definition(ParserContext *ctx, const char *name)
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

static void get_struct_name(ParserContext *ctx, ASTNode *node, char **out_struct_name,
                            char **out_var_ref_name)
{
    if (node->type == NODE_EXPR_UNARY && strcmp(node->unary.op, "&") == 0 &&
        node->unary.operand->type == NODE_EXPR_VAR)
    {
        *out_var_ref_name = node->unary.operand->var_ref.name;
        *out_struct_name = find_symbol_type(ctx, *out_var_ref_name);
    }
    else if (node->type == NODE_EXPR_VAR)
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
                    strncpy(st, clean_rhs, struct_type_len);
                    st[struct_type_len] = '\0';
                    *out_struct_name = st;
                    *out_var_ref_name = node->var_ref.name;
                    // Note: 'st' might leak if not handled, but we follow the existing pattern
                }
            }
        }
    }
}

ASTNode *transform_to_trait_object(ParserContext *ctx, const char *target_trait,
                                   ASTNode *source_expr)
{
    if (!target_trait || !source_expr)
    {
        return source_expr;
    }

    char *clean_trait = xstrdup(target_trait);
    char *p = strchr(clean_trait, '*');
    if (p)
    {
        *p = '\0';
    }

    if (!is_trait(clean_trait))
    {
        free(clean_trait);
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
            if (source_expr->type == NODE_EXPR_UNARY && strcmp(source_expr->unary.op, "&") == 0)
            {
                sprintf(code, "(%s){.self=&%s, .vtable=&%s_%s_VTable}", clean_trait, var_ref_name,
                        clean_struct_type, clean_trait);
            }
            else
            {
                sprintf(code, "(%s){.self=%s, .vtable=&%s_%s_VTable}", clean_trait, var_ref_name,
                        clean_struct_type, clean_trait);
            }

            ASTNode *wrapper = ast_create(NODE_RAW_STMT);
            wrapper->token = source_expr->token;
            wrapper->raw_stmt.content = code;

            // Set Type Info so subsequent method calls work
            Type *trait_type = type_new(TYPE_STRUCT);
            trait_type->name = xstrdup(clean_trait);
            wrapper->type_info = trait_type;

            free(clean_trait);
            // If target_trait had a *, we might need to wrap in &addr?
            if (strchr(target_trait, '*'))
            {
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

    free(clean_trait);
    return source_expr;
}

static void validate_named_arguments(Token call_token, const char *func_name, char **arg_names,
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
        if (i >= func_def->func.arg_count)
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
            char msg[256];
            snprintf(
                msg, sizeof(msg),
                "Named arguments must follow function parameter order. Expected '%s' but got '%s'",
                expected_name, arg_names[i]);
            zpanic_at(call_token, "%s", msg);
        }
    }
}

extern ASTNode *global_user_structs;

// Forward declaration
char *resolve_struct_name_from_type(ParserContext *ctx, Type *t, int *is_ptr_out,
                                    char **allocated_out);

// Helper to check if a type is a struct type
int is_struct_type(ParserContext *ctx, const char *type_name)
{
    if (!type_name)
    {
        return 0;
    }
    return find_struct_def(ctx, type_name) != NULL;
}

Type *get_field_type(ParserContext *ctx, Type *struct_type, const char *field_name);
char *infer_type(ParserContext *ctx, ASTNode *node); // from codegen

static void check_move_usage(ParserContext *ctx, ASTNode *node, Token t)
{
    (void)t;
    (void)ctx;
    if (!node)
    {
        return;
    }
    if (node->type == NODE_EXPR_VAR)
    {
        // Move check placeholder: find_symbol_entry(ctx, node->var_ref.name);
    }
}

static int type_is_unsigned(Type *t)
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

static void __attribute__((unused)) check_format_string(ASTNode *call, Token t)
{
    if (call->type != NODE_EXPR_CALL)
    {
        return;
    }
    ASTNode *callee = call->call.callee;
    if (callee->type != NODE_EXPR_VAR)
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

    if (fmt_arg->type != NODE_EXPR_LITERAL || fmt_arg->literal.type_kind != 2)
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

ASTNode *parse_expression(ParserContext *ctx, Lexer *l)
{
    return parse_expr_prec(ctx, l, PREC_NONE);
}

Precedence get_token_precedence(Token t)
{
    if (t.type == TOK_INT || t.type == TOK_FLOAT || t.type == TOK_STRING || t.type == TOK_IDENT ||
        t.type == TOK_FSTRING)
    {
        return PREC_NONE;
    }

    if (t.type == TOK_QUESTION)
    {
        return PREC_CALL;
    }

    if (t.type == TOK_ARROW && t.start[0] == '-')
    {
        return PREC_CALL;
    }

    if (t.type == TOK_Q_DOT)
    {
        return PREC_CALL;
    }

    if (t.type == TOK_QQ)
    {
        return PREC_OR;
    }

    if (t.type == TOK_AND)
    {
        return PREC_AND;
    }

    if (t.type == TOK_OR)
    {
        return PREC_OR;
    }

    if (t.type == TOK_QQ_EQ)
    {
        return PREC_ASSIGNMENT;
    }

    if (t.type == TOK_PIPE)
    {
        return PREC_TERM;
    }

    if (t.type == TOK_LANGLE || t.type == TOK_RANGLE)
    {
        return PREC_COMPARISON;
    }

    if (t.type == TOK_OP)
    {
        if (is_token(t, "=") || is_token(t, "+=") || is_token(t, "-=") || is_token(t, "*=") ||
            is_token(t, "/=") || is_token(t, "%=") || is_token(t, "|=") || is_token(t, "&=") ||
            is_token(t, "^=") || is_token(t, "<<=") || is_token(t, ">>=") || is_token(t, "**="))
        {
            return PREC_ASSIGNMENT;
        }

        if (is_token(t, "||") || is_token(t, "or"))
        {
            return PREC_OR;
        }

        if (is_token(t, "&&") || is_token(t, "and"))
        {
            return PREC_AND;
        }

        if (is_token(t, "|"))
        {
            return PREC_TERM;
        }

        if (is_token(t, "^"))
        {
            return PREC_TERM;
        }

        if (is_token(t, "&"))
        {
            return PREC_TERM;
        }

        if (is_token(t, "<<") || is_token(t, ">>"))
        {
            return PREC_TERM;
        }

        if (is_token(t, "==") || is_token(t, "!="))
        {
            return PREC_EQUALITY;
        }

        if (is_token(t, "<") || is_token(t, ">") || is_token(t, "<=") || is_token(t, ">="))
        {
            return PREC_COMPARISON;
        }

        if (is_token(t, "+") || is_token(t, "-"))
        {
            return PREC_TERM;
        }

        if (is_token(t, "*") || is_token(t, "/") || is_token(t, "%"))
        {
            return PREC_FACTOR;
        }

        if (is_token(t, "**"))
        {
            return PREC_POWER;
        }

        if (is_token(t, "."))
        {
            return PREC_CALL;
        }

        if (is_token(t, "|>"))
        {
            return PREC_TERM;
        }
    }

    if (t.type == TOK_LBRACKET || t.type == TOK_LPAREN)
    {
        return PREC_CALL;
    }

    if (is_token(t, "??"))
    {
        return PREC_OR;
    }

    if (is_token(t, "\?\?="))
    {
        return PREC_ASSIGNMENT;
    }

    return PREC_NONE;
}

// Helper to check if a variable name is in a list.
static int is_in_list(const char *name, char **list, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (0 == strcmp(name, list[i]))
        {
            return 1;
        }
    }
    return 0;
}

// Recursively find all variable references in an expression/statement.
static void find_var_refs(ASTNode *node, char ***refs, int *ref_count)
{
    if (!node)
    {
        return;
    }

    if (node->type == NODE_EXPR_VAR)
    {
        *refs = xrealloc(*refs, sizeof(char *) * (*ref_count + 1));
        (*refs)[*ref_count] = xstrdup(node->var_ref.name);
        (*ref_count)++;
    }

    switch (node->type)
    {
    case NODE_EXPR_BINARY:
        find_var_refs(node->binary.left, refs, ref_count);
        find_var_refs(node->binary.right, refs, ref_count);
        break;
    case NODE_EXPR_UNARY:
        find_var_refs(node->unary.operand, refs, ref_count);
        break;
    case NODE_EXPR_CALL:
        find_var_refs(node->call.callee, refs, ref_count);
        for (ASTNode *arg = node->call.args; arg; arg = arg->next)
        {
            find_var_refs(arg, refs, ref_count);
        }
        break;
    case NODE_EXPR_MEMBER:
        find_var_refs(node->member.target, refs, ref_count);
        break;
    case NODE_EXPR_INDEX:
        find_var_refs(node->index.array, refs, ref_count);
        find_var_refs(node->index.index, refs, ref_count);
        break;
    case NODE_EXPR_SLICE:
        find_var_refs(node->slice.array, refs, ref_count);
        find_var_refs(node->slice.start, refs, ref_count);
        find_var_refs(node->slice.end, refs, ref_count);
        break;
    case NODE_BLOCK:
        for (ASTNode *stmt = node->block.statements; stmt; stmt = stmt->next)
        {
            find_var_refs(stmt, refs, ref_count);
        }
        break;
    case NODE_RETURN:
        find_var_refs(node->ret.value, refs, ref_count);
        break;
    case NODE_VAR_DECL:
    case NODE_CONST:
        find_var_refs(node->var_decl.init_expr, refs, ref_count);
        break;
    case NODE_IF:
        find_var_refs(node->if_stmt.condition, refs, ref_count);
        find_var_refs(node->if_stmt.then_body, refs, ref_count);
        if (node->if_stmt.else_body)
        {
            find_var_refs(node->if_stmt.else_body, refs, ref_count);
        }
        break;
    case NODE_WHILE:
        find_var_refs(node->while_stmt.condition, refs, ref_count);
        find_var_refs(node->while_stmt.body, refs, ref_count);
        break;
    case NODE_FOR:
        find_var_refs(node->for_stmt.init, refs, ref_count);
        find_var_refs(node->for_stmt.condition, refs, ref_count);
        find_var_refs(node->for_stmt.step, refs, ref_count);
        find_var_refs(node->for_stmt.body, refs, ref_count);
        break;
    case NODE_FOR_RANGE:
        find_var_refs(node->for_range.start, refs, ref_count);
        find_var_refs(node->for_range.end, refs, ref_count);
        find_var_refs(node->for_range.body, refs, ref_count);
        break;
    case NODE_MATCH:
        find_var_refs(node->match_stmt.expr, refs, ref_count);
        for (ASTNode *c = node->match_stmt.cases; c; c = c->next)
        {
            find_var_refs(c->match_case.body, refs, ref_count);
        }
        break;
    case NODE_RAW_STMT:
        for (int i = 0; i < node->raw_stmt.used_symbol_count; i++)
        {
            *refs = xrealloc(*refs, sizeof(char *) * (*ref_count + 1));
            (*refs)[*ref_count] = xstrdup(node->raw_stmt.used_symbols[i]);
            (*ref_count)++;
        }
        break;
    case NODE_EXPR_CAST:
        find_var_refs(node->cast.expr, refs, ref_count);
        break;
    case NODE_EXPR_STRUCT_INIT:
        for (ASTNode *f = node->struct_init.fields; f; f = f->next)
        {
            find_var_refs(f, refs, ref_count);
        }
        break;
    case NODE_EXPR_ARRAY_LITERAL:
        for (ASTNode *e = node->array_literal.elements; e; e = e->next)
        {
            find_var_refs(e, refs, ref_count);
        }
        break;
    case NODE_TERNARY:
        find_var_refs(node->ternary.cond, refs, ref_count);
        find_var_refs(node->ternary.true_expr, refs, ref_count);
        find_var_refs(node->ternary.false_expr, refs, ref_count);
        break;
    case NODE_ASSERT:
        find_var_refs(node->assert_stmt.condition, refs, ref_count);
        break;
    case NODE_DEFER:
        find_var_refs(node->defer_stmt.stmt, refs, ref_count);
        break;
    case NODE_TRY:
        find_var_refs(node->try_stmt.expr, refs, ref_count);
        break;
    default:
        break;
    }
}

// Helper to find variable declarations in a subtree
static void find_declared_vars(ASTNode *node, char ***decls, int *count)
{
    if (!node)
    {
        return;
    }

    if (node->type == NODE_VAR_DECL)
    {
        *decls = xrealloc(*decls, sizeof(char *) * (*count + 1));
        (*decls)[*count] = xstrdup(node->var_decl.name);
        (*count)++;
    }

    if (node->type == NODE_MATCH_CASE)
    {
        if (node->match_case.binding_names)
        {
            for (int i = 0; i < node->match_case.binding_count; i++)
            {
                *decls = xrealloc(*decls, sizeof(char *) * (*count + 1));
                (*decls)[*count] = xstrdup(node->match_case.binding_names[i]);
                (*count)++;
            }
        }
    }

    switch (node->type)
    {
    case NODE_BLOCK:
        for (ASTNode *stmt = node->block.statements; stmt; stmt = stmt->next)
        {
            find_declared_vars(stmt, decls, count);
        }
        break;
    case NODE_IF:
        find_declared_vars(node->if_stmt.then_body, decls, count);
        find_declared_vars(node->if_stmt.else_body, decls, count);
        break;
    case NODE_WHILE:
        find_declared_vars(node->while_stmt.body, decls, count);
        break;
    case NODE_FOR:
        find_declared_vars(node->for_stmt.init, decls, count);
        find_declared_vars(node->for_stmt.body, decls, count);
        break;
    case NODE_FOR_RANGE:
        find_declared_vars(node->for_range.start, decls, count);
        find_declared_vars(node->for_range.body, decls, count);
        break;
    case NODE_MATCH:
        for (ASTNode *c = node->match_stmt.cases; c; c = c->next)
        {
            find_declared_vars(c, decls, count);
            find_declared_vars(c->match_case.body, decls, count);
        }
        break;
    default:
        break;
    }
}

// Analyze lambda body to find captured variables.
void analyze_lambda_captures(ParserContext *ctx, ASTNode *lambda)
{
    if (!lambda || lambda->type != NODE_LAMBDA)
    {
        return;
    }

    char **all_refs = NULL;
    int num_refs = 0;
    find_var_refs(lambda->lambda.body, &all_refs, &num_refs);

    char **local_decls = NULL;
    int num_local_decls = 0;
    find_declared_vars(lambda->lambda.body, &local_decls, &num_local_decls);

    char **captures = xmalloc(sizeof(char *) * 32);
    char **capture_types = xmalloc(sizeof(char *) * 32);
    int *capture_modes = xmalloc(sizeof(int) * 32);
    int num_captures = 0;

    for (int i = 0; i < lambda->lambda.num_explicit_captures; i++)
    {
        const char *var_name = lambda->lambda.explicit_captures[i];
        if (!is_in_list(var_name, captures, num_captures))
        {
            captures[num_captures] = xstrdup(var_name);
            capture_modes[num_captures] = lambda->lambda.explicit_capture_modes[i];

            Type *t = find_symbol_type_info(ctx, var_name);
            capture_types[num_captures] = t ? type_to_string(t) : xstrdup("int");
            num_captures++;
        }
    }

    for (int i = 0; i < num_refs; i++)
    {
        const char *var_name = all_refs[i];

        if (is_in_list(var_name, lambda->lambda.param_names, lambda->lambda.num_params))
        {
            continue;
        }

        if (is_in_list(var_name, local_decls, num_local_decls))
        {
            continue;
        }

        if (is_in_list(var_name, captures, num_captures))
        {
            continue;
        }

        if (strcmp(var_name, "printf") == 0 || strcmp(var_name, "malloc") == 0 ||
            strcmp(var_name, "strcmp") == 0 || strcmp(var_name, "free") == 0 ||
            strcmp(var_name, "sprintf") == 0 || strcmp(var_name, "snprintf") == 0 ||
            strcmp(var_name, "fprintf") == 0 || strcmp(var_name, "stderr") == 0 ||
            strcmp(var_name, "stdout") == 0 || strcmp(var_name, "strcat") == 0 ||
            strcmp(var_name, "strcpy") == 0 || strcmp(var_name, "strlen") == 0 ||
            strcmp(var_name, "Vec_new") == 0 || strcmp(var_name, "Vec_push") == 0)
        {
            continue;
        }

        FuncSig *fs = ctx->func_registry;
        int is_func = 0;
        while (fs)
        {
            if (0 == strcmp(fs->name, var_name))
            {
                is_func = 1;
                break;
            }
            fs = fs->next;
        }
        if (is_func)
        {
            continue;
        }

        Scope *s = ctx->current_scope;
        int is_local = 0;
        int is_found = 0;
        while (s)
        {
            ZenSymbol *cur = s->symbols;
            while (cur)
            {
                if (0 == strcmp(cur->name, var_name))
                {
                    is_found = 1;
                    if (s->parent != NULL)
                    {
                        is_local = 1;
                    }
                    break;
                }
                cur = cur->next;
            }
            if (is_found)
            {
                break;
            }
            s = s->parent;
        }

        if (is_found && !is_local)
        {
            continue;
        }

        int found = 0;
        for (int j = 0; j < num_captures; j++)
        {
            if (strcmp(var_name, captures[j]) == 0)
            {
                found = 1;
                break;
            }
        }
        if (found)
        {
            continue;
        }

        captures[num_captures] = xstrdup(var_name);
        capture_modes[num_captures] = lambda->lambda.default_capture_mode;

        Type *t = find_symbol_type_info(ctx, var_name);
        if (t)
        {
            capture_types[num_captures] = type_to_string(t);
        }
        else
        {
            capture_types[num_captures] = xstrdup("int");
        }
        num_captures++;
    }

    lambda->lambda.captured_vars = captures;
    lambda->lambda.captured_types = capture_types;
    lambda->lambda.capture_modes = capture_modes;
    lambda->lambda.num_captures = num_captures;

    if (local_decls)
    {
        for (int i = 0; i < num_local_decls; i++)
        {
            free(local_decls[i]);
        }
        free(local_decls);
    }
    for (int i = 0; i < num_refs; i++)
    {
        free(all_refs[i]);
        free(all_refs);
    }
}

ASTNode *parse_lambda(ParserContext *ctx, Lexer *l)
{
    lexer_next(l);

    int default_capture_mode = 0; // 0=Value, 1=Reference
    char **explicit_captures = xmalloc(sizeof(char *) * 32);
    int *explicit_capture_modes = xmalloc(sizeof(int) * 32);
    int num_explicit_captures = 0;

    if (lexer_peek(l).type == TOK_LBRACKET)
    {
        lexer_next(l); // eat [

        while (lexer_peek(l).type != TOK_RBRACKET && lexer_peek(l).type != TOK_EOF)
        {
            if (lexer_peek(l).type == TOK_OP && lexer_peek(l).len == 1 &&
                lexer_peek(l).start[0] == '&')
            {
                lexer_next(l);
                if (lexer_peek(l).type == TOK_IDENT)
                {
                    explicit_captures[num_explicit_captures] = token_strdup(lexer_peek(l));
                    explicit_capture_modes[num_explicit_captures] = 1; // By-Reference
                    num_explicit_captures++;
                    lexer_next(l);
                }
                else
                {
                    default_capture_mode = 1;
                }
            }
            else if (lexer_peek(l).type == TOK_OP && lexer_peek(l).len == 1 &&
                     lexer_peek(l).start[0] == '=')
            {
                default_capture_mode = 0;
                lexer_next(l);
            }
            else if (lexer_peek(l).type == TOK_IDENT)
            {
                explicit_captures[num_explicit_captures] = token_strdup(lexer_peek(l));
                explicit_capture_modes[num_explicit_captures] = 0; // By-Value
                num_explicit_captures++;
                lexer_next(l);
            }
            else
            {
                zpanic_at(lexer_peek(l), "Invalid capture list item");
            }

            if (lexer_peek(l).type == TOK_COMMA)
            {
                lexer_next(l);
            }
            else
            {
                break;
            }
        }

        if (lexer_peek(l).type != TOK_RBRACKET)
        {
            zpanic_at(lexer_peek(l), "Expected ']' after capture list");
        }
        lexer_next(l); // eat ]
    }

    if (lexer_peek(l).type != TOK_LPAREN)
    {
        zpanic_at(lexer_peek(l), "Expected '(' after 'fn' in lambda");
    }

    lexer_next(l);

    Type *t = type_new(TYPE_FUNCTION);
    t->args = xmalloc(sizeof(Type *) * 16);
    char **param_names = xmalloc(sizeof(char *) * 16);
    char **param_types = xmalloc(sizeof(char *) * 16);
    int num_params = 0;

    while (lexer_peek(l).type != TOK_RPAREN)
    {
        if (num_params > 0)
        {
            if (lexer_peek(l).type != TOK_COMMA)
            {
                zpanic_at(lexer_peek(l), "Expected ',' between parameters");
            }

            lexer_next(l);
        }

        Token name_tok = lexer_next(l);
        if (name_tok.type != TOK_IDENT)
        {
            zpanic_at(name_tok, "Expected parameter name");
        }

        param_names[num_params] = token_strdup(name_tok);

        if (lexer_peek(l).type != TOK_COLON)
        {
            zpanic_at(lexer_peek(l), "Expected ':' after parameter name");
        }

        lexer_next(l);

        Type *typef = parse_type_formal(ctx, l);
        t->args[t->arg_count] = typef;
        param_types[num_params] = type_to_string(typef);
        num_params++;
        t->arg_count = num_params;
    }
    lexer_next(l);

    char *return_type = xstrdup("void");
    if (lexer_peek(l).type == TOK_ARROW)
    {
        lexer_next(l);

        t->inner = parse_type_formal(ctx, l);
        return_type = type_to_string(t->inner);
    }

    enter_scope(ctx);

    for (int i = 0; i < num_params; i++)
    {
        add_symbol(ctx, param_names[i], param_types[i], t->args[i]);
    }

    ASTNode *body = NULL;
    if (lexer_peek(l).type == TOK_LBRACE)
    {
        body = parse_block(ctx, l);
    }
    else
    {
        zpanic_at(lexer_peek(l), "Expected '{' for lambda body");
    }

    ASTNode *lambda = ast_create(NODE_LAMBDA);
    lambda->lambda.param_names = param_names;
    lambda->lambda.param_types = param_types;
    lambda->lambda.return_type = return_type;
    lambda->lambda.body = body;
    lambda->lambda.body = body;
    lambda->lambda.num_params = num_params;
    lambda->lambda.default_capture_mode = default_capture_mode;
    lambda->lambda.explicit_captures = explicit_captures;
    lambda->lambda.explicit_capture_modes = explicit_capture_modes;
    lambda->lambda.num_explicit_captures = num_explicit_captures;
    lambda->lambda.capture_modes = NULL; // Will be allocated in analysis
    lambda->lambda.lambda_id = ctx->lambda_counter++;
    lambda->lambda.is_expression = 0;
    lambda->type_info = t;
    lambda->resolved_type = type_to_string(t);
    register_lambda(ctx, lambda);
    analyze_lambda_captures(ctx, lambda);

    exit_scope(ctx);

    return lambda;
}

char *escape_c_string(const char *input)
{
    char *out = xmalloc(strlen(input) * 2 + 1);
    char *p = out;
    while (*input)
    {
        if (*input == '\\' && (input[1] == 'u' || input[1] == 'U') && input[2] == '{')
        {
            input += 3;
            uint32_t val = 0;
            while (*input && *input != '}')
            {
                val = (val << 4);
                if (*input >= '0' && *input <= '9')
                {
                    val += *input - '0';
                }
                else if (*input >= 'a' && *input <= 'f')
                {
                    val += *input - 'a' + 10;
                }
                else if (*input >= 'A' && *input <= 'F')
                {
                    val += *input - 'A' + 10;
                }
                input++;
            }
            if (*input == '}')
            {
                input++;
            }

            if (val < 128)
            {
                char c = (char)val;
                if (c == '\n')
                {
                    *p++ = '\\';
                    *p++ = 'n';
                }
                else if (c == '\r')
                {
                    *p++ = '\\';
                    *p++ = 'r';
                }
                else if (c == '\t')
                {
                    *p++ = '\\';
                    *p++ = 't';
                }
                else if (c == '"')
                {
                    *p++ = '\\';
                    *p++ = '"';
                }
                else if (c == '\\')
                {
                    *p++ = '\\';
                    *p++ = '\\';
                }
                else
                {
                    *p++ = c;
                }
            }
            else if (val < 0x800)
            {
                *p++ = (char)(0xC0 | (val >> 6));
                *p++ = (char)(0x80 | (val & 0x3F));
            }
            else if (val < 0x10000)
            {
                *p++ = (char)(0xE0 | (val >> 12));
                *p++ = (char)(0x80 | ((val >> 6) & 0x3F));
                *p++ = (char)(0x80 | (val & 0x3F));
            }
            else
            {
                *p++ = (char)(0xF0 | (val >> 18));
                *p++ = (char)(0x80 | ((val >> 12) & 0x3F));
                *p++ = (char)(0x80 | ((val >> 6) & 0x3F));
                *p++ = (char)(0x80 | (val & 0x3F));
            }
            continue;
        }

        if (*input == '\\')
        {
            *p++ = *input++;
            if (*input)
            {
                *p++ = *input++;
            }
        }
        else if (*input == '\n')
        {
            *p++ = '\\';
            *p++ = 'n';
            input++;
        }
        else if (*input == '\r')
        {
            *p++ = '\\';
            *p++ = 'r';
            input++;
        }
        else if (*input == '\t')
        {
            *p++ = '\\';
            *p++ = 't';
            input++;
        }
        else if (*input == '"')
        {
            *p++ = '\\';
            *p++ = '"';
            input++;
        }
        else
        {
            *p++ = *input++;
        }
    }
    *p = '\0';
    return out;
}

static ASTNode *create_fstring_block(ParserContext *ctx, Token parent_token, char *content, int len)
{
    ASTNode *block = ast_create(NODE_BLOCK);
    block->token = parent_token;
    block->type_info = type_new(TYPE_STRING);
    block->resolved_type = xstrdup("string");

    ASTNode *head = NULL, *tail = NULL;

    ASTNode *decl_b = ast_create(NODE_RAW_STMT);
    decl_b->token = parent_token;
    decl_b->raw_stmt.content = xstrdup("static char _b[4096]; _b[0]=0;");
    if (!head)
    {
        head = decl_b;
    }
    else
    {
        tail->next = decl_b;
    }
    tail = decl_b;

    ASTNode *decl_t = ast_create(NODE_RAW_STMT);
    decl_t->token = parent_token;
    decl_t->raw_stmt.content = xstrdup("char _t[128];");
    tail->next = decl_t;
    tail = decl_t;

    char *cur = content;
    char *end = content + len;

    while (cur < end)
    {
        char *brace = strchr(cur, '{');
        while (brace && brace < end)
        {
            if (brace > cur + 1 && (brace[-1] == 'u' || brace[-1] == 'U') && brace[-2] == '\\')
            {
                brace = strchr(brace + 1, '{');
            }
            else
            {
                break;
            }
        }
        // Check for double brace }} first
        char *dbl_close = strstr(cur, "}}");

        if (dbl_close && (!brace || dbl_close < brace))
        {
            // Check boundary
            if (dbl_close >= end)
            {
                dbl_close = NULL;
            }
        }

        if (dbl_close && (!brace || dbl_close < brace))
        {
            if (dbl_close > cur)
            {
                int seg_len = dbl_close - cur;
                ASTNode *cat = ast_create(NODE_RAW_STMT);
                cat->token = parent_token;
                char *txt = xmalloc(seg_len + 1);
                strncpy(txt, cur, seg_len);
                txt[seg_len] = 0;
                char *escaped = escape_c_string(txt);
                cat->raw_stmt.content = xmalloc(strlen(escaped) + 32);
                sprintf(cat->raw_stmt.content, "strcat(_b, \"%s\");", escaped);
                tail->next = cat;
                tail = cat;
                free(escaped);
                free(txt);
            }
            ASTNode *cat = ast_create(NODE_RAW_STMT);
            cat->token = parent_token;
            cat->raw_stmt.content = xstrdup("strcat(_b, \"}\");");
            tail->next = cat;
            tail = cat;
            cur = dbl_close + 2;
            continue;
        }

        if (!brace || brace >= end)
        {
            if (cur < end)
            {
                int seg_len = end - cur;
                ASTNode *cat = ast_create(NODE_RAW_STMT);
                cat->token = parent_token;
                char *txt = xmalloc(seg_len + 1);
                strncpy(txt, cur, seg_len);
                txt[seg_len] = 0;
                char *escaped = escape_c_string(txt);
                cat->raw_stmt.content = xmalloc(strlen(escaped) + 32);
                sprintf(cat->raw_stmt.content, "strcat(_b, \"%s\");", escaped);
                tail->next = cat;
                tail = cat;
                free(escaped);
                free(txt);
            }
            break;
        }

        if (brace > cur)
        {
            int seg_len = brace - cur;
            ASTNode *cat = ast_create(NODE_RAW_STMT);
            cat->token = parent_token;
            char *txt = xmalloc(seg_len + 1);
            strncpy(txt, cur, seg_len);
            txt[seg_len] = 0;
            char *escaped = escape_c_string(txt);
            cat->raw_stmt.content = xmalloc(strlen(escaped) + 32);
            sprintf(cat->raw_stmt.content, "strcat(_b, \"%s\");", escaped);
            tail->next = cat;
            tail = cat;
            free(escaped);
            free(txt);
        }

        if (brace + 1 < end && brace[1] == '{')
        {
            ASTNode *cat = ast_create(NODE_RAW_STMT);
            cat->token = parent_token;
            cat->raw_stmt.content = xstrdup("strcat(_b, \"{\");");
            tail->next = cat;
            tail = cat;
            cur = brace + 2;
            continue;
        }

        char *end_brace = strchr(brace, '}');
        if (!end_brace || end_brace >= end)
        {
            zpanic_at(parent_token, "Unclosed f-string brace");
        }

        char *colon = NULL;
        char *p = brace + 1;
        int depth = 1;
        while (p < end_brace)
        {
            if (*p == '{')
            {
                depth++;
            }
            if (*p == '}')
            {
                depth--;
            }
            if (depth == 1 && *p == ':' && !colon)
            {
                if ((p + 1) < end_brace && *(p + 1) == ':')
                {
                    p++;
                }
                else
                {
                    colon = p;
                }
            }
            p++;
        }

        char *expr_start = brace + 1;
        // char *expr_end = colon ? colon : end_brace;

        Lexer sub_l;
        lexer_init(&sub_l, expr_start);

        // Sync line info
        sub_l.line = parent_token.line;
        const char *last_nl = NULL;
        for (const char *c = parent_token.start; c < expr_start; c++)
        {
            if (*c == '\n')
            {
                sub_l.line++;
                last_nl = c;
            }
        }

        if (last_nl)
        {
            sub_l.col = (int)(expr_start - last_nl);
        }
        else
        {
            sub_l.col = parent_token.col + (int)(expr_start - parent_token.start);
        }

        int start_line = sub_l.line;
        int start_col = sub_l.col;

        // Check for common invalid starts before parsing
        Token peek = lexer_peek(&sub_l);
        int invalid_start = 0;

        if (peek.type == TOK_OP)
        {
            // Check for =, ==, >=, <=, ., etc.
            if (peek.len == 1 && (peek.start[0] == '=' || peek.start[0] == '.'))
            {
                invalid_start = 1;
            }
            else if (peek.len >= 2 &&
                     (strncmp(peek.start, "==", 2) == 0 || strncmp(peek.start, ">=", 2) == 0 ||
                      strncmp(peek.start, "<=", 2) == 0))
            {
                invalid_start = 1;
            }
        }
        else if (peek.type == TOK_LANGLE || peek.type == TOK_RANGLE || peek.type == TOK_RBRACE ||
                 peek.type == TOK_COLON || peek.type == TOK_COMMA)
        {
            invalid_start = 1;
        }
        else if (peek.type == TOK_IDENT)
        {
            // Check keywords if they are parsed as identifiers
            if ((peek.len == 6 && strncmp(peek.start, "return", 6) == 0) ||
                (peek.len == 5 && strncmp(peek.start, "yield", 5) == 0))
            {
                invalid_start = 1;
            }
        }

        if (invalid_start)
        {
            char err_msg[1024];
            snprintf(err_msg, sizeof(err_msg), "Invalid expression start in f-string: '%.*s'.",
                     peek.len, peek.start);

            const char *hints[] = {"braces in f-strings must be escaped with '{{' or '}}'",
                                   "use a raw string literal (r\"...\") to disable interpolation",
                                   NULL};
            zpanic_with_hints(peek, err_msg, hints);
        }

        ASTNode *expr_node = parse_expression(ctx, &sub_l);

        // Check for leftover tokens.
        Token next = lexer_peek(&sub_l);
        if (next.type != TOK_RBRACE && next.type != TOK_COLON)
        {
            if (next.type == TOK_COMMA)
            {
                const char *hints[] = {"Escape braces with '{{' and '}}' or use a raw string "
                                       "(r\"...\") to disable interpolation",
                                       NULL};
                zpanic_with_hints(
                    next, "Invalid expression in f-string. This looks like a regex quantifier.",
                    hints);
            }
            else
            {
                // Use a temporary buffer for the token to ensure safe printing
                char tok_buf[64];
                int t_len = next.len < 63 ? next.len : 63;
                strncpy(tok_buf, next.start, t_len);
                tok_buf[t_len] = '\0';
                if (next.len > 63)
                {
                    strcat(tok_buf, "...");
                }

                char err_msg[256];
                snprintf(err_msg, sizeof(err_msg),
                         "Invalid expression in f-string: expected '}' or ':', found '%s'.",
                         tok_buf);

                const char *hints[] = {
                    "braces in f-strings must be escaped with '{{' or '}}'",
                    "use a raw string literal (r\"...\") to disable interpolation", NULL};
                zpanic_with_hints(next, err_msg, hints);
            }
        }

        char *fmt = NULL;
        if (colon)
        {
            int fmt_len = end_brace - (colon + 1);
            fmt = xmalloc(fmt_len + 1);
            strncpy(fmt, colon + 1, fmt_len);
            fmt[fmt_len] = 0;
        }

        if (expr_node && expr_node->type == NODE_EXPR_VAR)
        {
            ZenSymbol *sym = find_symbol_entry(ctx, expr_node->var_ref.name);
            if (sym)
            {
                sym->is_used = 1;
            }
        }

        ASTNode *call_sprintf = ast_create(NODE_EXPR_CALL);
        call_sprintf->token = peek;
        ASTNode *callee = ast_create(NODE_EXPR_VAR);
        callee->token = peek;
        callee->var_ref.name = xstrdup("sprintf");
        call_sprintf->call.callee = callee;

        ASTNode *arg_t = ast_create(NODE_EXPR_VAR);
        arg_t->var_ref.name = xstrdup("_t");

        ASTNode *arg_fmt = NULL;
        if (fmt)
        {
            char *fmt_str = xmalloc(strlen(fmt) + 3);
            sprintf(fmt_str, "%%%s", fmt);
            arg_fmt = ast_create(NODE_EXPR_LITERAL);
            arg_fmt->literal.type_kind = LITERAL_STRING;
            arg_fmt->literal.string_val = fmt_str;
            arg_fmt->type_info = type_new(TYPE_STRING);
        }
        else
        {
            ASTNode *call_macro = ast_create(NODE_EXPR_CALL);
            call_macro->token = peek;
            ASTNode *macro_callee = ast_create(NODE_EXPR_VAR);
            macro_callee->var_ref.name = xstrdup("_z_str");
            call_macro->call.callee = macro_callee;

            // Re-parse for macro argument
            Lexer l2;
            lexer_init(&l2, expr_start);
            l2.line = start_line;
            l2.col = start_col;
            ASTNode *expr_copy = parse_expression(ctx, &l2);

            call_macro->call.args = expr_copy;
            arg_fmt = call_macro;
        }

        call_sprintf->call.args = arg_t;
        arg_t->next = arg_fmt;
        arg_fmt->next = expr_node;

        tail->next = call_sprintf;
        tail = call_sprintf;

        // strcat(_b, _t)
        ASTNode *cat_t = ast_create(NODE_RAW_STMT);
        cat_t->token = peek;
        cat_t->raw_stmt.content = xstrdup("strcat(_b, _t);");
        tail->next = cat_t;
        tail = cat_t;

        cur = end_brace + 1;
        if (fmt)
        {
            free(fmt);
        }
    }

    ASTNode *ret_b = ast_create(NODE_RAW_STMT);
    ret_b->token = parent_token;
    ret_b->raw_stmt.content = xstrdup("_b;");
    tail->next = ret_b;
    tail = ret_b;

    block->block.statements = head;
    return block;
}

// Check if the suffix is a valid integer suffix
static int is_valid_int_suffix(const char *s)
{
    if (!s || !*s)
    {
        return 1;
    }
    // Standard and Zen C suffixes
    const char *valid[] = {"u",   "U",   "l",   "L",   "ul",    "UL",  "uL",  "Ul",  "lu",
                           "LU",  "lU",  "Lu",  "ll",  "LL",    "ull", "ULL", "uLL", "Ull",
                           "llu", "LLu", "lLu", "llU", "u8",    "u16", "u32", "u64", "usize",
                           "i8",  "i16", "i32", "i64", "isize", NULL};
    for (int i = 0; valid[i]; i++)
    {
        if (strcmp(s, valid[i]) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static int is_valid_float_suffix(const char *s)
{
    if (!s || !*s)
    {
        return 1;
    }
    const char *valid[] = {"f", "F", "d", "D", NULL};
    for (int i = 0; valid[i]; i++)
    {
        if (strcmp(s, valid[i]) == 0)
        {
            return 1;
        }
    }
    return 0;
}

// Parse integer literal (decimal, hex, binary, octal)
static ASTNode *parse_int_literal(Token t)
{
    ASTNode *node = ast_create(NODE_EXPR_LITERAL);
    node->token = t;
    node->literal.type_kind = LITERAL_INT;
    node->type_info = type_new(TYPE_INT);
    char *s = token_strdup(t);
    int write_idx = 0;
    for (int read_idx = 0; s[read_idx]; read_idx++)
    {
        if (s[read_idx] != '_')
        {
            s[write_idx++] = s[read_idx];
        }
    }
    s[write_idx] = '\0';

    unsigned long long val;
    char *endptr = NULL;

    if (t.len > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B'))
    {
        val = strtoull(s + 2, &endptr, 2);
    }
    else if (t.len > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        val = strtoull(s + 2, &endptr, 16);
    }
    else if (t.len > 2 && s[0] == '0' && (s[1] == 'o' || s[1] == 'O'))
    {
        val = strtoull(s + 2, &endptr, 8);
    }
    else
    {
        val = strtoull(s, &endptr, 10);
    }

    // Validate suffix
    if (endptr && *endptr)
    {
        if (!is_valid_int_suffix(endptr))
        {
            char err[256];
            snprintf(err, sizeof(err), "Invalid integer literal suffix: '%s'", endptr);
            zpanic_at(t, "%s", err);
        }
    }

    node->literal.int_val = val;
    free(s);
    return node;
}

// Parse float literal
static ASTNode *parse_float_literal(Token t)
{
    ASTNode *node = ast_create(NODE_EXPR_LITERAL);
    node->token = t;
    node->literal.type_kind = LITERAL_FLOAT;
    char *s = token_strdup(t);
    int write_idx = 0;
    for (int read_idx = 0; s[read_idx]; read_idx++)
    {
        if (s[read_idx] != '_')
        {
            s[write_idx++] = s[read_idx];
        }
    }
    s[write_idx] = '\0';

    node->literal.float_val = atof(s);
    node->type_info = type_new(TYPE_F64);

    // Check for suffix
    char *endptr = NULL;
    strtod(s, &endptr);
    if (endptr && *endptr)
    {
        if (!is_valid_float_suffix(endptr))
        {
            char err[256];
            snprintf(err, sizeof(err), "Invalid float literal suffix: '%s'", endptr);
            zpanic_at(t, "%s", err);
        }
        if (strcmp(endptr, "f") == 0 || strcmp(endptr, "F") == 0)
        {
            node->type_info->kind = TYPE_F32;
        }
    }

    free(s);
    return node;
}

// Parse string literal
static ASTNode *parse_string_literal(ParserContext *ctx, Token t)
{
    int is_multi = (t.len >= 6 && t.start[0] == '"' && t.start[1] == '"' && t.start[2] == '"');
    int str_offset = is_multi ? 3 : 1;
    int str_len = t.len - (is_multi ? 6 : 2);

    // Check for implicit interpolation
    int has_interpolation = 0;
    for (int i = str_offset; i < str_offset + str_len; i++)
    {
        if (t.start[i] == '{')
        {
            // Ignore if part of \u{ or \U{
            if (i >= str_offset + 2 && (t.start[i - 1] == 'u' || t.start[i - 1] == 'U') &&
                t.start[i - 2] == '\\')
            {
                continue;
            }
            has_interpolation = 1;
            break;
        }
    }

    if (has_interpolation)
    {
        ASTNode *node = create_fstring_block(ctx, t, (char *)t.start + str_offset, str_len);
        return node;
    }

    ASTNode *node = ast_create(NODE_EXPR_LITERAL);
    node->token = t;
    node->literal.type_kind = LITERAL_STRING;

    char *tmp = xmalloc(str_len + 1);
    strncpy(tmp, t.start + str_offset, str_len);
    tmp[str_len] = 0;

    node->literal.string_val = escape_c_string(tmp);
    free(tmp);

    node->type_info = type_new(TYPE_STRING);
    return node;
}

// Parse f-string literal
static ASTNode *parse_fstring_literal(ParserContext *ctx, Token t)
{
    int is_multi = (t.len >= 7 && t.start[1] == '"' && t.start[2] == '"' && t.start[3] == '"');
    int str_offset = is_multi ? 4 : 2;
    int str_len = t.len - (is_multi ? 7 : 3);

    ASTNode *node = create_fstring_block(ctx, t, (char *)t.start + str_offset, str_len);
    return node;
}

// Parse character literal
static ASTNode *parse_char_literal(Token t)
{
    ASTNode *node = ast_create(NODE_EXPR_LITERAL);
    node->token = t;
    node->literal.type_kind = LITERAL_CHAR;
    node->literal.string_val = token_strdup(t);

    // Decode character value
    uint32_t val = 0;
    const char *s = t.start + 1; // skip '
    if (*s == '\\')
    {
        s++;
        switch (*s)
        {
        case 'n':
            val = '\n';
            break;
        case 'r':
            val = '\r';
            break;
        case 't':
            val = '\t';
            break;
        case '\\':
            val = '\\';
            break;
        case '\'':
            val = '\'';
            break;
        case '"':
            val = '"';
            break;
        case '0':
            val = '\0';
            break;
        case 'u':
        case 'U':
        {
            if (s[1] == '{')
            {
                s += 2;
                while (*s && *s != '}' && *s != '\'')
                {
                    val = (val << 4);
                    if (*s >= '0' && *s <= '9')
                    {
                        val += *s - '0';
                    }
                    else if (*s >= 'a' && *s <= 'f')
                    {
                        val += *s - 'a' + 10;
                    }
                    else if (*s >= 'A' && *s <= 'F')
                    {
                        val += *s - 'A' + 10;
                    }
                    s++;
                }
                node->type_info = type_new(TYPE_RUNE);
                node->literal.int_val = val;
                return node;
            }
        }
        /* fallthrough */
        default:
            val = (unsigned char)*s;
            break;
        }
        node->type_info = type_new(TYPE_CHAR);
    }
    else
    {
        unsigned char first = (unsigned char)*s;
        if ((first & 0x80) == 0)
        {
            val = first;
            node->type_info = type_new(TYPE_CHAR);
        }
        else if ((first & 0xE0) == 0xC0)
        {
            val = ((first & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
            node->type_info = type_new(TYPE_RUNE);
        }
        else if ((first & 0xF0) == 0xE0)
        {
            val = ((first & 0x0F) << 12) | (((unsigned char)s[1] & 0x3F) << 6) |
                  ((unsigned char)s[2] & 0x3F);
            node->type_info = type_new(TYPE_RUNE);
        }
        else if ((first & 0xF8) == 0xF0)
        {
            val = ((first & 0x07) << 18) | (((unsigned char)s[1] & 0x3F) << 12) |
                  (((unsigned char)s[2] & 0x3F) << 6) | ((unsigned char)s[3] & 0x3F);
            node->type_info = type_new(TYPE_RUNE);
        }
        else
        {
            val = first;
            node->type_info = type_new(TYPE_I8);
        }
    }

    node->literal.int_val = val;
    return node;
}

// Parse sizeof expression: sizeof(type) or sizeof(expr)
static ASTNode *parse_sizeof_expr(ParserContext *ctx, Lexer *l)
{
    if (lexer_peek(l).type != TOK_LPAREN)
    {
        zpanic_at(lexer_peek(l), "Expected ( after sizeof");
    }
    lexer_next(l);

    int pos = l->pos;
    int col = l->col;
    int line = l->line;
    TypeUsage *old_pending = ctx->pending_type_validations;
    SliceType *old_slices = ctx->used_slices;
    TupleType *old_tuples = ctx->used_tuples;

    Type *ty = parse_type_formal(ctx, l);

    int is_actually_var = 0;
    if (ty->kind != TYPE_UNKNOWN)
    {
        Type *base = ty;
        while (base->inner)
        {
            base = base->inner;
        }
        if (base->kind == TYPE_STRUCT && base->name)
        {
            if (!is_primitive_type_name(base->name) && !find_struct_def(ctx, base->name) &&
                !find_type_alias_node(ctx, base->name) && find_symbol_entry(ctx, base->name))
            {
                is_actually_var = 1;
            }
        }
    }

    ASTNode *node;
    if (ty->kind != TYPE_UNKNOWN && !is_actually_var && lexer_peek(l).type == TOK_RPAREN)
    {
        lexer_next(l);
        char *ts = type_to_string(ty);
        node = ast_create(NODE_EXPR_SIZEOF);
        node->size_of.target_type = ts;
        node->size_of.target_type_info = ty;
        node->size_of.is_type = 1;
        node->size_of.expr = NULL;
        node->type_info = type_new(TYPE_USIZE);
    }
    else
    {
        ctx->pending_type_validations = old_pending;
        ctx->used_slices = old_slices;
        ctx->used_tuples = old_tuples;

        l->pos = pos;
        l->col = col;
        l->line = line;
        ASTNode *ex = parse_expression(ctx, l);
        if (lexer_next(l).type != TOK_RPAREN)
        {
            zpanic_at(lexer_peek(l), "Expected ) after sizeof identifier");
        }
        node = ast_create(NODE_EXPR_SIZEOF);
        node->size_of.target_type = NULL;
        node->size_of.target_type_info = NULL;
        node->size_of.is_type = 0;
        node->size_of.expr = ex;
        node->type_info = type_new(TYPE_USIZE);
    }
    return node;
}

// Parse typeof expression: typeof(type) or typeof(expr)
static ASTNode *parse_typeof_expr(ParserContext *ctx, Lexer *l)
{
    if (lexer_peek(l).type != TOK_LPAREN)
    {
        zpanic_at(lexer_peek(l), "Expected ( after typeof");
    }
    lexer_next(l);

    int pos = l->pos;
    int col = l->col;
    int line = l->line;
    TypeUsage *old_pending = ctx->pending_type_validations;
    SliceType *old_slices = ctx->used_slices;
    TupleType *old_tuples = ctx->used_tuples;

    Type *ty = parse_type_formal(ctx, l);

    int is_actually_var = 0;
    if (ty->kind != TYPE_UNKNOWN)
    {
        Type *base = ty;
        while (base->inner)
        {
            base = base->inner;
        }
        if (base->kind == TYPE_STRUCT && base->name)
        {
            if (!is_primitive_type_name(base->name) && !find_struct_def(ctx, base->name) &&
                !find_type_alias_node(ctx, base->name) && find_symbol_entry(ctx, base->name))
            {
                is_actually_var = 1;
            }
        }
    }

    ASTNode *node;
    if (ty->kind != TYPE_UNKNOWN && !is_actually_var && lexer_peek(l).type == TOK_RPAREN)
    {
        lexer_next(l);
        char *ts = type_to_string(ty);
        node = ast_create(NODE_TYPEOF);
        node->size_of.target_type = ts;
        node->size_of.target_type_info = ty;
        node->size_of.is_type = 1;
        node->size_of.expr = NULL;
    }
    else
    {
        ctx->pending_type_validations = old_pending;
        ctx->used_slices = old_slices;
        ctx->used_tuples = old_tuples;

        l->pos = pos;
        l->col = col;
        l->line = line;
        ASTNode *ex = parse_expression(ctx, l);
        if (lexer_next(l).type != TOK_RPAREN)
        {
            zpanic_at(lexer_peek(l), "Expected ) after typeof expression");
        }
        node = ast_create(NODE_TYPEOF);
        node->size_of.target_type = NULL;
        node->size_of.target_type_info = NULL;
        node->size_of.is_type = 0;
        node->size_of.expr = ex;
    }
    return node;
}

// Parse intrinsic expression: @type_name(T), @fields(T)
static ASTNode *parse_intrinsic(ParserContext *ctx, Lexer *l)
{
    Token ident = lexer_next(l);
    if (ident.type != TOK_IDENT)
    {
        zpanic_at(ident, "Expected intrinsic name after @");
    }

    int kind = -1;
    if (strncmp(ident.start, "type_name", 9) == 0 && ident.len == 9)
    {
        kind = 0;
    }
    else if (strncmp(ident.start, "fields", 6) == 0 && ident.len == 6)
    {
        kind = 1;
    }
    else
    {
        zpanic_at(ident, "Unknown intrinsic @%.*s", ident.len, ident.start);
    }

    Token lparen = lexer_next(l);
    if (lparen.type != TOK_LPAREN)
    {
        zpanic_at(lparen, "Expected ( after intrinsic");
    }

    Type *target = parse_type_formal(ctx, l);

    Token rparen = lexer_next(l);
    if (rparen.type != TOK_RPAREN)
    {
        zpanic_at(rparen, "Expected ) after intrinsic type");
    }

    ASTNode *node = ast_create(NODE_REFLECTION);
    node->reflection.kind = kind;
    node->reflection.target_type = target;
    node->type_info = (kind == 0) ? type_new(TYPE_STRING) : type_new_ptr(type_new(TYPE_VOID));
    return node;
}

ASTNode *parse_primary(ParserContext *ctx, Lexer *l)
{
    ASTNode *node = NULL;

    Token peek = lexer_peek(l);
    if (ctx->in_method_with_self && peek.type == TOK_OP && peek.len == 1 && peek.start[0] == '.')
    {
        Token dot_tok = lexer_next(l); // consume .
        Token member_tok = lexer_peek(l);
        if (member_tok.type == TOK_IDENT)
        {
            lexer_next(l); // consume identifier
            // Create self node
            ASTNode *self_node = ast_create(NODE_EXPR_VAR);
            self_node->var_ref.name = xstrdup("self");
            self_node->token = dot_tok;

            Type *inner_type = type_new(TYPE_STRUCT);
            if (ctx->current_impl_struct)
            {
                inner_type->name = xstrdup(ctx->current_impl_struct);
            }
            else
            {
                inner_type->name = xstrdup("Self");
            }
            self_node->type_info = type_new_ptr(inner_type);
            if (ctx->current_impl_struct)
            {
                char *rt = xmalloc(strlen(ctx->current_impl_struct) + 2);
                sprintf(rt, "%s*", ctx->current_impl_struct);
                self_node->resolved_type = rt;
            }
            else
            {
                self_node->resolved_type = xstrdup("Self*");
            }

            node = ast_create(NODE_EXPR_MEMBER);
            node->member.target = self_node;
            node->member.field = token_strdup(member_tok);
            node->member.is_pointer_access = 1;
            node->token = dot_tok;

            // Handle chained member access (.x.y) or method calls (.method())
            return node;
        }
        else
        {
            // Not an identifier after dot - error or other handling
            zpanic_at(dot_tok, "Expected identifier after '.' in self shorthand");
        }
    }

    Token t = lexer_next(l);

    // ** Prefixes **

    // Literals
    if (t.type == TOK_INT)
    {
        node = parse_int_literal(t);
    }
    else if (t.type == TOK_FLOAT)
    {
        node = parse_float_literal(t);
    }
    else if (t.type == TOK_STRING)
    {
        node = parse_string_literal(ctx, t);
    }
    else if (t.type == TOK_FSTRING)
    {
        node = parse_fstring_literal(ctx, t);
    }
    else if (t.type == TOK_RAW_STRING)
    {
        node = ast_create(NODE_EXPR_LITERAL);
        node->token = t;
        node->literal.type_kind = LITERAL_RAW_STRING;
        node->literal.string_val = xmalloc(t.len - 2);
        strncpy(node->literal.string_val, t.start + 2, t.len - 3);
        node->literal.string_val[t.len - 3] = 0;
        node->type_info = type_new(TYPE_STRING);
    }
    else if (t.type == TOK_CHAR)
    {
        node = parse_char_literal(t);
    }

    else if (t.type == TOK_SIZEOF)
    {
        node = parse_sizeof_expr(ctx, l);
    }

    else if (t.type == TOK_IDENT && strncmp(t.start, "typeof", 6) == 0 && t.len == 6)
    {
        node = parse_typeof_expr(ctx, l);
    }

    else if (t.type == TOK_AT)
    {
        node = parse_intrinsic(ctx, l);
    }

    else if (t.type == TOK_IDENT && strncmp(t.start, "if", 2) == 0 && t.len == 2)
    {
        // If-expression: parse inline (already consumed 'if')
        ASTNode *cond = parse_expression(ctx, l);

        ASTNode *then_b = NULL;
        if (lexer_peek(l).type == TOK_LBRACE)
        {
            then_b = parse_block(ctx, l);
        }
        else
        {
            enter_scope(ctx);
            ASTNode *s = parse_statement(ctx, l);
            exit_scope(ctx);
            then_b = ast_create(NODE_BLOCK);
            then_b->block.statements = s;
        }

        ASTNode *else_b = NULL;
        skip_comments(l);
        if (lexer_peek(l).type == TOK_IDENT && strncmp(lexer_peek(l).start, "else", 4) == 0 &&
            lexer_peek(l).len == 4)
        {
            lexer_next(l); // eat 'else'
            if (lexer_peek(l).type == TOK_LBRACE)
            {
                else_b = parse_block(ctx, l);
            }
            else
            {
                enter_scope(ctx);
                ASTNode *s = parse_statement(ctx, l);
                exit_scope(ctx);
                else_b = ast_create(NODE_BLOCK);
                else_b->block.statements = s;
            }
        }

        node = ast_create(NODE_IF);
        node->token = t;
        node->if_stmt.condition = cond;
        node->if_stmt.then_body = then_b;
        node->if_stmt.else_body = else_b;
    }

    else if (t.type == TOK_IDENT && strncmp(t.start, "match", 5) == 0 && t.len == 5)
    {
        ASTNode *expr = parse_expression(ctx, l);
        skip_comments(l);
        {
            Token inner_t = lexer_next(l);
            if (inner_t.type != TOK_LBRACE)
            {
                zpanic_at(inner_t, "Expected { after match expression");
            }
        }

        ASTNode *h = 0, *tl = 0;
        while (1)
        {
            skip_comments(l);
            if (lexer_peek(l).type == TOK_RBRACE)
            {
                break;
            }
            if (lexer_peek(l).type == TOK_COMMA)
            {
                lexer_next(l);
            }

            skip_comments(l);
            if (lexer_peek(l).type == TOK_RBRACE)
            {
                break;
            }

            char pat_buf[1024] = {0};
            Token mpeek;
            while (1)
            {
                mpeek = lexer_peek(l);
                if (mpeek.type == TOK_ARROW || (mpeek.type == TOK_IDENT && mpeek.len == 2 &&
                                                strncmp(mpeek.start, "if", 2) == 0))
                {
                    break;
                }
                if (mpeek.type == TOK_LPAREN && pat_buf[0] != 0)
                {
                    break;
                }

                Token pt = lexer_next(l);
                char *s = token_strdup(pt);
                if (pt.type == TOK_COMMA)
                {
                    strncat(pat_buf, "|", sizeof(pat_buf) - strlen(pat_buf) - 1);
                }
                else
                {
                    strncat(pat_buf, s, sizeof(pat_buf) - strlen(pat_buf) - 1);
                }
                free(s);

                if (mpeek.type == TOK_IDENT && strcmp(pat_buf, "_") == 0)
                {
                    break;
                }
            }

            char *pattern = xstrdup(pat_buf);
            int is_default = (strcmp(pattern, "_") == 0);

            // Handle Destructuring: Ok(v) or Rect(w, h)
            char **bindings = NULL;
            int *binding_refs = NULL;
            int binding_count = 0;
            int is_destructure = 0; // Initialize here

            // Assuming pattern_count is 1 for now, or needs to be determined
            // For single identifier patterns, pattern_count would be 1.
            // This logic needs to be adjusted if `pattern_count` is not available or needs to be
            // calculated. For now, assuming `pattern_count == 1` is implicitly true for single
            // token patterns.
            if (!is_default && lexer_peek(l).type == TOK_LPAREN)
            {
                lexer_next(l);                           // eat (
                bindings = xmalloc(sizeof(char *) * 8);  // Initial capacity
                binding_refs = xmalloc(sizeof(int) * 8); // unused but consistent

                while (1)
                {
                    int is_r = 0;
                    if (lexer_peek(l).type == TOK_IDENT && lexer_peek(l).len == 3 &&
                        strncmp(lexer_peek(l).start, "ref", 3) == 0)
                    {
                        lexer_next(l); // eat ref
                        is_r = 1;
                    }
                    Token b = lexer_next(l);
                    if (b.type != TOK_IDENT)
                    {
                        zpanic_at(b, "Expected binding");
                    }
                    bindings[binding_count] = token_strdup(b);
                    binding_refs[binding_count] = is_r;
                    binding_count++;
                    if (lexer_peek(l).type == TOK_COMMA)
                    {
                        lexer_next(l);
                        continue;
                    }
                    break;
                }
                if (lexer_next(l).type != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected )");
                }
                is_destructure = 1;
            }

            ASTNode *guard = NULL;
            skip_comments(l);
            if (lexer_peek(l).type == TOK_IDENT && strncmp(lexer_peek(l).start, "if", 2) == 0)
            {
                lexer_next(l);
                guard = parse_expression(ctx, l);
            }

            skip_comments(l);
            if (lexer_next(l).type != TOK_ARROW)
            {
                zpanic_at(lexer_peek(l), "Expected '=>'");
            }

            // Create scope for the case to hold the binding
            enter_scope(ctx);
            if (binding_count > 0)
            {
                for (int i = 0; i < binding_count; i++)
                {
                    add_symbol(ctx, bindings[i], NULL,
                               NULL); // Let inference handle it or default to void*?
                }
            }

            ASTNode *body;
            skip_comments(l);
            Token pk = lexer_peek(l);
            if (pk.type == TOK_LBRACE)
            {
                body = parse_block(ctx, l);
            }
            else if (pk.type == TOK_ASSERT ||
                     (pk.type == TOK_IDENT && strncmp(pk.start, "assert", 6) == 0))
            {
                body = parse_assert(ctx, l);
            }
            else if (pk.type == TOK_IDENT && strncmp(pk.start, "return", 6) == 0)
            {
                body = parse_return(ctx, l);
            }
            else
            {
                body = parse_expression(ctx, l);
            }

            exit_scope(ctx);

            ASTNode *c = ast_create(NODE_MATCH_CASE);
            c->token = pk;
            c->match_case.pattern = pattern;
            c->match_case.binding_names = bindings;      // New multi-binding field
            c->match_case.binding_count = binding_count; // New binding count field
            c->match_case.binding_refs = binding_refs;
            c->match_case.is_destructuring = is_destructure;
            c->match_case.guard = guard;
            c->match_case.body = body;
            c->match_case.is_default = is_default;
            if (!h)
            {
                h = c;
            }
            else
            {
                tl->next = c;
            }
            tl = c;
        }
        lexer_next(l);
        node = ast_create(NODE_MATCH);
        node->match_stmt.expr = expr;
        node->match_stmt.cases = h;
    }

    else if (t.type == TOK_IDENT)
    {
        if (t.len == 2 && strncmp(t.start, "fn", 2) == 0 &&
            (lexer_peek(l).type == TOK_LPAREN || lexer_peek(l).type == TOK_LBRACKET))
        {
            l->pos -= t.len;
            l->col -= t.len;
            return parse_lambda(ctx, l);
        }

        char *ident = token_strdup(t);

        if (lexer_peek(l).type == TOK_OP && lexer_peek(l).start[0] == '!' && lexer_peek(l).len == 1)
        {
            node = parse_macro_call(ctx, l, ident);
            if (node)
            {
                free(ident);
                return node;
            }
        }

        if (lexer_peek(l).type == TOK_COLON)
        {
            Lexer lookahead = *l;
            lexer_next(&lookahead); // consume ':'

            int found_arrow = 0;
            Lexer arrow_scan = lookahead;
            int angles = 0;
            while (1)
            {
                Token tk = lexer_peek(&arrow_scan);
                if (tk.type == TOK_EOF || tk.type == TOK_SEMICOLON || tk.type == TOK_COMMA ||
                    tk.type == TOK_LBRACE || tk.type == TOK_RBRACE ||
                    (tk.type == TOK_OP && tk.start[0] == '=' && tk.len == 1))
                {
                    break;
                }
                if (tk.type == TOK_LANGLE)
                {
                    angles++;
                }
                else if (tk.type == TOK_RANGLE)
                {
                    if (angles > 0)
                    {
                        angles--;
                    }
                }
                else if (tk.type == TOK_ARROW && angles == 0)
                {
                    found_arrow = 1;
                    break;
                }
                lexer_next(&arrow_scan);
            }

            if (found_arrow)
            {
                Type *param_type = parse_type_formal(ctx, &lookahead);
                if (lexer_peek(&lookahead).type == TOK_ARROW)
                {
                    *l = lookahead;
                    lexer_next(l); // consume '->'
                    char **params = xmalloc(sizeof(char *));
                    Type **param_types = xmalloc(sizeof(Type *));
                    params[0] = ident; // pass ownership of ident
                    param_types[0] = param_type;
                    return parse_arrow_lambda_multi(ctx, l, params, param_types, 1, 0);
                }
                // If it fails to match exactly, we'll just fall through to the rest of the parsing
            }
        }

        if (lexer_peek(l).type == TOK_ARROW)
        {
            lexer_next(l);
            return parse_arrow_lambda_single(ctx, l, ident, 0);
        }

        char *acc = ident;
        while (1)
        {
            int changed = 0;
            if (lexer_peek(l).type == TOK_DCOLON)
            {
                lexer_next(l);
                Token suffix = lexer_next(l);
                if (suffix.type != TOK_IDENT)
                {
                    zpanic_at(suffix, "Expected identifier after ::");
                }

                SelectiveImport *si =
                    (!ctx->current_module_prefix) ? find_selective_import(ctx, acc) : NULL;
                if (si)
                {
                    char *tmp =
                        xmalloc(strlen(si->source_module) + strlen(si->symbol) + suffix.len + 4);

                    char base[256];
                    sprintf(base, "%s_%s", si->source_module, si->symbol);
                    ASTNode *def = find_struct_def(ctx, base);
                    int is_type = (def != NULL);

                    if (is_type)
                    {
                        // Check Enum Variant
                        int is_variant = 0;
                        if (def->type == NODE_ENUM)
                        {
                            ASTNode *v = def->enm.variants;
                            char sbuf[128];
                            strncpy(sbuf, suffix.start, suffix.len);
                            sbuf[suffix.len] = 0;
                            while (v)
                            {
                                if (strcmp(v->variant.name, sbuf) == 0)
                                {
                                    is_variant = 1;
                                    break;
                                }
                                v = v->next;
                            }
                        }
                        if (is_variant)
                        {
                            sprintf(tmp, "%s_%s_%.*s", si->source_module, si->symbol, suffix.len,
                                    suffix.start);
                        }
                        else
                        {
                            sprintf(tmp, "%s_%s__%.*s", si->source_module, si->symbol, suffix.len,
                                    suffix.start);
                        }
                    }
                    else
                    {
                        sprintf(tmp, "%s_%s_%.*s", si->source_module, si->symbol, suffix.len,
                                suffix.start);
                    }

                    free(acc);
                    acc = tmp;
                }
                else
                {
                    Module *mod = find_module(ctx, acc);
                    if (mod)
                    {
                        if (mod->is_c_header)
                        {
                            char *tmp = xmalloc(suffix.len + 1);
                            strncpy(tmp, suffix.start, suffix.len);
                            tmp[suffix.len] = 0;
                            free(acc);
                            acc = tmp;

                            register_extern_symbol(ctx, acc);
                        }

                        else
                        {
                            char *tmp = xmalloc(strlen(mod->base_name) + suffix.len + 2);
                            snprintf(tmp, strlen(mod->base_name) + suffix.len + 2, "%s_",
                                     mod->base_name);
                            strncat(tmp, suffix.start, suffix.len);
                            free(acc);
                            acc = tmp;
                        }
                    }
                    else
                    {
                        char *tmp = xmalloc(strlen(acc) + suffix.len + 3);

                        ASTNode *def = find_struct_def(ctx, acc);

                        int is_opaque_alias = 0;
                        if (!def)
                        {
                            TypeAlias *ta = find_type_alias_node(ctx, acc);
                            if (ta)
                            {
                                is_opaque_alias = ta->is_opaque;
                                if (!is_opaque_alias)
                                {
                                    const char *aliased = find_type_alias(ctx, acc);
                                    if (aliased)
                                    {
                                        free(acc);
                                        acc = xstrdup(aliased);
                                        def = find_struct_def(ctx, acc);
                                    }
                                }
                            }
                        }

                        if (def || is_opaque_alias)
                        {
                            int is_variant = 0;
                            if (def && def->type == NODE_ENUM)
                            {
                                ASTNode *v = def->enm.variants;
                                size_t slen = suffix.len;
                                char *sbuf = xmalloc(slen + 1);
                                strncpy(sbuf, suffix.start, slen);
                                sbuf[slen] = 0;
                                while (v)
                                {
                                    if (strcmp(v->variant.name, sbuf) == 0)
                                    {
                                        is_variant = 1;
                                        break;
                                    }
                                    v = v->next;
                                }
                                free(sbuf);
                            }
                            if (is_variant)
                            {
                                sprintf(tmp, "%s_%.*s", acc, suffix.len, suffix.start);
                            }
                            else
                            {
                                sprintf(tmp, "%s__%.*s", acc, suffix.len, suffix.start);
                            }
                        }
                        else
                        {
                            int handled_as_generic = 0;
                            for (int i = 0; i < ctx->known_generics_count; i++)
                            {
                                char *gname = ctx->known_generics[i];
                                int glen = strlen(gname);
                                if (strncmp(acc, gname, glen) == 0 && acc[glen] == '_')
                                {
                                    ASTNode *tpl_def = find_struct_def(ctx, gname);
                                    if (tpl_def)
                                    {
                                        int is_variant = 0;
                                        if (tpl_def->type == NODE_ENUM)
                                        {
                                            ASTNode *v = tpl_def->enm.variants;
                                            size_t slen = suffix.len;
                                            char *sbuf = xmalloc(slen + 1);
                                            strncpy(sbuf, suffix.start, slen);
                                            sbuf[slen] = 0;
                                            while (v)
                                            {
                                                if (strcmp(v->variant.name, sbuf) == 0)
                                                {
                                                    is_variant = 1;
                                                    break;
                                                }
                                                v = v->next;
                                            }
                                            free(sbuf);
                                        }
                                        int resolved = 0;
                                        if (is_variant)
                                        {
                                            sprintf(tmp, "%s_%.*s", acc, suffix.len, suffix.start);
                                            resolved = 1;
                                        }
                                        else
                                        {
                                            size_t ih_len = strlen(acc) + suffix.len + 3;
                                            char *inherent_name = xmalloc(ih_len);
                                            snprintf(inherent_name, ih_len, "%s__%.*s", acc,
                                                     suffix.len, suffix.start);

                                            if (find_func(ctx, inherent_name))
                                            {
                                                free(tmp);
                                                tmp = inherent_name;
                                                resolved = 1;
                                            }
                                            else
                                            {
                                                free(inherent_name);
                                                GenericImplTemplate *it = ctx->impl_templates;
                                                while (it)
                                                {
                                                    if (strcmp(it->struct_name, gname) == 0)
                                                    {
                                                        char *tname = NULL;
                                                        if (it->impl_node &&
                                                            it->impl_node->type == NODE_IMPL_TRAIT)
                                                        {
                                                            tname = it->impl_node->impl_trait
                                                                        .trait_name;
                                                        }
                                                        if (tname)
                                                        {
                                                            size_t cand_len = strlen(acc) +
                                                                              strlen(tname) +
                                                                              suffix.len + 5;
                                                            char *cand = xmalloc(cand_len);
                                                            snprintf(cand, cand_len, "%s__%s_%.*s",
                                                                     acc, tname, suffix.len,
                                                                     suffix.start);

                                                            if (find_func(ctx, cand))
                                                            {
                                                                free(tmp);
                                                                tmp = cand;
                                                                resolved = 1;
                                                                break;
                                                            }
                                                            free(cand);
                                                        }
                                                    }
                                                    it = it->next;
                                                }
                                            }
                                            if (!resolved)
                                            {
                                                sprintf(tmp, "%s__%.*s", acc, suffix.len,
                                                        suffix.start);
                                            }
                                        }
                                        handled_as_generic = 1;
                                        break; // Found and handled
                                    }
                                }
                            }
                            // Explicit check for Vec to ensure it works
                            if (!handled_as_generic && strncmp(acc, "Vec_", 4) == 0)
                            {
                                sprintf(tmp, "%s__%.*s", acc, suffix.len, suffix.start);
                                handled_as_generic = 1;
                            }

                            // Also check registered templates list
                            if (!handled_as_generic)
                            {
                                GenericTemplate *gt = ctx->templates;
                                while (gt)
                                {
                                    char *gname = gt->name;
                                    int glen = strlen(gname);
                                    if ((strncmp(acc, gname, glen) == 0 && acc[glen] == '_') ||
                                        strcmp(acc, gname) == 0)
                                    {
                                        ASTNode *tpl_def = gt->struct_node;
                                        if (tpl_def)
                                        {
                                            int is_variant = 0;
                                            if (tpl_def->type == NODE_ENUM)
                                            {
                                                ASTNode *v = tpl_def->enm.variants;
                                                char sbuf[128];
                                                strncpy(sbuf, suffix.start, suffix.len);
                                                sbuf[suffix.len] = 0;
                                                while (v)
                                                {
                                                    if (strcmp(v->variant.name, sbuf) == 0)
                                                    {
                                                        is_variant = 1;
                                                        break;
                                                    }
                                                    v = v->next;
                                                }
                                            }
                                            if (is_variant)
                                            {
                                                sprintf(tmp, "%s_%.*s", acc, suffix.len,
                                                        suffix.start);
                                            }
                                            else
                                            {
                                                sprintf(tmp, "%s__%.*s", acc, suffix.len,
                                                        suffix.start);
                                            }
                                            handled_as_generic = 1;
                                            break;
                                        }
                                    }
                                    gt = gt->next;
                                }
                            }

                            if (!handled_as_generic)
                            {
                                sprintf(tmp, "%s_%.*s", acc, suffix.len, suffix.start);
                            }
                        }

                        free(acc);
                        acc = tmp;
                    }
                }
                changed = 1;
            }

            if (lexer_peek(l).type == TOK_LANGLE)
            {
                Lexer lookahead = *l;
                lexer_next(&lookahead);

                int valid_generic = 0;
                int saved_speculative = ctx->is_speculative;
                ctx->is_speculative = 1;
                while (1)
                {
                    parse_type(ctx, &lookahead);
                    if (lexer_peek(&lookahead).type == TOK_COMMA)
                    {
                        lexer_next(&lookahead);
                        continue;
                    }
                    if (lexer_peek(&lookahead).type == TOK_RANGLE)
                    {
                        valid_generic = 1;
                    }
                    break;
                }
                ctx->is_speculative = saved_speculative;

                if (valid_generic)
                {
                    lexer_next(l); // eat <

                    char **concrete_types = xmalloc(sizeof(char *) * 8);
                    char **unmangled_types = xmalloc(sizeof(char *) * 8);
                    int arg_count = 0;

                    while (1)
                    {
                        Type *formal_type = parse_type_formal(ctx, l);
                        concrete_types[arg_count] = type_to_string(formal_type);
                        unmangled_types[arg_count] = type_to_string(formal_type);
                        arg_count++;

                        if (lexer_peek(l).type == TOK_COMMA)
                        {
                            lexer_next(l);
                            continue;
                        }
                        break;
                    }
                    lexer_next(l); // eat >

                    int is_struct = 0;
                    GenericTemplate *st = ctx->templates;
                    while (st)
                    {
                        if (strcmp(st->name, acc) == 0)
                        {
                            is_struct = 1;
                            break;
                        }
                        st = st->next;
                    }
                    if (!is_struct && (strcmp(acc, "Result") == 0 || strcmp(acc, "Option") == 0))
                    {
                        is_struct = 1;
                    }

                    if (is_struct)
                    {
                        // Calculate mangled length
                        size_t mangled_len = strlen(acc) + 1;
                        for (int i = 0; i < arg_count; ++i)
                        {
                            char *clean = sanitize_mangled_name(concrete_types[i]);
                            mangled_len += 1 + strlen(clean);
                            free(clean);
                        }
                        char *mangled = xmalloc(mangled_len);
                        strcpy(mangled, acc);
                        for (int i = 0; i < arg_count; ++i)
                        {
                            char *clean = sanitize_mangled_name(concrete_types[i]);
                            strcat(mangled, "_");
                            strcat(mangled, clean);
                            free(clean);
                        }

                        int is_generic_dep = 0;
                        for (int i = 0; i < arg_count; ++i)
                        {
                            for (int k = 0; k < ctx->known_generics_count; ++k)
                            {
                                if (strcmp(concrete_types[i], ctx->known_generics[k]) == 0)
                                {
                                    is_generic_dep = 1;
                                    break;
                                }
                            }
                        }

                        if (arg_count == 1)
                        {
                            // Single-arg: only instantiate if not generic dependent
                            if (!is_generic_dep)
                            {
                                instantiate_generic(ctx, acc, concrete_types[0], unmangled_types[0],
                                                    t);
                            }
                            free(acc);
                            acc = mangled;
                        }
                        else
                        {
                            // Multi-arg struct instantiation
                            if (!is_generic_dep)
                            {
                                instantiate_generic_multi(ctx, acc, concrete_types, arg_count, t);
                            }
                            free(acc);
                            acc = mangled;
                        }
                    }
                    else
                    {
                        // Function Template
                        // Join types with comma
                        char full_concrete[1024] = {0};
                        char full_unmangled[1024] = {0};

                        for (int i = 0; i < arg_count; ++i)
                        {
                            if (i > 0)
                            {
                                strcat(full_concrete, ",");
                                strcat(full_unmangled, ",");
                            }
                            strcat(full_concrete, concrete_types[i]);
                            strcat(full_unmangled, unmangled_types[i]);
                        }

                        char *m =
                            instantiate_function_template(ctx, acc, full_concrete, full_unmangled);
                        if (m)
                        {
                            free(acc);
                            acc = m;
                        }
                        else
                        {
                            zpanic_at(t, "Unknown generic %s", acc);
                        }
                    }

                    // Cleanup
                    for (int i = 0; i < arg_count; ++i)
                    {
                        free(concrete_types[i]);
                        free(unmangled_types[i]);
                    }
                    free(concrete_types);
                    free(unmangled_types);

                    changed = 1;
                }
            }
            if (!changed)
            {
                break;
            }
        }

        if (lexer_peek(l).type == TOK_LBRACE)
        {
            int is_struct_init = 0;
            Lexer pl = *l;
            lexer_next(&pl);
            Token fi = lexer_peek(&pl);

            if (fi.type == TOK_RBRACE)
            {
                // Empty struct init often conflicts with block start (e.g. if x == y {})
                // We allow it only if we verify 'acc' is a struct name.
                if (find_struct_def(ctx, acc) || is_known_generic(ctx, acc) ||
                    is_primitive_type_name(acc))
                {
                    is_struct_init = 1;
                }
                else
                {
                    // Fallback: Check if it's a generic instantiation (e.g. Optional_T)
                    // We check if 'acc' starts with any known struct name followed by '_'
                    StructRef *sr = ctx->parsed_structs_list;
                    while (sr)
                    {
                        if (sr->node && sr->node->type == NODE_STRUCT)
                        {
                            size_t len = strlen(sr->node->strct.name);
                            if (strncmp(acc, sr->node->strct.name, len) == 0 && acc[len] == '_')
                            {
                                is_struct_init = 1;
                                break;
                            }
                        }
                        sr = sr->next;
                    }

                    if (!is_struct_init && global_user_structs)
                    {
                        ASTNode *gn = global_user_structs;
                        while (gn)
                        {
                            if (gn->type == NODE_STRUCT)
                            {
                                size_t len = strlen(gn->strct.name);
                                if (strncmp(acc, gn->strct.name, len) == 0 && acc[len] == '_')
                                {
                                    is_struct_init = 1;
                                    break;
                                }
                            }
                            gn = gn->next;
                        }
                    }

                    if (!is_struct_init)
                    {
                        is_struct_init = 0;
                    }
                }
            }
            else if (fi.type == TOK_IDENT)
            {
                lexer_next(&pl);
                if (lexer_peek(&pl).type == TOK_COLON)
                {
                    is_struct_init = 1;
                }
            }
            else
            {
                // Vector positional init: e.g. f32x4{1.0, 2.0, 3.0, 4.0}
                // First token is not an ident (it's a literal/expr), check if this is a vector type
                ASTNode *vec_def = find_struct_def(ctx, acc);
                if (vec_def && vec_def->type == NODE_STRUCT && vec_def->type_info &&
                    vec_def->type_info->kind == TYPE_VECTOR)
                {
                    is_struct_init = 1;
                }
            }

            // Allow primitive types to be initialized with positional values e.g., int{5}
            if (!is_struct_init && is_primitive_type_name(acc))
            {
                is_struct_init = 1;
            }

            if (is_struct_init)
            {
                // Special case for primitive types (e.g. i32{})
                if (is_primitive_type_name(acc))
                {
                    lexer_next(l); // Eat {
                    if (lexer_peek(l).type == TOK_RBRACE)
                    {
                        lexer_next(l); // Eat }
                        // Return 0 for empty primitive init
                        node = ast_create(NODE_EXPR_LITERAL);
                        node->literal.type_kind = LITERAL_INT;
                        node->literal.int_val = 0;
                        // Determine type kind from name
                        TypeKind tk = get_primitive_type_kind(acc);
                        if (tk == TYPE_UNKNOWN)
                        {
                            tk = TYPE_INT; // fallback
                        }

                        if (tk == TYPE_F32 || tk == TYPE_F64 || tk == TYPE_FLOAT)
                        {
                            node->literal.type_kind = LITERAL_FLOAT;
                            node->literal.float_val = 0.0;
                        }

                        node->type_info = type_new(tk);
                        return node;
                    }
                    else
                    {
                        node = parse_expression(ctx, l);
                        if (lexer_peek(l).type != TOK_RBRACE)
                        {
                            zpanic_at(lexer_peek(l), "Expected '}' after primitive initialization");
                        }
                        lexer_next(l); // Eat }

                        if (is_trait(acc))
                        {
                            return transform_to_trait_object(ctx, acc, node);
                        }

                        ASTNode *cast = ast_create(NODE_EXPR_CAST);
                        cast->cast.target_type = xstrdup(acc);
                        cast->cast.expr = node;

                        TypeKind tk = get_primitive_type_kind(acc);
                        if (tk == TYPE_UNKNOWN)
                        {
                            tk = TYPE_INT;
                        }

                        cast->type_info = type_new(tk);
                        return cast;
                    }
                }

                char *struct_name = acc;
                if (!ctx->current_module_prefix)
                {
                    SelectiveImport *si = find_selective_import(ctx, acc);
                    if (si)
                    {
                        struct_name = xmalloc(strlen(si->source_module) + strlen(si->symbol) + 2);
                        sprintf(struct_name, "%s_%s", si->source_module, si->symbol);
                    }
                }
                if (struct_name == acc && ctx->current_module_prefix && !is_known_generic(ctx, acc))
                {
                    char *prefixed = xmalloc(strlen(ctx->current_module_prefix) + strlen(acc) + 2);
                    sprintf(prefixed, "%s_%s", ctx->current_module_prefix, acc);
                    struct_name = prefixed;
                }

                // Opaque Struct Check
                ASTNode *def = find_struct_def(ctx, struct_name);
                if (def && def->type == NODE_STRUCT && def->strct.is_opaque)
                {
                    if (!def->strct.defined_in_file ||
                        (g_current_filename &&
                         strcmp(def->strct.defined_in_file, g_current_filename) != 0))
                    {
                        zpanic_at(lexer_peek(l),
                                  "Cannot initialize opaque struct '%s' outside its module",
                                  struct_name);
                    }
                }
                lexer_next(l);
                node = ast_create(NODE_EXPR_STRUCT_INIT);
                node->token = t;
                node->struct_init.struct_name = struct_name;
                Type *init_type = type_new(TYPE_STRUCT);
                init_type->name = xstrdup(struct_name);
                node->type_info = init_type;

                ASTNode *head = NULL, *tail = NULL;
                int first = 1;

                // Check if this is a vector type for positional init
                int is_vector_init = 0;
                if (def && def->type == NODE_STRUCT && def->type_info &&
                    def->type_info->kind == TYPE_VECTOR)
                {
                    // Peek ahead: if first value is not followed by ':',
                    // this is positional vector init like f32x4{1.0, 2.0, 3.0, 4.0}
                    Lexer saved = *l;
                    Token first_tok = lexer_next(l);
                    if (first_tok.type != TOK_RBRACE)
                    {
                        Token maybe_colon = lexer_peek(l);
                        if (maybe_colon.type != TOK_COLON)
                        {
                            is_vector_init = 1;
                        }
                    }
                    *l = saved; // restore lexer state
                }

                if (is_vector_init)
                {
                    // Parse positional values: f32x4{1.0, 2.0, 3.0, 4.0}
                    int idx = 0;
                    while (lexer_peek(l).type != TOK_RBRACE)
                    {
                        if (idx > 0 && lexer_peek(l).type == TOK_COMMA)
                        {
                            lexer_next(l);
                        }
                        if (lexer_peek(l).type == TOK_RBRACE)
                        {
                            break;
                        }
                        ASTNode *val = parse_expression(ctx, l);
                        ASTNode *assign = ast_create(NODE_VAR_DECL);
                        char name[16];
                        snprintf(name, sizeof(name), "_v%d", idx);
                        assign->token = t;
                        assign->var_decl.name = xstrdup(name);
                        assign->var_decl.init_expr = val;
                        if (!head)
                        {
                            head = assign;
                        }
                        else
                        {
                            tail->next = assign;
                        }
                        tail = assign;
                        idx++;
                    }
                    lexer_next(l); // eat '}'
                }
                else
                {
                    while (lexer_peek(l).type != TOK_RBRACE)
                    {
                        if (!first && lexer_peek(l).type == TOK_COMMA)
                        {
                            lexer_next(l);
                        }
                        if (lexer_peek(l).type == TOK_RBRACE)
                        {
                            break;
                        }
                        Token fn = lexer_next(l);
                        if (lexer_next(l).type != TOK_COLON)
                        {
                            zpanic_at(lexer_peek(l), "Expected :");
                        }
                        ASTNode *val = parse_expression(ctx, l);
                        ASTNode *assign = ast_create(NODE_VAR_DECL);
                        assign->token = t;
                        assign->var_decl.name = token_strdup(fn);
                        assign->var_decl.init_expr = val;
                        if (!head)
                        {
                            head = assign;
                        }
                        else
                        {
                            tail->next = assign;
                        }
                        tail = assign;
                        first = 0;
                    }
                    lexer_next(l);
                }
                node->struct_init.fields = head;

                GenericTemplate *gtpl = ctx->templates;
                while (gtpl)
                {
                    if (strcmp(gtpl->name, acc) == 0 || strcmp(gtpl->name, struct_name) == 0)
                    {
                        break;
                    }
                    gtpl = gtpl->next;
                }
                if (gtpl && gtpl->struct_node && gtpl->struct_node->type == NODE_STRUCT)
                {
                    const char *gen_param = (gtpl->struct_node->strct.generic_param_count > 0)
                                                ? gtpl->struct_node->strct.generic_params[0]
                                                : "T";

                    char *inferred = NULL;
                    ASTNode *init_field = head;
                    while (init_field && !inferred)
                    {
                        if (init_field->var_decl.init_expr && init_field->var_decl.name)
                        {
                            ASTNode *tpl_field = gtpl->struct_node->strct.fields;
                            while (tpl_field)
                            {
                                if (tpl_field->type == NODE_FIELD && tpl_field->field.name &&
                                    strcmp(tpl_field->field.name, init_field->var_decl.name) == 0)
                                {
                                    break;
                                }
                                tpl_field = tpl_field->next;
                            }

                            if (tpl_field && tpl_field->field.type)
                            {
                                const char *ft = tpl_field->field.type;
                                Type *val_type = init_field->var_decl.init_expr->type_info;

                                if (strcmp(ft, gen_param) == 0 && val_type)
                                {
                                    inferred = type_to_string(val_type);
                                }
                                else if (val_type && strncmp(ft, "Slice_", 6) == 0 &&
                                         strcmp(ft + 6, gen_param) == 0)
                                {
                                    if (val_type->kind == TYPE_ARRAY && val_type->inner)
                                    {
                                        inferred = type_to_string(val_type->inner);
                                    }
                                }
                                else if (val_type && ft[0] == '[')
                                {
                                    size_t ftl = strlen(ft);
                                    if (ftl >= 3 && ft[ftl - 1] == ']')
                                    {
                                        char inner_name[256];
                                        size_t inner_len = ftl - 2;
                                        if (inner_len < sizeof(inner_name))
                                        {
                                            memcpy(inner_name, ft + 1, inner_len);
                                            inner_name[inner_len] = '\0';
                                            if (strcmp(inner_name, gen_param) == 0 &&
                                                val_type->kind == TYPE_ARRAY && val_type->inner)
                                            {
                                                inferred = type_to_string(val_type->inner);
                                            }
                                        }
                                    }
                                }
                                else if (val_type)
                                {
                                    size_t ftl = strlen(ft);
                                    if (ftl >= 2 && ft[ftl - 1] == '*')
                                    {
                                        char base_name[256];
                                        size_t base_len = ftl - 1;
                                        if (base_len < sizeof(base_name))
                                        {
                                            memcpy(base_name, ft, base_len);
                                            base_name[base_len] = '\0';
                                            if (strcmp(base_name, gen_param) == 0 &&
                                                val_type->kind == TYPE_POINTER && val_type->inner)
                                            {
                                                inferred = type_to_string(val_type->inner);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        init_field = init_field->next;
                    }

                    if (inferred)
                    {
                        Token t_tok = lexer_peek(l);
                        instantiate_generic(ctx, gtpl->name, inferred, inferred, t_tok);

                        char *clean = sanitize_mangled_name(inferred);
                        size_t mlen = strlen(gtpl->name) + 1 + strlen(clean) + 1;
                        char *mangled = xmalloc(mlen);
                        sprintf(mangled, "%s_%s", gtpl->name, clean);
                        free(clean);

                        node->struct_init.struct_name = mangled;
                        struct_name = mangled;

                        free(inferred);
                    }
                }

                Type *st = type_new(TYPE_STRUCT);
                st->name = xstrdup(struct_name);
                node->type_info = st;
                return node;
            }
        }

        FuncSig *sig = find_func(ctx, acc);
        if (strcmp(acc, "readln") == 0 && lexer_peek(l).type == TOK_LPAREN)
        {
            lexer_next(l);
            ASTNode *args[16];
            int ac = 0;
            if (lexer_peek(l).type != TOK_RPAREN)
            {
                while (1)
                {
                    args[ac++] = parse_expression(ctx, l);
                    if (lexer_peek(l).type == TOK_COMMA)
                    {
                        lexer_next(l);
                    }
                    else
                    {
                        break;
                    }
                }
            }
            if (lexer_next(l).type != TOK_RPAREN)
            {
                zpanic_at(lexer_peek(l), "Expected )");
            }

            if (ac == 0)
            {
                node = ast_create(NODE_EXPR_CALL);
                node->token = t;
                ASTNode *callee = ast_create(NODE_EXPR_VAR);
                callee->var_ref.name = xstrdup("_z_readln_raw");
                node->call.callee = callee;
                node->type_info = type_new(TYPE_STRING);
            }
            else
            {
                char fmt[256];
                fmt[0] = 0;
                for (int i = 0; i < ac; i++)
                {
                    Type *inner_t = args[i]->type_info;
                    if (!inner_t && args[i]->type == NODE_EXPR_VAR)
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
                                  (inner_t->inner->kind == TYPE_CHAR ||
                                   inner_t->inner->kind == TYPE_U8 ||
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

                node = ast_create(NODE_EXPR_CALL);
                node->token = t;
                ASTNode *callee = ast_create(NODE_EXPR_VAR);
                callee->var_ref.name = xstrdup("_z_scan_helper");
                node->call.callee = callee;
                node->type_info = type_new(TYPE_INT); // Returns count

                ASTNode *fmt_node = ast_create(NODE_EXPR_LITERAL);
                fmt_node->literal.type_kind = LITERAL_STRING; // string
                fmt_node->literal.string_val = xstrdup(fmt);

                ASTNode *head = fmt_node, *tail = fmt_node;

                for (int i = 0; i < ac; i++)
                {
                    // Create Unary & (AddressOf) node wrapping the arg
                    ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                    addr->unary.op = xstrdup("&");
                    addr->unary.operand = args[i];
                    // Link
                    tail->next = addr;
                    tail = addr;
                }
                node->call.args = head;
            }
            free(acc);
        }
        else if (sig && lexer_peek(l).type == TOK_LPAREN)
        {
            lexer_next(l);
            ASTNode *head = NULL, *tail = NULL;
            int args_provided = 0;
            char **arg_names = NULL;
            int arg_names_cap = 0;
            int has_named = 0;

            if (lexer_peek(l).type != TOK_RPAREN)
            {
                while (1)
                {
                    char *arg_name = NULL;

                    Token t1 = lexer_peek(l);
                    if (t1.type == TOK_IDENT)
                    {
                        Token t2 = lexer_peek2(l);
                        if (t2.type == TOK_COLON)
                        {
                            arg_name = token_strdup(t1);
                            has_named = 1;
                            lexer_next(l);
                            lexer_next(l);
                        }
                    }

                    ASTNode *arg = parse_expression(ctx, l);

                    // Move Semantics Logic
                    check_move_usage(ctx, arg, arg ? arg->token : t1);
                    if (arg && arg->type == NODE_EXPR_VAR)
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
                    if (sig && args_provided < sig->total_args && arg)
                    {
                        Type *expected = sig->arg_types[args_provided];

                        if (expected && expected->name && is_trait(expected->name))
                        {
                            arg = transform_to_trait_object(ctx, expected->name, arg);
                        }
                    }

                    if (!head)
                    {
                        head = arg;
                    }
                    else
                    {
                        tail->next = arg;
                    }
                    tail = arg;
                    args_provided++;

                    if (args_provided > arg_names_cap)
                    {
                        arg_names_cap = arg_names_cap ? arg_names_cap * 2 : 8;
                        arg_names = xrealloc(arg_names, arg_names_cap * sizeof(char *));
                    }
                    arg_names[args_provided - 1] = arg_name;

                    if (lexer_peek(l).type == TOK_COMMA)
                    {
                        lexer_next(l);
                    }
                    else
                    {
                        break;
                    }
                }
            }
            if (lexer_next(l).type != TOK_RPAREN)
            {
                zpanic_at(lexer_peek(l), "Expected )");
            }
            for (int i = args_provided; i < sig->total_args; i++)
            {
                if (sig->defaults[i])
                {
                    Lexer def_l;
                    lexer_init(&def_l, sig->defaults[i]);
                    ASTNode *def = parse_expression(ctx, &def_l);
                    Type *expected = sig->arg_types[i];
                    if (expected && expected->name && is_trait(expected->name))
                    {
                        Type *arg_type = def->type_info
                                             ? def->type_info
                                             : ((def->type == NODE_EXPR_VAR)
                                                    ? find_symbol_type_info(ctx, def->var_ref.name)
                                                    : NULL);

                        if (!arg_type && def->type == NODE_EXPR_UNARY &&
                            strcmp(def->unary.op, "&") == 0)
                        {
                            if (def->unary.operand->type == NODE_EXPR_VAR)
                            {
                                Type *inner =
                                    find_symbol_type_info(ctx, def->unary.operand->var_ref.name);
                                if (inner && inner->kind == TYPE_STRUCT)
                                {
                                    if (check_impl(ctx, expected->name, inner->name))
                                    {
                                        ASTNode *init = ast_create(NODE_EXPR_STRUCT_INIT);
                                        init->struct_init.struct_name = xstrdup(expected->name);

                                        Type *trait_type = type_new(TYPE_STRUCT);
                                        trait_type->name = xstrdup(expected->name);
                                        init->type_info = trait_type;

                                        ASTNode *f_self = ast_create(NODE_VAR_DECL);
                                        f_self->var_decl.name = xstrdup("self");
                                        f_self->var_decl.init_expr = def;

                                        char vtable_name[256];
                                        sprintf(vtable_name, "%s_%s_VTable", inner->name,
                                                expected->name);

                                        ASTNode *vtable_var = ast_create(NODE_EXPR_VAR);
                                        vtable_var->var_ref.name = xstrdup(vtable_name);

                                        ASTNode *vtable_ref = ast_create(NODE_EXPR_UNARY);
                                        vtable_ref->unary.op = xstrdup("&");
                                        vtable_ref->unary.operand = vtable_var;

                                        ASTNode *f_vtable = ast_create(NODE_VAR_DECL);
                                        f_vtable->var_decl.name = xstrdup("vtable");
                                        f_vtable->var_decl.init_expr = vtable_ref;

                                        f_self->next = f_vtable;
                                        init->struct_init.fields = f_self;

                                        def = init;
                                    }
                                }
                            }
                        }
                        else if (arg_type && arg_type->kind == TYPE_POINTER && arg_type->inner &&
                                 arg_type->inner->kind == TYPE_STRUCT)
                        {
                            if (check_impl(ctx, expected->name, arg_type->inner->name))
                            {
                                ASTNode *init = ast_create(NODE_EXPR_STRUCT_INIT);
                                init->struct_init.struct_name = xstrdup(expected->name);

                                Type *trait_type = type_new(TYPE_STRUCT);
                                trait_type->name = xstrdup(expected->name);
                                init->type_info = trait_type;

                                ASTNode *f_self = ast_create(NODE_VAR_DECL);
                                f_self->var_decl.name = xstrdup("self");
                                f_self->var_decl.init_expr = def;

                                char vtable_name[256];
                                sprintf(vtable_name, "%s_%s_VTable", arg_type->inner->name,
                                        expected->name);

                                ASTNode *vtable_var = ast_create(NODE_EXPR_VAR);
                                vtable_var->var_ref.name = xstrdup(vtable_name);

                                ASTNode *vtable_ref = ast_create(NODE_EXPR_UNARY);
                                vtable_ref->unary.op = xstrdup("&");
                                vtable_ref->unary.operand = vtable_var;

                                ASTNode *f_vtable = ast_create(NODE_VAR_DECL);
                                f_vtable->var_decl.name = xstrdup("vtable");
                                f_vtable->var_decl.init_expr = vtable_ref;

                                f_self->next = f_vtable;
                                init->struct_init.fields = f_self;

                                def = init;
                            }
                        }
                    }

                    if (!head)
                    {
                        head = def;
                    }
                    else
                    {
                        tail->next = def;
                    }
                    tail = def;
                }
            }

            if (has_named && arg_names)
            {
                ASTNode *def = find_function_definition(ctx, sig->name);
                if (def)
                {
                    validate_named_arguments(t, sig->name, arg_names, args_provided, def);
                }
            }

            node = ast_create(NODE_EXPR_CALL);
            node->token = t; // Set source token
            ASTNode *callee = ast_create(NODE_EXPR_VAR);
            callee->var_ref.name = acc;
            node->call.callee = callee;
            node->call.args = head;
            node->call.arg_names = has_named ? arg_names : NULL;
            node->call.arg_count = args_provided;
            if (sig)
            {
                node->definition_token = sig->decl_token;
            }
            if (sig->is_async)
            {
                Type *async_type = type_new(TYPE_STRUCT);
                async_type->name = xstrdup("Async");
                node->type_info = async_type;

                if (sig->ret_type)
                {
                    char *inner = type_to_string(sig->ret_type);
                    if (inner)
                    {
                        char buf[512];
                        snprintf(buf, 511, "Async<%s>", inner);
                        node->resolved_type = xstrdup(buf);
                        async_type->name = xstrdup(buf); // HACK: Persist generic info in name
                        free(inner);
                    }
                    else
                    {
                        node->resolved_type = xstrdup("Async");
                    }
                }
                else
                {
                    node->resolved_type = xstrdup("Async");
                }
                node->type_info = async_type;
            }
            else if (sig->ret_type)
            {
                node->type_info = sig->ret_type;
                node->resolved_type = type_to_string(sig->ret_type);
            }
            else
            {
                node->resolved_type = xstrdup("void");
            }
        }
        else if (!sig && !find_symbol_entry(ctx, acc) && lexer_peek(l).type == TOK_LPAREN)
        {
            lexer_next(l); // eat (
            ASTNode *head = NULL, *tail = NULL;
            char **arg_names = NULL;
            int args_provided = 0;
            int has_named = 0;

            if (lexer_peek(l).type != TOK_RPAREN)
            {
                while (1)
                {
                    char *arg_name = NULL;

                    // Check for named argument: name: value
                    Token t1 = lexer_peek(l);
                    if (t1.type == TOK_IDENT)
                    {
                        Token t2 = lexer_peek2(l);
                        if (t2.type == TOK_COLON)
                        {
                            arg_name = token_strdup(t1);
                            has_named = 1;
                            lexer_next(l);
                            lexer_next(l);
                        }
                    }

                    ASTNode *arg = parse_expression(ctx, l);

                    // Move Semantics Logic
                    check_move_usage(ctx, arg, arg ? arg->token : t1);
                    if (arg && arg->type == NODE_EXPR_VAR)
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

                    if (!head)
                    {
                        head = arg;
                    }
                    else
                    {
                        tail->next = arg;
                    }
                    tail = arg;
                    args_provided++;

                    arg_names = xrealloc(arg_names, args_provided * sizeof(char *));
                    arg_names[args_provided - 1] = arg_name;

                    if (lexer_peek(l).type == TOK_COMMA)
                    {
                        lexer_next(l);
                    }
                    else
                    {
                        break;
                    }
                }
            }
            if (lexer_next(l).type != TOK_RPAREN)
            {
                zpanic_at(lexer_peek(l), "Expected )");
            }

            node = ast_create(NODE_EXPR_CALL);
            node->token = t;
            ASTNode *callee = ast_create(NODE_EXPR_VAR);
            callee->token = t;
            callee->var_ref.name = acc;
            node->call.callee = callee;
            node->call.args = head;
            node->call.arg_names = has_named ? arg_names : NULL;
            node->call.arg_count = args_provided;

            GenericFuncTemplate *tpl = find_func_template(ctx, acc);
            if (tpl && tpl->func_node && args_provided > 0 && head)
            {
                char *inferred_type = NULL;
                ASTNode *func_node = tpl->func_node;
                char *gen_param = tpl->generic_param;

                if (func_node->func.arg_types && gen_param && !strchr(gen_param, ','))
                {
                    ASTNode *actual_arg = head;
                    for (int i = 0; i < func_node->func.arg_count && actual_arg; i++)
                    {
                        Type *formal = func_node->func.arg_types[i];
                        if (!formal)
                        {
                            actual_arg = actual_arg->next;
                            continue;
                        }

                        Type *actual_type = actual_arg->type_info;
                        if (!actual_type && actual_arg->type == NODE_EXPR_VAR)
                        {
                            actual_type = find_symbol_type_info(ctx, actual_arg->var_ref.name);
                        }
                        if (!actual_type)
                        {
                            actual_arg = actual_arg->next;
                            continue;
                        }

                        if (formal->kind == TYPE_STRUCT && formal->name &&
                            strcmp(formal->name, gen_param) == 0)
                        {
                            inferred_type = type_to_string(actual_type);
                            break;
                        }

                        if (formal->kind == TYPE_ARRAY && formal->inner &&
                            formal->inner->kind == TYPE_STRUCT && formal->inner->name &&
                            strcmp(formal->inner->name, gen_param) == 0)
                        {
                            if (actual_type->kind == TYPE_ARRAY && actual_type->inner)
                            {
                                inferred_type = type_to_string(actual_type->inner);
                            }
                            break;
                        }

                        if (formal->kind == TYPE_POINTER && formal->inner &&
                            formal->inner->kind == TYPE_STRUCT && formal->inner->name &&
                            strcmp(formal->inner->name, gen_param) == 0)
                        {
                            if (actual_type->kind == TYPE_POINTER && actual_type->inner)
                            {
                                inferred_type = type_to_string(actual_type->inner);
                            }
                            break;
                        }

                        actual_arg = actual_arg->next;
                    }
                }

                if (inferred_type)
                {
                    char *mangled =
                        instantiate_function_template(ctx, acc, inferred_type, inferred_type);
                    if (mangled)
                    {
                        // Rewrite the callee to point to the instantiated function
                        free(callee->var_ref.name);
                        callee->var_ref.name = mangled;

                        // Update type info from the newly registered FuncSig
                        FuncSig *inst_sig = find_func(ctx, mangled);
                        if (inst_sig)
                        {
                            node->definition_token = inst_sig->decl_token;
                            if (inst_sig->ret_type)
                            {
                                node->type_info = inst_sig->ret_type;
                                node->resolved_type = type_to_string(inst_sig->ret_type);
                            }
                            else
                            {
                                node->resolved_type = xstrdup("void");
                            }
                        }
                        else
                        {
                            node->resolved_type = xstrdup("unknown");
                        }
                    }
                    else
                    {
                        node->resolved_type = xstrdup("unknown");
                    }
                    free(inferred_type);
                }
                else
                {
                    // Could not infer type - leave as unknown
                    node->resolved_type = xstrdup("unknown");
                }
            }
            else
            {
                // Unknown return type - let codegen infer it
                node->resolved_type = xstrdup("unknown");
            }
            // Fall through to Postfix
        }
        else
        {
            ZenSymbol *sym = find_symbol_entry(ctx, acc);
            if (sym && sym->is_def && sym->is_const_value)
            {
                sym->is_used = 1;

                // Constant Folding for 'def', emits literal
                node = ast_create(NODE_EXPR_LITERAL);
                node->token = t;
                node->literal.type_kind = LITERAL_INT; // INT (assumed for now from const_int_val)
                node->literal.int_val = sym->const_int_val;
                node->type_info = type_new(TYPE_INT);
                // No need for resolution
            }
            else
            {
                node = ast_create(NODE_EXPR_VAR);
                node->token = t; // Set source token
                node->var_ref.name = acc;
                node->type_info = find_symbol_type_info(ctx, acc);

                if (sym)
                {
                    sym->is_used = 1;
                    node->definition_token = sym->decl_token;
                }

                char *type_str = find_symbol_type(ctx, acc);

                if (type_str)
                {
                    node->resolved_type = type_str;
                    node->var_ref.suggestion = NULL;
                }
                else
                {
                    node->resolved_type = xstrdup("unknown");
                    if (sym || should_suppress_undef_warning(ctx, acc))
                    {
                        node->var_ref.suggestion = NULL;
                    }
                    else
                    {
                        node->var_ref.suggestion = find_similar_symbol(ctx, acc);
                    }
                }
            }
        }
    }

    else if (t.type == TOK_LPAREN)
    {

        Lexer lookahead = *l;
        int is_lambda = 0;
        char **params = xmalloc(sizeof(char *) * 16);
        Type **param_types = xmalloc(sizeof(Type *) * 16);
        int nparams = 0;

        while (1)
        {
            if (lexer_peek(&lookahead).type != TOK_IDENT)
            {
                if (nparams == 0 && lexer_peek(&lookahead).type == TOK_RPAREN)
                {
                    lexer_next(&lookahead);
                    if (lexer_peek(&lookahead).type == TOK_ARROW)
                    {
                        lexer_next(&lookahead);
                        is_lambda = 1;
                    }
                }
                break;
            }
            params[nparams] = token_strdup(lexer_next(&lookahead));
            param_types[nparams] = NULL;
            lookahead.pos = lookahead.pos;

            if (lexer_peek(&lookahead).type == TOK_COLON)
            {
                lexer_next(&lookahead);
                while (1)
                {
                    Token tk = lexer_peek(&lookahead);
                    if (tk.type == TOK_EOF || tk.type == TOK_COMMA || tk.type == TOK_RPAREN)
                    {
                        break;
                    }
                    lexer_next(&lookahead);
                }
            }
            nparams++;

            Token sep = lexer_peek(&lookahead);
            if (sep.type == TOK_COMMA)
            {
                lexer_next(&lookahead);
                continue;
            }
            else if (sep.type == TOK_RPAREN)
            {
                lexer_next(&lookahead);
                if (lexer_peek(&lookahead).type == TOK_ARROW)
                {
                    lexer_next(&lookahead);
                    is_lambda = 1;
                }
                break;
            }
            else
            {
                break;
            }
        }

        if (is_lambda)
        {
            Lexer actual_parse = *l;
            if (nparams > 0)
            {
                for (int i = 0; i < nparams; i++)
                {
                    lexer_next(&actual_parse);
                    if (lexer_peek(&actual_parse).type == TOK_COLON)
                    {
                        lexer_next(&actual_parse);
                        param_types[i] = parse_type_formal(ctx, &actual_parse);
                    }
                    Token sep = lexer_next(&actual_parse);
                    if (sep.type == TOK_RPAREN)
                    {
                        lexer_next(&actual_parse);
                        break;
                    }
                }
            }
            else
            {
                lexer_next(&actual_parse); // consume ')'
                lexer_next(&actual_parse); // consume '->'
            }
            *l = actual_parse;

            ASTNode *node_multi = parse_arrow_lambda_multi(ctx, l, params, param_types, nparams, 0);
            free(param_types);
            return node_multi;
        }
        else
        {
            for (int i = 0; i < nparams; i++)
            {
                free(params[i]);
            }
            free(params);
            free(param_types);
        }

        int saved = l->pos;
        if (lexer_peek(l).type == TOK_IDENT)
        {
            Lexer cast_look = *l;
            lexer_next(&cast_look);
            while (lexer_peek(&cast_look).type == TOK_DCOLON)
            { // handle A::B
                lexer_next(&cast_look);
                if (lexer_peek(&cast_look).type == TOK_IDENT)
                {
                    lexer_next(&cast_look);
                }
                else
                {
                    break;
                }
            }
            while (lexer_peek(&cast_look).type == TOK_OP && lexer_peek(&cast_look).start[0] == '*')
            {
                Token st = lexer_peek(&cast_look);
                int valid = 1;
                for (int i = 0; i < st.len; i++)
                {
                    if (st.start[i] != '*')
                    {
                        valid = 0;
                    }
                }
                if (!valid)
                {
                    break;
                }
                lexer_next(&cast_look);
            }

            if (lexer_peek(&cast_look).type == TOK_RPAREN)
            {
                lexer_next(&cast_look);
                Token next = lexer_peek(&cast_look);
                if (next.type == TOK_STRING || next.type == TOK_INT || next.type == TOK_FLOAT ||
                    next.type == TOK_CHAR || next.type == TOK_FSTRING ||
                    next.type == TOK_RAW_STRING ||
                    (next.type == TOK_OP && (is_token(next, "&") || is_token(next, "*") ||
                                             is_token(next, "**") || is_token(next, "!"))) ||
                    next.type == TOK_IDENT || next.type == TOK_LPAREN)
                {

                    Type *cast_type_obj = parse_type_formal(ctx, l);
                    char *cast_type = type_to_string(cast_type_obj);
                    {
                        Token inner_t = lexer_next(l);
                        if (inner_t.type != TOK_RPAREN)
                        {
                            zpanic_at(inner_t, "Expected ) after cast");
                        }
                    }
                    ASTNode *target = parse_expr_prec(ctx, l, PREC_UNARY);

                    if (is_trait(cast_type))
                    {
                        free(cast_type);
                        return transform_to_trait_object(ctx, type_to_string(cast_type_obj),
                                                         target);
                    }

                    node = ast_create(NODE_EXPR_CAST);
                    node->token = next;
                    node->cast.target_type = cast_type;
                    node->cast.expr = target;
                    node->type_info = cast_type_obj;
                    return node;
                }
            }
        }
        l->pos = saved;

        ASTNode *expr = parse_expression(ctx, l);

        if (lexer_peek(l).type == TOK_COMMA)
        {
            return parse_tuple_expression(ctx, l, NULL, expr);
        }
        else
        {
            if (lexer_next(l).type != TOK_RPAREN)
            {
                zpanic_at(lexer_peek(l), "Expected )");
            }
            node = expr;
        }
    }

    else if (t.type == TOK_LBRACKET)
    {
        int default_capture_mode = -1;
        Lexer capture_look = *l;
        if (lexer_peek(&capture_look).type == TOK_OP && lexer_peek(&capture_look).len == 1)
        {
            char op = lexer_peek(&capture_look).start[0];
            if (op == '&')
            {
                default_capture_mode = 1;
            }
            else if (op == '=')
            {
                default_capture_mode = 0;
            }

            if (default_capture_mode != -1)
            {
                lexer_next(&capture_look);
                if (lexer_peek(&capture_look).type == TOK_RBRACKET)
                {
                    lexer_next(&capture_look);

                    Token next = lexer_peek(&capture_look);
                    if (next.type == TOK_IDENT)
                    {
                        lexer_next(&capture_look);
                        if (lexer_peek(&capture_look).type == TOK_ARROW)
                        {
                            lexer_next(l);
                            lexer_next(l);
                            Token param = lexer_next(l);
                            lexer_next(l);
                            return parse_arrow_lambda_single(ctx, l, token_strdup(param),
                                                             default_capture_mode);
                        }
                    }
                    else if (next.type == TOK_LPAREN)
                    {
                        int depth = 0;
                        Lexer scan = capture_look;
                        int is_arrow = 0;
                        while (1)
                        {
                            Token tk = lexer_next(&scan);
                            if (tk.type == TOK_EOF)
                            {
                                break;
                            }
                            if (tk.type == TOK_LPAREN)
                            {
                                depth++;
                            }
                            else if (tk.type == TOK_RPAREN)
                            {
                                depth--;
                                if (depth == 0)
                                {
                                    if (lexer_peek(&scan).type == TOK_ARROW)
                                    {
                                        is_arrow = 1;
                                    }
                                    break;
                                }
                            }
                        }

                        if (is_arrow)
                        {
                            lexer_next(l); // consume '&'
                            lexer_next(l); // consume ']'

                            lexer_next(l); // consume '('
                            char **params = xmalloc(sizeof(char *) * 16);
                            Type **param_types = xmalloc(sizeof(Type *) * 16);
                            int nparams = 0;
                            while (1)
                            {
                                if (lexer_peek(l).type != TOK_IDENT)
                                {
                                    break;
                                }
                                params[nparams] = token_strdup(lexer_next(l));
                                param_types[nparams] = NULL;

                                if (lexer_peek(l).type == TOK_COLON)
                                {
                                    lexer_next(l);
                                    param_types[nparams] = parse_type_formal(ctx, l);
                                }
                                nparams++;

                                Token sep = lexer_peek(l);
                                if (sep.type == TOK_COMMA)
                                {
                                    lexer_next(l);
                                    continue;
                                }
                                else if (sep.type == TOK_RPAREN)
                                {
                                    lexer_next(l);
                                    break;
                                }
                                else
                                {
                                    break;
                                }
                            }

                            if (lexer_next(l).type != TOK_ARROW)
                            {
                                zpanic_at(t, "Expected ->");
                            }

                            ASTNode *node_multi = parse_arrow_lambda_multi(
                                ctx, l, params, param_types, nparams, default_capture_mode);
                            free(param_types);
                            return node_multi;
                        }
                    }
                }
            }
        }

        ASTNode *head = NULL, *tail = NULL;
        int count = 0;
        while (lexer_peek(l).type != TOK_RBRACKET)
        {
            ASTNode *elem = parse_expression(ctx, l);
            count++;
            if (!head)
            {
                head = elem;
                tail = elem;
            }
            else
            {
                tail->next = elem;
                tail = elem;
            }
            if (lexer_peek(l).type == TOK_COMMA)
            {
                lexer_next(l);
            }
            else
            {
                break;
            }
        }
        if (lexer_next(l).type != TOK_RBRACKET)
        {
            zpanic_at(lexer_peek(l), "Expected ] after array literal");
        }
        node = ast_create(NODE_EXPR_ARRAY_LITERAL);
        node->array_literal.elements = head;
        node->array_literal.count = count;
        if (head && head->type_info)
        {
            Type *elem_type = head->type_info;
            Type *arr_type = type_new(TYPE_ARRAY);
            arr_type->inner = elem_type;
            arr_type->array_size = count;
            node->type_info = arr_type;
        }
    }
    else
    {
        const char *hints[] = {"Valid primary expressions include:",
                               "- Identifiers (variables, functions, fields)",
                               "- Literals (numbers, strings, booleans, runes)",
                               "- Parenthesized expressions `(...)`",
                               "- Array literals `[...]`",
                               "- Block expressions `{...}`",
                               NULL};
        char msg[256];
        snprintf(msg, sizeof(msg), "Unexpected token '%.*s' while parsing expression", t.len,
                 t.start);
        zpanic_with_hints(t, msg, hints);
        return NULL;
    }

    while (1)
    {
        if (lexer_peek(l).type == TOK_LPAREN)
        {
            Token op = lexer_next(l); // consume '('
            ASTNode *head = NULL, *tail = NULL;
            char **arg_names = NULL;
            int arg_count = 0;
            int has_named = 0;

            if (lexer_peek(l).type != TOK_RPAREN)
            {
                while (1)
                {
                    char *arg_name = NULL;

                    // Check for named argument: IDENT : expr
                    Token t1 = lexer_peek(l);
                    if (t1.type == TOK_IDENT)
                    {
                        Token t2 = lexer_peek2(l);
                        if (t2.type == TOK_COLON)
                        {
                            arg_name = token_strdup(t1);
                            has_named = 1;
                            lexer_next(l); // eat IDENT
                            lexer_next(l); // eat :
                        }
                    }

                    ASTNode *arg = parse_expression(ctx, l);

                    // Move Semantics Logic
                    check_move_usage(ctx, arg, arg ? arg->token : t1);
                    if (arg && arg->type == NODE_EXPR_VAR)
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

                    if (!head)
                    {
                        head = arg;
                    }
                    else
                    {
                        tail->next = arg;
                    }
                    tail = arg;

                    arg_names = xrealloc(arg_names, (arg_count + 1) * sizeof(char *));
                    arg_names[arg_count] = arg_name;
                    arg_count++;

                    if (lexer_peek(l).type == TOK_COMMA)
                    {
                        lexer_next(l);
                    }
                    else
                    {
                        break;
                    }
                }
            }
            {
                Token inner_t = lexer_next(l);
                if (inner_t.type != TOK_RPAREN)
                {
                    zpanic_at(inner_t, "Expected ) after call arguments");
                }
            }

            ASTNode *call = ast_create(NODE_EXPR_CALL);
            call->token = t;
            call->call.callee = node;
            call->call.args = head;
            call->call.arg_names = has_named ? arg_names : NULL;
            call->call.arg_count = arg_count;
            check_format_string(call, op);

            // Try to infer type if callee has function type info
            call->resolved_type = xstrdup("unknown"); // Default (was int)
            if (node->type_info && node->type_info->kind == TYPE_FUNCTION && node->type_info->inner)
            {
                call->type_info = node->type_info->inner;

                // Update resolved_type based on real return
                // (Optional: type_to_string(call->type_info))
            }
            node = call;
        }

        else if (lexer_peek(l).type == TOK_LBRACKET)
        {
            Token bracket = lexer_next(l); // consume '['
            ASTNode *index = parse_expression(ctx, l);

            ASTNode *extra_head = NULL;
            ASTNode *extra_tail = NULL;
            int extra_count = 0;
            while (lexer_peek(l).type == TOK_COMMA)
            {
                lexer_next(l); // eat comma
                ASTNode *idx = parse_expression(ctx, l);
                if (!extra_head)
                {
                    extra_head = idx;
                }
                else
                {
                    extra_tail->next = idx;
                }
                extra_tail = idx;
                extra_count++;
            }

            {
                Token inner_t = lexer_next(l);
                if (inner_t.type != TOK_RBRACKET)
                {
                    zpanic_at(inner_t, "Expected ] after index");
                }
            }

            // Static Array Bounds Check (only for single-index)
            if (extra_count == 0 && node->type_info && node->type_info->kind == TYPE_ARRAY &&
                node->type_info->array_size > 0)
            {
                if (index->type == NODE_EXPR_LITERAL && index->literal.type_kind == LITERAL_INT)
                {
                    int idx = index->literal.int_val;
                    if (idx < 0 || idx >= node->type_info->array_size)
                    {
                        warn_array_bounds(bracket, idx, node->type_info->array_size);
                    }
                }
            }

            int overloaded_get = 0;
            if (node->type_info && node->type_info->kind != TYPE_ARRAY &&
                node->type_info->kind == TYPE_STRUCT)
            {
                Type *st = node->type_info;
                char *struct_name = (st->kind == TYPE_STRUCT) ? st->name : st->inner->name;
                int is_ptr = (st->kind == TYPE_POINTER);

                size_t mangled_len = strlen(struct_name) + 8; // Max "____get" + null
                char *mangled = xmalloc(mangled_len);
                snprintf(mangled, mangled_len, "%s____get", struct_name);
                FuncSig *sig = find_func(ctx, mangled);
                if (!sig)
                {
                    snprintf(mangled, mangled_len, "%s__get", struct_name);
                    sig = find_func(ctx, mangled);
                }
                if (sig)
                {
                    // Rewrite to Call: node.get(index, ...)
                    ASTNode *call = ast_create(NODE_EXPR_CALL);
                    call->token = t;
                    ASTNode *callee = ast_create(NODE_EXPR_VAR);
                    callee->var_ref.name = xstrdup(mangled);
                    call->call.callee = callee;

                    // Arg 1: Self
                    ASTNode *arg1 = node;
                    if (sig->total_args > 0 && sig->arg_types[0]->kind == TYPE_POINTER && !is_ptr)
                    {
                        // Needs ptr, have value -> &node
                        ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                        addr->unary.op = xstrdup("&");
                        addr->unary.operand = node;
                        addr->type_info = type_new_ptr(st);
                        arg1 = addr;
                    }
                    else if (is_ptr && sig->arg_types[0]->kind != TYPE_POINTER)
                    {
                        // Needs value, have ptr -> *node
                        ASTNode *deref = ast_create(NODE_EXPR_UNARY);
                        deref->unary.op = xstrdup("*");
                        deref->unary.operand = node;
                        arg1 = deref;
                    }

                    // Arg 2: Index
                    arg1->next = index;
                    // Chain extra indices as additional args
                    if (extra_head)
                    {
                        index->next = extra_head;
                    }
                    else
                    {
                        index->next = NULL;
                    }
                    call->call.args = arg1;

                    call->type_info = sig->ret_type;
                    call->resolved_type = type_to_string(sig->ret_type);

                    node = call;
                    overloaded_get = 1;
                }
                free(mangled);
            }

            if (!overloaded_get)
            {
                ASTNode *idx_node = ast_create(NODE_EXPR_INDEX);
                idx_node->token = t;
                idx_node->index.array = node;
                idx_node->index.index = index;
                idx_node->index.extra_indices = extra_head;
                idx_node->index.index_count = 1 + extra_count;

                // Resolve array type_info from symbol table if needed
                Type *arr_type = node->type_info;
                if (!arr_type && node->type == NODE_EXPR_VAR)
                {
                    arr_type = find_symbol_type_info(ctx, node->var_ref.name);
                }

                if (arr_type && arr_type->inner)
                {
                    idx_node->type_info = arr_type->inner;
                }
                else if (arr_type && arr_type->name &&
                         (arr_type->kind == TYPE_VECTOR || arr_type->kind == TYPE_STRUCT ||
                          arr_type->kind == TYPE_ARRAY))
                {
                    // Look up struct def to check if it's a vector type
                    ASTNode *def = find_struct_def(ctx, arr_type->name);
                    if (def && def->type == NODE_STRUCT && def->type_info &&
                        def->type_info->kind == TYPE_VECTOR && def->strct.fields &&
                        def->strct.fields->type_info)
                    {
                        arr_type->inner = def->strct.fields->type_info;
                        idx_node->type_info = def->strct.fields->type_info;
                    }
                    else
                    {
                        idx_node->type_info = type_new(TYPE_INT);
                    }
                }
                else
                {
                    idx_node->type_info = type_new(TYPE_INT);
                }
                node = idx_node;
            }
        }

        else
        {
            break;
        }
    }

    return node;
}

int is_comparison_op(const char *op)
{
    return (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 || strcmp(op, "<") == 0 ||
            strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0);
}

Type *get_field_type(ParserContext *ctx, Type *struct_type, const char *field_name)
{
    if (!struct_type)
    {
        return NULL;
    }

    // Built-in Fields for Arrays/Slices
    if (struct_type->kind == TYPE_ARRAY)
    {
        if (strcmp(field_name, "len") == 0)
        {
            return type_new(TYPE_INT);
        }
        if (struct_type->array_size == 0)
        { // Slice
            if (strcmp(field_name, "cap") == 0)
            {
                return type_new(TYPE_INT);
            }
            if (strcmp(field_name, "data") == 0)
            {
                return type_new_ptr(struct_type->inner);
            }
        }
    }

    // Use resolve_struct_name_from_type to handle Generics and Pointers correctly
    int is_ptr = 0;
    char *alloc_name = NULL;
    char *sname = resolve_struct_name_from_type(ctx, struct_type, &is_ptr, &alloc_name);

    if (!sname)
    {
        return NULL;
    }

    ASTNode *def = find_struct_def(ctx, sname);
    if (!def)
    {
        if (alloc_name)
        {
            free(alloc_name);
        }
        return NULL;
    }

    ASTNode *f = def->strct.fields;
    while (f)
    {
        if (strcmp(f->field.name, field_name) == 0)
        {
            if (alloc_name)
            {
                free(alloc_name);
            }
            return f->type_info;
        }
        f = f->next;
    }
    if (alloc_name)
    {
        free(alloc_name);
    }
    return NULL;
}

const char *get_operator_method(const char *op)
{
    // Arithmetic
    if (strcmp(op, "+") == 0)
    {
        return "add";
    }
    if (strcmp(op, "-") == 0)
    {
        return "sub";
    }
    if (strcmp(op, "*") == 0)
    {
        return "mul";
    }
    if (strcmp(op, "/") == 0)
    {
        return "div";
    }
    if (strcmp(op, "%") == 0)
    {
        return "rem";
    }
    if (strcmp(op, "**") == 0)
    {
        return "pow";
    }

    // Comparison
    if (strcmp(op, "==") == 0)
    {
        return "eq";
    }
    if (strcmp(op, "!=") == 0)
    {
        return "neq";
    }
    if (strcmp(op, "<") == 0)
    {
        return "lt";
    }
    if (strcmp(op, ">") == 0)
    {
        return "gt";
    }
    if (strcmp(op, "<=") == 0)
    {
        return "le";
    }
    if (strcmp(op, ">=") == 0)
    {
        return "ge";
    }

    if (strcmp(op, "&") == 0)
    {
        return "bitand";
    }
    if (strcmp(op, "|") == 0)
    {
        return "bitor";
    }
    if (strcmp(op, "^") == 0)
    {
        return "bitxor";
    }
    if (strcmp(op, "<<") == 0)
    {
        return "shl";
    }
    if (strcmp(op, ">>") == 0)
    {
        return "shr";
    }

    return NULL;
}

char *resolve_struct_name_from_type(ParserContext *ctx, Type *t, int *is_ptr_out,
                                    char **allocated_out)
{
    if (!t)
    {
        return NULL;
    }
    char *struct_name = NULL;
    *allocated_out = NULL;
    *is_ptr_out = 0;

    if (t->kind == TYPE_ALIAS && t->alias.is_opaque_alias)
    {
        struct_name = t->name;
        *is_ptr_out = 0;
        return struct_name;
    }

    if (t->kind == TYPE_POINTER && t->inner && t->inner->kind == TYPE_ALIAS &&
        t->inner->alias.is_opaque_alias)
    {
        struct_name = t->inner->name;
        *is_ptr_out = 1;
        return struct_name;
    }

    const char *alias_target = NULL;
    if (t->kind == TYPE_STRUCT)
    {
        alias_target = find_type_alias(ctx, t->name);
    }

    if (alias_target)
    {
        char *tpl = xstrdup(alias_target);
        char *args_ptr = strchr(tpl, '<');
        if (args_ptr)
        {
            *args_ptr = 0;
            args_ptr++;
            char *end = strrchr(args_ptr, '>');
            if (end)
            {
                *end = 0;
            }

            const char *c_type = args_ptr;
            if (strcmp(args_ptr, "f32") == 0)
            {
                c_type = "float";
            }
            else if (strcmp(args_ptr, "f64") == 0)
            {
                c_type = "double";
            }
            else if (strcmp(args_ptr, "i32") == 0)
            {
                c_type = "int";
            }
            else if (strcmp(args_ptr, "u32") == 0)
            {
                c_type = "uint";
            }
            else if (strcmp(args_ptr, "bool") == 0)
            {
                c_type = "bool";
            }

            char *clean = sanitize_mangled_name(c_type);
            char *mangled = xmalloc(strlen(tpl) + strlen(clean) + 2);
            sprintf(mangled, "%s_%s", tpl, clean);
            struct_name = mangled;
            *allocated_out = mangled;
            free(clean);
            *is_ptr_out = 0;
        }
        else if (strchr(alias_target, '*'))
        {
            *is_ptr_out = 1;
            char *tmp = xstrdup(alias_target);
            char *p = strchr(tmp, '*');
            if (p)
            {
                *p = 0;
            }
            struct_name = xstrdup(tmp);
            *allocated_out = struct_name;
            free(tmp);
        }
        else
        {
            struct_name = xstrdup(alias_target);
            *allocated_out = struct_name;
            *is_ptr_out = 0;
        }
        free(tpl);
    }
    else
    {
        Type *struct_type = NULL;
        if (t->kind == TYPE_STRUCT)
        {
            struct_type = t;
            struct_name = t->name;
            *is_ptr_out = 0;
        }
        else if (t->kind == TYPE_POINTER && t->inner->kind == TYPE_STRUCT)
        {
            struct_type = t->inner;
            *is_ptr_out = 1;
        }

        if (struct_type)
        {
            if (struct_type->args && struct_type->arg_count > 0 && struct_type->name)
            {
                // It's a generic type instance (e.g. Foo<T>).
                // We must construct Foo_T, ensuring we measure SANITIZED length.
                int len = strlen(struct_type->name) + 1;

                // Pass 1: Calculate Length
                for (int i = 0; i < struct_type->arg_count; i++)
                {
                    Type *arg = struct_type->args[i];
                    if (arg)
                    {
                        char *s = type_to_string(arg);
                        if (s)
                        {
                            char *clean = sanitize_mangled_name(s);
                            if (clean)
                            {
                                len += strlen(clean) + 1; // +1 for '_'
                                free(clean);
                            }
                            free(s);
                        }
                    }
                }

                char *mangled = xmalloc(len + 1);
                strcpy(mangled, struct_type->name);

                // Pass 2: Build String
                for (int i = 0; i < struct_type->arg_count; i++)
                {
                    Type *arg = struct_type->args[i];
                    if (arg)
                    {
                        char *arg_str = type_to_string(arg);
                        if (arg_str)
                        {
                            char *clean = sanitize_mangled_name(arg_str);
                            if (clean)
                            {
                                strcat(mangled, "_");
                                strcat(mangled, clean);
                                free(clean);
                            }
                            free(arg_str);
                        }
                    }
                }
                struct_name = mangled;
                *allocated_out = mangled;
            }
            else if (struct_type->name && strchr(struct_type->name, '<'))
            {
                // Fallback: It's a generic type string. We need to mangle it.
                char *tpl = xstrdup(struct_type->name);
                char *args_ptr = strchr(tpl, '<');
                if (args_ptr)
                {
                    *args_ptr = 0;
                    args_ptr++;
                    char *end = strrchr(args_ptr, '>');
                    if (end)
                    {
                        *end = 0;
                    }

                    char *clean = sanitize_mangled_name(args_ptr);
                    char *mangled = xmalloc(strlen(tpl) + strlen(clean) + 2);
                    sprintf(mangled, "%s_%s", tpl, clean);
                    struct_name = mangled;
                    *allocated_out = mangled;
                    free(clean);
                }
                free(tpl);
            }
            else
            {
                struct_name = struct_type->name;
            }
        }
    }
    return struct_name;
}

ASTNode *parse_expr_prec(ParserContext *ctx, Lexer *l, Precedence min_prec)
{
    Token t = lexer_peek(l);
    ASTNode *lhs = NULL;

    if (t.type == TOK_QUESTION)
    {
        Lexer lookahead = *l;
        lexer_next(&lookahead);
        Token next = lexer_peek(&lookahead);

        if (next.type == TOK_STRING || next.type == TOK_FSTRING)
        {
            lexer_next(l); // consume '?'
            Token t_str = lexer_next(l);

            char *inner = xmalloc(t_str.len);
            if (t_str.type == TOK_FSTRING)
            {
                strncpy(inner, t_str.start + 2, t_str.len - 3);
                inner[t_str.len - 3] = 0;
            }
            else
            {
                strncpy(inner, t_str.start + 1, t_str.len - 2);
                inner[t_str.len - 2] = 0;
            }

            // Reuse printf sugar to generate the prompt print
            char *print_code = process_printf_sugar(ctx, t_str, inner, 0, "stdout", NULL, NULL, 1);
            free(inner);

            // Checks for (args...) suffix for SCAN mode
            if (lexer_peek(l).type == TOK_LPAREN)
            {
                lexer_next(l); // consume (

                // Parse args
                ASTNode *args[16];
                int ac = 0;
                if (lexer_peek(l).type != TOK_RPAREN)
                {
                    while (1)
                    {
                        args[ac++] = parse_expression(ctx, l);
                        if (lexer_peek(l).type == TOK_COMMA)
                        {
                            lexer_next(l);
                        }
                        else
                        {
                            break;
                        }
                    }
                }
                if (lexer_next(l).type != TOK_RPAREN)
                {
                    zpanic_at(lexer_peek(l), "Expected )");
                }

                char fmt[256];
                fmt[0] = 0;
                for (int i = 0; i < ac; i++)
                {
                    Type *inner_t = args[i]->type_info;
                    if (!inner_t && args[i]->type == NODE_EXPR_VAR)
                    {
                        inner_t = find_symbol_type_info(ctx, args[i]->var_ref.name);
                    }

                    if (!inner_t)
                    {
                        strcat(fmt, "%d");
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
                        else if (inner_t->kind == TYPE_STRING)
                        {
                            strcat(fmt, "%ms");
                        }
                        else if (inner_t->kind == TYPE_ARRAY && inner_t->inner &&
                                 (inner_t->inner->kind == TYPE_CHAR ||
                                  inner_t->inner->kind == TYPE_U8 ||
                                  inner_t->inner->kind == TYPE_I8))
                        {
                            strcat(fmt, "%s");
                        }
                        else if (inner_t->kind == TYPE_CHAR || inner_t->kind == TYPE_I8 ||
                                 inner_t->kind == TYPE_U8 || inner_t->kind == TYPE_BYTE)
                        {
                            strcat(fmt, " %c");
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

                ASTNode *block = ast_create(NODE_BLOCK);

                ASTNode *s1 = ast_create(NODE_RAW_STMT);
                s1->token = t_str;
                // Append semicolon to ensure it's a valid statement
                char *s1_code = xmalloc(strlen(print_code) + 2);
                sprintf(s1_code, "%s;", print_code);
                s1->raw_stmt.content = s1_code;
                free(print_code);

                ASTNode *call = ast_create(NODE_EXPR_CALL);
                call->token = t;
                ASTNode *callee = ast_create(NODE_EXPR_VAR);
                callee->var_ref.name = xstrdup("_z_scan_helper");
                call->call.callee = callee;
                call->type_info = type_new(TYPE_INT);

                ASTNode *fmt_node = ast_create(NODE_EXPR_LITERAL);
                fmt_node->literal.type_kind = LITERAL_STRING;
                fmt_node->literal.string_val = xstrdup(fmt);
                ASTNode *head = fmt_node, *tail = fmt_node;

                for (int i = 0; i < ac; i++)
                {
                    ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                    addr->unary.op = xstrdup("&");
                    addr->unary.operand = args[i];
                    tail->next = addr;
                    tail = addr;
                }
                call->call.args = head;

                // Link Statements
                s1->next = call;
                block->block.statements = s1;

                return block;
            }
            else
            {
                // String Mode (Original)
                size_t len = strlen(print_code);
                if (len > 5)
                {
                    print_code[len - 5] = 0; // Strip "0; })"
                }

                char *final_code = xmalloc(strlen(print_code) + 64);
                sprintf(final_code, "%s readln(); })", print_code);
                free(print_code);

                ASTNode *n = ast_create(NODE_RAW_STMT);
                n->token = next;
                char *stmt_code = xmalloc(strlen(final_code) + 2);
                sprintf(stmt_code, "%s;", final_code);
                free(final_code);
                n->raw_stmt.content = stmt_code;
                return n;
            }
        }
        else
        {
            lexer_next(l);

            ASTNode *args[16];
            int ac = 0;
            while (1)
            {
                args[ac++] = parse_expr_prec(ctx, l, PREC_ASSIGNMENT);
                if (lexer_peek(l).type == TOK_COMMA)
                {
                    lexer_next(l);
                }
                else
                {
                    break;
                }
            }

            char fmt[256];
            fmt[0] = 0;
            for (int i = 0; i < ac; i++)
            {
                Type *inner_t = args[i]->type_info;
                if (!inner_t && args[i]->type == NODE_EXPR_VAR)
                {
                    inner_t = find_symbol_type_info(ctx, args[i]->var_ref.name);
                }

                if (!inner_t)
                {
                    strcat(fmt, "%d");
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
                              (inner_t->inner->kind == TYPE_CHAR ||
                               inner_t->inner->kind == TYPE_U8 || inner_t->inner->kind == TYPE_I8)))
                    {
                        strcat(fmt, "%s");
                    }
                    else if (inner_t->kind == TYPE_CHAR || inner_t->kind == TYPE_I8 ||
                             inner_t->kind == TYPE_U8 || inner_t->kind == TYPE_BYTE)
                    {
                        strcat(fmt, " %c");
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

            ASTNode *call = ast_create(NODE_EXPR_CALL);
            call->token = t;
            ASTNode *callee = ast_create(NODE_EXPR_VAR);
            callee->var_ref.name = xstrdup("_z_scan_helper");
            call->call.callee = callee;
            call->type_info = type_new(TYPE_INT);

            ASTNode *fmt_node = ast_create(NODE_EXPR_LITERAL);
            fmt_node->literal.type_kind = LITERAL_STRING;
            fmt_node->literal.string_val = xstrdup(fmt);
            ASTNode *head = fmt_node, *tail = fmt_node;

            for (int i = 0; i < ac; i++)
            {
                ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                addr->unary.op = xstrdup("&");
                addr->unary.operand = args[i];
                tail->next = addr;
                tail = addr;
            }
            call->call.args = head;

            return call;
        }
    }
    if (t.type == TOK_OP && is_token(t, "!"))
    {
        Lexer lookahead = *l;
        lexer_next(&lookahead);
        Token next = lexer_peek(&lookahead);

        if (next.type == TOK_STRING || next.type == TOK_FSTRING)
        {
            lexer_next(l); // consume '!'
            Token t_str = lexer_next(l);

            char *inner = xmalloc(t_str.len);
            if (t_str.type == TOK_FSTRING)
            {
                strncpy(inner, t_str.start + 2, t_str.len - 3);
                inner[t_str.len - 3] = 0;
            }
            else
            {
                strncpy(inner, t_str.start + 1, t_str.len - 2);
                inner[t_str.len - 2] = 0;
            }

            // Check for .. suffix (.. suppresses newline)
            int newline = 1;
            if (lexer_peek(l).type == TOK_DOTDOT)
            {
                lexer_next(l); // consume ..
                newline = 0;
            }

            char *code =
                process_printf_sugar(ctx, lexer_peek(l), inner, newline, "stderr", NULL, NULL, 1);
            free(inner);

            ASTNode *n = ast_create(NODE_RAW_STMT);
            n->token = t_str;
            char *stmt_code = xmalloc(strlen(code) + 2);
            sprintf(stmt_code, "%s;", code);
            free(code);
            n->raw_stmt.content = stmt_code;
            return n;
        }
    }

    if (t.type == TOK_AWAIT)
    {
        lexer_next(l); // consume await
        ASTNode *operand = parse_expr_prec(ctx, l, PREC_UNARY);

        lhs = ast_create(NODE_AWAIT);
        lhs->unary.operand = operand;
        // Type inference: await Async<T> yields T
        // If operand is a call to an async function, look up its ret_type (not
        // Async)
        if (operand->type == NODE_EXPR_CALL && operand->call.callee->type == NODE_EXPR_VAR)
        {
            FuncSig *sig = find_func(ctx, operand->call.callee->var_ref.name);
            if (sig && sig->is_async && sig->ret_type)
            {
                lhs->type_info = sig->ret_type;
                lhs->resolved_type = type_to_string(sig->ret_type);
            }
            else if (sig && !sig->is_async)
            {
                // Not an async function - shouldn't await it
                lhs->type_info = type_new(TYPE_VOID);
                lhs->resolved_type = xstrdup("void");
            }
            else
            {
                lhs->type_info = type_new_ptr(type_new(TYPE_VOID));
                lhs->resolved_type = xstrdup("void*");
            }
        }
        else
        {
            // Awaiting a variable - look up its type and extract T from Async<T>
            if (operand->type == NODE_EXPR_VAR)
            {
                ZenSymbol *sym = find_symbol_entry(ctx, operand->var_ref.name);
                if (sym)
                {
                    char *var_type = sym->type_name;
                    if (!var_type && sym->type_info)
                    {
                        var_type = type_to_string(sym->type_info);
                    }
                    if (var_type)
                    {
                        // Extract T from Async<T>
                        char *start = strchr(var_type, '<');
                        if (start)
                        {
                            start++; // Skip <
                            char *end = strrchr(var_type, '>');
                            if (end && end > start)
                            {
                                int len = end - start;
                                char *inner_type = xmalloc(len + 1);
                                strncpy(inner_type, start, len);
                                inner_type[len] = 0;
                                lhs->resolved_type = inner_type;
                                // Try to create proper type_info
                                if (is_primitive_type_name(inner_type))
                                {
                                    if (strcmp(inner_type, "string") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_STRING);
                                    }
                                    else if (strcmp(inner_type, "int") == 0 ||
                                             strcmp(inner_type, "i32") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_INT);
                                    }
                                    else if (strcmp(inner_type, "bool") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_BOOL);
                                    }
                                    else if (strcmp(inner_type, "float") == 0 ||
                                             strcmp(inner_type, "f32") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_F32);
                                    }
                                    else if (strcmp(inner_type, "double") == 0 ||
                                             strcmp(inner_type, "f64") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_F64);
                                    }
                                    else if (strcmp(inner_type, "char") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_CHAR);
                                    }
                                    else if (strcmp(inner_type, "void") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_VOID);
                                    }
                                    else if (strcmp(inner_type, "usize") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_USIZE);
                                    }
                                    else if (strcmp(inner_type, "isize") == 0)
                                    {
                                        lhs->type_info = type_new(TYPE_ISIZE);
                                    }
                                    else
                                    {
                                        lhs->type_info = type_new(TYPE_INT);
                                    }
                                }
                                else
                                {
                                    lhs->type_info = type_new(TYPE_STRUCT);
                                    lhs->type_info->name = xstrdup(inner_type);
                                }
                                goto await_done;
                            }
                        }
                    }
                }
            }
            // Fallback to void* if we couldn't determine the type
            lhs->type_info = type_new_ptr(type_new(TYPE_VOID));
            lhs->resolved_type = xstrdup("void*");
        await_done:;
        }

        goto after_unary;
    }

    if ((t.type == TOK_OP && (is_token(t, "-") || is_token(t, "!") || is_token(t, "*") ||
                              is_token(t, "&") || is_token(t, "~") || is_token(t, "&&") ||
                              is_token(t, "++") || is_token(t, "--") || is_token(t, "**"))) ||
        t.type == TOK_NOT)
    {
        lexer_next(l); // consume op

        ASTNode *operand;
        if (is_token(t, "**"))
        {
            operand = parse_expr_prec(ctx, l, PREC_UNARY);
            ASTNode *inner_deref = ast_create(NODE_EXPR_UNARY);
            inner_deref->token = t;
            inner_deref->unary.op = xstrdup("*");
            inner_deref->unary.operand = operand;
            if (operand->type_info && operand->type_info->kind == TYPE_POINTER)
            {
                inner_deref->type_info = operand->type_info->inner;
            }
            operand = inner_deref;
            t.len = 1;
        }
        else
        {
            operand = parse_expr_prec(ctx, l, PREC_UNARY);
        }

        if (is_token(t, "&") && operand->type == NODE_EXPR_VAR)
        {
            ZenSymbol *s = find_symbol_entry(ctx, operand->var_ref.name);
            if (s && s->is_def)
            {
                zpanic_at(t,
                          "Cannot take address of manifest constant '%s' (use 'var' if you need an "
                          "address)",
                          operand->var_ref.name);
            }
        }

        char *method = NULL;
        if (is_token(t, "-"))
        {
            method = "neg";
        }
        if (is_token(t, "!") || t.type == TOK_NOT)
        {
            method = "not";
        }
        if (is_token(t, "~"))
        {
            method = "bitnot";
        }

        if (method && operand->type_info)
        {
            Type *ot = operand->type_info;
            int is_ptr = 0;
            char *allocated_name = NULL;
            char *struct_name = resolve_struct_name_from_type(ctx, ot, &is_ptr, &allocated_name);

            if (struct_name)
            {
                size_t mangled_sz = strlen(struct_name) + strlen(method) + 3;
                char *mangled = xmalloc(mangled_sz);
                snprintf(mangled, mangled_sz, "%s__%s", struct_name, method);

                if (find_func(ctx, mangled))
                {
                    // Rewrite: ~x -> Struct_bitnot(x)
                    ASTNode *call = ast_create(NODE_EXPR_CALL);
                    call->token = t;
                    ASTNode *callee = ast_create(NODE_EXPR_VAR);
                    callee->var_ref.name = xstrdup(mangled);
                    call->call.callee = callee;

                    // Handle 'self' argument adjustment (Pointer vs Value)
                    ASTNode *arg = operand;
                    FuncSig *sig = find_func(ctx, mangled);

                    if (sig->total_args > 0 && sig->arg_types[0]->kind == TYPE_POINTER && !is_ptr)
                    {
                        int is_rvalue =
                            (operand->type == NODE_EXPR_CALL || operand->type == NODE_EXPR_BINARY ||
                             operand->type == NODE_MATCH);
                        ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                        addr->unary.op = is_rvalue ? xstrdup("&_rval") : xstrdup("&");
                        addr->unary.operand = operand;
                        addr->type_info = type_new_ptr(ot);
                        arg = addr;
                    }
                    else if (is_ptr && sig->arg_types[0]->kind != TYPE_POINTER)
                    {
                        // Function wants Value, we have Pointer -> Dereference (*)
                        ASTNode *deref = ast_create(NODE_EXPR_UNARY);
                        deref->unary.op = xstrdup("*");
                        deref->unary.operand = operand;
                        deref->type_info = ot->inner;
                        arg = deref;
                    }

                    call->call.args = arg;
                    call->type_info = sig->ret_type;
                    call->resolved_type = type_to_string(sig->ret_type);
                    lhs = call;

                    if (allocated_name)
                    {
                        free(allocated_name);
                    }
                    // Skip standard unary node creation
                    goto after_unary;
                }
                if (allocated_name)
                {
                    free(allocated_name);
                }
            }
        }

        // Standard Unary Node (for primitives or if no overload found)
        lhs = ast_create(NODE_EXPR_UNARY);
        lhs->token = t;
        if (t.type == TOK_NOT)
        {
            lhs->unary.op = xstrdup("!");
        }
        else
        {
            lhs->unary.op = token_strdup(t);
        }
        lhs->unary.operand = operand;

        if (operand->type_info)
        {
            if (is_token(t, "&"))
            {
                lhs->type_info = type_new_ptr(operand->type_info);
            }
            else if (is_token(t, "*"))
            {
                if (operand->type_info->kind == TYPE_POINTER)
                {
                    lhs->type_info = operand->type_info->inner;
                }
            }
            else
            {
                lhs->type_info = operand->type_info;
            }
        }

    after_unary:; // Label to skip standard creation if overloaded
    }

    else if (is_token(t, "va_start"))
    {
        lexer_next(l);
        if (lexer_peek(l).type != TOK_LPAREN)
        {
            zpanic_at(t, "Expected '(' after va_start");
        }
        lexer_next(l);
        ASTNode *ap = parse_expression(ctx, l);
        if (lexer_next(l).type != TOK_COMMA)
        {
            zpanic_at(t, "Expected ',' in va_start");
        }
        ASTNode *last = parse_expression(ctx, l);
        if (lexer_next(l).type != TOK_RPAREN)
        {
            zpanic_at(t, "Expected ')' after va_start args");
        }
        lhs = ast_create(NODE_VA_START);
        lhs->va_start.ap = ap;
        lhs->va_start.last_arg = last;
    }
    else if (is_token(t, "va_end"))
    {
        lexer_next(l);
        if (lexer_peek(l).type != TOK_LPAREN)
        {
            zpanic_at(t, "Expected '(' after va_end");
        }
        lexer_next(l);
        ASTNode *ap = parse_expression(ctx, l);
        if (lexer_next(l).type != TOK_RPAREN)
        {
            zpanic_at(t, "Expected ')' after va_end arg");
        }
        lhs = ast_create(NODE_VA_END);
        lhs->va_end.ap = ap;
    }
    else if (is_token(t, "va_copy"))
    {
        lexer_next(l);
        if (lexer_peek(l).type != TOK_LPAREN)
        {
            zpanic_at(t, "Expected '(' after va_copy");
        }
        lexer_next(l);
        ASTNode *dest = parse_expression(ctx, l);
        if (lexer_next(l).type != TOK_COMMA)
        {
            zpanic_at(t, "Expected ',' in va_copy");
        }
        ASTNode *src = parse_expression(ctx, l);
        if (lexer_next(l).type != TOK_RPAREN)
        {
            zpanic_at(t, "Expected ')' after va_copy args");
        }
        lhs = ast_create(NODE_VA_COPY);
        lhs->va_copy.dest = dest;
        lhs->va_copy.src = src;
    }
    else if (is_token(t, "va_arg"))
    {
        lexer_next(l);
        if (lexer_peek(l).type != TOK_LPAREN)
        {
            zpanic_at(t, "Expected '(' after va_arg");
        }
        lexer_next(l);
        ASTNode *ap = parse_expression(ctx, l);
        if (lexer_next(l).type != TOK_COMMA)
        {
            zpanic_at(t, "Expected ',' in va_arg");
        }

        Type *tinfo = parse_type_formal(ctx, l);

        if (lexer_next(l).type != TOK_RPAREN)
        {
            zpanic_at(t, "Expected ')' after va_arg args");
        }

        lhs = ast_create(NODE_VA_ARG);
        lhs->va_arg.ap = ap;
        lhs->va_arg.type_info = tinfo;
        lhs->type_info = tinfo; // The expression evaluates to this type
    }
    else if (is_token(t, "sizeof"))
    {
        lexer_next(l); // consume sizeof
        lhs = parse_sizeof_expr(ctx, l);
    }
    else
    {
        lhs = parse_primary(ctx, l);
        if (!lhs)
        {
            return NULL;
        }
    }

    while (1)
    {
        Token op = lexer_peek(l);

        if (op.line > l->line && op.type == TOK_OP &&
            (is_token(op, "*") || is_token(op, "&") || is_token(op, "+") || is_token(op, "-")))
        {
            break;
        }

        Precedence prec = get_token_precedence(op);

        // Handle postfix ++ and -- (highest postfix precedence)
        if (op.type == TOK_OP && op.len == 2 &&
            ((op.start[0] == '+' && op.start[1] == '+') ||
             (op.start[0] == '-' && op.start[1] == '-')))
        {
            lexer_next(l); // consume ++ or --
            ASTNode *node = ast_create(NODE_EXPR_UNARY);
            node->unary.op = (op.start[0] == '+') ? xstrdup("_post++") : xstrdup("_post--");
            node->unary.operand = lhs;
            node->type_info = lhs->type_info;
            lhs = node;
            continue;
        }

        if (prec == PREC_NONE || prec < min_prec)
        {
            break;
        }

        // Pointer access: ->
        if (op.type == TOK_ARROW && op.start[0] == '-')
        {
            lexer_next(l);
            Token field = lexer_next(l);
            if (field.type != TOK_IDENT && field.type != TOK_INT)
            {
                zpanic_at(field, "Expected field name after ->");
                break;
            }
            ASTNode *node = ast_create(NODE_EXPR_MEMBER);
            node->token = field;
            node->member.target = lhs;
            node->member.field = token_strdup(field);
            node->member.is_pointer_access = 1;

            // Opaque Check
            int is_ptr_dummy = 0;
            char *alloc_name = NULL;
            char *sname =
                resolve_struct_name_from_type(ctx, lhs->type_info, &is_ptr_dummy, &alloc_name);
            if (sname)
            {
                ASTNode *def = find_struct_def(ctx, sname);
                if (def && def->type == NODE_STRUCT && def->strct.is_opaque)
                {
                    if (!def->strct.defined_in_file ||
                        (g_current_filename &&
                         strcmp(def->strct.defined_in_file, g_current_filename) != 0))
                    {
                        zpanic_at(field, "Cannot access private field '%s' of opaque struct '%s'",
                                  node->member.field, sname);
                    }
                }
                if (alloc_name)
                {
                    free(alloc_name);
                }
            }

            node->type_info = get_field_type(ctx, lhs->type_info, node->member.field);
            if (node->type_info)
            {
                node->resolved_type = type_to_string(node->type_info);
            }
            else
            {
                node->resolved_type = xstrdup("unknown");
            }

            lhs = node;
            continue;
        }

        // Null-safe access: ?.
        if (op.type == TOK_Q_DOT)
        {
            lexer_next(l);
            Token field = lexer_next(l);
            if (field.type != TOK_IDENT && field.type != TOK_INT)
            {
                zpanic_at(field, "Expected field name after ?.");
                break;
            }
            ASTNode *node = ast_create(NODE_EXPR_MEMBER);
            node->token = field;
            node->member.target = lhs;
            node->member.field = token_strdup(field);
            node->member.is_pointer_access = 2;

            // Opaque Check
            int is_ptr_dummy = 0;
            char *alloc_name = NULL;
            char *sname =
                resolve_struct_name_from_type(ctx, lhs->type_info, &is_ptr_dummy, &alloc_name);
            if (sname)
            {
                ASTNode *def = find_struct_def(ctx, sname);
                if (def && def->type == NODE_STRUCT && def->strct.is_opaque)
                {
                    if (!def->strct.defined_in_file ||
                        (g_current_filename &&
                         strcmp(def->strct.defined_in_file, g_current_filename) != 0))
                    {
                        zpanic_at(field, "Cannot access private field '%s' of opaque struct '%s'",
                                  node->member.field, sname);
                    }
                }
                if (alloc_name)
                {
                    free(alloc_name);
                }
            }

            node->type_info = get_field_type(ctx, lhs->type_info, node->member.field);
            if (node->type_info)
            {
                node->resolved_type = type_to_string(node->type_info);
            }

            lhs = node;
            continue;
        }

        // Postfix ? (Result Unwrap OR Ternary)
        if (op.type == TOK_QUESTION)
        {
            // Disambiguate
            Lexer lookahead = *l;
            lexer_next(&lookahead); // skip ?
            Token next = lexer_peek(&lookahead);

            // Heuristic: If next token starts an expression => Ternary
            // (Ident, Number, String, (, {, -, !, *, etc)
            int is_ternary = 0;
            if (next.type == TOK_INT || next.type == TOK_FLOAT || next.type == TOK_STRING ||
                next.type == TOK_IDENT || next.type == TOK_LPAREN || next.type == TOK_LBRACE ||
                next.type == TOK_SIZEOF || next.type == TOK_DEFER || next.type == TOK_AUTOFREE ||
                next.type == TOK_FSTRING || next.type == TOK_CHAR)
            {
                is_ternary = 1;
            }
            // Check unary ops
            if (next.type == TOK_OP)
            {
                if (is_token(next, "-") || is_token(next, "!") || is_token(next, "*") ||
                    is_token(next, "&") || is_token(next, "~"))
                {
                    is_ternary = 1;
                }
            }

            if (is_ternary)
            {
                if (PREC_TERNARY < min_prec)
                {
                    break; // Return to caller to handle precedence
                }

                lexer_next(l); // consume ?
                ASTNode *true_expr = parse_expression(ctx, l);
                expect(l, TOK_COLON, "Expected : in ternary");
                ASTNode *false_expr = parse_expr_prec(ctx, l, PREC_TERNARY); // Right associative

                ASTNode *tern = ast_create(NODE_TERNARY);
                zen_trigger_at(TRIGGER_TERNARY, lhs->token);

                tern->token = lhs->token;
                tern->ternary.cond = lhs;
                tern->ternary.true_expr = true_expr;
                tern->ternary.false_expr = false_expr;

                // Type inference hint: Both branches should match?
                // Logic later in codegen/semant.
                lhs = tern;
                continue;
            }

            // Otherwise: Unwrap (High Precedence)
            if (PREC_CALL < min_prec)
            {
                break;
            }

            lexer_next(l);
            ASTNode *n = ast_create(NODE_TRY);
            n->try_stmt.expr = lhs;
            lhs = n;
            continue;
        }

        // Pipe: |>
        if (op.type == TOK_PIPE || (op.type == TOK_OP && is_token(op, "|>")))
        {
            lexer_next(l);
            ASTNode *rhs = parse_expr_prec(ctx, l, prec + 1);
            if (rhs->type == NODE_EXPR_CALL)
            {
                ASTNode *old_args = rhs->call.args;
                lhs->next = old_args;
                rhs->call.args = lhs;
                lhs = rhs;
            }
            else
            {
                ASTNode *call = ast_create(NODE_EXPR_CALL);
                call->token = t;
                call->call.callee = rhs;
                call->call.args = lhs;
                lhs->next = NULL;
                lhs = call;
            }
            continue;
        }

        lexer_next(l); // Consume operator/paren/bracket

        // Call: (...)
        if (op.type == TOK_LPAREN)
        {
            ASTNode *call = ast_create(NODE_EXPR_CALL);
            call->token = t;

            // Method Resolution Logic (Struct Method -> Trait Method)
            ASTNode *self_arg = NULL;
            FuncSig *resolved_sig = NULL;
            char *resolved_name = NULL;

            if (lhs->type == NODE_EXPR_MEMBER)
            {
                Type *lt = lhs->member.target->type_info;
                int is_lhs_ptr = 0;
                char *alloc_name = NULL;
                char *struct_name =
                    resolve_struct_name_from_type(ctx, lt, &is_lhs_ptr, &alloc_name);

                if (struct_name)
                {
                    size_t mangled_sz = strlen(struct_name) + strlen(lhs->member.field) + 3;
                    char *mangled = xmalloc(mangled_sz);
                    snprintf(mangled, mangled_sz, "%s__%s", struct_name, lhs->member.field);
                    FuncSig *sig = find_func(ctx, mangled);

                    if (!sig)
                    {
                        // Trait method lookup: Struct__Trait_Method
                        StructRef *ref = ctx->parsed_impls_list;
                        while (ref)
                        {
                            if (ref->node && ref->node->type == NODE_IMPL_TRAIT)
                            {
                                if (ref->node->impl_trait.target_type &&
                                    strcmp(ref->node->impl_trait.target_type, struct_name) == 0)
                                {
                                    size_t tm_sz = strlen(struct_name) +
                                                   strlen(ref->node->impl_trait.trait_name) +
                                                   strlen(lhs->member.field) + 4;
                                    char *trait_mangled = xmalloc(tm_sz);
                                    snprintf(trait_mangled, tm_sz, "%s__%s_%s", struct_name,
                                             ref->node->impl_trait.trait_name, lhs->member.field);
                                    if (find_func(ctx, trait_mangled))
                                    {
                                        sig = find_func(ctx, trait_mangled);
                                        free(mangled);
                                        mangled = trait_mangled;
                                        break;
                                    }
                                    free(trait_mangled);
                                }
                            }
                            ref = ref->next;
                        }
                    }

                    if (sig)
                    {
                        // Check if this is a static method being called with dot operator
                        // Static methods don't have 'self' as first parameter
                        int is_static_method = 0;
                        if (sig->total_args == 0)
                        {
                            // No arguments at all - definitely static
                            is_static_method = 1;
                        }
                        else if (sig->arg_types[0])
                        {
                            // Check if first parameter is a pointer to the struct type
                            // Instance methods have: fn method(self) where self is StructType*
                            // Static methods have: fn method(x: int, y: int) etc.
                            Type *first_param = sig->arg_types[0];

                            // If first param is not a pointer, it's likely static
                            // OR if it's a pointer but not to this struct type
                            if (first_param->kind != TYPE_POINTER)
                            {
                                is_static_method = 1;
                            }
                            else if (first_param->inner)
                            {
                                // Check if the inner type matches the struct
                                char *inner_name = NULL;
                                if (first_param->inner->kind == TYPE_STRUCT)
                                {
                                    inner_name = first_param->inner->name;
                                }

                                if (!inner_name || strcmp(inner_name, struct_name) != 0)
                                {
                                    is_static_method = 1;
                                }
                            }
                        }

                        if (is_static_method)
                        {
                            zpanic_at(lhs->token,
                                      "Cannot call static method '%s' with dot operator\n"
                                      "   = help: Use '%s::%s(...)' instead of instance.%s(...)",
                                      lhs->member.field, struct_name, lhs->member.field,
                                      lhs->member.field);
                        }

                        resolved_name = xstrdup(mangled);
                        resolved_sig = sig;

                        // Create 'self' argument
                        ASTNode *obj = lhs->member.target;

                        // Handle Reference/Pointer adjustment based on signature
                        if (sig->total_args > 0 && sig->arg_types[0] &&
                            sig->arg_types[0]->kind == TYPE_POINTER)
                        {
                            if (!is_lhs_ptr)
                            {
                                // Function expects ptr, have value -> &obj
                                int is_rvalue =
                                    (obj->type == NODE_EXPR_CALL || obj->type == NODE_EXPR_BINARY ||
                                     obj->type == NODE_EXPR_STRUCT_INIT ||
                                     obj->type == NODE_EXPR_CAST || obj->type == NODE_MATCH);

                                ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                                addr->unary.op = is_rvalue ? xstrdup("&_rval") : xstrdup("&");
                                addr->unary.operand = obj;
                                addr->type_info = type_new_ptr(lt);
                                self_arg = addr;
                            }
                            else
                            {
                                self_arg = obj;
                            }
                        }
                        else
                        {
                            // Function expects value
                            if (is_lhs_ptr)
                            {
                                // Have ptr, need value -> *obj
                                ASTNode *deref = ast_create(NODE_EXPR_UNARY);
                                deref->unary.op = xstrdup("*");
                                deref->unary.operand = obj;
                                if (lt && lt->kind == TYPE_POINTER && lt->inner)
                                {
                                    deref->type_info = lt->inner;
                                }
                                self_arg = deref;
                            }
                            else
                            {
                                self_arg = obj;
                            }
                        }
                    }
                }
                if (alloc_name)
                {
                    free(alloc_name);
                }
            }

            if (resolved_name)
            {
                ASTNode *callee = ast_create(NODE_EXPR_VAR);
                callee->var_ref.name = resolved_name;
                call->call.callee = callee;
            }
            else
            {
                call->call.callee = lhs;
            }

            ASTNode *head = NULL, *tail = NULL;
            char **arg_names = NULL;
            int arg_count = 0;
            int has_named = 0;

            if (lexer_peek(l).type != TOK_RPAREN)
            {
                while (1)
                {
                    char *arg_name = NULL;

                    // Check for named argument: IDENT : expr
                    Token t1 = lexer_peek(l);
                    if (t1.type == TOK_IDENT)
                    {
                        // Lookahead for colon
                        Token t2 = lexer_peek2(l);
                        if (t2.type == TOK_COLON)
                        {
                            arg_name = token_strdup(t1);
                            has_named = 1;
                            lexer_next(l); // eat IDENT
                            lexer_next(l); // eat :
                        }
                    }

                    ASTNode *arg = parse_expression(ctx, l);

                    // Move Semantics Logic
                    check_move_usage(ctx, arg, arg ? arg->token : t1);
                    if (arg && arg->type == NODE_EXPR_VAR)
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

                    if (!head)
                    {
                        head = arg;
                    }
                    else
                    {
                        tail->next = arg;
                    }
                    tail = arg;

                    // Store arg name
                    arg_names = xrealloc(arg_names, (arg_count + 1) * sizeof(char *));
                    arg_names[arg_count] = arg_name;
                    arg_count++;

                    if (lexer_peek(l).type == TOK_COMMA)
                    {
                        lexer_next(l);
                    }
                    else
                    {
                        break;
                    }
                }
            }
            if (lexer_next(l).type != TOK_RPAREN)
            {
                zpanic_at(lexer_peek(l), "Expected )");
            }

            // Prepend 'self' argument if resolved
            if (self_arg)
            {
                self_arg->next = head;
                head = self_arg;
                arg_count++;

                if (has_named)
                {
                    // Prepend NULL to arg_names for self
                    char **new_names = xmalloc(sizeof(char *) * arg_count);
                    new_names[0] = NULL;
                    for (int i = 0; i < arg_count - 1; i++)
                    {
                        new_names[i + 1] = arg_names[i];
                    }
                    free(arg_names);
                    arg_names = new_names;
                }
            }

            call->call.args = head;
            call->call.arg_names = has_named ? arg_names : NULL;
            call->call.arg_count = arg_count;

            call->resolved_type = xstrdup("unknown");

            if (resolved_sig)
            {
                call->type_info = resolved_sig->ret_type;
                if (call->type_info)
                {
                    call->resolved_type = type_to_string(call->type_info);
                }
            }
            else if (lhs->type_info && lhs->type_info->kind == TYPE_FUNCTION &&
                     lhs->type_info->inner)
            {
                call->type_info = lhs->type_info->inner;
            }

            lhs = call;
            continue;
        }

        // Index: [...] or Slice: [start..end]
        if (op.type == TOK_LBRACKET || (op.type == TOK_OP && is_token(op, "[")))
        {
            ASTNode *start = NULL;
            ASTNode *end = NULL;
            int is_slice = 0;

            // Fallback: If LHS is a variable but missing type info, look it up now
            if (!lhs->type_info && lhs->type == NODE_EXPR_VAR)
            {
                Type *sym_type = find_symbol_type_info(ctx, lhs->var_ref.name);
                if (sym_type)
                {
                    lhs->type_info = sym_type;
                    lhs->resolved_type = type_to_string(sym_type);
                }
            }

            // Case: [..] or [..end]
            if (lexer_peek(l).type == TOK_DOTDOT || lexer_peek(l).type == TOK_DOTDOT_LT)
            {
                is_slice = 1;
                lexer_next(l); // consume .. or ..<
                if (lexer_peek(l).type != TOK_RBRACKET)
                {
                    end = parse_expression(ctx, l);
                }
            }
            else
            {
                // Case: [start] or [start..] or [start..end] or [start, expr, ...]
                start = parse_expression(ctx, l);
                if (lexer_peek(l).type == TOK_DOTDOT || lexer_peek(l).type == TOK_DOTDOT_LT)
                {
                    is_slice = 1;
                    lexer_next(l); // consume ..
                    if (lexer_peek(l).type != TOK_RBRACKET)
                    {
                        end = parse_expression(ctx, l);
                    }
                }
            }

            // Multi-index: [expr, expr, ...] -> collect extra indices
            ASTNode *extra_head = NULL;
            ASTNode *extra_tail = NULL;
            int extra_count = 0;
            if (!is_slice)
            {
                while (lexer_peek(l).type == TOK_COMMA)
                {
                    lexer_next(l); // eat comma
                    ASTNode *idx = parse_expression(ctx, l);
                    if (!extra_head)
                    {
                        extra_head = idx;
                    }
                    else
                    {
                        extra_tail->next = idx;
                    }
                    extra_tail = idx;
                    extra_count++;
                }
            }

            if (lexer_next(l).type != TOK_RBRACKET)
            {
                zpanic_at(lexer_peek(l), "Expected ]");
            }

            if (is_slice)
            {
                ASTNode *node = ast_create(NODE_EXPR_SLICE);
                node->slice.array = lhs;
                node->slice.start = start;
                node->slice.end = end;

                // Type Inference & Registration
                if (lhs->type_info)
                {
                    Type *inner = NULL;
                    if (lhs->type_info->kind == TYPE_ARRAY)
                    {
                        inner = lhs->type_info->inner;
                    }
                    else if (lhs->type_info->kind == TYPE_POINTER)
                    {
                        inner = lhs->type_info->inner;
                    }

                    if (inner)
                    {
                        node->type_info = type_new(TYPE_ARRAY);
                        node->type_info->inner = inner;
                        node->type_info->array_size = 0; // Slice

                        // Clean up string for registration (e.g. "int" from "int*")
                        char *inner_str = type_to_string(inner);

                        // Strip * if it somehow managed to keep one, though
                        // parse_type_formal should handle it For now assume type_to_string
                        // gives base type
                        register_slice(ctx, inner_str);
                    }
                }

                lhs = node;
            }
            else
            {
                ASTNode *node = ast_create(NODE_EXPR_INDEX);
                node->token = t;
                node->index.array = lhs;
                node->index.index = start;
                node->index.extra_indices = extra_head;
                node->index.index_count = 1 + extra_count;

                char *struct_name = NULL;
                Type *inner_t = lhs->type_info;
                int is_ptr = 0;

                if (inner_t)
                {
                    if (inner_t->kind == TYPE_STRUCT)
                    {
                        struct_name = inner_t->name;
                    }
                    /*
                    else if (t->kind == TYPE_POINTER && t->inner && t->inner->kind == TYPE_STRUCT)
                    {
                        // struct_name = t->inner->name;
                        // is_ptr = 1;
                        // DISABLE: Pointers should use array indexing by default, not operator
                    overload.
                        // If users want operator overload, they must dereference first (*ptr)[idx]
                    }
                    */
                }
                if (!struct_name && lhs->resolved_type)
                {
                    char *s = lhs->resolved_type;
                    if (strncmp(s, "struct ", 7) == 0)
                    {
                        s += 7;
                    }
                    ASTNode *def = find_struct_def(ctx, s);
                    if (def && def->type == NODE_STRUCT)
                    {
                        struct_name = s;
                        if (strchr(lhs->resolved_type, '*'))
                        {
                            is_ptr = 1;
                        }
                    }
                    if (!struct_name)
                    {
                        def = find_struct_def(ctx, lhs->resolved_type);
                        if (def && def->type == NODE_STRUCT)
                        {
                            struct_name = lhs->resolved_type;
                            // Just assume val type
                        }
                    }
                }

                if (struct_name)
                {
                    size_t sname_len = strlen(struct_name);
                    char *mangled_index = xmalloc(sname_len + sizeof("__index"));
                    snprintf(mangled_index, sname_len + sizeof("__index"), "%s__index",
                             struct_name);

                    char *mangled_get = xmalloc(sname_len + sizeof("__get"));
                    snprintf(mangled_get, sname_len + sizeof("__get"), "%s__get", struct_name);

                    FuncSig *sig = find_func(ctx, mangled_index);
                    char *resolved_name = NULL;
                    char *method_name = "index";

                    if (sig)
                    {
                        resolved_name = mangled_index;
                    }
                    else
                    {
                        sig = find_func(ctx, mangled_get);
                        if (sig)
                        {
                            resolved_name = mangled_get;
                            method_name = "get";
                        }
                    }

                    // Fallback for Generics (e.g. Vec<T> -> Vec)
                    int is_generic_template = 0;
                    if (!sig && strchr(struct_name, '<'))
                    {
                        size_t len = strcspn(struct_name, "<");
                        char *base = xmalloc(len + 1);
                        strncpy(base, struct_name, len);
                        base[len] = 0;

                        GenericImplTemplate *it = ctx->impl_templates;
                        while (it)
                        {
                            if (strcmp(it->struct_name, base) == 0)
                            {
                                ASTNode *m = it->impl_node->impl.methods;
                                size_t base_len = strlen(base);
                                char *mangled_idx = xmalloc(base_len + 8);
                                sprintf(mangled_idx, "%s__index", base);
                                char *mangled_g = xmalloc(base_len + 6);
                                sprintf(mangled_g, "%s__get", base);

                                while (m)
                                {
                                    int found_idx =
                                        m->func.name && strcmp(m->func.name, mangled_idx) == 0;
                                    int found_get =
                                        m->func.name && strcmp(m->func.name, mangled_g) == 0;

                                    if (found_idx || found_get)
                                    {
                                        if (found_idx)
                                        {
                                            method_name = "index";
                                        }
                                        else
                                        {
                                            method_name = "get";
                                        }

                                        is_generic_template = 1;

                                        // Construct temporary signature for checking
                                        sig = xmalloc(sizeof(FuncSig));
                                        memset(sig, 0, sizeof(FuncSig));
                                        sig->is_pure = 1;
                                        sig->ret_type = m->func.ret_type_info;
                                        sig->arg_types = m->func.arg_types;
                                        sig->total_args = m->func.arg_count;

                                        break;
                                    }
                                    m = m->next;
                                }
                            }
                            if (is_generic_template)
                            {
                                break;
                            }
                            it = it->next;
                        }
                        free(base);
                    }

                    if (sig)
                    {
                        // Rewrite to Call
                        ASTNode *call = ast_create(NODE_EXPR_CALL);
                        call->token = t;
                        ASTNode *callee;

                        if (is_generic_template)
                        {
                            // For generics, keep it as a Member access so instantiation
                            // can re-resolve the correct concrete function name.
                            callee = ast_create(NODE_EXPR_MEMBER);
                            callee->member.target = lhs;
                            callee->member.field = xstrdup(method_name);
                            callee->member.is_pointer_access = is_ptr;
                            // Just a hint, logic below handles adjustments
                        }
                        else
                        {
                            callee = ast_create(NODE_EXPR_VAR);
                            callee->var_ref.name = xstrdup(resolved_name);
                        }

                        callee->token = t;
                        call->call.callee = callee;

                        ASTNode *self_arg = lhs;

                        // Pointer adjustment logic
                        if (sig->total_args > 0 && sig->arg_types[0]->kind == TYPE_POINTER &&
                            !is_ptr)
                        {
                            ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                            addr->unary.op = xstrdup("&");
                            addr->unary.operand = lhs;
                            if (inner_t)
                            {
                                addr->type_info = type_new_ptr(inner_t);
                            }
                            self_arg = addr;
                        }
                        else if (is_ptr && sig->arg_types[0]->kind != TYPE_POINTER)
                        {
                            ASTNode *deref = ast_create(NODE_EXPR_UNARY);
                            deref->unary.op = xstrdup("*");
                            deref->unary.operand = lhs;
                            self_arg = deref;
                        }

                        // If using MEMBER access, we don't pass self as argument in AST
                        // because codegen/semant handles "obj.method(args)" by injecting obj.
                        // BUT here we are manually constructing the call.
                        // If we use MEMBER, we should NOT put self in args?
                        // standard parser produces: Call(Member, args). Member has target=obj.

                        if (is_generic_template)
                        {
                            // Update target of member to be the adjusted self?
                            // Member access usually expects the object as 'target'.
                            // If we adjusted it (e.g. &obj), then target should be &obj.
                            callee->member.target = self_arg;

                            // And we DO NOT add self to call.args
                            call->call.args = start;
                        }
                        else
                        {
                            // For VAR call (direct function call), we MUST pass self as first arg.
                            self_arg->next = start;
                            call->call.args = self_arg;
                        }

                        call->type_info = sig->ret_type;
                        if (call->type_info)
                        {
                            call->resolved_type = type_to_string(sig->ret_type);
                        }
                        else
                        {
                            call->resolved_type = xstrdup("unknown");
                        }

                        lhs = call;
                        free(mangled_index);
                        free(mangled_get);
                        continue;
                    }
                    free(mangled_index);
                    free(mangled_get);
                }

                // Static Array Bounds Check
                if (lhs->type_info && lhs->type_info->kind == TYPE_ARRAY &&
                    lhs->type_info->array_size > 0)
                {
                    if (start->type == NODE_EXPR_LITERAL && start->literal.type_kind == LITERAL_INT)
                    {
                        int idx = start->literal.int_val;
                        if (idx < 0 || idx >= lhs->type_info->array_size)
                        {
                            warn_array_bounds(op, idx, lhs->type_info->array_size);
                        }
                    }
                }

                // Assign type_info for index access (Fix for nested generics)
                if (lhs->type_info &&
                    (lhs->type_info->kind == TYPE_ARRAY || lhs->type_info->kind == TYPE_POINTER))
                {
                    node->type_info = lhs->type_info->inner;
                }
                if (!node->type_info)
                {
                    node->type_info = type_new(TYPE_INT);
                }

                lhs = node;
            }
            continue;
        }

        // Member: .
        if (op.type == TOK_OP && is_token(op, "."))
        {
            Token field = lexer_next(l);
            if (field.type != TOK_IDENT && field.type != TOK_INT)
            {
                zpanic_at(field, "Expected field name after .");
                break;
            }
            ASTNode *node = ast_create(NODE_EXPR_MEMBER);
            node->token = field;
            node->member.target = lhs;
            node->member.field = token_strdup(field);
            node->member.is_pointer_access = 0;

            // Opaque Check
            int is_ptr_dummy = 0;
            char *alloc_name = NULL;
            char *sname =
                resolve_struct_name_from_type(ctx, lhs->type_info, &is_ptr_dummy, &alloc_name);
            if (sname)
            {
                ASTNode *def = find_struct_def(ctx, sname);
                if (def && def->type == NODE_STRUCT && def->strct.is_opaque)
                {
                    if (!def->strct.defined_in_file ||
                        (g_current_filename &&
                         strcmp(def->strct.defined_in_file, g_current_filename) != 0))
                    {
                        zpanic_at(field, "Cannot access private field '%s' of opaque struct '%s'",
                                  node->member.field, sname);
                    }
                }
                if (alloc_name)
                {
                    free(alloc_name);
                }
            }

            node->member.field = token_strdup(field);
            node->member.is_pointer_access = 0;

            if (lhs->type_info && lhs->type_info->kind == TYPE_POINTER)
            {
                node->member.is_pointer_access = 1;

                // Special case: .val() on pointer = dereference
                if (strcmp(node->member.field, "val") == 0 && lexer_peek(l).type == TOK_LPAREN)
                {
                    lexer_next(l); // consume (
                    if (lexer_peek(l).type == TOK_RPAREN)
                    {
                        lexer_next(l); // consume )
                        // Rewrite to dereference: *ptr
                        ASTNode *deref = ast_create(NODE_EXPR_UNARY);
                        deref->unary.op = xstrdup("*");
                        deref->unary.operand = lhs;
                        deref->type_info = lhs->type_info->inner;
                        lhs = deref;
                        continue;
                    }
                }
            }
            else if (lhs->type == NODE_EXPR_VAR)
            {
                char *type = find_symbol_type(ctx, lhs->var_ref.name);
                if (type && strchr(type, '*'))
                {
                    node->member.is_pointer_access = 1;

                    // Special case: .val() on pointer = dereference
                    if (strcmp(node->member.field, "val") == 0 && lexer_peek(l).type == TOK_LPAREN)
                    {
                        lexer_next(l); // consume (
                        if (lexer_peek(l).type == TOK_RPAREN)
                        {
                            lexer_next(l); // consume )
                            // Rewrite to dereference: *ptr
                            ASTNode *deref = ast_create(NODE_EXPR_UNARY);
                            deref->unary.op = xstrdup("*");
                            deref->unary.operand = lhs;
                            // Try to get inner type
                            if (lhs->type_info && lhs->type_info->kind == TYPE_POINTER)
                            {
                                deref->type_info = lhs->type_info->inner;
                            }
                            lhs = deref;
                            continue;
                        }
                    }
                }
                if (strcmp(lhs->var_ref.name, "self") == 0 && !node->member.is_pointer_access)
                {
                    node->member.is_pointer_access = 1;
                }
            }

            node->type_info = get_field_type(ctx, lhs->type_info, node->member.field);

            if (!node->type_info && lhs->type_info)
            {
                char *struct_name = NULL;
                Type *st = lhs->type_info;
                if (st->kind == TYPE_STRUCT)
                {
                    struct_name = st->name;
                }
                else if (st->kind == TYPE_POINTER && st->inner && st->inner->kind == TYPE_STRUCT)
                {
                    struct_name = st->inner->name;
                }

                if (struct_name)
                {
                    size_t mangled_sz = strlen(struct_name) + strlen(node->member.field) + 3;
                    char *mangled = xmalloc(mangled_sz);
                    snprintf(mangled, mangled_sz, "%s__%s", struct_name, node->member.field);

                    FuncSig *sig = find_func(ctx, mangled);

                    if (!sig)
                    {
                        // Try resolving as a trait method: Struct__Trait__Method
                        StructRef *ref = ctx->parsed_impls_list;
                        while (ref)
                        {
                            if (ref->node && ref->node->type == NODE_IMPL_TRAIT)
                            {
                                const char *t_struct = ref->node->impl_trait.target_type;
                                if (t_struct && strcmp(t_struct, struct_name) == 0)
                                {
                                    size_t tm_sz = strlen(struct_name) +
                                                   strlen(ref->node->impl_trait.trait_name) +
                                                   strlen(node->member.field) + 4;
                                    char *trait_mangled = xmalloc(tm_sz);
                                    snprintf(trait_mangled, tm_sz, "%s__%s_%s", struct_name,
                                             ref->node->impl_trait.trait_name, node->member.field);
                                    if (find_func(ctx, trait_mangled))
                                    {
                                        free(mangled);
                                        mangled = trait_mangled;
                                        sig = find_func(ctx, mangled);
                                        break;
                                    }
                                    free(trait_mangled);
                                }
                            }
                            ref = ref->next;
                        }
                    }

                    if (sig)
                    {
                        // It is a method! Create a Function Type Info to carry the return
                        // type
                        Type *ft = type_new(TYPE_FUNCTION);
                        ft->name = xstrdup(mangled);
                        ft->inner = sig->ret_type; // Return type
                        node->type_info = ft;
                    }
                }
            }

            if (node->type_info)
            {
                node->resolved_type = type_to_string(node->type_info);
            }
            else
            {
                node->resolved_type = xstrdup("unknown");
            }

            // Handle Generic Method Call: object.method<T>
            if (lexer_peek(l).type == TOK_LANGLE)
            {
                Lexer lookahead = *l;
                lexer_next(&lookahead);

                int valid_generic = 0;
                int saved = ctx->is_speculative;
                ctx->is_speculative = 1;

                // Speculatively check if it's a valid generic list
                while (1)
                {
                    parse_type(ctx, &lookahead);
                    if (lexer_peek(&lookahead).type == TOK_COMMA)
                    {
                        lexer_next(&lookahead);
                        continue;
                    }
                    if (lexer_peek(&lookahead).type == TOK_RANGLE)
                    {
                        valid_generic = 1;
                    }
                    break;
                }
                ctx->is_speculative = saved;

                if (valid_generic)
                {
                    lexer_next(l); // consume <

                    char **concrete = xmalloc(sizeof(char *) * 8);
                    char **unmangled = xmalloc(sizeof(char *) * 8);
                    int argc = 0;
                    while (1)
                    {
                        Type *inner_t = parse_type_formal(ctx, l);
                        concrete[argc] = type_to_string(inner_t);
                        unmangled[argc] = type_to_c_string(inner_t);
                        argc++;
                        if (lexer_peek(l).type == TOK_COMMA)
                        {
                            lexer_next(l);
                        }
                        else
                        {
                            break;
                        }
                    }
                    if (lexer_next(l).type != TOK_RANGLE)
                    {
                        zpanic_at(lexer_peek(l), "Expected >");
                    }

                    // Locate the generic template
                    char *mn = NULL; // method name
                    char *full_name = NULL;

                    // If logic above found a sig, we have a mangled name in node->type_info->name
                    // But for templates, find_func might have failed.
                    // Construct potential template name: Struct__method
                    char *struct_name = NULL;
                    if (lhs->type_info)
                    {
                        if (lhs->type_info->kind == TYPE_STRUCT)
                        {
                            struct_name = lhs->type_info->name;
                        }
                        else if (lhs->type_info->kind == TYPE_POINTER && lhs->type_info->inner &&
                                 lhs->type_info->inner->kind == TYPE_STRUCT)
                        {
                            struct_name = lhs->type_info->inner->name;
                        }
                    }

                    if (struct_name)
                    {
                        size_t fn_sz = strlen(struct_name) + strlen(node->member.field) + 3;
                        full_name = xmalloc(fn_sz);
                        snprintf(full_name, fn_sz, "%s__%s", struct_name, node->member.field);

                        // Join types
                        size_t ac_sz = 1024, au_sz = 1024;
                        char *all_concrete = xmalloc(ac_sz);
                        char *all_unmangled = xmalloc(au_sz);
                        all_concrete[0] = 0;
                        all_unmangled[0] = 0;
                        for (int i = 0; i < argc; i++)
                        {
                            if (i > 0)
                            {
                                if (strlen(all_concrete) + 2 >= ac_sz)
                                {
                                    ac_sz *= 2;
                                    all_concrete = xrealloc(all_concrete, ac_sz);
                                }
                                if (strlen(all_unmangled) + 2 >= au_sz)
                                {
                                    au_sz *= 2;
                                    all_unmangled = xrealloc(all_unmangled, au_sz);
                                }
                                strcat(all_concrete, ",");
                                strcat(all_unmangled, ",");
                            }
                            if (strlen(all_concrete) + strlen(concrete[i]) + 1 >= ac_sz)
                            {
                                ac_sz += strlen(concrete[i]) + 1;
                                all_concrete = xrealloc(all_concrete, ac_sz);
                            }
                            if (strlen(all_unmangled) + strlen(unmangled[i]) + 1 >= au_sz)
                            {
                                au_sz += strlen(unmangled[i]) + 1;
                                all_unmangled = xrealloc(all_unmangled, au_sz);
                            }
                            strcat(all_concrete, concrete[i]);
                            strcat(all_unmangled, unmangled[i]);
                            free(concrete[i]);
                            free(unmangled[i]);
                        }
                        free(concrete);
                        free(unmangled);

                        mn = instantiate_function_template(ctx, full_name, all_concrete,
                                                           all_unmangled);
                        if (mn)
                        {
                            char *p = strstr(mn, "__");
                            if (p)
                            {
                                free(node->member.field);
                                node->member.field = xstrdup(p + 2);
                            }

                            Type *ft = type_new(TYPE_FUNCTION);
                            ft->name = xstrdup(mn);
                            FuncSig *isig = find_func(ctx, mn);
                            if (isig)
                            {
                                ft->inner = isig->ret_type;
                            }
                            node->type_info = ft;
                        }
                        if (full_name)
                        {
                            free(full_name);
                        }
                        if (all_concrete)
                        {
                            free(all_concrete);
                        }
                        if (all_unmangled)
                        {
                            free(all_unmangled);
                        }
                    }
                }
            }

            lhs = node;
            continue;
        }

        int next_prec = prec + 1;
        if (op.type == TOK_OP && (is_token(op, "**") || is_token(op, "**=")))
        {
            next_prec = prec;
        }

        ASTNode *rhs = parse_expr_prec(ctx, l, next_prec);
        ASTNode *bin = ast_create(NODE_EXPR_BINARY);
        bin->token = op;
        if (op.type == TOK_OP)
        {
            if (is_token(op, "&") || is_token(op, "|") || is_token(op, "^"))
            {
                zen_trigger_at(TRIGGER_BITWISE, op);
            }
            else if (is_token(op, "<<") || is_token(op, ">>"))
            {
                zen_trigger_at(TRIGGER_BITWISE, op);
            }
        }
        bin->binary.left = lhs;
        bin->binary.right = rhs;

        // Move Semantics Logic
        if (op.type == TOK_OP && is_token(op, "=")) // Assignment "="
        {
            // 1. RHS is being read: Check validity
            check_move_usage(ctx, rhs, op);

            // 2. Mark RHS as moved (Transfer ownership) if it's a Move type
            if (rhs->type == NODE_EXPR_VAR)
            {
                Type *inner_t = find_symbol_type_info(ctx, rhs->var_ref.name);
                // If type info not on var, try looking up symbol
                if (!inner_t)
                {
                    ZenSymbol *s = find_symbol_entry(ctx, rhs->var_ref.name);
                    if (s)
                    {
                        inner_t = s->type_info;
                    }
                }

                if (!is_type_copy(ctx, inner_t))
                {
                    ZenSymbol *s = find_symbol_entry(ctx, rhs->var_ref.name);
                    if (s)
                    {
                        s->is_moved = 1;
                    }
                }
            }

            // 3. LHS is being written: Resurrect (it is now valid)
            if (lhs->type == NODE_EXPR_VAR)
            {
                ZenSymbol *s = find_symbol_entry(ctx, lhs->var_ref.name);
                if (s)
                {
                    s->is_moved = 0;
                }
            }

            // 4. Trait Object Wrapping for Assignment
            char *raw_lhs_type = NULL;
            int allocated_lhs = 0;
            if (lhs->type == NODE_EXPR_VAR)
            {
                raw_lhs_type = find_symbol_type(ctx, lhs->var_ref.name);
            }
            if (!raw_lhs_type && lhs->resolved_type)
            {
                raw_lhs_type = lhs->resolved_type;
            }
            if (!raw_lhs_type && lhs->type_info)
            {
                raw_lhs_type = type_to_string(lhs->type_info);
                allocated_lhs = 1;
            }

            if (raw_lhs_type)
            {
                char *clean_lhs_type = raw_lhs_type;
                if (strncmp(clean_lhs_type, "const ", 6) == 0)
                {
                    clean_lhs_type += 6;
                }

                if (is_trait(clean_lhs_type) && rhs)
                {
                    rhs = transform_to_trait_object(ctx, clean_lhs_type, rhs);
                    bin->binary.right = rhs;
                }
                if (allocated_lhs)
                {
                    free(raw_lhs_type);
                }
            }
        }
        else // All other binary ops (including +=, -=, etc. which read LHS first)
        {
            check_move_usage(ctx, lhs, op);
            check_move_usage(ctx, rhs, op);
        }

        if (op.type == TOK_LANGLE)
        {
            bin->binary.op = xstrdup("<");
        }
        else if (op.type == TOK_RANGLE)
        {
            bin->binary.op = xstrdup(">");
        }
        else if (op.type == TOK_AND)
        {
            bin->binary.op = xstrdup("&&");
        }
        else if (op.type == TOK_OR)
        {
            bin->binary.op = xstrdup("||");
        }
        else
        {
            bin->binary.op = token_strdup(op);
        }

        if (is_comparison_op(bin->binary.op))
        {
            // Check for identical operands (x == x)
            if (lhs->type == NODE_EXPR_VAR && rhs->type == NODE_EXPR_VAR)
            {
                if (strcmp(lhs->var_ref.name, rhs->var_ref.name) == 0)
                {
                    if (strcmp(bin->binary.op, "==") == 0 || strcmp(bin->binary.op, ">=") == 0 ||
                        strcmp(bin->binary.op, "<=") == 0)
                    {
                        warn_comparison_always_true(op, "Comparing a variable to itself");
                    }
                    else if (strcmp(bin->binary.op, "!=") == 0 ||
                             strcmp(bin->binary.op, ">") == 0 || strcmp(bin->binary.op, "<") == 0)
                    {
                        warn_comparison_always_false(op, "Comparing a variable to itself");
                    }
                }
            }
            else if (lhs->type == NODE_EXPR_LITERAL && lhs->literal.type_kind == LITERAL_INT &&
                     rhs->type == NODE_EXPR_LITERAL && rhs->literal.type_kind == LITERAL_INT)
            {
                // Check if literals make sense (e.g. 5 > 5)
                if (lhs->literal.int_val == rhs->literal.int_val)
                {
                    if (strcmp(bin->binary.op, "==") == 0 || strcmp(bin->binary.op, ">=") == 0 ||
                        strcmp(bin->binary.op, "<=") == 0)
                    {
                        warn_comparison_always_true(op, "Comparing identical literals");
                    }
                    else
                    {
                        warn_comparison_always_false(op, "Comparing identical literals");
                    }
                }
            }

            if (lhs->type_info && type_is_unsigned(lhs->type_info))
            {
                if (rhs->type == NODE_EXPR_LITERAL && rhs->literal.type_kind == LITERAL_INT &&
                    rhs->literal.int_val == 0)
                {
                    if (strcmp(bin->binary.op, ">=") == 0)
                    {
                        warn_comparison_always_true(op, "Unsigned value is always >= 0");
                    }
                    else if (strcmp(bin->binary.op, "<") == 0)
                    {
                        warn_comparison_always_false(op, "Unsigned value is never < 0");
                    }
                }
            }
        }

        if (strcmp(bin->binary.op, "=") == 0 || strcmp(bin->binary.op, "+=") == 0 ||
            strcmp(bin->binary.op, "-=") == 0 || strcmp(bin->binary.op, "*=") == 0 ||
            strcmp(bin->binary.op, "/=") == 0)
        {

            if (lhs->type == NODE_EXPR_VAR)
            {
                // Check if the variable is const
                Type *inner_t = find_symbol_type_info(ctx, lhs->var_ref.name);
                if (inner_t && inner_t->is_const)
                {
                    zpanic_at(op, "Cannot assign to const variable '%s'", lhs->var_ref.name);
                }
            }
            else if (lhs->type == NODE_EXPR_INDEX || lhs->type == NODE_EXPR_MEMBER)
            {
                ASTNode *base = lhs;
                while (base)
                {
                    if (base->type == NODE_EXPR_INDEX)
                    {
                        base = base->index.array;
                    }
                    else if (base->type == NODE_EXPR_MEMBER)
                    {
                        base = base->member.target;
                    }
                    else
                    {
                        break;
                    }
                }
                if (base && base->type == NODE_EXPR_VAR)
                {
                    Type *inner_t = find_symbol_type_info(ctx, base->var_ref.name);
                    if (inner_t && inner_t->is_const)
                    {
                        zpanic_at(op, "Cannot assign to element of const variable '%s'",
                                  base->var_ref.name);
                    }
                }
            }
        }

        int is_compound = 0;
        size_t op_len = strlen(bin->binary.op);

        // Check if operator ends with '=' but is not ==, !=, <=, >=
        if (op_len > 1 && bin->binary.op[op_len - 1] == '=')
        {
            char c = bin->binary.op[0];
            if (c != '=' && c != '!' && c != '<' && c != '>')
            {
                is_compound = 1;
            }
            // Special handle for <<= and >>=
            if (strcmp(bin->binary.op, "<<=") == 0 || strcmp(bin->binary.op, ">>=") == 0)
            {
                is_compound = 1;
            }
        }

        if (is_compound)
        {
            ASTNode *op_node = ast_create(NODE_EXPR_BINARY);
            op_node->binary.left = lhs;
            op_node->binary.right = rhs;

            // Extract the base operator (remove last char '=')
            size_t inner_op_len = op_len - 1;
            char *inner_op = xmalloc(inner_op_len + 1);
            strncpy(inner_op, bin->binary.op, inner_op_len);
            inner_op[inner_op_len] = '\0';
            op_node->binary.op = inner_op;

            // Inherit type info temporarily
            if (lhs->type_info && rhs->type_info && type_eq(lhs->type_info, rhs->type_info))
            {
                op_node->type_info = lhs->type_info;
            }

            const char *inner_method = get_operator_method(inner_op);
            if (inner_method)
            {
                Type *lt = lhs->type_info;
                int is_lhs_ptr = 0;
                char *allocated_name = NULL;
                char *struct_name =
                    resolve_struct_name_from_type(ctx, lt, &is_lhs_ptr, &allocated_name);

                if (struct_name)
                {
                    size_t mangled_sz = strlen(struct_name) + strlen(inner_method) + 3;
                    char *mangled = xmalloc(mangled_sz);
                    snprintf(mangled, mangled_sz, "%s__%s", struct_name, inner_method);
                    FuncSig *sig = find_func(ctx, mangled);
                    if (sig)
                    {
                        // Rewrite op_node from BINARY -> CALL
                        ASTNode *call = ast_create(NODE_EXPR_CALL);
                        call->token = t;
                        ASTNode *callee = ast_create(NODE_EXPR_VAR);
                        callee->var_ref.name = xstrdup(mangled);
                        call->call.callee = callee;

                        // Handle 'self' argument
                        ASTNode *arg1 = lhs;
                        if (sig->total_args > 0 && sig->arg_types[0]->kind == TYPE_POINTER &&
                            !is_lhs_ptr)
                        {
                            ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                            addr->unary.op = xstrdup("&");
                            addr->unary.operand = lhs;
                            addr->type_info = type_new_ptr(lt);
                            arg1 = addr;
                        }
                        else if (is_lhs_ptr && sig->arg_types[0]->kind != TYPE_POINTER)
                        {
                            ASTNode *deref = ast_create(NODE_EXPR_UNARY);
                            deref->unary.op = xstrdup("*");
                            deref->unary.operand = lhs;
                            arg1 = deref;
                        }

                        call->call.args = arg1;
                        arg1->next = rhs;
                        rhs->next = NULL;
                        call->type_info = sig->ret_type;

                        // Replace op_node with the call
                        op_node = call;
                    }
                }
                if (allocated_name)
                {
                    free(allocated_name);
                }
            }

            free(bin->binary.op);
            bin->binary.op = xstrdup("=");
            bin->binary.right = op_node;
        }

        // Index Set Overload: Call(get, idx) = val  -->  Call(set, idx, val)
        if (strcmp(bin->binary.op, "=") == 0 && lhs->type == NODE_EXPR_CALL)
        {
            if (lhs->call.callee->type == NODE_EXPR_VAR)
            {
                char *name = lhs->call.callee->var_ref.name;
                // Check if it ends in "_get"
                size_t len = strlen(name);
                if (len > 4 && strcmp(name + len - 4, "_get") == 0)
                {
                    char *set_name = xstrdup(name);
                    set_name[len - 3] = 's'; // Replace 'g' with 's' -> _set
                    set_name[len - 2] = 'e';
                    set_name[len - 1] = 't';

                    if (find_func(ctx, set_name))
                    {
                        // Create NEW Call Node for Set
                        ASTNode *set_call = ast_create(NODE_EXPR_CALL);
                        set_call->token = t;
                        ASTNode *set_callee = ast_create(NODE_EXPR_VAR);
                        set_callee->var_ref.name = set_name;
                        set_call->call.callee = set_callee;

                        // Clone argument list (Shallow copy of arg nodes to preserve chain
                        // for get)
                        ASTNode *lhs_args = lhs->call.args;
                        ASTNode *new_head = NULL;
                        ASTNode *new_tail = NULL;

                        while (lhs_args)
                        {
                            ASTNode *arg_copy = xmalloc(sizeof(ASTNode));
                            memcpy(arg_copy, lhs_args, sizeof(ASTNode));
                            arg_copy->next = NULL;

                            if (!new_head)
                            {
                                new_head = arg_copy;
                            }
                            else
                            {
                                new_tail->next = arg_copy;
                            }
                            new_tail = arg_copy;

                            lhs_args = lhs_args->next;
                        }

                        // Append RHS to new args
                        ASTNode *val_expr = bin->binary.right;
                        if (new_tail)
                        {
                            new_tail->next = val_expr;
                        }
                        else
                        {
                            new_head = val_expr;
                        }

                        set_call->call.args = new_head;
                        set_call->type_info = type_new(TYPE_VOID);

                        lhs = set_call; // Use the new Set call as the result
                        continue;
                    }
                    else
                    {
                        free(set_name);
                    }
                }
            }
        }

        const char *method = get_operator_method(bin->binary.op);

        if (method)
        {
            Type *lt = lhs->type_info;
            int is_lhs_ptr = 0;
            char *allocated_name = NULL;
            char *struct_name =
                resolve_struct_name_from_type(ctx, lt, &is_lhs_ptr, &allocated_name);

            // If we are comparing pointers with == or !=, do NOT rewrite to .eq()
            // We want pointer equality, not value equality (which requires dereferencing)
            // But strict check: Only if BOTH are pointers. If one is value, we might need rewrite.
            if (is_lhs_ptr && struct_name &&
                (strcmp(bin->binary.op, "==") == 0 || strcmp(bin->binary.op, "!=") == 0))
            {
                int is_rhs_ptr = 0;
                char *r_alloc = NULL;

                // This gives a warning as "unused" but it's needed for the rewrite.
                char *r_name =
                    resolve_struct_name_from_type(ctx, rhs->type_info, &is_rhs_ptr, &r_alloc);
                (void)r_name;
                if (r_alloc)
                {
                    free(r_alloc);
                }

                if (is_rhs_ptr)
                {
                    // Both are pointers: Skip rewrite to allow pointer comparison
                    if (allocated_name)
                    {
                        free(allocated_name);
                    }
                    struct_name = NULL;
                }
            }

            if (struct_name)
            {
                size_t mangled_sz = strlen(struct_name) + strlen(method) + 3;
                char *mangled = xmalloc(mangled_sz);
                snprintf(mangled, mangled_sz, "%s__%s", struct_name, method);

                FuncSig *sig = find_func(ctx, mangled);

                if (!sig)
                {
                    // Try resolving as a trait method: Struct__Trait__Method
                    StructRef *ref = ctx->parsed_impls_list;
                    while (ref)
                    {
                        if (ref->node && ref->node->type == NODE_IMPL_TRAIT)
                        {
                            const char *t_struct = ref->node->impl_trait.target_type;
                            if (t_struct && strcmp(t_struct, struct_name) == 0)
                            {
                                char *trait_mangled;
                                size_t tm_sz = strlen(struct_name) +
                                               strlen(ref->node->impl_trait.trait_name) +
                                               strlen(method) + 4;
                                trait_mangled = xmalloc(tm_sz);
                                snprintf(trait_mangled, tm_sz, "%s__%s_%s", struct_name,
                                         ref->node->impl_trait.trait_name, method);
                                if (find_func(ctx, trait_mangled))
                                {
                                    free(mangled);
                                    mangled = trait_mangled;
                                    sig = find_func(ctx, mangled);
                                    break;
                                }
                                free(trait_mangled);
                            }
                        }
                        ref = ref->next;
                    }
                }

                if (sig)
                {
                    ASTNode *call = ast_create(NODE_EXPR_CALL);
                    call->token = t;
                    ASTNode *callee = ast_create(NODE_EXPR_VAR);
                    callee->var_ref.name = xstrdup(mangled);
                    call->call.callee = callee;

                    ASTNode *arg1 = lhs;

                    // Check if function expects a pointer for 'self'
                    if (sig->total_args > 0 && sig->arg_types[0] &&
                        sig->arg_types[0]->kind == TYPE_POINTER)
                    {
                        if (!is_lhs_ptr)
                        {
                            // Value -> Pointer.
                            int is_rvalue =
                                (lhs->type == NODE_EXPR_CALL || lhs->type == NODE_EXPR_BINARY ||
                                 lhs->type == NODE_EXPR_STRUCT_INIT ||
                                 lhs->type == NODE_EXPR_CAST || lhs->type == NODE_MATCH);

                            ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                            addr->unary.op = is_rvalue ? xstrdup("&_rval") : xstrdup("&");
                            addr->unary.operand = lhs;
                            addr->type_info = type_new_ptr(lt);
                            arg1 = addr;
                        }
                    }
                    else
                    {
                        // Function expects value
                        if (is_lhs_ptr)
                        {
                            // Have pointer, need value -> *lhs
                            ASTNode *deref = ast_create(NODE_EXPR_UNARY);
                            deref->unary.op = xstrdup("*");
                            deref->unary.operand = lhs;
                            if (lt && lt->kind == TYPE_POINTER)
                            {
                                deref->type_info = lt->inner;
                            }
                            arg1 = deref;
                        }
                    }

                    // Handle RHS (Argument 2) Auto-Ref if needed
                    ASTNode *arg2 = rhs;
                    if (sig->total_args > 1 && sig->arg_types[1] &&
                        sig->arg_types[1]->kind == TYPE_POINTER)
                    {
                        Type *rt = rhs->type_info;

                        // If rhs is a variable reference without type_info, look it up
                        if (!rt && rhs->type == NODE_EXPR_VAR)
                        {
                            ZenSymbol *sym = find_symbol_entry(ctx, rhs->var_ref.name);
                            if (sym && sym->type_info)
                            {
                                rt = sym->type_info;
                                rhs->type_info = rt;
                                if (sym->type_name)
                                {
                                    rhs->resolved_type = xstrdup(sym->type_name);
                                }
                            }
                        }

                        int is_rhs_ptr = (rt && rt->kind == TYPE_POINTER);
                        if (!is_rhs_ptr) // Need pointer, have value
                        {
                            int is_rvalue =
                                (rhs->type == NODE_EXPR_CALL || rhs->type == NODE_EXPR_BINARY ||
                                 rhs->type == NODE_EXPR_STRUCT_INIT ||
                                 rhs->type == NODE_EXPR_CAST || rhs->type == NODE_MATCH);

                            ASTNode *addr = ast_create(NODE_EXPR_UNARY);
                            addr->unary.op = is_rvalue ? xstrdup("&_rval") : xstrdup("&");
                            addr->unary.operand = rhs;
                            if (rt)
                            {
                                addr->type_info = type_new_ptr(rt);
                            }
                            arg2 = addr;
                        }
                    }

                    call->call.args = arg1;
                    arg1->next = arg2;
                    arg2->next = NULL;

                    call->type_info = sig->ret_type;
                    call->resolved_type = type_to_string(sig->ret_type);

                    lhs = call;
                    if (allocated_name)
                    {
                        free(allocated_name);
                    }
                    continue; // Loop again with result as new lhs
                }
                if (allocated_name)
                {
                    free(allocated_name);
                }
            }
        }

        // Standard Type Checking (if no overload found)
        if (lhs->type_info && rhs->type_info)
        {
            // Ensure type_info is set for variables (critical for inference)
            if (lhs->type == NODE_EXPR_VAR && !lhs->type_info)
            {
                ZenSymbol *s = find_symbol_entry(ctx, lhs->var_ref.name);
                if (s)
                {
                    lhs->type_info = s->type_info;
                }
            }
            if (rhs->type == NODE_EXPR_VAR && !rhs->type_info)
            {
                ZenSymbol *s = find_symbol_entry(ctx, rhs->var_ref.name);
                if (s)
                {
                    rhs->type_info = s->type_info;
                }
            }

            // Backward Inference for Lambda Params
            // LHS is Unknown Var, RHS is Known
            if (lhs->type == NODE_EXPR_VAR && lhs->type_info &&
                lhs->type_info->kind == TYPE_UNKNOWN && rhs->type_info &&
                rhs->type_info->kind != TYPE_UNKNOWN)
            {
                // Infer LHS type from RHS
                ZenSymbol *sym = find_symbol_entry(ctx, lhs->var_ref.name);
                if (sym)
                {
                    // Update ZenSymbol
                    sym->type_info = rhs->type_info;
                    sym->type_name = type_to_string(rhs->type_info);

                    // Update AST Node
                    lhs->type_info = rhs->type_info;
                    lhs->resolved_type = xstrdup(sym->type_name);
                }
            }

            // RHS is Unknown Var, LHS is Known
            if (rhs->type == NODE_EXPR_VAR && rhs->type_info &&
                rhs->type_info->kind == TYPE_UNKNOWN && lhs->type_info &&
                lhs->type_info->kind != TYPE_UNKNOWN)
            {
                // Infer RHS type from LHS
                ZenSymbol *sym = find_symbol_entry(ctx, rhs->var_ref.name);
                if (sym)
                {
                    // Update ZenSymbol
                    sym->type_info = lhs->type_info;
                    sym->type_name = type_to_string(lhs->type_info);

                    // Update AST Node
                    rhs->type_info = lhs->type_info;
                    rhs->resolved_type = xstrdup(sym->type_name);
                }
            }

            if (is_comparison_op(bin->binary.op))
            {
                bin->type_info = type_new(TYPE_INT); // bool
                char *t1 = type_to_string(lhs->type_info);
                char *t2 = type_to_string(rhs->type_info);
                // Skip type check if either operand is void* (escape hatch type)
                // or if either operand is a generic type parameter (T, K, V, etc.)
                int skip_check = (strcmp(t1, "void*") == 0 || strcmp(t2, "void*") == 0);
                if (lhs->type_info->kind == TYPE_GENERIC || rhs->type_info->kind == TYPE_GENERIC)
                {
                    skip_check = 1;
                }
                // Also check if type name is a single uppercase letter (common generic param)
                if ((strlen(t1) == 1 && isupper(t1[0])) || (strlen(t2) == 1 && isupper(t2[0])))
                {
                    skip_check = 1;
                }

                // Allow comparing pointers/strings with integer literal 0 (NULL)
                if (!skip_check)
                {
                    int lhs_is_ptr =
                        (lhs->type_info->kind == TYPE_POINTER ||
                         lhs->type_info->kind == TYPE_STRING || (t1 && strstr(t1, "*")));
                    int rhs_is_ptr =
                        (rhs->type_info->kind == TYPE_POINTER ||
                         rhs->type_info->kind == TYPE_STRING || (t2 && strstr(t2, "*")));

                    if (lhs_is_ptr && rhs->type == NODE_EXPR_LITERAL && rhs->literal.int_val == 0)
                    {
                        skip_check = 1;
                    }
                    if (rhs_is_ptr && lhs->type == NODE_EXPR_LITERAL && lhs->literal.int_val == 0)
                    {
                        skip_check = 1;
                    }
                }

                int lhs_is_num =
                    is_integer_type(lhs->type_info) || lhs->type_info->kind == TYPE_F32 ||
                    lhs->type_info->kind == TYPE_F64 || lhs->type_info->kind == TYPE_FLOAT;
                int rhs_is_num =
                    is_integer_type(rhs->type_info) || rhs->type_info->kind == TYPE_F32 ||
                    rhs->type_info->kind == TYPE_F64 || rhs->type_info->kind == TYPE_FLOAT;

                if (!skip_check && !type_eq(lhs->type_info, rhs->type_info) &&
                    !(lhs_is_num && rhs_is_num))
                {
                    char msg[256];
                    sprintf(msg, "Type mismatch in comparison: cannot compare '%s' and '%s'", t1,
                            t2);

                    char suggestion[256];
                    sprintf(suggestion, "Both operands must have compatible types for comparison");

                    if (g_config.mode_lsp)
                    {
                        zwarn_at(op, "LSP: %s", msg);
                        // Assume result is int (bool) to continue
                        bin->type_info = type_new(TYPE_INT);
                    }
                    else
                    {
                        zpanic_with_suggestion(op, msg, suggestion);
                    }
                }
            }
            else
            {
                if (type_eq(lhs->type_info, rhs->type_info) ||
                    check_opaque_alias_compat(ctx, lhs->type_info, rhs->type_info))
                {
                    bin->type_info = lhs->type_info;
                }
                else
                {
                    // Check aliases
                    char *al = NULL, *ar = NULL;
                    int pl = 0, pr = 0;
                    char *sl = resolve_struct_name_from_type(ctx, lhs->type_info, &pl, &al);
                    char *sr = resolve_struct_name_from_type(ctx, rhs->type_info, &pr, &ar);

                    int alias_match = 0;
                    if (sl && sr && strcmp(sl, sr) == 0 && pl == pr)
                    {
                        alias_match = 1;
                        bin->type_info = lhs->type_info;
                    }
                    if (al)
                    {
                        free(al);
                    }
                    if (ar)
                    {
                        free(ar);
                    }

                    char *t1 = type_to_string(lhs->type_info);
                    char *t2 = type_to_string(rhs->type_info);

                    // Allow pointer arithmetic: ptr + int, ptr - int, int + ptr
                    int is_ptr_arith = 0;
                    if (!alias_match)
                    {
                        if (strcmp(bin->binary.op, "+") == 0 || strcmp(bin->binary.op, "-") == 0)
                        {
                            int lhs_is_ptr = (lhs->type_info->kind == TYPE_POINTER ||
                                              lhs->type_info->kind == TYPE_STRING ||
                                              (t1 && strstr(t1, "*") != NULL));
                            int rhs_is_ptr = (rhs->type_info->kind == TYPE_POINTER ||
                                              rhs->type_info->kind == TYPE_STRING ||
                                              (t2 && strstr(t2, "*") != NULL));
                            int lhs_is_int =
                                (lhs->type_info->kind == TYPE_INT ||
                                 lhs->type_info->kind == TYPE_I8 ||
                                 lhs->type_info->kind == TYPE_U8 ||
                                 lhs->type_info->kind == TYPE_I16 ||
                                 lhs->type_info->kind == TYPE_U16 ||
                                 lhs->type_info->kind == TYPE_I32 ||
                                 lhs->type_info->kind == TYPE_U32 ||
                                 lhs->type_info->kind == TYPE_I64 ||
                                 lhs->type_info->kind == TYPE_U64 ||
                                 lhs->type_info->kind == TYPE_ISIZE ||
                                 lhs->type_info->kind == TYPE_USIZE ||
                                 lhs->type_info->kind == TYPE_UINT ||
                                 lhs->type_info->kind == TYPE_BYTE ||
                                 lhs->type_info->kind == TYPE_RUNE || (t1 && IS_INT_TYPE(t1)) ||
                                 (t1 && IS_USIZE_TYPE(t1)) || (t1 && IS_ISIZE_TYPE(t1)));
                            int rhs_is_int =
                                (rhs->type_info->kind == TYPE_INT ||
                                 rhs->type_info->kind == TYPE_I8 ||
                                 rhs->type_info->kind == TYPE_U8 ||
                                 rhs->type_info->kind == TYPE_I16 ||
                                 rhs->type_info->kind == TYPE_U16 ||
                                 rhs->type_info->kind == TYPE_I32 ||
                                 rhs->type_info->kind == TYPE_U32 ||
                                 rhs->type_info->kind == TYPE_I64 ||
                                 rhs->type_info->kind == TYPE_U64 ||
                                 rhs->type_info->kind == TYPE_ISIZE ||
                                 rhs->type_info->kind == TYPE_USIZE ||
                                 rhs->type_info->kind == TYPE_UINT ||
                                 rhs->type_info->kind == TYPE_BYTE ||
                                 rhs->type_info->kind == TYPE_RUNE || (t2 && IS_INT_TYPE(t2)) ||
                                 (t2 && IS_USIZE_TYPE(t2)) || (t2 && IS_ISIZE_TYPE(t2)));

                            if ((lhs_is_ptr && rhs_is_int) || (lhs_is_int && rhs_is_ptr))
                            {
                                is_ptr_arith = 1;
                                bin->type_info = lhs_is_ptr ? lhs->type_info : rhs->type_info;
                            }
                        }
                    }

                    if (!is_ptr_arith && !alias_match)
                    {
                        // ** Backward Inference for Binary Ops **
                        // Case 1: LHS is Unknown Var, RHS is Known
                        if (lhs->type == NODE_EXPR_VAR && lhs->type_info &&
                            lhs->type_info->kind == TYPE_UNKNOWN && rhs->type_info &&
                            rhs->type_info->kind != TYPE_UNKNOWN)
                        {
                            // Infer LHS type from RHS
                            ZenSymbol *sym = find_symbol_entry(ctx, lhs->var_ref.name);
                            if (sym)
                            {
                                // Update ZenSymbol
                                sym->type_info = rhs->type_info;
                                sym->type_name = type_to_string(rhs->type_info);

                                // Update AST Node
                                lhs->type_info = rhs->type_info;
                                lhs->resolved_type = xstrdup(sym->type_name);

                                bin->type_info = rhs->type_info;
                                goto bin_inference_success;
                            }
                        }

                        // Case 2: RHS is Unknown Var, LHS is Known
                        if (rhs->type == NODE_EXPR_VAR && rhs->type_info &&
                            rhs->type_info->kind == TYPE_UNKNOWN && lhs->type_info &&
                            lhs->type_info->kind != TYPE_UNKNOWN)
                        {
                            // Infer RHS type from LHS
                            ZenSymbol *sym = find_symbol_entry(ctx, rhs->var_ref.name);
                            if (sym)
                            {
                                // Update ZenSymbol
                                sym->type_info = lhs->type_info;
                                sym->type_name = type_to_string(lhs->type_info);

                                // Update AST Node
                                rhs->type_info = lhs->type_info;
                                rhs->resolved_type = xstrdup(sym->type_name);

                                bin->type_info = lhs->type_info;
                                goto bin_inference_success;
                            }
                        }

                        // Allow assigning 0 to pointer (NULL)
                        int is_null_assign = 0;
                        if (strcmp(bin->binary.op, "=") == 0)
                        {
                            int lhs_is_ptr = (lhs->type_info->kind == TYPE_POINTER ||
                                              lhs->type_info->kind == TYPE_STRING ||
                                              (t1 && strstr(t1, "*") != NULL));
                            if (lhs_is_ptr && rhs->type == NODE_EXPR_LITERAL &&
                                rhs->literal.int_val == 0)
                            {
                                is_null_assign = 1;
                            }
                        }

                        if (!is_null_assign)
                        {
                            // Check for arithmetic promotion (Int * Float, etc)
                            int lhs_is_num = is_integer_type(lhs->type_info) ||
                                             lhs->type_info->kind == TYPE_F32 ||
                                             lhs->type_info->kind == TYPE_F64 ||
                                             lhs->type_info->kind == TYPE_FLOAT;
                            int rhs_is_num = is_integer_type(rhs->type_info) ||
                                             rhs->type_info->kind == TYPE_F32 ||
                                             rhs->type_info->kind == TYPE_F64 ||
                                             rhs->type_info->kind == TYPE_FLOAT;

                            int valid_arith = 0;
                            if (lhs_is_num && rhs_is_num)
                            {
                                if (strcmp(bin->binary.op, "+") == 0 ||
                                    strcmp(bin->binary.op, "-") == 0 ||
                                    strcmp(bin->binary.op, "*") == 0 ||
                                    strcmp(bin->binary.op, "/") == 0)
                                {
                                    valid_arith = 1;
                                    // Result is the float type if one is float
                                    if (lhs->type_info->kind == TYPE_F64 ||
                                        rhs->type_info->kind == TYPE_F64)
                                    {
                                        bin->type_info = lhs->type_info->kind == TYPE_F64
                                                             ? lhs->type_info
                                                             : rhs->type_info;
                                    }
                                    else if (lhs->type_info->kind == TYPE_F32 ||
                                             rhs->type_info->kind == TYPE_F32 ||
                                             lhs->type_info->kind == TYPE_FLOAT ||
                                             rhs->type_info->kind == TYPE_FLOAT)
                                    {
                                        // Pick the float type. If both float, pick lhs.
                                        if (lhs->type_info->kind == TYPE_F32 ||
                                            lhs->type_info->kind == TYPE_FLOAT)
                                        {
                                            bin->type_info = lhs->type_info;
                                        }
                                        else
                                        {
                                            bin->type_info = rhs->type_info;
                                        }
                                    }
                                    else
                                    {
                                        // Both int (but failed equality check previously? - rare
                                        // but possible if diff int types) If diff int types, we
                                        // usually allow it in C (promotion). For now, assume LHS
                                        // dominates or standard promotion.
                                        bin->type_info = lhs->type_info;
                                    }
                                }
                            }

                            if (!valid_arith)
                            {
                                char msg[256];
                                sprintf(msg, "Type mismatch in binary operation '%s'",
                                        bin->binary.op);

                                char suggestion[512];
                                sprintf(
                                    suggestion,
                                    "Left operand has type '%s', right operand has type '%s'\n   = "
                                    "note: Consider casting one operand to match the other",
                                    t1, t2);

                                zpanic_with_suggestion(op, msg, suggestion);
                            }
                        }

                    bin_inference_success:;
                    }
                }
            }
        }

        lhs = bin;
    }
    return lhs;
}

ASTNode *parse_arrow_lambda_single(ParserContext *ctx, Lexer *l, char *param_name,
                                   int default_capture_mode)
{
    ASTNode *lambda = ast_create(NODE_LAMBDA);
    lambda->lambda.num_params = 1;
    lambda->lambda.default_capture_mode = default_capture_mode;
    lambda->lambda.param_names = xmalloc(sizeof(char *));
    lambda->lambda.param_names[0] = param_name;
    lambda->lambda.num_params = 1;

    // Default param type: unknown (to be inferred)
    lambda->lambda.param_types = xmalloc(sizeof(char *));
    lambda->lambda.param_types[0] = NULL;

    // Create Type Info: unknown -> unknown
    Type *t = type_new(TYPE_FUNCTION);
    t->inner = type_new(TYPE_INT); // Return (default to int)
    t->args = xmalloc(sizeof(Type *));
    t->args[0] = type_new(TYPE_UNKNOWN); // Arg
    t->arg_count = 1;
    lambda->type_info = t;

    // Register parameter in scope for body parsing
    enter_scope(ctx);
    add_symbol(ctx, param_name, NULL, t->args[0]);

    // Body parsing...
    ASTNode *body_block = NULL;
    if (lexer_peek(l).type == TOK_LBRACE)
    {
        body_block = parse_block(ctx, l);
    }
    else
    {
        ASTNode *expr = parse_expression(ctx, l);
        ASTNode *ret = ast_create(NODE_RETURN);
        ret->ret.value = expr;
        body_block = ast_create(NODE_BLOCK);
        body_block->block.statements = ret;
    }
    lambda->lambda.body = body_block;

    // Attempt to infer return type from body if it's a simple return
    if (lambda->lambda.body->block.statements &&
        lambda->lambda.body->block.statements->type == NODE_RETURN &&
        !lambda->lambda.body->block.statements->next)
    {
        ASTNode *ret_val = lambda->lambda.body->block.statements->ret.value;
        if (ret_val->type_info && ret_val->type_info->kind != TYPE_UNKNOWN)
        {
            if (param_name[0] == 'x')
            {
            }
            // Update return type
            if (t->inner)
            {
                free(t->inner);
            }
            t->inner = ret_val->type_info;
        }
    }

    // Update parameter types from symbol table (in case inference happened)
    ZenSymbol *sym = find_symbol_entry(ctx, param_name);
    if (sym && sym->type_info && sym->type_info->kind != TYPE_UNKNOWN)
    {
        free(lambda->lambda.param_types[0]);
        lambda->lambda.param_types[0] = type_to_string(sym->type_info);
        t->args[0] = sym->type_info;
    }
    else
    {
        if (lambda->lambda.param_types[0])
        {
            free(lambda->lambda.param_types[0]);
        }
        lambda->lambda.param_types[0] = xstrdup("unknown");
        t->args[0] = type_new(TYPE_UNKNOWN);

        if (sym)
        {
            sym->type_name = xstrdup("unknown");
            sym->type_info = t->args[0]; // Bind AST node type implicitly!
        }
    }

    lambda->lambda.return_type = type_to_string(t->inner);
    lambda->lambda.lambda_id = ctx->lambda_counter++;
    lambda->lambda.is_expression = 1;
    register_lambda(ctx, lambda);
    analyze_lambda_captures(ctx, lambda);
    exit_scope(ctx);
    return lambda;
}

ASTNode *parse_arrow_lambda_multi(ParserContext *ctx, Lexer *l, char **param_names,
                                  Type **param_types, int num_params, int default_capture_mode)
{
    ASTNode *lambda = ast_create(NODE_LAMBDA);
    lambda->lambda.param_names = param_names;
    lambda->lambda.num_params = num_params;
    lambda->lambda.default_capture_mode = default_capture_mode;

    // Type Info construction
    Type *t = type_new(TYPE_FUNCTION);
    t->inner = type_new(TYPE_INT);
    t->args = xmalloc(sizeof(Type *) * num_params);
    t->arg_count = num_params;

    lambda->lambda.param_types = xmalloc(sizeof(char *) * num_params);
    for (int i = 0; i < num_params; i++)
    {
        if (param_types && param_types[i])
        {
            lambda->lambda.param_types[i] = type_to_string(param_types[i]);
            t->args[i] = param_types[i];
        }
        else
        {
            lambda->lambda.param_types[i] = xstrdup("unknown");
            t->args[i] = type_new(TYPE_UNKNOWN);
        }
    }
    lambda->type_info = t;

    // Register parameters in scope for body parsing
    enter_scope(ctx);
    for (int i = 0; i < num_params; i++)
    {
        if (param_types && param_types[i])
        {
            add_symbol(ctx, param_names[i], lambda->lambda.param_types[i], param_types[i]);
        }
        else
        {
            add_symbol(ctx, param_names[i], "unknown", t->args[i]);
        }
    }

    // Body parsing...
    ASTNode *body_block = NULL;
    if (lexer_peek(l).type == TOK_LBRACE)
    {
        body_block = parse_block(ctx, l);
    }
    else
    {
        ASTNode *expr = parse_expression(ctx, l);
        ASTNode *ret = ast_create(NODE_RETURN);
        ret->ret.value = expr;
        body_block = ast_create(NODE_BLOCK);
        body_block->block.statements = ret;
    }
    lambda->lambda.body = body_block;
    lambda->lambda.return_type = xstrdup("unknown");
    lambda->lambda.lambda_id = ctx->lambda_counter++;
    lambda->lambda.is_expression = 1;
    register_lambda(ctx, lambda);
    analyze_lambda_captures(ctx, lambda);
    exit_scope(ctx);
    return lambda;
}

ASTNode *parse_tuple_expression(ParserContext *ctx, Lexer *l, const char *type_name,
                                ASTNode *first_elem)
{
    Token tk;
    if (!first_elem)
    {
        tk = lexer_next(l); // eat (
    }
    else
    {
        tk = first_elem->token;
    }

    ASTNode *head = first_elem, *prev = first_elem;
    int count = (first_elem ? 1 : 0);

    // If first_elem was provided, we might be at a comma or the closing paren
    if (first_elem && lexer_peek(l).type == TOK_COMMA)
    {
        lexer_next(l); // eat comma
    }

    while (lexer_peek(l).type != TOK_RPAREN)
    {
        ASTNode *element = parse_expression(ctx, l);
        if (head == NULL)
        {
            head = element;
        }
        else
        {
            prev->next = element;
        }
        prev = element;
        count++;

        if (lexer_peek(l).type == TOK_COMMA)
        {
            lexer_next(l);
        }
        else
        {
            break;
        }
    }

    if (lexer_next(l).type != TOK_RPAREN)
    {
        zpanic_at(lexer_peek(l), "Expected ) after tuple literal");
    }

    ASTNode *n = ast_create(NODE_EXPR_TUPLE_LITERAL);
    n->token = tk;
    n->tuple_literal.elements = head;
    n->tuple_literal.count = count;

    if (type_name)
    {
        n->resolved_type = xstrdup(type_name);
    }
    else
    {
        char sig[1024];
        sig[0] = 0;
        ASTNode *curr = head;
        int i = 0;
        while (curr)
        {
            char *it = infer_type(ctx, curr);
            const char *type_name_to_append = it ? it : "int";

            if (i > 0)
            {
                if (strlen(sig) + 2 < sizeof(sig))
                {
                    strcat(sig, "__");
                }
            }

            if (strlen(sig) + strlen(type_name_to_append) < sizeof(sig))
            {
                strcat(sig, type_name_to_append);
            }

            curr = curr->next;
            i++;
        }
        register_tuple(ctx, sig);
        char tuple_name[1024 + 16];
        char *clean_sig = sanitize_mangled_name(sig);
        snprintf(tuple_name, sizeof(tuple_name), "Tuple_%s", clean_sig);
        free(clean_sig);
        n->resolved_type = xstrdup(tuple_name);
    }
    return n;
}