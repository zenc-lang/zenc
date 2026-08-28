// SPDX-License-Identifier: MIT
// Graphviz DOT backend
#include "codegen_backend.h"
#include "../parser/parser.h"
#include "../ast/ast.h"
#include <string.h>

static int node_id = 0;

static const char *dot_type_name(int type)
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
        return "VAR\\nDECL";
    case NODE_CONST:
        return "CONST";
    case NODE_TYPE_ALIAS:
        return "TYPE\\nALIAS";
    case NODE_IF:
        return "IF";
    case NODE_WHILE:
        return "WHILE";
    case NODE_FOR:
        return "FOR";
    case NODE_FOR_RANGE:
        return "FOR\\nRANGE";
    case NODE_LOOP:
        return "LOOP";
    case NODE_REPEAT:
        return "REPEAT";
    case NODE_UNLESS:
        return "UNLESS";
    case NODE_GUARD:
        return "GUARD";
    case NODE_BREAK:
        return "BREAK";
    case NODE_CONTINUE:
        return "CONTINUE";
    case NODE_MATCH:
        return "MATCH";
    case NODE_MATCH_CASE:
        return "MATCH\\nCASE";
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
        return "STRUCT\\nINIT";
    case NODE_EXPR_ARRAY_LITERAL:
        return "ARRAY";
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
        return "VARIANT";
    case NODE_TRAIT:
        return "TRAIT";
    case NODE_IMPL:
        return "IMPL";
    case NODE_IMPL_TRAIT:
        return "IMPL\\nTRAIT";
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
    case NODE_ASSERT:
        return "ASSERT";
    case NODE_LAMBDA:
        return "LAMBDA";
    case NODE_PLUGIN:
        return "PLUGIN";
    case NODE_TERNARY:
        return "TERNARY";
    case NODE_DO_WHILE:
        return "DO\\nWHILE";
    case NODE_TYPEOF:
        return "TYPEOF";
    case NODE_TRY:
        return "TRY";
    case NODE_AWAIT:
        return "AWAIT";
    default:
        return "?";
    }
}

static void dot_escape_label(ParserContext *ctx, const char *s)
{
    if (!s)
    {
        return;
    }
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
        else if (c == '>')
        {
            emitter_printf(&ctx->cg.emitter, "\\>");
        }
        else if (c == '<')
        {
            emitter_printf(&ctx->cg.emitter, "\\<");
        }
        else if (c == '{' || c == '}')
        {
            emitter_printf(&ctx->cg.emitter, "%c", c);
        }
        else if (c >= 32 && c < 127)
        {
            emitter_printf(&ctx->cg.emitter, "%c", c);
        }
    }
}

static int dot_emit_node(ParserContext *ctx, ASTNode *node);
static int dot_emit_children(ParserContext *ctx, ASTNode *children)
{
    if (!children)
    {
        return -1;
    }
    int first_child = -1;
    int prev = -1;
    for (ASTNode *c = children; c; c = c->next)
    {
        int cid = dot_emit_node(ctx, c);
        if (first_child < 0)
        {
            first_child = cid;
        }
        if (prev >= 0)
        {
            emitter_printf(&ctx->cg.emitter, "  %d -> %d [style=dotted,arrowhead=none];\n", prev,
                           cid);
        }
        prev = cid;
    }
    return first_child;
}

