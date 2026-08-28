// SPDX-License-Identifier: MIT
// AST dump backend - Unicode tree format
// Licensed under MIT.
// Repository: https://github.com/z-libs
#include "codegen_backend.h"
#include "../parser/parser.h"
#include "../ast/ast.h"

static const char *node_type_name(int type)
{
    switch (type)
    {
    case NODE_ROOT:
        return "ROOT";
    case NODE_FUNCTION:
        return "FUNCTION";
    case NODE_BLOCK:
        return "BLOCK";
    case NODE_RETURN:
        return "RETURN";
    case NODE_VAR_DECL:
        return "VAR_DECL";
    case NODE_CONST:
        return "CONST";
    case NODE_TYPE_ALIAS:
        return "TYPE_ALIAS";
    case NODE_IF:
        return "IF";
    case NODE_WHILE:
        return "WHILE";
    case NODE_FOR:
        return "FOR";
    case NODE_FOR_RANGE:
        return "FOR_RANGE";
    case NODE_LOOP:
        return "LOOP";
    case NODE_BREAK:
        return "BREAK";
    case NODE_CONTINUE:
        return "CONTINUE";
    case NODE_MATCH:
        return "MATCH";
    case NODE_MATCH_CASE:
        return "MATCH_CASE";
    case NODE_EXPR_BINARY:
        return "BINARY";
    case NODE_EXPR_UNARY:
        return "UNARY";
    case NODE_EXPR_LITERAL:
        return "LITERAL";
    case NODE_EXPR_VAR:
        return "VAR";
    case NODE_EXPR_CALL:
        return "CALL";
    case NODE_EXPR_MEMBER:
        return "MEMBER";
    case NODE_EXPR_INDEX:
        return "INDEX";
    case NODE_EXPR_CAST:
        return "CAST";
    case NODE_EXPR_SIZEOF:
        return "SIZEOF";
    case NODE_EXPR_STRUCT_INIT:
        return "STRUCT_INIT";
    case NODE_EXPR_ARRAY_LITERAL:
        return "ARRAY_LIT";
    case NODE_EXPR_TUPLE_LITERAL:
        return "TUPLE";
    case NODE_EXPR_SLICE:
        return "SLICE";
    case NODE_STRUCT:
        return "STRUCT";
    case NODE_FIELD:
        return "FIELD";
    case NODE_ENUM:
        return "ENUM";
    case NODE_ENUM_VARIANT:
        return "ENUM_VARIANT";
    case NODE_TRAIT:
        return "TRAIT";
    case NODE_IMPL:
        return "IMPL";
    case NODE_IMPL_TRAIT:
        return "IMPL_TRAIT";
    case NODE_INCLUDE:
        return "INCLUDE";
    case NODE_IMPORT:
        return "IMPORT";
    case NODE_RAW_STMT:
        return "RAW";
    case NODE_GOTO:
        return "GOTO";
    case NODE_LABEL:
        return "LABEL";
    case NODE_DEFER:
        return "DEFER";
    case NODE_TEST:
        return "TEST";
    case NODE_LAMBDA:
        return "LAMBDA";
    case NODE_TERNARY:
        return "TERNARY";
    case NODE_DO_WHILE:
        return "DO_WHILE";
    case NODE_ASM:
        return "ASM";
    case NODE_AWAIT:
        return "AWAIT";
    default:
        return "UNKNOWN";
    }
}

static int is_printable(const char *s)
{
    return s && s[0] >= 32 && s[0] < 127;
}

// Forward declaration
static void dump_node(ParserContext *ctx, ASTNode *node, int is_last, const char *prefix);

