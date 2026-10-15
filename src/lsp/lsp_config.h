// SPDX-License-Identifier: MIT
#ifndef LSP_CONFIG_H
#define LSP_CONFIG_H

#ifndef ZC_ALLOW_INTERNAL
#error "lsp/lsp_config.h is internal to Zen C. Include the appropriate public header instead."
#endif

#include "lsp_project.h"

/**
 * @brief Apply the per-project settings in `<root_path>/zenc.server.json` to `cfg` and `project`.
 *
 * A missing file is not an error. Invalid JSON ignores the whole file; an unknown key or a
 * value of the wrong type ignores just that key or entry. Problems are logged to stderr
 * and reported to the client with window/showMessage.
 */
void lsp_config_load(const char *root_path, CompilerConfig *cfg, LSPProject *project);

#endif // LSP_CONFIG_H