static int dot_emit_node(ParserContext *ctx, ASTNode *node)
{
    if (!node)
    {
        return -1;
    }

    int id = node_id++;
    emitter_printf(&ctx->cg.emitter, "  %d [label=\"%s", id, dot_type_name((int)(node->kind)));

    // Name
    const char *name = NULL;
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
    case NODE_LABEL:
        name = node->var_decl.name;
        break;
    case NODE_IMPL:
        name = node->impl.struct_name;
        break;
    case NODE_IMPORT:
        name = node->import_stmt.path;
        break;
    case NODE_EXPR_BINARY:
    case NODE_EXPR_UNARY:
        name = node->unary.op;
        break;
    default:
        break;
    }
    if (name)
    {
        emitter_printf(&ctx->cg.emitter, "|");
        dot_escape_label(ctx, name);
    }

    // Literal value
    if (node->kind == NODE_EXPR_LITERAL)
    {
        emitter_printf(&ctx->cg.emitter, "|");
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
            if (node->literal.string_val)
            {
                emitter_printf(&ctx->cg.emitter, "\"");
                dot_escape_label(ctx, node->literal.string_val);
                emitter_printf(&ctx->cg.emitter, "\"");
            }
            break;
        case LITERAL_CHAR:
            emitter_printf(&ctx->cg.emitter, "'%c'", (char)node->literal.int_val);
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
            emitter_printf(&ctx->cg.emitter, "|%s", tn);
            zfree(tn);
        }
    }

    emitter_printf(&ctx->cg.emitter, "\"];\n");

    // Children
    switch (node->kind)
    {
    case NODE_ROOT:
        if (node->root.children)
        {
            int cid = dot_emit_children(ctx, node->root.children);
            if (cid >= 0)
            {
                emitter_printf(&ctx->cg.emitter, "  %d -> %d;\n", id, cid);
            }
        }
        break;
    case NODE_FUNCTION:
        if (node->func.body)
        {
            int cid = dot_emit_node(ctx, node->func.body);
            emitter_printf(&ctx->cg.emitter, "  %d -> %d;\n", id, cid);
        }
        break;
    case NODE_BLOCK:
        if (node->block.statements)
        {
            int cid = dot_emit_children(ctx, node->block.statements);
            if (cid >= 0)
            {
                emitter_printf(&ctx->cg.emitter, "  %d -> %d;\n", id, cid);
            }
        }
        break;
    case NODE_IF:
    {
        ASTNode *kids[3] = {node->if_stmt.condition, node->if_stmt.then_body,
                            node->if_stmt.else_body};
        for (int j = 0; j < 3; j++)
        {
            if (kids[j])
            {
                int cid = dot_emit_node(ctx, kids[j]);
                const char *lbl = j == 0 ? "cond" : j == 1 ? "then" : "else";
                emitter_printf(&ctx->cg.emitter, "  %d -> %d [label=\"%s\"];\n", id, cid, lbl);
            }
        }
        break;
    }
    case NODE_VAR_DECL:
        if (node->var_decl.init_expr)
        {
            int cid = dot_emit_node(ctx, node->var_decl.init_expr);
            emitter_printf(&ctx->cg.emitter, "  %d -> %d;\n", id, cid);
        }
        break;
    case NODE_RETURN:
        if (node->ret.value)
        {
            int cid = dot_emit_node(ctx, node->ret.value);
            emitter_printf(&ctx->cg.emitter, "  %d -> %d;\n", id, cid);
        }
        break;
    case NODE_EXPR_BINARY:
        if (node->binary.left)
        {
            int cid = dot_emit_node(ctx, node->binary.left);
            emitter_printf(&ctx->cg.emitter, "  %d -> %d [label=\"L\"];\n", id, cid);
        }
        if (node->binary.right)
        {
            int cid = dot_emit_node(ctx, node->binary.right);
            emitter_printf(&ctx->cg.emitter, "  %d -> %d [label=\"R\"];\n", id, cid);
        }
        break;
    case NODE_EXPR_UNARY:
        if (node->unary.operand)
        {
            int cid = dot_emit_node(ctx, node->unary.operand);
            emitter_printf(&ctx->cg.emitter, "  %d -> %d;\n", id, cid);
        }
        break;
    case NODE_EXPR_CALL:
        if (node->call.args)
        {
            int cid = dot_emit_children(ctx, node->call.args);
            if (cid >= 0)
            {
                emitter_printf(&ctx->cg.emitter, "  %d -> %d;\n", id, cid);
            }
        }
        break;
    case NODE_STRUCT:
        if (node->strct.fields)
        {
            int cid = dot_emit_children(ctx, node->strct.fields);
            if (cid >= 0)
            {
                emitter_printf(&ctx->cg.emitter, "  %d -> %d;\n", id, cid);
            }
        }
        break;
    case NODE_ENUM:
        if (node->enm.variants)
        {
            int cid = dot_emit_children(ctx, node->enm.variants);
            if (cid >= 0)
            {
                emitter_printf(&ctx->cg.emitter, "  %d -> %d;\n", id, cid);
            }
        }
        break;
    case NODE_MATCH:
        if (node->match_stmt.cases)
        {
            int cid = dot_emit_children(ctx, node->match_stmt.cases);
            if (cid >= 0)
            {
                emitter_printf(&ctx->cg.emitter, "  %d -> %d;\n", id, cid);
            }
        }
        break;
    case NODE_IMPL:
        if (node->impl.methods)
        {
            int cid = dot_emit_children(ctx, node->impl.methods);
            if (cid >= 0)
            {
                emitter_printf(&ctx->cg.emitter, "  %d -> %d;\n", id, cid);
            }
        }
        break;
    case NODE_EXPR_STRUCT_INIT:
        if (node->struct_init.fields)
        {
            int cid = dot_emit_children(ctx, node->struct_init.fields);
            if (cid >= 0)
            {
                emitter_printf(&ctx->cg.emitter, "  %d -> %d;\n", id, cid);
            }
        }
        break;
    case NODE_EXPR_ARRAY_LITERAL:
        if (node->array_literal.elements)
        {
            int cid = dot_emit_children(ctx, node->array_literal.elements);
            if (cid >= 0)
            {
                emitter_printf(&ctx->cg.emitter, "  %d -> %d;\n", id, cid);
            }
        }
        break;
    case NODE_FOR_RANGE:
        if (node->for_range.start)
        {
            int cid = dot_emit_node(ctx, node->for_range.start);
            emitter_printf(&ctx->cg.emitter, "  %d -> %d [label=\"from\"];\n", id, cid);
        }
        if (node->for_range.end)
        {
            int cid = dot_emit_node(ctx, node->for_range.end);
            emitter_printf(&ctx->cg.emitter, "  %d -> %d [label=\"to\"];\n", id, cid);
        }
        if (node->for_range.body)
        {
            int cid = dot_emit_node(ctx, node->for_range.body);
            emitter_printf(&ctx->cg.emitter, "  %d -> %d;\n", id, cid);
        }
        break;
    case NODE_FOR:
    {
        if (node->for_stmt.init)
        {
            int cid = dot_emit_node(ctx, node->for_stmt.init);
            emitter_printf(&ctx->cg.emitter, "  %d -> %d [label=\"init\"];\n", id, cid);
        }
        if (node->for_stmt.condition)
        {
            int cid = dot_emit_node(ctx, node->for_stmt.condition);
            emitter_printf(&ctx->cg.emitter, "  %d -> %d [label=\"cond\"];\n", id, cid);
        }
        if (node->for_stmt.step)
        {
            int cid = dot_emit_node(ctx, node->for_stmt.step);
            emitter_printf(&ctx->cg.emitter, "  %d -> %d [label=\"step\"];\n", id, cid);
        }
        if (node->for_stmt.body)
        {
            int cid = dot_emit_node(ctx, node->for_stmt.body);
            emitter_printf(&ctx->cg.emitter, "  %d -> %d;\n", id, cid);
        }
        break;
    }
    case NODE_WHILE:
        if (node->while_stmt.condition)
        {
            int cid = dot_emit_node(ctx, node->while_stmt.condition);
            emitter_printf(&ctx->cg.emitter, "  %d -> %d [label=\"cond\"];\n", id, cid);
        }
        if (node->while_stmt.body)
        {
            int cid = dot_emit_node(ctx, node->while_stmt.body);
            emitter_printf(&ctx->cg.emitter, "  %d -> %d;\n", id, cid);
        }
        break;
    default:
        break;
    }

    return id;
}

static void dot_emit_program(ParserContext *ctx, ASTNode *root)
{
    node_id = 0;
    emitter_printf(&ctx->cg.emitter, "digraph AST {\n");
    emitter_printf(&ctx->cg.emitter, "  rankdir=LR;\n");
    emitter_printf(&ctx->cg.emitter, "  node [shape=record,style=filled,fillcolor=lightyellow];\n");
    dot_emit_node(ctx, root);
    emitter_printf(&ctx->cg.emitter, "}\n");
}

static void dot_emit_preamble(ParserContext *ctx)
{
    (void)ctx;
}

static const BackendOptAlias dot_aliases[] = {
    {NULL, NULL, NULL},
};

static const CodegenBackend dot_backend = {
    .name = "dot",
    .extension = ".dot",
    .emit_program = dot_emit_program,
    .emit_preamble = dot_emit_preamble,
    .needs_cc = 0,
    .aliases = dot_aliases,
};

void codegen_register_dot_backend(void)
{
    codegen_register_backend(&dot_backend);
}