// Emit a node label: name, literals, type info, location
static void emit_label(ParserContext *ctx, ASTNode *node)
{
    // Title: short type name
    emitter_printf(&ctx->cg.emitter, " %s", node_type_name((int)(node->kind)));

    // Name or operator for relevant node types
    switch (node->kind)
    {
    case NODE_FUNCTION:
    case NODE_VAR_DECL:
    case NODE_CONST:
    case NODE_TYPE_ALIAS:
    case NODE_STRUCT:
    case NODE_ENUM:
    case NODE_TRAIT:
    case NODE_FIELD:
    case NODE_ENUM_VARIANT:
        if (node->var_decl.name)
        {
            emitter_printf(&ctx->cg.emitter, " '%s'", node->var_decl.name);
        }
        break;
    case NODE_IMPL:
        if (node->impl.struct_name)
        {
            emitter_printf(&ctx->cg.emitter, " '%s'", node->impl.struct_name);
        }
        break;
    case NODE_IMPORT:
        if (node->import_stmt.path)
        {
            const char *p = node->import_stmt.path;
            const char *last = (char *)strrchr(p, '/');
            emitter_printf(&ctx->cg.emitter, " '%s'", last ? last + 1 : p);
        }
        break;
    case NODE_INCLUDE:
        if (node->include.path)
        {
            emitter_printf(&ctx->cg.emitter, " '%s'%s", node->include.path,
                           node->include.is_system ? " (system)" : "");
        }
        break;
    case NODE_EXPR_BINARY:
        if (node->binary.op && is_printable(node->binary.op))
        {
            emitter_printf(&ctx->cg.emitter, " '%s'", node->binary.op);
        }
        break;
    case NODE_EXPR_UNARY:
        if (node->unary.op && is_printable(node->unary.op))
        {
            emitter_printf(&ctx->cg.emitter, " '%s'", node->unary.op);
        }
        break;
    case NODE_EXPR_CALL:
        if (node->call.callee && node->call.callee->kind == NODE_EXPR_VAR &&
            node->call.callee->var_ref.name)
        {
            const char *fn = node->call.callee->var_ref.name;
            // Skip mangled Vec__int32_t__new prefix, show just the method name
            const char *method = (char *)strstr(fn, "__") ? strrchr(fn, '_') + 1 : fn;
            if (method && method[0] == '_')
            {
                method = fn; // fallback
            }
            emitter_printf(&ctx->cg.emitter, " '%s'", method);
        }
        break;
    case NODE_RAW_STMT:
    {
        if (node->raw_stmt.content)
        {
            if (backend_opt(&ctx->config->backend_opts, "full-content"))
            {
                emitter_printf(&ctx->cg.emitter, " '%s'", node->raw_stmt.content);
            }
            else
            {
                char preview[44];
                size_t len = strlen(node->raw_stmt.content);
                int slen = (int)(len < 40 ? len : 40);
                snprintf(preview, sizeof(preview), "%.*s%s", slen, node->raw_stmt.content,
                         (size_t)slen < len ? "..." : "");
                emitter_printf(&ctx->cg.emitter, " '%s'", preview);
            }
        }
        break;
    }
    default:
        break;
    }

    // Literal values
    if (node->kind == NODE_EXPR_LITERAL)
    {
        switch (node->literal.kind)
        {
        case LITERAL_INT:
            emitter_printf(&ctx->cg.emitter, " %llu", node->literal.int_val);
            break;
        case LITERAL_FLOAT:
            emitter_printf(&ctx->cg.emitter, " %g", node->literal.float_val);
            break;
        case LITERAL_STRING:
        case LITERAL_RAW_STRING:
            if (node->literal.string_val)
            {
                emitter_printf(&ctx->cg.emitter, " \"%s\"", node->literal.string_val);
            }
            break;
        case LITERAL_CHAR:
            emitter_printf(&ctx->cg.emitter, " '%c'", (char)node->literal.int_val);
            break;
        default:
            break;
        }
    }

    // Type annotation
    if (node->type_info)
    {
        char *tn = type_to_string(node->type_info);
        if (tn)
        {
            emitter_printf(&ctx->cg.emitter, " :%s", tn);
            zfree(tn);
        }
    }

    if (node->token.start)
    {
        emitter_printf(&ctx->cg.emitter, " [%d:%d]", node->token.line, node->token.col);
    }
}

// Recursively dump linked-list children
static void dump_children(ParserContext *ctx, ASTNode *children, const char *parent_prefix,
                          int parent_is_last)
{
    if (!children)
    {
        return;
    }

    // Count siblings
    int total = 0;
    for (ASTNode *c = children; c; c = c->next)
    {
        total++;
    }

    // Build child prefix
    char child_prefix[512];
    snprintf(child_prefix, sizeof(child_prefix), "%s%s", parent_prefix,
             parent_is_last ? "    " : "\342\224\202   "); // │

    int idx = 0;
    for (ASTNode *c = children; c; c = c->next, idx++)
    {
        dump_node(ctx, c, (idx >= total - 1), child_prefix);
    }
}

