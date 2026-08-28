#include "zprep.h"
#include "parser/parser.h"
#include "analysis/typecheck.h"
#include "ast/ast.h"
#include "diagnostics/diagnostics.h"
#include "plugins/plugin_manager.h"
#include "zen/zen_facts.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

extern ZenCompiler g_compiler;
extern char *curr_func_ret;

static int initialized = 0;

static void free_everything(void)
{
    zptr_plugin_mgr_cleanup();
    zvec_free_Str(&g_compiler.config.include_paths);
    zvec_free_Str(&g_compiler.config.cfg_defines);
    zvec_free_Str(&g_compiler.config.c_files);
    zvec_free_Str(&g_compiler.config.extra_files);
    zvec_free_Str(&g_compiler.config.backend_opts);
    zarena_free(&g_compiler.arena);
}

// LibFuzzer entry point — called by the fuzzer runtime
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void initialize(void)
{
    if (initialized)
        return;

    memset(&g_compiler, 0, sizeof(g_compiler));
    g_compiler.config.mode_check = 1;
    g_compiler.config.use_typecheck = 1;
    g_compiler.config.quiet = 1;

    zarena_init(&g_compiler.arena);

    init_builtins();

    atexit(free_everything);

    initialized = 1;
}

// No-op error callback for fault tolerance
static void fuzz_noop_error(void *data, Token t, const char *msg)
{
    (void)data;
    (void)t;
    (void)msg;
}

__attribute__((used)) int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0)
        return 0;

    initialize();

    g_compiler.error_count = 0;
    g_compiler.warning_count = 0;

    char *src = zarena_alloc_zero(&g_compiler.arena, size + 1);
    if (!src)
        return 0;
    memcpy(src, data, size);
    src[size] = '\0';

    ParserContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.compiler = &g_compiler;
    ctx.config = &g_compiler.config;
    ctx.current_filename = "fuzz_input.zc";
    module_state_init(&ctx.imports);
    token_set_parser_ctx(&ctx);
    diag_set_parser_ctx(&ctx);

    // Reset persistent parser globals that would otherwise dangle into the
    // reset arena between inputs.
    curr_func_ret = NULL;

    scan_build_directives(&ctx, src);

    // Enable fault tolerance so zpanic_at returns instead of exit()-ing
    ctx.is_fault_tolerant = 1;
    ctx.had_error = 0;
    ctx.on_error = fuzz_noop_error;

    ctx.cg.hoist_out = fopen("/dev/null", "w");
    if (!ctx.cg.hoist_out)
    {
        zarena_reset(&g_compiler.arena);
        return 0;
    }

    Lexer l;
    lexer_init(&l, src, ctx.config, ctx.current_filename);

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

    fclose(ctx.cg.hoist_out);
    zarena_reset(&g_compiler.arena);

    // Reset parser globals that point into the reclaimed arena.
    clear_registered_traits();

    // Reset config vectors (free system-heap buffers, clear pointers)
    zvec_free_Str(&g_compiler.config.include_paths);
    zvec_free_Str(&g_compiler.config.cfg_defines);
    zvec_free_Str(&g_compiler.config.c_files);
    zvec_free_Str(&g_compiler.config.extra_files);
    zvec_free_Str(&g_compiler.config.backend_opts);

    return 0;
}

// AFL++/standalone entry point.
// This main() is used by AFL builds (which define __AFL_HAVE_MANUAL_CONTROL)
// and manual testing. The libFuzzer build (-fsanitize=fuzzer) provides its own main().
#if defined(__AFL_HAVE_MANUAL_CONTROL)
int main(int argc, char **argv)
{
    unsigned char buf[1048576];
    ssize_t len;

    if (argc > 1)
    {
        // AFL @@ mode: read from file
        int fd = open(argv[1], O_RDONLY);
        if (fd < 0)
            return 1;
        len = read(fd, buf, sizeof(buf));
        close(fd);
    }
    else
    {
        // stdin mode (AFL pipe, or manual testing)
        len = read(STDIN_FILENO, buf, sizeof(buf));
    }

    if (len > 0)
    {
        LLVMFuzzerTestOneInput(buf, (size_t)len);
    }

    return 0;
}
#endif
