// SPDX-License-Identifier: MIT

#include "../ast/ast.h"
#include "../constants.h"
#include "../parser/parser.h"
#include "../zprep.h"
#include "codegen.h"
#include "codegen_internal.h"
#include "compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../platform/misra.h"

static int last_source_mapping_line = -1;
static NodeType last_source_mapping_type = NODE_ROOT;
static int allow_duplicate_source_mapping = 0;

int should_emit_source_mapping(ASTNode *node)
{
    return node && node->kind < NODE_REPL_PRINT && node->kind != NODE_BLOCK &&
           node->kind != NODE_EXPR_UNARY && node->kind != NODE_FIELD;
}

void emit_source_mapping_duplicate(ParserContext *ctx, ASTNode *node)
{
    allow_duplicate_source_mapping++;
    emit_source_mapping(ctx, node);
    allow_duplicate_source_mapping--;
}

void emit_source_mapping(ParserContext *ctx, ASTNode *node)
{
    if (!ctx->config->mode_debug)
    {
        return;
    }

    if (!should_emit_source_mapping(node))
    {
        return;
    }

    if (allow_duplicate_source_mapping <= 0)
    {
        if (node->token.line == last_source_mapping_line)
        {
            return;
        }
    }

    if (!node->token.start || !node->token.file)
    {
        zwarn_at(node->token,
                 "Encountered source mapping issue for node type %i, please report this issue.",
                 (int)node->kind);
        return;
    }

    last_source_mapping_line = node->token.line;
    last_source_mapping_type = node->kind;

    if (!ctx->config->misra_mode)
    {
        char *safe_file = sanitize_path_for_c_string(node->token.file);
        EMIT(ctx, "\n#line %i \"%s\"\n", node->token.line, safe_file);
    }
}