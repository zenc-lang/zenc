// SPDX-License-Identifier: MIT
// Standalone Zen C documentation generator. Formerly `zc doc`.
#include "tool_common.h"
#include "../compiler.h"
#include "../parser/parser.h"
#include "../token.h"
#include "../diagnostics/diagnostics.h"
#include "../utils/utils.h"
#include "../zen/zen_doc.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    z_compiler_setup();

    g_config.mode_doc = 1;
    g_config.keep_comments = 1;
    g_config.recursive_doc = 1;
    g_config.mode_check = 0; // Documentation does not require typechecking
    g_config.use_typecheck = 0;

    const char *input = NULL;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--recursive-doc") == 0)
        {
            g_config.recursive_doc = 1;
        }
        else if (strcmp(argv[i], "--no-recursive-doc") == 0)
        {
            g_config.recursive_doc = 0;
        }
        else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0)
        {
            g_config.verbose = 1;
        }
        else if (argv[i][0] != '-' && !input)
        {
            input = argv[i];
        }
        else
        {
            fprintf(stderr, "error: unknown argument '%s'\n", argv[i]);
            return 1;
        }
    }

    if (!input)
    {
        fprintf(stderr, "error: no input file specified\n");
        fprintf(stderr, "usage: zc-doc [--recursive-doc|--no-recursive-doc] <file.zc>\n");
        return 1;
    }

    g_config.input_file = (char *)input;

    ParserContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.compiler = &g_compiler;
    ctx.config = &g_compiler.config;
    ctx.current_filename = g_compiler.config.input_file;
    module_state_init(&ctx.imports);
    diag_set_parser_ctx(&ctx);
    token_set_parser_ctx(&ctx);

    char *src = load_file(g_compiler.config.input_file, ctx.current_filename);
    if (!src)
    {
        fprintf(stderr, "error: could not read file '%s'\n", g_compiler.config.input_file);
        return 1;
    }

    scan_build_directives(&ctx, src);

    Lexer l;
    lexer_init(&l, src, ctx.config, ctx.current_filename);

    ASTNode *root = parse_program(&ctx, &l);
    if (!root)
    {
        return 1;
    }

    generate_docs(&ctx, root);
    return 0;
}
