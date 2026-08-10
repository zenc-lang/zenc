// SPDX-License-Identifier: MIT
// Standalone Zen C language server (JSON-RPC over stdio). Formerly `zc lsp`.
#include "tool_common.h"
#include "../lsp/lsp_main.h"

int main(int argc, char **argv)
{
    z_compiler_setup();
    return lsp_main(argc, argv);
}
