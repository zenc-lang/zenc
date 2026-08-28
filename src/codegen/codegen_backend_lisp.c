// SPDX-License-Identifier: MIT
// Lisp transpiler backend - clean implementation
#include "codegen_backend.h"
#include "codegen.h"
#include "../parser/parser.h"
#include "../ast/ast.h"
#include <string.h>
#include <ctype.h>

// Buffer and stack size constants
#define LISP_NAME_BUF 256
#define LISP_FNAME_BUF 256
#define LISP_ZBUF_SIZE 1024
#define LISP_VARNAME_BUF 64
#define MAX_VAR_NAMES 128
#define MAX_VAR_NAME_LEN 64
#define WALK_STACK_SIZE 256
#define MAX_EMITTED_NAMES 512

static const char *current_func = NULL;

static void lemit_i(ParserContext *ctx, int depth)
{
    for (int i = 0; i < depth; i++)
    {
        emitter_printf(&ctx->cg.emitter, "  ");
    }
}

static void lemit_s(ParserContext *ctx, const char *s)
{
    if (!s)
    {
        emitter_printf(&ctx->cg.emitter, "nil");
        return;
    }
    emitter_printf(&ctx->cg.emitter, "\"");
    for (const char *p = s; *p; p++)
    {
        char c = *p;
        if (c == '"')
        {
            emitter_printf(&ctx->cg.emitter, "\\\"");
        }
        else if (c == '\\')
        {
            emitter_printf(&ctx->cg.emitter, "\\\\");
        }
        else if (c == '\n')
        {
            emitter_printf(&ctx->cg.emitter, "\\n");
        }
        else
        {
            emitter_printf(&ctx->cg.emitter, "%c", c);
        }
    }
    emitter_printf(&ctx->cg.emitter, "\"");
}

static const char *lop(const char *zop)
{
    if (!zop)
    {
        return "?";
    }
    if (strcmp(zop, "+") == 0)
    {
        return "+";
    }
    if (strcmp(zop, "-") == 0)
    {
        return "-";
    }
    if (strcmp(zop, "*") == 0)
    {
        return "*";
    }
    if (strcmp(zop, "/") == 0)
    {
        return "/";
    }
    if (strcmp(zop, "%") == 0)
    {
        return "mod";
    }
    if (strcmp(zop, "==") == 0)
    {
        return "=";
    }
    if (strcmp(zop, "!=") == 0)
    {
        return "/=";
    }
    if (strcmp(zop, "<") == 0)
    {
        return "<";
    }
    if (strcmp(zop, "<=") == 0)
    {
        return "<=";
    }
    if (strcmp(zop, ">") == 0)
    {
        return ">";
    }
    if (strcmp(zop, ">=") == 0)
    {
        return ">=";
    }
    if (strcmp(zop, "&&") == 0)
    {
        return "and";
    }
    if (strcmp(zop, "||") == 0)
    {
        return "or";
    }
    if (strcmp(zop, "&") == 0)
    {
        return "logand";
    }
    if (strcmp(zop, "|") == 0)
    {
        return "logior";
    }
    if (strcmp(zop, "^") == 0)
    {
        return "logxor";
    }
    if (strcmp(zop, "<<") == 0)
    {
        return "ash";
    }
    if (strcmp(zop, ">>") == 0)
    {
        return "ash";
    }
    if (strcmp(zop, "~") == 0)
    {
        return "lognot";
    }
    return zop;
}

// Forward declarations
static void lemit_expr(ParserContext *ctx, ASTNode *node, int depth);
static void lemit_stmt(ParserContext *ctx, ASTNode *node, int depth, int *first);
static void lemit_stmts(ParserContext *ctx, ASTNode *stmts, int depth);
static void lemit_func(ParserContext *ctx, ASTNode *node, int depth, int *first);

static void lisp_get_struct_field_info(ParserContext *ctx, const char *type_str,
                                       const char *field_name, int *out_idx)
{
    *out_idx = -1;
    if (!type_str || !field_name)
    {
        return;
    }
    const char *sname = type_str;
    if (strncmp(sname, "struct ", 7) == 0)
    {
        sname += 7;
    }
    char *star = (char *)strchr(sname, '*');
    if (star)
    {
        *star = '\0';
    }
    StructRef *sr = ctx->parsed_structs_list;
    while (sr)
    {
        if (sr->node && sr->node->kind == NODE_STRUCT && sr->node->strct.name &&
            strcmp(sr->node->strct.name, sname) == 0)
        {
            int idx = 0;
            for (ASTNode *fd = sr->node->strct.fields; fd; fd = fd->next)
            {
                if (fd->kind == NODE_FIELD && fd->var_decl.name &&
                    strcmp(fd->var_decl.name, field_name) == 0)
                {
                    *out_idx = idx;
                    return;
                }
                idx++;
            }
            return;
        }
        sr = sr->next;
    }
    // Also search instantiated structs
    for (ASTNode *s = ctx->instantiated_structs; s; s = s->next)
    {
        if (s->kind == NODE_STRUCT && s->strct.name && strcmp(s->strct.name, sname) == 0)
        {
            int idx = 0;
            for (ASTNode *fd = s->strct.fields; fd; fd = fd->next)
            {
                if (fd->kind == NODE_FIELD && fd->var_decl.name &&
                    strcmp(fd->var_decl.name, field_name) == 0)
                {
                    *out_idx = idx;
                    return;
                }
                idx++;
            }
            return;
        }
    }
}

// Simplified: use positional list + nth (no defclass needed)
// This avoids CLOS dependency and keeps things simple.

