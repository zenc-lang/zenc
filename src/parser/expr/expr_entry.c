// SPDX-License-Identifier: MIT
#include "parser.h"
#include "constants.h"
#include "expr_internal.h"
#include "ast/ast.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ASTNode *parse_expression(ParserContext *ctx, Lexer *l)
{
    ASTNode *n = parse_expr_prec(ctx, l, PREC_NONE);
    // Return a safe sentinel instead of NULL — eliminates null-deref crashes
    // across all ~150 call sites without needing individual NULL checks.
    if (!n)
    {
        n = ast_create(NODE_ERRONEOUS);
        n->token = lexer_peek(l);
    }
    return n;
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
        *refs = xrealloc(*refs, sizeof(char *) * (size_t)(*ref_count + 1));
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
            *refs = xrealloc(*refs, sizeof(char *) * (size_t)(*ref_count + 1));
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
    case NODE_EXPECT:
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
        *decls = xrealloc(*decls, sizeof(char *) * (size_t)(*count + 1));
        (*decls)[*count] = xstrdup(node->var_decl.name);
        (*count)++;
    }

    if (node->type == NODE_MATCH_CASE)
    {
        if (node->match_case.binding_names)
        {
            for (int i = 0; i < node->match_case.binding_count; i++)
            {
                if (node->match_case.binding_names[i])
                {
                    *decls = xrealloc(*decls, sizeof(char *) * (size_t)(*count + 1));
                    (*decls)[*count] = xstrdup(node->match_case.binding_names[i]);
                    (*count)++;
                }
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
    Type **captured_types_info = xmalloc(sizeof(Type *) * 32);
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
            captured_types_info[num_captures] = t;
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
            strcmp(var_name, "Vec_new") == 0 || strcmp(var_name, "Vec_push") == 0 ||
            strcmp(var_name, "NULL") == 0 || strcmp(var_name, "true") == 0 ||
            strcmp(var_name, "false") == 0)
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
        captured_types_info[num_captures] = t;
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
    lambda->lambda.captured_types_info = captured_types_info;
    lambda->lambda.capture_modes = capture_modes;
    lambda->lambda.num_captures = num_captures;

    if (local_decls)
    {
        for (int i = 0; i < num_local_decls; i++)
        {
            zfree(local_decls[i]);
        }
        zfree(local_decls);
    }
    for (int i = 0; i < num_refs; i++)
    {
        zfree(all_refs[i]);
        zfree(all_refs);
    }
}
