// SPDX-License-Identifier: MIT
#include "../utils/colors.h"
#include "typecheck_internal.h"
#include "../constants.h"

#include "typecheck.h"
#include "comptime_interpreter.h"
#include "diagnostics/diagnostics.h"
#include "move_check.h"
#include "platform/misra.h"
#include <ctype.h>
#include <string.h>

// External helpers from parser

int tc_expr_has_side_effects(ASTNode *node)
{
    if (!node)
    {
        return 0;
    }

    switch (node->kind)
    {
    case NODE_EXPR_CALL:
        // Function calls are always considered to have potential side effects in MISRA
        return 1;

    case NODE_EXPR_BINARY:
        // Assignment operators are side effects
        if (node->binary.op && strstr(node->binary.op, "="))
        {
            return 1;
        }
        return tc_expr_has_side_effects(node->binary.left) ||
               tc_expr_has_side_effects(node->binary.right);

    case NODE_EXPR_UNARY:
        // Increment/Decrement are side effects (prefix and postfix)
        if (node->unary.op &&
            (strcmp(node->unary.op, "++") == 0 || strcmp(node->unary.op, "--") == 0 ||
             strcmp(node->unary.op, "_post++") == 0 || strcmp(node->unary.op, "_post--") == 0))
        {
            return 1;
        }
        return tc_expr_has_side_effects(node->unary.operand);

    case NODE_RAW_STMT:
        // Inline assembly always has potential side effects
        return 1;

    case NODE_EXPR_STRUCT_INIT:
    {
        ASTNode *f = node->struct_init.fields;
        while (f)
        {
            if (tc_expr_has_side_effects(f->var_decl.init_expr))
            {
                return 1;
            }
            f = f->next;
        }
        return 0;
    }

    case NODE_EXPR_ARRAY_LITERAL:
    {
        ASTNode *e = node->array_literal.elements;
        while (e)
        {
            if (tc_expr_has_side_effects(e))
            {
                return 1;
            }
            e = e->next;
        }
        return 0;
    }

    case NODE_EXPR_TUPLE_LITERAL:
    {
        ASTNode *e = node->tuple_literal.elements;
        while (e)
        {
            if (tc_expr_has_side_effects(e))
            {
                return 1;
            }
            e = e->next;
        }
        return 0;
    }

    case NODE_EXPR_MEMBER:
        return tc_expr_has_side_effects(node->member.target);

    case NODE_EXPR_INDEX:
        return tc_expr_has_side_effects(node->index.array) ||
               tc_expr_has_side_effects(node->index.index);

    case NODE_EXPR_CAST:
        return tc_expr_has_side_effects(node->cast.expr);

    case NODE_EXPR_SLICE:
        return tc_expr_has_side_effects(node->slice.array) ||
               tc_expr_has_side_effects(node->slice.start) ||
               tc_expr_has_side_effects(node->slice.end);

    default:
        // Most other nodes (LITERAL, VAR, SIZEOF itself if nested) are side-effect free
        return 0;
    }
}

void collect_symbols(ASTNode *node, SymbolSet *reads, SymbolSet *writes)
{
    if (!node)
    {
        return;
    }

    switch (node->kind)
    {
    case NODE_EXPR_BINARY:
        if (node->binary.op && strstr(node->binary.op, "="))
        {
            // LHS is a write
            if (node->binary.left->kind == NODE_EXPR_VAR)
            {
                if (writes->count < 32)
                {
                    writes->syms[writes->count++] = node->binary.left->var_ref.symbol;
                }
            }
            collect_symbols(node->binary.left, reads, writes); // In case of complex LHS
            collect_symbols(node->binary.right, reads, writes);
        }
        else
        {
            collect_symbols(node->binary.left, reads, writes);
            collect_symbols(node->binary.right, reads, writes);
        }
        break;

    case NODE_EXPR_UNARY:
        if (node->unary.op &&
            (strcmp(node->unary.op, "++") == 0 || strcmp(node->unary.op, "--") == 0 ||
             strcmp(node->unary.op, "_post++") == 0 || strcmp(node->unary.op, "_post--") == 0))
        {
            if (node->unary.operand->kind == NODE_EXPR_VAR)
            {
                if (writes->count < 32)
                {
                    writes->syms[writes->count++] = node->unary.operand->var_ref.symbol;
                }
            }
        }
        collect_symbols(node->unary.operand, reads, writes);
        break;

    case NODE_EXPR_VAR:
        if (reads->count < 32)
        {
            reads->syms[reads->count++] = node->var_ref.symbol;
        }
        break;

    case NODE_EXPR_CALL:
        collect_symbols(node->call.callee, reads, writes);
        ASTNode *arg = node->call.args;
        while (arg)
        {
            collect_symbols(arg, reads, writes);
            arg = arg->next;
        }
        break;

    default:
        // Generic traversal? For now just handle these.
        break;
    }
}
void check_side_effect_collision(TypeChecker *tc, ASTNode *left, ASTNode *right, Token token)
{
    CompilerConfig *cfg = &tc->pctx->compiler->config;
    if (!cfg->misra_mode || !left || !right)
    {
        return;
    }

    SymbolSet l_reads = {0}, l_writes = {0};
    SymbolSet r_reads = {0}, r_writes = {0};

    collect_symbols(left, &l_reads, &l_writes);
    collect_symbols(right, &r_reads, &r_writes);

    // Rule 13.2: Modification collision
    for (int i = 0; i < l_writes.count; i++)
    {
        ZenSymbol *s = l_writes.syms[i];
        if (!s)
        {
            continue;
        }

        // Check against other writes
        for (int j = 0; j < r_writes.count; j++)
        {
            if (s == r_writes.syms[j])
            {
                tc_error(tc, token, "MISRA Rule 13.2: symbol modified in multiple sub-expressions");
                return;
            }
        }

        // Check against other reads
        for (int j = 0; j < r_reads.count; j++)
        {
            if (s == r_reads.syms[j])
            {
                tc_error(tc, token,
                         "MISRA Rule 13.2: symbol both read and modified in same expression");
                return;
            }
        }
    }

    // Vice versa for r_writes against l_reads
    for (int i = 0; i < r_writes.count; i++)
    {
        ZenSymbol *s = r_writes.syms[i];
        if (!s)
        {
            continue;
        }

        for (int j = 0; j < l_reads.count; j++)
        {
            if (s == l_reads.syms[j])
            {
                tc_error(tc, token,
                         "MISRA Rule 13.2: symbol both read and modified in same expression");
                return;
            }
        }
    }
}