static void lemit_expr(ParserContext *ctx, ASTNode *node, int depth)
{
    if (!node)
    {
        emitter_printf(&ctx->cg.emitter, "nil");
        return;
    }
    switch (node->kind)
    {
    case NODE_EXPR_LITERAL:
        switch (node->literal.kind)
        {
        case LITERAL_INT:
            emitter_printf(&ctx->cg.emitter, "%llu", node->literal.int_val);
            break;
        case LITERAL_FLOAT:
            emitter_printf(&ctx->cg.emitter, "%g", node->literal.float_val);
            break;
        case LITERAL_STRING:
        case LITERAL_RAW_STRING:
            lemit_s(ctx, node->literal.string_val);
            break;
        case LITERAL_CHAR:
            emitter_printf(&ctx->cg.emitter, "#\\%c", (char)node->literal.int_val);
            break;
        default:
            break;
        }
        break;

    case NODE_EXPR_VAR:
        if (node->var_ref.name)
        {
            if (strcmp(node->var_ref.name, "true") == 0)
            {
                emitter_printf(&ctx->cg.emitter, "t");
            }
            else if (strcmp(node->var_ref.name, "false") == 0 ||
                     strcmp(node->var_ref.name, "NULL") == 0 ||
                     strcmp(node->var_ref.name, "nullptr") == 0)
            {
                emitter_printf(&ctx->cg.emitter, "nil");
            }
            else
            {
                emitter_printf(&ctx->cg.emitter, "%s", node->var_ref.name);
            }
        }
        else
        {
            emitter_printf(&ctx->cg.emitter, "?var");
        }
        break;

    case NODE_EXPR_BINARY:
    {
        const char *op = lop(node->binary.op);
        if (strcmp(node->binary.op, "=") == 0 && node->binary.left && node->binary.right)
        {
            // Assignment
            emitter_printf(&ctx->cg.emitter, "(setf ");
            lemit_expr(ctx, node->binary.left, depth);
            emitter_printf(&ctx->cg.emitter, " ");
            lemit_expr(ctx, node->binary.right, depth);
            emitter_printf(&ctx->cg.emitter, ")");
        }
        else if (strcmp(op, "..") == 0)
        {
            emitter_printf(&ctx->cg.emitter, "(cons ");
            lemit_expr(ctx, node->binary.left, depth);
            emitter_printf(&ctx->cg.emitter, " ");
            lemit_expr(ctx, node->binary.right, depth);
            emitter_printf(&ctx->cg.emitter, ")");
        }
        else
        {
            emitter_printf(&ctx->cg.emitter, "(%s ", op);
            lemit_expr(ctx, node->binary.left, depth);
            emitter_printf(&ctx->cg.emitter, " ");
            lemit_expr(ctx, node->binary.right, depth);
            emitter_printf(&ctx->cg.emitter, ")");
        }
        break;
    }

    case NODE_EXPR_UNARY:
    {
        const char *op = node->unary.op;
        if (strcmp(op, "!") == 0)
        {
            emitter_printf(&ctx->cg.emitter, "(not ");
            lemit_expr(ctx, node->unary.operand, depth);
            emitter_printf(&ctx->cg.emitter, ")");
        }
        else if (strcmp(op, "-") == 0)
        {
            emitter_printf(&ctx->cg.emitter, "(- ");
            lemit_expr(ctx, node->unary.operand, depth);
            emitter_printf(&ctx->cg.emitter, ")");
        }
        else if (strcmp(op, "*") == 0)
        {
            emitter_printf(&ctx->cg.emitter, "(_z_deref ");
            lemit_expr(ctx, node->unary.operand, depth);
            emitter_printf(&ctx->cg.emitter, ")");
        }
        else if (strcmp(op, "&") == 0)
        {
            emitter_printf(&ctx->cg.emitter, "(_z_ref ");
            lemit_expr(ctx, node->unary.operand, depth);
            emitter_printf(&ctx->cg.emitter, ")");
        }
        else if (strcmp(op, "++") == 0 || strcmp(op, "post++") == 0 || strcmp(op, "_post++") == 0)
        {
            emitter_printf(&ctx->cg.emitter, "(setf ");
            lemit_expr(ctx, node->unary.operand, depth);
            emitter_printf(&ctx->cg.emitter, " (+ ");
            lemit_expr(ctx, node->unary.operand, depth);
            emitter_printf(&ctx->cg.emitter, " 1))");
        }
        else if (strcmp(op, "--") == 0 || strcmp(op, "post--") == 0 || strcmp(op, "_post--") == 0)
        {
            emitter_printf(&ctx->cg.emitter, "(setf ");
            lemit_expr(ctx, node->unary.operand, depth);
            emitter_printf(&ctx->cg.emitter, " (- ");
            lemit_expr(ctx, node->unary.operand, depth);
            emitter_printf(&ctx->cg.emitter, " 1))");
        }
        else if (strcmp(op, "~") == 0)
        {
            emitter_printf(&ctx->cg.emitter, "(lognot ");
            lemit_expr(ctx, node->unary.operand, depth);
            emitter_printf(&ctx->cg.emitter, ")");
        }
        else
        {
            emitter_printf(&ctx->cg.emitter, "(%s ", op);
            lemit_expr(ctx, node->unary.operand, depth);
            emitter_printf(&ctx->cg.emitter, ")");
        }
        break;
    }

    case NODE_EXPR_CALL:
    {
        // Emit call — use short name with _z_ prefix for monomorphized methods
        ASTNode *callee = node->call.callee;
        const char *fname = NULL;
        int is_member_call = 0;
        if (callee && callee->kind == NODE_EXPR_MEMBER && callee->member.field)
        {
            fname = callee->member.field;
            is_member_call = 1;
        }
        else if (callee && callee->kind == NODE_EXPR_VAR && callee->var_ref.name)
        {
            fname = callee->var_ref.name;
        }
        if (!fname)
        {
            fname = "?call";
        }

        // For ref/deref/unwrap/is_none stubs
        if (strcmp(fname, "ref") == 0)
        {
            fname = "_z_ref";
        }
        else if (strcmp(fname, "deref") == 0)
        {
            fname = "_z_deref";
        }
        else if (strcmp(fname, "unwrap") == 0)
        {
            fname = "_z_unwrap";
        }
        else if (strcmp(fname, "is_none") == 0)
        {
            fname = "_z_is_none";
        }
        else if (strcmp(fname, "is_some") == 0)
        {
            fname = "_z_is_some";
        }
        else if (strcmp(fname, "strcmp") == 0)
        {
            fname = "string=";
        }
        else if (strcmp(fname, "exit") == 0)
        {
            fname = "_z_exit";
        }
        else if (strcmp(fname, "memset") == 0)
        {
            fname = "_z_memset";
        }
        else if (strcmp(fname, "memcpy") == 0)
        {
            fname = "_z_memcpy";
        }
        else if (strcmp(fname, "memcmp") == 0)
        {
            fname = "_z_memcmp";
        }
        else if (strcmp(fname, "memmove") == 0)
        {
            fname = "_z_memmove";
        }
        else if (strcmp(fname, "fill") == 0)
        {
            fname = "_z_fill";
        }
        else if (strcmp(fname, "replace") == 0)
        {
            fname = "_z_replace";
        }
        else if (is_member_call)
        {
            // Use _z_ prefix for member method calls
            static char zname[LISP_ZBUF_SIZE];
            snprintf(zname, sizeof(zname), "_z_%s", fname);
            fname = zname;
        }
        // Strip mangled prefix for VAR calls with __ in original name
        if (!is_member_call && callee && callee->kind == NODE_EXPR_VAR && callee->var_ref.name)
        {
            const char *orig = callee->var_ref.name;
            const char *last_du = NULL;
            const char *sc = orig;
            while ((sc = strstr(sc, "__")) != NULL)
            {
                last_du = sc;
                sc += 2;
            }
            if (last_du)
            {
                static char zname[LISP_ZBUF_SIZE];
                snprintf(zname, sizeof(zname), "_z_%s", last_du + 2);
                fname = zname;
            }
            else
            {
                // CL built-in name clash check (only for non-mangled names)
                static const char *cl_clash[] = {"apply",  "max",    "classify", "min",    "map",
                                                 "sort",   "find",   "count",    "remove", "import",
                                                 "export", "string", "list",     "vector", NULL};
                for (int ci = 0; cl_clash[ci]; ci++)
                {
                    if (strcmp(fname, cl_clash[ci]) == 0)
                    {
                        static char zbuf[64];
                        zbuf[0] = '_';
                        zbuf[1] = 'z';
                        zbuf[2] = '_';
                        strncpy(zbuf + 3, fname, sizeof(zbuf) - 4);
                        zbuf[sizeof(zbuf) - 1] = '\0';
                        fname = zbuf;
                        break;
                    }
                }
            }
        }

        // Detect lambda-in-variable calls: short names (f, f1, fn) need funcall
        int need_funcall = 0;
        if (callee && callee->kind == NODE_EXPR_VAR && fname && !is_member_call &&
            strlen(fname) <= 4 && strncmp(fname, "_z_", 3) != 0 && strcmp(fname, "nil") != 0 &&
            strcmp(fname, "t") != 0)
        {
            need_funcall = 1;
        }

        if (need_funcall)
        {
            emitter_printf(&ctx->cg.emitter, "(funcall %s", fname);
        }
        else
        {
            emitter_printf(&ctx->cg.emitter, "(%s", fname);
        }

        // For member calls, emit member target as first argument
        if (is_member_call && callee->member.target)
        {
            emitter_printf(&ctx->cg.emitter, " ");
            lemit_expr(ctx, callee->member.target, depth);
        }

        for (ASTNode *a = node->call.args; a; a = a->next)
        {
            emitter_printf(&ctx->cg.emitter, " ");
            lemit_expr(ctx, a, depth);
        }
        emitter_printf(&ctx->cg.emitter, ")");
        break;
    }

    case NODE_EXPR_MEMBER:
    {
        // Member access: self.field -> (nth idx self)
        int idx = -1;
        if (node->member.target && node->member.target->type_info)
        {
            char *tstr = type_to_string(node->member.target->type_info);
            if (tstr)
            {
                lisp_get_struct_field_info(ctx, tstr, node->member.field, &idx);
                zfree(tstr);
            }
        }
        if (idx >= 0)
        {
            emitter_printf(&ctx->cg.emitter, "(nth %d ", idx);
            lemit_expr(ctx, node->member.target, depth);
            emitter_printf(&ctx->cg.emitter, ")");
        }
        else
        {
            // Fallback: (nth 0 ...)
            emitter_printf(&ctx->cg.emitter, "(nth 0 ");
            lemit_expr(ctx, node->member.target, depth);
            emitter_printf(&ctx->cg.emitter, ")");
        }
        break;
    }

    case NODE_EXPR_INDEX:
        emitter_printf(&ctx->cg.emitter, "(aref ");
        lemit_expr(ctx, node->index.array, depth);
        emitter_printf(&ctx->cg.emitter, " ");
        lemit_expr(ctx, node->index.index, depth);
        emitter_printf(&ctx->cg.emitter, ")");
        break;

    case NODE_EXPR_CAST:
        // Cast — just emit the inner expression without type assertion
        if (node->cast.expr)
        {
            lemit_expr(ctx, node->cast.expr, depth);
        }
        else
        {
            emitter_printf(&ctx->cg.emitter, "nil");
        }
        break;

    case NODE_EXPR_SIZEOF:
        emitter_printf(&ctx->cg.emitter, "8");
        break;

    case NODE_TYPEOF:
        emitter_printf(&ctx->cg.emitter, "(type-of ");
        if (node->size_of.expr)
        {
            lemit_expr(ctx, node->size_of.expr, depth);
        }
        else
        {
            emitter_printf(&ctx->cg.emitter, "nil");
        }
        emitter_printf(&ctx->cg.emitter, ")");
        break;

    case NODE_EXPR_STRUCT_INIT:
        emitter_printf(&ctx->cg.emitter, "(list");
        for (ASTNode *f = node->struct_init.fields; f; f = f->next)
        {
            emitter_printf(&ctx->cg.emitter, " ");
            ASTNode *fval = NULL;
            if (f->kind == NODE_EXPR_BINARY && f->binary.op && strcmp(f->binary.op, "=") == 0)
            {
                fval = f->binary.right;
            }
            else if (f->kind == NODE_FIELD)
            {
                fval = f->var_decl.init_expr;
            }
            else if (f->kind == NODE_RETURN)
            {
                fval = f->ret.value;
            }
            if (fval)
            {
                lemit_expr(ctx, fval, depth);
            }
            else
            {
                // Uninitialized field — emit 0
                emitter_printf(&ctx->cg.emitter, "0");
            }
        }
        emitter_printf(&ctx->cg.emitter, ")");
        break;

    case NODE_EXPR_ARRAY_LITERAL:
        emitter_printf(&ctx->cg.emitter, "(vector");
        for (ASTNode *e = node->array_literal.elements; e; e = e->next)
        {
            emitter_printf(&ctx->cg.emitter, " ");
            lemit_expr(ctx, e, depth);
        }
        emitter_printf(&ctx->cg.emitter, ")");
        break;
    case NODE_EXPR_TUPLE_LITERAL:
        emitter_printf(&ctx->cg.emitter, "(list");
        for (ASTNode *e = node->array_literal.elements; e; e = e->next)
        {
            emitter_printf(&ctx->cg.emitter, " ");
            lemit_expr(ctx, e, depth);
        }
        emitter_printf(&ctx->cg.emitter, ")");
        break;

    case NODE_TERNARY:
        emitter_printf(&ctx->cg.emitter, "(if ");
        lemit_expr(ctx, node->ternary.cond, depth);
        emitter_printf(&ctx->cg.emitter, " ");
        lemit_expr(ctx, node->ternary.true_expr, depth);
        emitter_printf(&ctx->cg.emitter, " ");
        lemit_expr(ctx, node->ternary.false_expr, depth);
        emitter_printf(&ctx->cg.emitter, ")");
        break;

    case NODE_LAMBDA:
        emitter_printf(&ctx->cg.emitter, "(lambda (");
        for (int i = 0; i < node->lambda.count; i++)
        {
            if (i > 0)
            {
                emitter_printf(&ctx->cg.emitter, " ");
            }
            emitter_printf(&ctx->cg.emitter, "%s",
                           node->lambda.param_names && node->lambda.param_names[i]
                               ? node->lambda.param_names[i]
                               : "?");
        }
        emitter_printf(&ctx->cg.emitter, ")\n");
        lemit_i(ctx, depth + 1);
        if (node->lambda.body)
        {
            if (node->lambda.body->kind == NODE_BLOCK && node->lambda.body->block.statements)
            {
                int bf = 1;
                for (ASTNode *s = node->lambda.body->block.statements; s; s = s->next)
                {
                    if (!bf)
                    {
                        emitter_printf(&ctx->cg.emitter, "\n");
                        lemit_i(ctx, depth + 1);
                    }
                    bf = 0;
                    if (s->kind == NODE_RETURN && s->ret.value)
                    {
                        lemit_expr(ctx, s->ret.value, depth + 1);
                    }
                    else
                    {
                        int sf = 0;
                        lemit_stmt(ctx, s, depth + 1, &sf);
                    }
                }
            }
            else
            {
                int bf = 0;
                lemit_stmt(ctx, node->lambda.body, depth + 1, &bf);
            }
        }
        emitter_printf(&ctx->cg.emitter, ")");
        break;

    case NODE_EXPR_SLICE:
        emitter_printf(&ctx->cg.emitter, "nil");
        break;

    default:
        emitter_printf(&ctx->cg.emitter, "nil");
        break;
    }
}

