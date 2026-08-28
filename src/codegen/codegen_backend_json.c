// SPDX-License-Identifier: MIT
// JSON backend - machine-readable AST output
#include "codegen_backend.h"
#include "../parser/parser.h"
#include "../ast/ast.h"
#include <string.h>

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
    case NODE_ASSERT:
        return "ASSERT";
    case NODE_EXPECT:
        return "EXPECT";
    case NODE_DESTRUCT_VAR:
        return "DESTRUCT_VAR";
    case NODE_LAMBDA:
        return "LAMBDA";
    case NODE_PLUGIN:
        return "PLUGIN";
    case NODE_TERNARY:
        return "TERNARY";
    case NODE_DO_WHILE:
        return "DO_WHILE";
    case NODE_TYPEOF:
        return "TYPEOF";
    case NODE_TRY:
        return "TRY";
    case NODE_REFLECTION:
        return "REFLECTION";
    case NODE_AWAIT:
        return "AWAIT";
    case NODE_REPL_PRINT:
        return "REPL_PRINT";
    case NODE_CUDA_LAUNCH:
        return "CUDA_LAUNCH";
    case NODE_VA_START:
        return "VA_START";
    default:
        return "UNKNOWN";
    }
}

static void json_escape(ParserContext *ctx, const char *s)
{
    if (!s)
    {
        emitter_printf(&ctx->cg.emitter, "null");
        return;
    }
    emitter_printf(&ctx->cg.emitter, "\"");
    for (const char *p = s; *p; p++)
    {
        char c = *p;
        switch (c)
        {
        case '"':
            emitter_printf(&ctx->cg.emitter, "\\\"");
            break;
        case '\\':
            emitter_printf(&ctx->cg.emitter, "\\\\");
            break;
        case '\n':
            emitter_printf(&ctx->cg.emitter, "\\n");
            break;
        case '\r':
            emitter_printf(&ctx->cg.emitter, "\\r");
            break;
        case '\t':
            emitter_printf(&ctx->cg.emitter, "\\t");
            break;
        default:
            emitter_printf(&ctx->cg.emitter, "%c", c);
            break;
        }
    }
    emitter_printf(&ctx->cg.emitter, "\"");
}

static void json_indent(ParserContext *ctx, int depth)
{
    for (int i = 0; i < depth; i++)
    {
        emitter_printf(&ctx->cg.emitter, "  ");
    }
}

static void json_emit_maybe_newline(ParserContext *ctx, int pretty, int depth)
{
    if (pretty)
    {
        emitter_printf(&ctx->cg.emitter, "\n");
        json_indent(ctx, depth);
    }
}

static void json_emit_separator(ParserContext *ctx, int pretty, int depth, int *first)
{
    if (*first)
    {
        *first = 0;
    }
    else
    {
        emitter_printf(&ctx->cg.emitter, ",");
        json_emit_maybe_newline(ctx, pretty, depth);
    }
}

// Forward declaration
static void json_emit_node(ParserContext *ctx, ASTNode *node, int pretty, int depth, int *first);

static void json_emit_named_children(ParserContext *ctx, ASTNode *children, const char *key,
                                     int pretty, int depth)
{
    if (!children)
    {
        return;
    }
    emitter_printf(&ctx->cg.emitter, ",\"%s\":", key);
    if (pretty)
    {
        emitter_printf(&ctx->cg.emitter, "\n");
        json_indent(ctx, depth);
    }
    emitter_printf(&ctx->cg.emitter, "[");
    if (pretty)
    {
        emitter_printf(&ctx->cg.emitter, "\n");
    }
    int first = 1;
    for (ASTNode *c = children; c; c = c->next)
    {
        json_emit_node(ctx, c, pretty, depth + 1, &first);
    }
    if (pretty && !first)
    {
        emitter_printf(&ctx->cg.emitter, "\n");
        json_indent(ctx, depth);
    }
    emitter_printf(&ctx->cg.emitter, "]");
}

static void json_emit_callee(ParserContext *ctx, ASTNode *callee)
{
    if (!callee)
    {
        return;
    }
    const char *name = NULL;
    if (callee->kind == NODE_EXPR_VAR && callee->var_ref.name)
    {
        name = callee->var_ref.name;
        // Skip mangled prefixes like Vec__int32_t__push -> push
        const char *last_underscore = (char *)strrchr(name, '_');
        if (last_underscore && last_underscore > name && *(last_underscore - 1) == '_')
        {
            name = last_underscore + 1;
        }
    }
    else if (callee->kind == NODE_EXPR_MEMBER && callee->member.field)
    {
        name = callee->member.field;
    }
    if (name)
    {
        emitter_printf(&ctx->cg.emitter, ",\"callee\":");
        json_escape(ctx, name);
    }
}

