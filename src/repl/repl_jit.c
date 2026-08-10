// SPDX-License-Identifier: MIT
/**
 * @file repl_jit.c
 * @brief JIT execution implementation using LibTCC.
 */

#include "repl_jit.h"
#include "repl_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ZC_HAS_JIT) && ZC_HAS_JIT
/* TCC Error Handler */
static void tcc_error_handler(void *opaque, const char *msg)
{
    (void)opaque;
    fprintf(stderr, "\033[1;31mtcc error:\033[0m %s\n", msg);
}

int repl_jit_execute(const char *c_code, CompilerConfig *cfg)
{
    TCCState *s = tcc_new();
    if (!s)
    {
        fprintf(stderr, "Could not create TCC state\n");
        return 1;
    }

    tcc_set_error_func(s, NULL, tcc_error_handler);
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);

    /* Add standard include paths if available */
    if (cfg->root_path)
    {
        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s/std/include", cfg->root_path);
        tcc_add_include_path(s, path);
    }

    /* Add common system header paths */
    tcc_add_include_path(s, "/usr/local/include");
    tcc_add_include_path(s, "/usr/include");

    /* Compile the code */
    if (tcc_compile_string(s, c_code) == -1)
    {
        tcc_delete(s);
        return 1;
    }

    /* Get main function and execute */
    char *argv[] = {"zrepl", NULL};
    if (tcc_run(s, 1, argv) == -1)
    {
        tcc_delete(s);
        return 1;
    }

    tcc_delete(s);
    return 0;
}
#else
int repl_jit_execute(const char *c_code, CompilerConfig *cfg)
{
    (void)cfg;
    /* Fallback for systems without libtcc: write to tmp, compile and run */
    const char *tmp_c = ".repl_fallback.c";
    const char *tmp_exe = ".repl_fallback.exe";

    FILE *f = fopen(tmp_c, "w");
    if (!f)
    {
        return 1;
    }
    fprintf(f, "%s", c_code);
    fclose(f);

    char cmd[1024];
#ifdef _WIN32
    /* The generated C uses the Zen runtime preamble plus C runtime functions. */
    snprintf(cmd, sizeof(cmd), "gcc %s -o %s -lm && .\\%s", tmp_c, tmp_exe, tmp_exe);
#else
    snprintf(cmd, sizeof(cmd), "cc %s -o %s -lm && ./%s", tmp_c, tmp_exe, tmp_exe);
#endif

    int res = system(cmd);

    /* Cleanup */
    remove(tmp_c);
    remove(tmp_exe);

    return res;
}
#endif