static void lemit_stmt(ParserContext *ctx, ASTNode *node, int depth, int *first)
{
    if (!node)
    {
        return;
    }
    if (!*first)
    {
        emitter_printf(&ctx->cg.emitter, "\n");
        lemit_i(ctx, depth);
    }
    *first = 0;

    switch (node->kind)
    {
    case NODE_BLOCK:
        lemit_stmts(ctx, node->block.statements, depth);
        break;

    case NODE_VAR_DECL:
    {
        const char *vname = node->var_decl.name ? node->var_decl.name : "?";
        if (node->var_decl.init_expr)
        {
            emitter_printf(&ctx->cg.emitter, "(setf %s ", vname);
            lemit_expr(ctx, node->var_decl.init_expr, depth);
            emitter_printf(&ctx->cg.emitter, ")");
        }
        break;
    }

    case NODE_EXPR_BINARY:
    case NODE_EXPR_UNARY:
    case NODE_EXPR_LITERAL:
    case NODE_EXPR_VAR:
    case NODE_EXPR_CALL:
    case NODE_EXPR_MEMBER:
    case NODE_EXPR_INDEX:
    case NODE_EXPR_CAST:
    case NODE_TYPEOF:
    case NODE_EXPR_TUPLE_LITERAL:
    case NODE_EXPR_ARRAY_LITERAL:
    case NODE_EXPR_SLICE:
    case NODE_EXPR_SIZEOF:
        lemit_expr(ctx, node, depth);
        break;

    case NODE_RETURN:
    {
        const char *fn = current_func ? current_func : "main";
        emitter_printf(&ctx->cg.emitter, "(return-from %s", fn);
        if (node->ret.value)
        {
            emitter_printf(&ctx->cg.emitter, " ");
            lemit_expr(ctx, node->ret.value, depth);
        }
        emitter_printf(&ctx->cg.emitter, ")");
        break;
    }

    case NODE_IF:
        emitter_printf(&ctx->cg.emitter, "(if ");
        lemit_expr(ctx, node->if_stmt.condition, depth);
        emitter_printf(&ctx->cg.emitter, "\n");
        lemit_i(ctx, depth + 1);
        {
            int bf = 1;
            lemit_stmt(ctx, node->if_stmt.then_body, depth + 1, &bf);
        }
        if (node->if_stmt.else_body)
        {
            emitter_printf(&ctx->cg.emitter, "\n");
            lemit_i(ctx, depth + 1);
            int bf = 1;
            lemit_stmt(ctx, node->if_stmt.else_body, depth + 1, &bf);
        }
        emitter_printf(&ctx->cg.emitter, ")");
        break;

    case NODE_WHILE:
        emitter_printf(&ctx->cg.emitter, "(loop while ");
        lemit_expr(ctx, node->while_stmt.condition, depth);
        emitter_printf(&ctx->cg.emitter, " do\n");
        lemit_i(ctx, depth + 1);
        {
            int bf = 1;
            lemit_stmt(ctx, node->while_stmt.body, depth + 1, &bf);
        }
        emitter_printf(&ctx->cg.emitter, ")");
        break;

    case NODE_FOR:
        emitter_printf(&ctx->cg.emitter, "(progn");
        if (node->for_stmt.init)
        {
            emitter_printf(&ctx->cg.emitter, "\n");
            lemit_i(ctx, depth + 1);
            {
                int bf = 1;
                lemit_stmt(ctx, node->for_stmt.init, depth + 1, &bf);
            }
        }
        emitter_printf(&ctx->cg.emitter, "\n");
        lemit_i(ctx, depth + 1);
        emitter_printf(&ctx->cg.emitter, "(loop while ");
        if (node->for_stmt.condition)
        {
            lemit_expr(ctx, node->for_stmt.condition, depth);
        }
        else
        {
            emitter_printf(&ctx->cg.emitter, "t");
        }
        emitter_printf(&ctx->cg.emitter, " do\n");
        lemit_i(ctx, depth + 2);
        {
            int bf = 1;
            lemit_stmt(ctx, node->for_stmt.body, depth + 2, &bf);
        }
        if (node->for_stmt.step)
        {
            emitter_printf(&ctx->cg.emitter, "\n");
            lemit_i(ctx, depth + 2);
            // Emit step: if it's =, emit setf directly
            if (node->for_stmt.step->kind == NODE_EXPR_BINARY && node->for_stmt.step->binary.op &&
                strcmp(node->for_stmt.step->binary.op, "=") == 0)
            {
                emitter_printf(&ctx->cg.emitter, "(setf ");
                lemit_expr(ctx, node->for_stmt.step->binary.left, depth);
                emitter_printf(&ctx->cg.emitter, " ");
                lemit_expr(ctx, node->for_stmt.step->binary.right, depth);
                emitter_printf(&ctx->cg.emitter, ")");
            }
            else
            {
                lemit_expr(ctx, node->for_stmt.step, depth);
            }
        }
        emitter_printf(&ctx->cg.emitter, ")");
        emitter_printf(&ctx->cg.emitter, ")");
        break;

    case NODE_FOR_RANGE:
        emitter_printf(&ctx->cg.emitter, "(loop for %s",
                       node->for_range.var_name ? node->for_range.var_name : "?");
        if (node->for_range.start)
        {
            emitter_printf(&ctx->cg.emitter, " from ");
            lemit_expr(ctx, node->for_range.start, depth);
        }
        if (node->for_range.end)
        {
            emitter_printf(&ctx->cg.emitter, " %s ", node->for_range.is_inclusive ? "to" : "below");
            lemit_expr(ctx, node->for_range.end, depth);
        }
        if (node->for_range.step)
        {
            emitter_printf(&ctx->cg.emitter, " by %s", node->for_range.step);
        }
        emitter_printf(&ctx->cg.emitter, " do\n");
        lemit_i(ctx, depth + 1);
        {
            int bf = 1;
            lemit_stmt(ctx, node->for_range.body, depth + 1, &bf);
        }
        emitter_printf(&ctx->cg.emitter, ")");
        break;

    case NODE_LOOP:
        emitter_printf(&ctx->cg.emitter, "(loop do\n");
        lemit_i(ctx, depth + 1);
        {
            int bf = 1;
            lemit_stmt(ctx, node->loop_stmt.body, depth + 1, &bf);
        }
        emitter_printf(&ctx->cg.emitter, ")");
        break;

    case NODE_BREAK:
        emitter_printf(&ctx->cg.emitter, "(return)");
        break;

    case NODE_MATCH:
    {
        emitter_printf(&ctx->cg.emitter, "(cond");
        const char *ev = "__mv";
        int need_var = !(node->match_stmt.expr && node->match_stmt.expr->kind == NODE_EXPR_VAR);
        if (!need_var)
        {
            ev = node->match_stmt.expr->var_ref.name ? node->match_stmt.expr->var_ref.name : "__mv";
        }
        else
        {
            emitter_printf(&ctx->cg.emitter, "\n");
            lemit_i(ctx, depth + 1);
            emitter_printf(&ctx->cg.emitter, "((let ((%s ", ev);
            lemit_expr(ctx, node->match_stmt.expr, depth);
            emitter_printf(&ctx->cg.emitter, "))");
        }
        for (ASTNode *c = node->match_stmt.cases; c; c = c->next)
        {
            emitter_printf(&ctx->cg.emitter, "\n");
            lemit_i(ctx, depth + 1);
            emitter_printf(&ctx->cg.emitter, "(");
            if (c->match_case.is_default ||
                (c->match_case.pattern && strcmp(c->match_case.pattern, "_") == 0))
            {
                emitter_printf(&ctx->cg.emitter, "t");
            }
            else if (c->match_case.pattern)
            {
                emitter_printf(&ctx->cg.emitter, "(= %s %s)", ev, c->match_case.pattern);
            }
            else
            {
                emitter_printf(&ctx->cg.emitter, "t");
            }
            emitter_printf(&ctx->cg.emitter, "\n");
            lemit_i(ctx, depth + 2);
            {
                int bf = 1;
                lemit_stmt(ctx, c->match_case.body, depth + 2, &bf);
            }
            emitter_printf(&ctx->cg.emitter, ")");
        }
        if (need_var)
        {
            emitter_printf(&ctx->cg.emitter, "))");
        }
        emitter_printf(&ctx->cg.emitter, ")");
        break;
    }

    case NODE_RAW_STMT:
        if (node->raw_stmt.content)
        {
            // Try to extract print content from fprintf patterns
            const char *rp = node->raw_stmt.content;
            while (*rp && strchr(" ({", *rp))
            {
                rp++;
            }
            if (strncmp(rp, "fprintf(stdout,", 15) == 0)
            {
                // Parse fprintf calls: emit princ for strings, terpri for newlines
                int emitted_any = 0;
                while (*rp)
                {
                    while (*rp && strchr(" \t\n;", *rp))
                    {
                        rp++;
                    }
                    if (!*rp || *rp == '0' || *rp == '}')
                    {
                        break;
                    }
                    if (strncmp(rp, "fprintf(stdout,", 15) != 0)
                    {
                        rp++;
                        continue;
                    }
                    rp += 15;
                    while (*rp && strchr(" \t\n,", *rp))
                    {
                        rp++;
                    }
                    if (!*rp)
                    {
                        break;
                    }
                    // Check what's being printed: "%s","literal" or _z_str(expr) or "\n"
                    if (*rp == '"')
                    {
                        rp++; // skip opening quote
                        if (*rp == '\\' && *(rp + 1) == 'n')
                        {
                            // "\n" → terpri
                            if (emitted_any)
                            {
                                emitter_printf(&ctx->cg.emitter, "\n");
                                lemit_i(ctx, depth + 1);
                            }
                            emitter_printf(&ctx->cg.emitter, "(terpri)");
                            emitted_any = 1;
                            rp += 2;
                            if (*rp == '"')
                            {
                                rp++;
                            }
                        }
                        else
                        {
                            // "%s" format string — skip it to get to the value
                            while (*rp && *rp != '"')
                            {
                                rp++;
                            }
                            if (*rp == '"')
                            {
                                rp++;
                            }
                            while (*rp && strchr(" \t\n,", *rp))
                            {
                                rp++;
                            }
                            // Quoted literal string
                            if (*rp == '"')
                            {
                                rp++;
                                if (emitted_any)
                                {
                                    emitter_printf(&ctx->cg.emitter, "\n");
                                    lemit_i(ctx, depth + 1);
                                }
                                emitter_printf(&ctx->cg.emitter, "(princ \"");
                                while (*rp && *rp != '"')
                                {
                                    if (*rp == '\\' && *(rp + 1) == 'n')
                                    {
                                        emitter_printf(&ctx->cg.emitter, "~%%");
                                        rp += 2;
                                    }
                                    else
                                    {
                                        emitter_printf(&ctx->cg.emitter, "%c", *rp);
                                        rp++;
                                    }
                                }
                                emitter_printf(&ctx->cg.emitter, "\")");
                                emitted_any = 1;
                                if (*rp == '"')
                                {
                                    rp++;
                                }
                            }
                        }
                    }
                    else if (strncmp(rp, "_z_str(", 7) == 0)
                    {
                        // _z_str(expr) — emit (princ expr)
                        rp += 7;
                        char varname[LISP_VARNAME_BUF];
                        int vi = 0;
                        while (*rp && *rp != ')' && *rp != ',' && vi < LISP_VARNAME_BUF - 1)
                        {
                            varname[vi++] = *rp++;
                        }
                        varname[vi] = '\0';
                        if (*rp == ')')
                        {
                            rp++;
                        }
                        if (emitted_any)
                        {
                            emitter_printf(&ctx->cg.emitter, "\n");
                            lemit_i(ctx, depth + 1);
                        }
                        emitter_printf(&ctx->cg.emitter, "(princ %s)", varname);
                        emitted_any = 1;
                        while (*rp && *rp != ';' && *rp != ')')
                        {
                            rp++;
                        }
                    }
                    // Skip to the next fprintf or end
                    while (*rp && *rp != ';' && *rp != '}')
                    {
                        rp++;
                    }
                }
                if (!emitted_any)
                {
                    emitter_printf(&ctx->cg.emitter, "(princ \"<?>\")");
                }
            }
            else
            {
                emitter_printf(&ctx->cg.emitter, "; raw C: %s", node->raw_stmt.content);
            }
        }
        break;

    case NODE_ASSERT:
    {
        const char *fn = current_func ? current_func : "main";
        emitter_printf(&ctx->cg.emitter, "(unless ");
        if (node->assert_stmt.condition)
        {
            lemit_expr(ctx, node->assert_stmt.condition, depth);
        }
        else
        {
            emitter_printf(&ctx->cg.emitter, "nil");
        }
        emitter_printf(&ctx->cg.emitter, " (return-from %s 1))", fn);
        break;
    }

    case NODE_TEST:
    {
        if (node->test_stmt.body)
        {
            int bf = 1;
            lemit_stmt(ctx, node->test_stmt.body, depth, &bf);
        }
        break;
    }
    case NODE_EXPECT:
        // Non-fatal assert — just evaluate condition
        if (node->assert_stmt.condition)
        {
            lemit_expr(ctx, node->assert_stmt.condition, depth);
        }
        break;
    case NODE_DESTRUCT_VAR:
        break;
    case NODE_DEFER:
        break;
    case NODE_INCLUDE:
    case NODE_IMPORT:
        break;

    default:
        emitter_printf(&ctx->cg.emitter, "; unhandled stmt %d", (int)node->kind);
        break;
    }
}