static void json_emit_raw_content(ParserContext *ctx, const char *content)
{
    if (!content)
    {
        return;
    }
    if (backend_opt(&ctx->config->backend_opts, "full-content"))
    {
        emitter_printf(&ctx->cg.emitter, ",\"content\":");
        json_escape(ctx, content);
    }
    else
    {
        size_t len = strlen(content);
        char preview[44];
        int slen = (int)(len < 40 ? len : 40);
        snprintf(preview, sizeof(preview), "%.*s%s", slen, content,
                 (size_t)slen < len ? "..." : "");
        emitter_printf(&ctx->cg.emitter, ",\"preview\":");
        json_escape(ctx, preview);
    }
}

static void json_emit_node(ParserContext *ctx, ASTNode *node, int pretty, int depth, int *first)
{
    if (!node)
    {
        return;
    }

    json_emit_separator(ctx, pretty, depth, first);
    if (pretty)
    {
        json_indent(ctx, depth);
    }
    emitter_printf(&ctx->cg.emitter, "{\"node\":\"%s\"", node_type_name((int)(node->kind)));

    // Name for named nodes
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
        if (node->var_decl.name)
        {
            emitter_printf(&ctx->cg.emitter, ",\"name\":");
            json_escape(ctx, node->var_decl.name);
        }
        break;
    case NODE_IMPL:
        if (node->impl.struct_name)
        {
            emitter_printf(&ctx->cg.emitter, ",\"name\":");
            json_escape(ctx, node->impl.struct_name);
        }
        break;
    case NODE_IMPORT:
        if (node->import_stmt.path)
        {
            emitter_printf(&ctx->cg.emitter, ",\"path\":");
            json_escape(ctx, node->import_stmt.path);
        }
        break;
    case NODE_INCLUDE:
        if (node->include.path)
        {
            emitter_printf(&ctx->cg.emitter, ",\"path\":");
            json_escape(ctx, node->include.path);
        }
        break;
    case NODE_EXPR_BINARY:
        if (node->binary.op)
        {
            emitter_printf(&ctx->cg.emitter, ",\"op\":");
            json_escape(ctx, node->binary.op);
        }
        break;
    case NODE_EXPR_UNARY:
        if (node->unary.op)
        {
            emitter_printf(&ctx->cg.emitter, ",\"op\":");
            json_escape(ctx, node->unary.op);
        }
        break;
    default:
        break;
    }

    // Callee name for CALL nodes
    if (node->kind == NODE_EXPR_CALL)
    {
        json_emit_callee(ctx, node->call.callee);
    }

    // For-range loop fields
    if (node->kind == NODE_FOR_RANGE)
    {
        if (node->for_range.var_name)
        {
            emitter_printf(&ctx->cg.emitter, ",\"var\":");
            json_escape(ctx, node->for_range.var_name);
        }
        if (node->for_range.step)
        {
            emitter_printf(&ctx->cg.emitter, ",\"step\":");
            json_escape(ctx, node->for_range.step);
        }
        if (node->for_range.is_inclusive)
        {
            emitter_printf(&ctx->cg.emitter, ",\"inclusive\":true");
        }
    }

    // RAW content preview
    if (node->kind == NODE_RAW_STMT)
    {
        json_emit_raw_content(ctx, node->raw_stmt.content);
    }

    // Literal values
    if (node->kind == NODE_EXPR_LITERAL)
    {
        switch (node->literal.kind)
        {
        case LITERAL_INT:
            emitter_printf(&ctx->cg.emitter, ",\"value\":%llu", node->literal.int_val);
            break;
        case LITERAL_FLOAT:
            emitter_printf(&ctx->cg.emitter, ",\"value\":%g", node->literal.float_val);
            break;
        case LITERAL_STRING:
        case LITERAL_RAW_STRING:
            if (node->literal.string_val)
            {
                emitter_printf(&ctx->cg.emitter, ",\"value\":");
                json_escape(ctx, node->literal.string_val);
            }
            break;
        case LITERAL_CHAR:
            emitter_printf(&ctx->cg.emitter, ",\"value\":\"%c\"", (char)node->literal.int_val);
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
            emitter_printf(&ctx->cg.emitter, ",\"type\":");
            json_escape(ctx, tn);
            zfree(tn);
        }
    }

    // Location
    if (node->token.start)
    {
        emitter_printf(&ctx->cg.emitter, ",\"loc\":{\"line\":%d,\"col\":%d}", node->token.line,
                       node->token.col);
    }

    // Children
    int child_depth = depth + 1;
    switch (node->kind)
    {
    case NODE_ROOT:
        if (node->root.children)
        {
            json_emit_named_children(ctx, node->root.children, "children", pretty, child_depth);
        }
        break;
    case NODE_FUNCTION:
        if (node->func.body)
        {
            json_emit_named_children(ctx, node->func.body, "body", pretty, child_depth);
        }
        break;
    case NODE_BLOCK:
        if (node->block.statements)
        {
            json_emit_named_children(ctx, node->block.statements, "statements", pretty,
                                     child_depth);
        }
        break;
    case NODE_STRUCT:
        if (node->strct.fields)
        {
            json_emit_named_children(ctx, node->strct.fields, "fields", pretty, child_depth);
        }
        break;
    case NODE_ENUM:
        if (node->enm.variants)
        {
            json_emit_named_children(ctx, node->enm.variants, "variants", pretty, child_depth);
        }
        break;
    case NODE_MATCH:
        if (node->match_stmt.cases)
        {
            json_emit_named_children(ctx, node->match_stmt.cases, "cases", pretty, child_depth);
        }
        break;
    case NODE_IMPL:
        if (node->impl.methods)
        {
            json_emit_named_children(ctx, node->impl.methods, "methods", pretty, child_depth);
        }
        break;
    case NODE_EXPR_STRUCT_INIT:
        if (node->struct_init.fields)
        {
            json_emit_named_children(ctx, node->struct_init.fields, "fields", pretty, child_depth);
        }
        break;
    case NODE_EXPR_ARRAY_LITERAL:
        if (node->array_literal.elements)
        {
            json_emit_named_children(ctx, node->array_literal.elements, "elements", pretty,
                                     child_depth);
        }
        break;
    case NODE_EXPR_CALL:
        if (node->call.args)
        {
            json_emit_named_children(ctx, node->call.args, "args", pretty, child_depth);
        }
        break;
    case NODE_FOR_RANGE:
    {
        int fc = 0;
        if (node->for_range.start)
        {
            json_emit_separator(ctx, pretty, child_depth, &fc);
            json_emit_maybe_newline(ctx, pretty, child_depth);
            emitter_printf(&ctx->cg.emitter, "\"start\":");
            int inf = 1;
            json_emit_node(ctx, node->for_range.start, pretty, child_depth + 1, &inf);
        }
        if (node->for_range.end)
        {
            json_emit_separator(ctx, pretty, child_depth, &fc);
            json_emit_maybe_newline(ctx, pretty, child_depth);
            emitter_printf(&ctx->cg.emitter, "\"end\":");
            int inf = 1;
            json_emit_node(ctx, node->for_range.end, pretty, child_depth + 1, &inf);
        }
        break;
    }
    case NODE_FOR:
    {
        int fc = 0;
        if (node->for_stmt.init)
        {
            json_emit_separator(ctx, pretty, child_depth, &fc);
            json_emit_maybe_newline(ctx, pretty, child_depth);
            emitter_printf(&ctx->cg.emitter, "\"init\":");
            int inf = 1;
            json_emit_node(ctx, node->for_stmt.init, pretty, child_depth + 1, &inf);
        }
        if (node->for_stmt.condition)
        {
            json_emit_separator(ctx, pretty, child_depth, &fc);
            json_emit_maybe_newline(ctx, pretty, child_depth);
            emitter_printf(&ctx->cg.emitter, "\"condition\":");
            int inf = 1;
            json_emit_node(ctx, node->for_stmt.condition, pretty, child_depth + 1, &inf);
        }
        if (node->for_stmt.step)
        {
            json_emit_separator(ctx, pretty, child_depth, &fc);
            json_emit_maybe_newline(ctx, pretty, child_depth);
            emitter_printf(&ctx->cg.emitter, "\"step\":");
            int inf = 1;
            json_emit_node(ctx, node->for_stmt.step, pretty, child_depth + 1, &inf);
        }
        if (node->for_stmt.body)
        {
            json_emit_named_children(ctx, node->for_stmt.body, "body", pretty, child_depth);
        }
        break;
    }
    default:
        break;
    }

    emitter_printf(&ctx->cg.emitter, "}");
}

static void json_emit_program(ParserContext *ctx, ASTNode *root)
{
    int pretty = backend_opt(&ctx->config->backend_opts, "pretty") != NULL;
    int first = 1;
    json_emit_node(ctx, root, pretty, 0, &first);
    emitter_printf(&ctx->cg.emitter, "\n");
}

static void json_emit_preamble(ParserContext *ctx)
{
    (void)ctx;
}

static const BackendOptAlias json_aliases[] = {
    {"--json-pretty", "pretty", NULL},
    {NULL, NULL, NULL},
};

static const CodegenBackend json_backend = {
    .name = "json",
    .extension = ".json",
    .emit_program = json_emit_program,
    .emit_preamble = json_emit_preamble,
    .needs_cc = 0,
    .aliases = json_aliases,
};

void codegen_register_json_backend(void)
{
    codegen_register_backend(&json_backend);
}
