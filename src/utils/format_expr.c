// SPDX-License-Identifier: MIT
#include "../parser/parser.h"
#include "../codegen/codegen.h"
#include "../utils/emitter.h"
#include "../ast/ast.h"
#include "../utils/format_expr.h"

char *format_expression_as_c(struct ParserContext *ctx, struct ASTNode *node)
{
    if (!ctx || !node)
    {
        return NULL;
    }

    emitter_push(&ctx->cg.emitter);
    emitter_init_buffer(&ctx->cg.emitter);
    codegen_expression(ctx, node);
    char *result = emitter_take_string(&ctx->cg.emitter);
    emitter_pop(&ctx->cg.emitter);
    // Copy to arena so callers don't need system heap management
    char *arena_copy = result ? xstrdup(result) : NULL;
    zfree(result);
    return arena_copy;
}