static void lemit_stmts(ParserContext *ctx, ASTNode *stmts, int depth)
{
    if (!stmts)
    {
        return;
    }
    int cnt = 0;
    for (ASTNode *s = stmts; s; s = s->next)
    {
        cnt++;
    }
    if (cnt > 1)
    {
        emitter_printf(&ctx->cg.emitter, "(progn\n");
        int first = 1;
        for (ASTNode *s = stmts; s; s = s->next)
        {
            if (!first)
            {
                emitter_printf(&ctx->cg.emitter, "\n");
                lemit_i(ctx, depth + 1);
            }
            first = 0;
            lemit_stmt(ctx, s, depth + 1, &first);
        }
        emitter_printf(&ctx->cg.emitter, ")");
    }
    else
    {
        int first = 1;
        lemit_stmt(ctx, stmts, depth + 1, &first);
    }
}

static void lemit_func(ParserContext *ctx, ASTNode *node, int depth, int *first)
{
    if (!*first)
    {
        emitter_printf(&ctx->cg.emitter, "\n\n");
        lemit_i(ctx, depth);
    }
    *first = 0;

    const char *raw_n = node->func.name ? node->func.name : "anon";
    static char fname_buf[LISP_FNAME_BUF];
    // Strip mangled prefix: Vec__int32_t__push -> push, then add _z_ prefix
    const char *last_du = NULL;
    const char *scan = raw_n;
    while ((scan = strstr(scan, "__")) != NULL)
    {
        last_du = scan;
        scan += 2;
    }
    if (last_du)
    {
        snprintf(fname_buf, sizeof(fname_buf), "_z_%s", last_du + 2);
    }
    else
    {
        snprintf(fname_buf, sizeof(fname_buf), "%s", raw_n);
    }
    const char *fname = fname_buf;
    // Prefix user functions that clash with CL built-ins
    if (!last_du)
    {
        static const char *cl_clash[] = {"apply",  "max",    "classify", "min",    "map",
                                         "sort",   "find",   "count",    "remove", "import",
                                         "export", "string", "list",     "vector", NULL};
        for (int ci = 0; cl_clash[ci]; ci++)
        {
            if (strcmp(fname, cl_clash[ci]) == 0)
            {
                static char zbuf[64];
                zbuf[0] = '_';
                zbuf[1] = 'z';
                zbuf[2] = '_';
                strncpy(zbuf + 3, fname, sizeof(zbuf) - 4);
                zbuf[sizeof(zbuf) - 1] = '\0';
                fname = zbuf;
                break;
            }
        }
    }
    // Suppress monomorphized functions that are provided by native Lisp modules
    {
        static const char *native_names[] = {
            "_z_new",      "_z_with_capacity", "_z_push",     "_z_pop",     "_z_at",
            "_z_set",      "_z_len",           "_z_is_empty", "_z_clear",   "_z_cap",
            "_z_iter_ref", "_z_next",          "_z_is_none",  "_z_is_some", "_z_unwrap",
            "_z_contains", "_z_reverse",       "_z_free",     "_z_int32_t", "_z_uint8_t",
            "_z_size_t",   "_z_alloc_bytes",   "_z_drop",     "_z_shr",     "_z_none",
            "_z_some",     "_z_None",          "_z_Some",     NULL};
        for (int ni = 0; native_names[ni]; ni++)
        {
            if (strcmp(fname, native_names[ni]) == 0)
            {
                emitter_printf(&ctx->cg.emitter, "; native module: %s\n", raw_n);
                return;
            }
        }
    }
    current_func = fname;

    emitter_printf(&ctx->cg.emitter, "(defun %s (", fname);
    for (int i = 0; i < node->func.count; i++)
    {
        if (i > 0)
        {
            emitter_printf(&ctx->cg.emitter, " ");
        }
        emitter_printf(
            &ctx->cg.emitter, "%s",
            node->func.param_names && node->func.param_names[i] ? node->func.param_names[i] : "?");
    }
    emitter_printf(&ctx->cg.emitter, ")\n");
    lemit_i(ctx, depth + 1);

    if (node->func.body)
    {
        if (node->func.body->kind == NODE_BLOCK)
        {
            // Scan for var decls recursively (including inside for-init blocks)
            ASTNode *stmts = node->func.body->block.statements;
            char vnames[MAX_VAR_NAMES][MAX_VAR_NAME_LEN];
            int vcnt = 0;
            {
                // Stack-based traversal for VAR_DECL nodes
                ASTNode *walk_stack[WALK_STACK_SIZE];
                int walk_sp = 0;
                for (ASTNode *s = stmts; s && walk_sp < WALK_STACK_SIZE; s = s->next)
                {
                    walk_stack[walk_sp++] = s;
                }
                while (walk_sp > 0)
                {
                    ASTNode *cur = walk_stack[--walk_sp];
                    if (cur->kind == NODE_VAR_DECL && cur->var_decl.name && vcnt < MAX_VAR_NAMES)
                    {
                        int found = 0;
                        for (int i = 0; i < vcnt; i++)
                        {
                            if (strcmp(vnames[i], cur->var_decl.name) == 0)
                            {
                                found = 1;
                                break;
                            }
                        }
                        if (!found)
                        {
                            strncpy(vnames[vcnt], cur->var_decl.name, 63);
                            vnames[vcnt][63] = '\0';
                            vcnt++;
                        }
                    }
                    // Push children for nested blocks
                    if (cur->kind == NODE_BLOCK && cur->block.statements)
                    {
                        for (ASTNode *c = cur->block.statements; c && walk_sp < WALK_STACK_SIZE;
                             c = c->next)
                        {
                            walk_stack[walk_sp++] = c;
                        }
                    }
                    // Push for-loop init children
                    if (cur->kind == NODE_FOR && cur->for_stmt.init && walk_sp < WALK_STACK_SIZE)
                    {
                        walk_stack[walk_sp++] = cur->for_stmt.init;
                    }
                    if (cur->kind == NODE_FOR && cur->for_stmt.body && walk_sp < WALK_STACK_SIZE)
                    {
                        walk_stack[walk_sp++] = cur->for_stmt.body;
                    }
                    if (cur->kind == NODE_FOR_RANGE && cur->for_range.body &&
                        walk_sp < WALK_STACK_SIZE)
                    {
                        walk_stack[walk_sp++] = cur->for_range.body;
                    }
                    if (cur->kind == NODE_IF && cur->if_stmt.then_body && walk_sp < WALK_STACK_SIZE)
                    {
                        walk_stack[walk_sp++] = cur->if_stmt.then_body;
                    }
                    if (cur->kind == NODE_IF && cur->if_stmt.else_body && walk_sp < WALK_STACK_SIZE)
                    {
                        walk_stack[walk_sp++] = cur->if_stmt.else_body;
                    }
                    if (cur->kind == NODE_WHILE && cur->while_stmt.body &&
                        walk_sp < WALK_STACK_SIZE)
                    {
                        walk_stack[walk_sp++] = cur->while_stmt.body;
                    }
                    if (cur->kind == NODE_LOOP && cur->loop_stmt.body && walk_sp < WALK_STACK_SIZE)
                    {
                        walk_stack[walk_sp++] = cur->loop_stmt.body;
                    }
                    if (cur->kind == NODE_DO_WHILE && cur->do_while_stmt.body &&
                        walk_sp < WALK_STACK_SIZE)
                    {
                        walk_stack[walk_sp++] = cur->do_while_stmt.body;
                    }
                }
            }
            if (vcnt > 0)
            {
                emitter_printf(&ctx->cg.emitter, "(let (");
                for (int i = 0; i < vcnt; i++)
                {
                    if (i > 0)
                    {
                        emitter_printf(&ctx->cg.emitter, " ");
                    }
                    emitter_printf(&ctx->cg.emitter, "%s", vnames[i]);
                }
                emitter_printf(&ctx->cg.emitter, ")\n");
                lemit_i(ctx, depth + 2);
            }
            // Declare all vars as ignorable
            emitter_printf(&ctx->cg.emitter, "(declare (ignorable ");
            for (int i = 0; i < node->func.count; i++)
            {
                if (i > 0)
                {
                    emitter_printf(&ctx->cg.emitter, " ");
                }
                emitter_printf(&ctx->cg.emitter, "%s",
                               node->func.param_names ? node->func.param_names[i] : "?");
            }
            if (vcnt > 0)
            {
                for (int i = 0; i < vcnt; i++)
                {
                    emitter_printf(&ctx->cg.emitter, " %s", vnames[i]);
                }
            }
            emitter_printf(&ctx->cg.emitter, "))\n");
            lemit_i(ctx, depth + (vcnt > 0 ? 2 : 1));
            lemit_stmts(ctx, stmts, depth + (vcnt > 0 ? 2 : 1));
            if (vcnt > 0)
            {
                emitter_printf(&ctx->cg.emitter, ")");
            }
        }
        else
        {
            int bf = 1;
            lemit_stmt(ctx, node->func.body, depth + 1, &bf);
        }
    }
    emitter_printf(&ctx->cg.emitter, ")");
    current_func = NULL;
}

