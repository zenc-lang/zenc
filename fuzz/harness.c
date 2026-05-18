#include "parser/parser.h"
#include "analysis/typecheck.h"
#include "ast/ast.h"
#include "diagnostics/diagnostics.h"
#include "zen/zen_facts.h"
#include "zprep.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Global config and state
extern int g_error_count;
extern int g_warning_count;
extern ParserContext *g_parser_ctx;

// Initialization
static int initialized = 0;
static void initialize()
{
    if (initialized)
    {
        return;
    }

    // Seed random with current time
    zen_init();

    memset(&g_config, 0, sizeof(g_config));
    g_config.mode_check = 1; // Fuzzing is mostly about checking
    g_config.use_typecheck = 1;
    g_config.quiet = 1; // Don't spam stderr during fuzzing

    init_builtins();

    initialized = 1;
}

#define MAX_FUZZ_INPUT_SIZE (64 * 1024) /* 64 KB – cap input to limit exposure to memory-safety bugs */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0 || size > MAX_FUZZ_INPUT_SIZE)
    {
        return 0;
    }

    initialize();

    // Reset diagnostics
    g_error_count = 0;
    g_warning_count = 0;

    // Create null-terminated string from fuzzer data
    char *src = malloc(size + 1);
    if (!src)
    {
        return 0;
    }
    memcpy(src, data, size);
    src[size] = '\0';

    ParserContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.current_filename = "fuzz_input.zc";
    module_state_init(&ctx.imports);

    // Use /dev/null for hoisting output
    ctx.hoist_out = fopen("/dev/null", "w");
    if (!ctx.hoist_out)
    {
        arena_reset();
        return 0;
    }

    Lexer l;
    lexer_init(&l, src, ctx.config, ctx.current_filename);

    // Fuzz Parser
    ASTNode *root = parse_program(&ctx, &l);

    if (root)
    {
        propagate_vector_inner_types(&ctx);
        propagate_drop_traits(&ctx);
        if (validate_types(&ctx))
        {
            check_program(&ctx, root);
        }
        ast_free(root);
    }

    fclose(ctx.hoist_out);

    // Everything (src, root, ctx allocations) was in the Arena.
    // Resetting it now to prevent leaks between iterations.
    arena_reset();
    clear_registered_traits();
    free(src);

    return 0;
}
