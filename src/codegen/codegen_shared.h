// SPDX-License-Identifier: MIT
#ifndef CODEGEN_SHARED_H
#define CODEGEN_SHARED_H

#include "../ast/ast.h"

/**
 * @file codegen_shared.h
 * @brief Backend-independent codegen utilities.
 *
 * These functions have no dependency on ParserContext or the emitter.
 * They can be used by any backend (C, x64, LLVM, etc.).
 */

// String manipulation
char *strip_template_suffix(const char *name);
char *extract_call_args(const char *args);
const char *parse_original_method_name(const char *mangled);
char *replace_string_type(const char *args);

// Type name mapping
const char *map_to_c_type(const char *t);

#endif // CODEGEN_SHARED_H