static void lemit_struct(ParserContext *ctx, ASTNode *node, int depth)
{
    (void)depth;
    if (!node || node->kind != NODE_STRUCT)
    {
        return;
    }
    const char *sname = node->strct.name ? node->strct.name : "unnamed";
    emitter_printf(&ctx->cg.emitter, "\n\n(defstruct %s", sname);
    for (ASTNode *f = node->strct.fields; f; f = f->next)
    {
        if (f->kind == NODE_FIELD && f->var_decl.name)
        {
            emitter_printf(&ctx->cg.emitter, "\n");
            lemit_i(ctx, 1);
            emitter_printf(&ctx->cg.emitter, "(%s nil)", f->var_decl.name);
        }
    }
    emitter_printf(&ctx->cg.emitter, ")");
}

static int lisp_func_emitted(ParserContext *ctx, const char *name, const char **emitted, int ecount)
{
    (void)ctx;
    for (int i = 0; i < ecount; i++)
    {
        if (strcmp(emitted[i], name) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static void lemit_root(ParserContext *ctx, ASTNode *root, int depth)
{
    if (!root || root->kind != NODE_ROOT)
    {
        return;
    }
    int first = 1;
    int has_main = 0;

    // Track emitted function names to avoid duplicates
    const char *emitted_names[MAX_EMITTED_NAMES];
    int ecount = 0;

    // User-defined functions from root
    for (ASTNode *c = root->root.children; c; c = c->next)
    {
        if (c->kind == NODE_FUNCTION && c->func.name && ecount < MAX_EMITTED_NAMES)
        {
            lemit_func(ctx, c, depth, &first);
            emitted_names[ecount++] = c->func.name;
            if (strcmp(c->func.name, "main") == 0)
            {
                has_main = 1;
            }
        }
        else if (c->kind == NODE_TEST && c->test_stmt.name && ecount < MAX_EMITTED_NAMES)
        {
            // Skip test blocks without fn main (test-only files are no-ops)
        }
    }

    // Functions from imported files (via IMPORT nodes)
    for (ASTNode *c = root->root.children; c; c = c->next)
    {
        if (c->kind == NODE_IMPORT && c->import_stmt.module_root)
        {
            // module_root is a linked list of parsed nodes (not wrapped in NODE_ROOT)
            int inner_first = 1;
            for (ASTNode *ic = c->import_stmt.module_root; ic; ic = ic->next)
            {
                if (ic->kind == NODE_FUNCTION && ic->func.name && ecount < MAX_EMITTED_NAMES &&
                    !lisp_func_emitted(ctx, ic->func.name, emitted_names, ecount))
                {
                    lemit_func(ctx, ic, depth, &inner_first);
                    emitted_names[ecount++] = ic->func.name;
                    if (strcmp(ic->func.name, "main") == 0)
                    {
                        has_main = 1;
                    }
                }
            }
        }
    }

    // Instantiated structs
    for (ASTNode *s = ctx->instantiated_structs; s; s = s->next)
    {
        if (s->kind == NODE_STRUCT)
        {
            lemit_struct(ctx, s, depth);
        }
    }

    // Instantiated functions (monomorphized)
    for (ASTNode *f = ctx->instantiated_funcs; f; f = f->next)
    {
        if (f->kind == NODE_FUNCTION && f->func.name && ecount < MAX_EMITTED_NAMES)
        {
            if (!lisp_func_emitted(ctx, f->func.name, emitted_names, ecount))
            {
                lemit_func(ctx, f, depth, &first);
                emitted_names[ecount++] = f->func.name;
                if (strcmp(f->func.name, "main") == 0)
                {
                    has_main = 1;
                }
            }
        }
        else if (f->kind == NODE_IMPL || f->kind == NODE_IMPL_TRAIT)
        {
            ASTNode *m = (f->kind == NODE_IMPL) ? f->impl.methods : f->impl_trait.methods;
            while (m)
            {
                if (m->kind == NODE_FUNCTION && m->func.name && ecount < MAX_EMITTED_NAMES &&
                    !lisp_func_emitted(ctx, m->func.name, emitted_names, ecount))
                {
                    lemit_func(ctx, m, depth, &first);
                    emitted_names[ecount++] = m->func.name;
                }
                m = m->next;
            }
        }
    }

    if (has_main)
    {
        emitter_printf(&ctx->cg.emitter, "\n\n(main)\n");
    }
}

// Parse a raw C print/println and emit Lisp princ/terpri
static void lemit_program(ParserContext *ctx, ASTNode *root)
{
    emitter_printf(&ctx->cg.emitter, "#!/usr/bin/env sbcl --script\n");
    // Check for stdlib imports and load native Lisp modules
    if (root && root->kind == NODE_ROOT)
    {
        for (ASTNode *c = root->root.children; c; c = c->next)
        {
            if (c->kind == NODE_IMPORT && c->import_stmt.path)
            {
                // Map Zen stdlib imports to native Lisp modules
                const char *path = c->import_stmt.path;
                const char *fname = (char *)strrchr(path, '/');
                fname = fname ? fname + 1 : path;
                if (strcmp(fname, "vec.zc") == 0)
                {
                    emitter_printf(&ctx->cg.emitter, "(load \"std-lisp/vec.lisp\")\n");
                }
                else if (strcmp(fname, "option.zc") == 0 || strcmp(fname, "option_.zc") == 0)
                {
                    emitter_printf(&ctx->cg.emitter, "(load \"std-lisp/option.lisp\")\n");
                }
            }
        }
    }
    // Runtime stubs
    emitter_printf(&ctx->cg.emitter, "(defun _z_ref (x) x)\n");
    emitter_printf(&ctx->cg.emitter, "(defun _z_deref (x) x)\n");
    emitter_printf(&ctx->cg.emitter, "(defun (setf _z_deref) (v x) v)\n");
    emitter_printf(&ctx->cg.emitter, "(defun printf (&rest args) (format t \"~{~A~^ ~}\" args))\n");
    emitter_printf(&ctx->cg.emitter, "(defun strcmp (a b) (string= a b))\n");
    emitter_printf(&ctx->cg.emitter,
                   "(defun _z_exit (&optional (code 0)) (sb-ext:exit :code code))\n");
    emitter_printf(&ctx->cg.emitter, "(defun _z_now () 0)\n");
    emitter_printf(&ctx->cg.emitter, "(defun _z_hash () 0)\n");
    emitter_printf(&ctx->cg.emitter,
                   "(defun _z_memset (ptr val n) (declare (ignore ptr val n)) nil)\n");
    emitter_printf(&ctx->cg.emitter, "(defun _z_run_tests () nil)\n");
    lemit_root(ctx, root, 0);
    emitter_printf(&ctx->cg.emitter, "\n");
}

static void lemit_preamble(ParserContext *ctx_unused)
{
    (void)ctx_unused;
}

static const BackendOptAlias lisp_aliases[] = {
    {"--backend-full-content", "full-content", NULL},
    {NULL, NULL, NULL},
};

static const CodegenBackend lisp_backend = {
    .name = "lisp",
    .extension = ".lisp",
    .emit_program = lemit_program,
    .emit_preamble = lemit_preamble,
    .needs_cc = 0,
    .aliases = lisp_aliases,
};

void codegen_register_lisp_backend(void)
{
    codegen_register_backend(&lisp_backend);
}