void check_all_args_side_effects(TypeChecker *tc, ASTNode *receiver, ASTNode *args, Token token)
{
    CompilerConfig *cfg = &tc->pctx->compiler->config;
    if (!cfg->misra_mode)
    {
        return;
    }

    SymbolSet reads = {0}, writes = {0};

    if (receiver)
    {
        collect_symbols(receiver, &reads, &writes);
    }

    ASTNode *arg = args;
    while (arg)
    {
        SymbolSet next_reads = {0}, next_writes = {0};
        collect_symbols(arg, &next_reads, &next_writes);

        // Check against cumulative sets
        for (int i = 0; i < next_writes.count; i++)
        {
            ZenSymbol *s = next_writes.syms[i];
            for (int r = 0; r < reads.count; r++)
            {
                if (s == reads.syms[r])
                {
                    tc_error(tc, token, "MISRA Rule 13.2: argument read and modified in same call");
                    return;
                }
            }
            for (int w = 0; w < writes.count; w++)
            {
                if (s == writes.syms[w])
                {
                    tc_error(tc, token, "MISRA Rule 13.2: symbol modified in multiple arguments");
                    return;
                }
            }
        }
        for (int i = 0; i < next_reads.count; i++)
        {
            ZenSymbol *s = next_reads.syms[i];
            for (int w = 0; w < writes.count; w++)
            {
                if (s == writes.syms[w])
                {
                    tc_error(tc, token, "MISRA Rule 13.2: argument read and modified in same call");
                    return;
                }
            }
        }

        // Add to cumulative sets
        for (int i = 0; i < next_reads.count && reads.count < 32; i++)
        {
            reads.syms[reads.count++] = next_reads.syms[i];
        }
        for (int i = 0; i < next_writes.count && writes.count < 32; i++)
        {
            writes.syms[writes.count++] = next_writes.syms[i];
        }

        arg = arg->next;
    }
}

// Internal MISRA helpers moved to platform/misra.c

void tc_error(TypeChecker *tc, Token t, const char *msg)
{
    if (tc->move_checks_only)
    {
        return;
    }
    zerror_at(t, "%s", msg);
}

int is_expression_invariant(TypeChecker *tc, ASTNode *node, int *val)
{
    if (!node)
    {
        return 0;
    }
    long long out;
    if (eval_const_int_expr(node, tc->pctx, &out))
    {
        if (val)
        {
            *val = (int)out;
        }
        return 1;
    }
    return 0;
}

// Global recursion guard

void tc_error_with_hints(TypeChecker *tc, Token t, const char *msg, const char *const *hints)
{
    if (tc->move_checks_only)
    {
        return;
    }
    zerror_with_hints(t, msg, hints);
}

void tc_ctrl_flow_error(TypeChecker *tc, Token t, const char *msg, const char *const *hints)
{
    (void)tc;
    // Control flow errors (missing return, unreachable code, etc.)
    // are reported even during move-only checking.
    zerror_with_hints(t, msg, hints);
}

void tc_move_error_with_hints(TypeChecker *tc, Token t, const char *msg, const char *const *hints)
{
    (void)tc;
    zerror_with_hints(t, msg, hints);
}

