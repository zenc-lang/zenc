// SPDX-License-Identifier: MIT
// Standalone Zen C read-eval-print loop. Formerly `zc repl`.
#include "tool_common.h"
#include "../repl/repl.h"
#include <stddef.h>
#include <string.h>

int main(int argc, char **argv)
{
    z_compiler_setup();

    // run_repl() reads options starting at argv[2] (it expects argv[1] to be
    // the "repl" subcommand). Normalize so direct `zc-repl -c <line>` and
    // wrapper `zc repl -c <line>` both land on the same argv layout.
    char *shifted[64];
    int n = 0;
    shifted[n++] = argv[0];
    if (argc < 2 || strcmp(argv[1], "repl") != 0)
    {
        shifted[n++] = "repl";
    }
    for (int i = 1; i < argc && n < 64; i++)
    {
        shifted[n++] = argv[i];
    }
    if (n == 64)
    {
        shifted[63] = NULL;
    }
    else
    {
        shifted[n] = NULL;
    }

    run_repl(argv[0], n, shifted);
    return 0;
}