// Dump two fixed children (left/right pair)
static void dump_node(ParserContext *ctx, ASTNode *node, int is_last, const char *prefix)
{
    if (!node)
    {
        return;
    }

    // Emit prefix + tree branch
    if (prefix && prefix[0])
    {
        emitter_printf(&ctx->cg.emitter, "%s%s", prefix,
                       is_last ? "\342\224\224\342\224\200\342\224\200 "   // └──
                               : "\342\224\234\342\224\200\342\224\200 "); // ├──
    }

    emit_label(ctx, node);
    emitter_printf(&ctx->cg.emitter, "\n");

    // Children
    switch (node->kind)
    {
    case NODE_ROOT:
        dump_children(ctx, node->root.children, prefix, is_last);
        break;
    case NODE_FUNCTION:
        if (node->func.body)
        {
            dump_node(ctx, node->func.body, 1, prefix);
        }
        break;
    case NODE_BLOCK:
        dump_children(ctx, node->block.statements, prefix, is_last);
        break;
    case NODE_IF:
    {
        ASTNode *c[3] = {node->if_stmt.condition, node->if_stmt.then_body, node->if_stmt.else_body};
        int n = 0;
        if (c[2])
        {
            n = 3;
        }
        else if (c[1])
        {
            n = 2;
        }
        else if (c[0])
        {
            n = 1;
        }
        char cp[512];
        snprintf(cp, sizeof(cp), "%s%s", prefix, is_last ? "    " : "\342\224\202   ");
        for (int i = 0; i < n; i++)
        {
            dump_node(ctx, c[i], (i == n - 1), cp);
        }
        break;
    }
    case NODE_WHILE:
        if (node->while_stmt.condition && node->while_stmt.body)
        {
            char cp[512];
            snprintf(cp, sizeof(cp), "%s%s", prefix, is_last ? "    " : "\342\224\202   ");
            dump_node(ctx, node->while_stmt.condition, 0, cp);
            dump_node(ctx, node->while_stmt.body, 1, cp);
        }
        break;
    case NODE_FOR_RANGE:
        if (node->for_range.body)
        {
            dump_node(ctx, node->for_range.body, 1, prefix);
        }
        break;
    case NODE_RETURN:
    {
        if (node->ret.value)
        {
            char cp[512];
            snprintf(cp, sizeof(cp), "%s%s", prefix, is_last ? "    " : "\342\224\202   ");
            dump_node(ctx, node->ret.value, 1, cp);
        }
        break;
    }
    case NODE_VAR_DECL:
    {
        if (node->var_decl.init_expr)
        {
            char cp[512];
            snprintf(cp, sizeof(cp), "%s%s", prefix, is_last ? "    " : "\342\224\202   ");
            dump_node(ctx, node->var_decl.init_expr, 1, cp);
        }
        break;
    }
    case NODE_EXPR_BINARY:
        if (node->binary.left || node->binary.right)
        {
            char cp[512];
            snprintf(cp, sizeof(cp), "%s%s", prefix, is_last ? "    " : "\342\224\202   ");
            if (node->binary.left)
            {
                dump_node(ctx, node->binary.left, node->binary.right ? 0 : 1, cp);
            }
            if (node->binary.right)
            {
                dump_node(ctx, node->binary.right, 1, cp);
            }
        }
        break;
    case NODE_EXPR_UNARY:
        if (node->unary.operand)
        {
            char cp[512];
            snprintf(cp, sizeof(cp), "%s%s", prefix, is_last ? "    " : "\342\224\202   ");
            dump_node(ctx, node->unary.operand, 1, cp);
        }
        break;
    case NODE_EXPR_CALL:
        if (node->call.args)
        {
            dump_children(ctx, node->call.args, prefix, is_last);
        }
        break;
    case NODE_EXPR_MEMBER:
        if (node->member.target)
        {
            dump_node(ctx, node->member.target, 1, prefix);
        }
        break;
    case NODE_EXPR_INDEX:
        if (node->index.array || node->index.index)
        {
            char cp[512];
            snprintf(cp, sizeof(cp), "%s%s", prefix, is_last ? "    " : "\342\224\202   ");
            if (node->index.array)
            {
                dump_node(ctx, node->index.array, node->index.index ? 0 : 1, cp);
            }
            if (node->index.index)
            {
                dump_node(ctx, node->index.index, 1, cp);
            }
        }
        break;
    case NODE_EXPR_CAST:
        if (node->cast.expr)
        {
            dump_node(ctx, node->cast.expr, 1, prefix);
        }
        break;
    case NODE_MATCH:
    {
        char cp[512];
        snprintf(cp, sizeof(cp), "%s%s", prefix, is_last ? "    " : "\342\224\202   ");
        if (node->match_stmt.expr)
        {
            dump_node(ctx, node->match_stmt.expr, node->match_stmt.cases ? 0 : 1, cp);
        }
        if (node->match_stmt.cases)
        {
            dump_children(ctx, node->match_stmt.cases, prefix, is_last);
        }
        break;
    }
    case NODE_STRUCT:
        dump_children(ctx, node->strct.fields, prefix, is_last);
        break;
    case NODE_ENUM:
        dump_children(ctx, node->enm.variants, prefix, is_last);
        break;
    case NODE_EXPR_STRUCT_INIT:
        dump_children(ctx, node->struct_init.fields, prefix, is_last);
        break;
    case NODE_EXPR_ARRAY_LITERAL:
        dump_children(ctx, node->array_literal.elements, prefix, is_last);
        break;
    case NODE_IMPL:
        dump_children(ctx, node->impl.methods, prefix, is_last);
        break;
    case NODE_INCLUDE:
    case NODE_RAW_STMT:
    case NODE_FIELD:
    case NODE_ENUM_VARIANT:
    case NODE_BREAK:
    case NODE_CONTINUE:
    case NODE_EXPR_LITERAL:
    case NODE_EXPR_VAR:
    case NODE_GOTO:
    case NODE_LABEL:
        break;
    default:
        break;
    }
}

static void astdump_emit_program(ParserContext *ctx, ASTNode *root)
{
    dump_node(ctx, root, 1, "");
}

static void astdump_emit_preamble(ParserContext *ctx)
{
    (void)ctx;
}

static const BackendOptAlias astdump_aliases[] = {
    {"--backend-full-content", "full-content", NULL},
    {NULL, NULL, NULL},
};

static const CodegenBackend astdump_backend = {
    .name = "ast-dump",
    .extension = ".ast",
    .emit_program = astdump_emit_program,
    .emit_preamble = astdump_emit_preamble,
    .needs_cc = 0,
    .aliases = astdump_aliases,
};

void codegen_register_astdump_backend(void)
{
    codegen_register_backend(&astdump_backend);
}