int is_char_type(Type *t)
{
    if (!t)
    {
        return 0;
    }
    Type *res = resolve_alias(t);
    return (res->kind == TYPE_CHAR || res->kind == TYPE_C_CHAR || res->kind == TYPE_C_UCHAR);
}

// tc_check_misra_10_4 moved to misra_check_binary_op_essential_types in misra.c

void tc_enter_scope(TypeChecker *tc)
{
    tc->current_depth++;
    enter_scope(tc->pctx);
}

void tc_exit_scope(TypeChecker *tc)
{
    if (tc->current_depth > 0)
    {
        tc->current_depth--;
    }
    exit_scope(tc->pctx);
}

void tc_add_symbol(TypeChecker *tc, const char *name, Type *type, Token t, int is_immutable)
{
    CompilerConfig *cfg = &tc->pctx->compiler->config;
    if (cfg->misra_mode)
    {
        misra_check_shadowing(tc->pctx, name, t);
        misra_check_typographic_ambiguity(tc->pctx, name, t);
    }
    add_symbol_with_token(tc->pctx, name, NULL, type, t, 0);
    ZenSymbol *sym = symbol_lookup(tc->pctx->current_scope, name);
    if (sym)
    {
        sym->is_immutable = is_immutable;
        sym->scope_depth = tc->current_depth;
    }
}

ZenSymbol *tc_lookup(TypeChecker *tc, const char *name)
{
    ZenSymbol *sym = symbol_lookup(tc->pctx->current_scope, name);
    if (sym)
    {
        sym->is_used = 1;
    }
    return sym;
}

void mark_type_as_used(TypeChecker *tc, Type *t)
{
    if (!t)
    {
        return;
    }

    // Unroll pointers, arrays, vectors
    Type *curr = t;
    while (curr &&
           (curr->kind == TYPE_POINTER || curr->kind == TYPE_ARRAY || curr->kind == TYPE_VECTOR))
    {
        curr = curr->inner;
    }

    if (!curr)
    {
        return;
    }

    if (curr->kind == TYPE_STRUCT || curr->kind == TYPE_ENUM)
    {
        if (curr->name)
        {
            ZenSymbol *sym =
                symbol_lookup_kind(tc->pctx->global_scope, curr->name,
                                   (curr->kind == TYPE_STRUCT) ? SYM_STRUCT : SYM_ENUM);
            if (sym)
            {
                sym->is_used = 1;
            }
        }
    }
    else if (curr->kind == TYPE_ALIAS)
    {
        if (curr->name)
        {
            ZenSymbol *sym = symbol_lookup_kind(tc->pctx->global_scope, curr->name, SYM_ALIAS);
            if (sym)
            {
                sym->is_used = 1;
            }
        }
    }

    // Generic arguments
    for (int i = 0; curr->args && i < curr->count; i++)
    {
        mark_type_as_used(tc, curr->args[i]);
    }

    // Function type return and args
    if (curr->kind == TYPE_FUNCTION)
    {
        mark_type_as_used(tc, curr->inner);
        for (int i = 0; curr->args && i < curr->count; i++)
        {
            mark_type_as_used(tc, curr->args[i]);
        }
    }
}

// Internal MISRA helpers moved to platform/misra.c

int get_asm_register_size(Type *t)
{
    if (!t)
    {
        return 0;
    }
    if (t->kind == TYPE_F64 || t->kind == TYPE_I64 || t->kind == TYPE_U64 ||
        (t->kind == TYPE_STRUCT && t->name &&
         (0 == strcmp(t->name, "int64_t") || 0 == strcmp(t->name, "uint64_t"))))
    {
        return 64;
    }
    if (t->kind == TYPE_I128 || t->kind == TYPE_U128)
    {
        return 128;
    }
    return 32;
}

int integer_type_width(Type *t)
{
    if (!t)
    {
        return 0;
    }
    switch (t->kind)
    {
    case TYPE_I8:
    case TYPE_U8:
    case TYPE_BYTE:
    case TYPE_C_CHAR:
    case TYPE_C_UCHAR:
        return 8;
    case TYPE_I16:
    case TYPE_U16:
    case TYPE_C_SHORT:
    case TYPE_C_USHORT:
        return 16;
    case TYPE_I32:
    case TYPE_U32:
    case TYPE_INT:
    case TYPE_UINT:
    case TYPE_RUNE:
    case TYPE_C_INT:
    case TYPE_C_UINT:
        return 32;
    case TYPE_I64:
    case TYPE_U64:
    case TYPE_ISIZE:
    case TYPE_USIZE:
    case TYPE_C_LONG:
    case TYPE_C_ULONG:
    case TYPE_C_LONGLONG:
    case TYPE_C_ULONGLONG:
        return 64;
    case TYPE_I128:
    case TYPE_U128:
        return 128;
    default:
        return 0;
    }
}

// EXPRESSION CHECKERS
