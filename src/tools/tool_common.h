// SPDX-License-Identifier: MIT
#ifndef ZC_TOOL_COMMON_H
#define ZC_TOOL_COMMON_H

// Common bootstrap shared by the zc compiler binary and the standalone tools
// (zc-lsp, zc-repl, zc-doc, zc-format). Each tool has its own thin main() and
// calls z_compiler_setup() before delegating to its entry point.
void z_compiler_setup(void);

#endif // ZC_TOOL_COMMON_H
