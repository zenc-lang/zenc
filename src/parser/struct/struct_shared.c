// SPDX-License-Identifier: MIT
#include "parser.h"
#include "constants.h"
#include "ast/ast.h"
#include "analysis/move_check.h"
#include "plugins/plugin_manager.h"
#include "zen/zen_facts.h"
#include "zprep_plugin.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void auto_import_std_mem(ParserContext *ctx)
{
    // Check if Drop trait is already registered (means mem.zc was imported)
    if (check_impl(ctx, "Drop", "__trait_marker__"))
    {
        // Check_impl returns 0 if not found, but we need a different check
        // Let's check if we can find any indicator that mem.zc was loaded
    }
    // Resolve path to std/mem.zc
    char *resolved = z_resolve_path("std/mem.zc", ctx->current_filename, ctx->config);
    if (!resolved)
    {
        return; // Could not find mem.zc
    }
    // Check if already imported or currently being parsed
    if (is_file_imported(ctx, resolved))
    {
        zfree(resolved);
        return;
    }
    if (zmap_get(&ctx->imports.currently_parsing, resolved))
    {
        zfree(resolved);
        return;
    }
    zmap_put(&ctx->imports.currently_parsing, resolved, resolved);
    // Load and parse the file
    char *src = load_file(resolved, ctx->current_filename);
    if (!src)
    {
        zmap_remove(&ctx->imports.currently_parsing, resolved);
        zfree(resolved);
        return;
    }
    Lexer i;
    lexer_init(&i, src, ctx->config, ctx->current_filename);
    // Save and restore filename context
    const char *saved_fn = ctx->current_filename;
    ctx->current_filename = resolved;
    // Parse the mem module contents
    parse_program_nodes(ctx, &i);
    ctx->current_filename = saved_fn;
    zmap_remove(&ctx->imports.currently_parsing, resolved);
    mark_file_imported(ctx, resolved);
    zfree(resolved);
}
// Canonical symbol for a method: `Struct__Method` (or `Struct__Trait__Method`).
// A method whose name starts with `_` must keep the resulting run of three
// underscores (separator "__" plus the method's leading "_"), otherwise
// merge_underscores would collapse it to two and collide with the plain
// `method` symbol on the same struct. This preserves injectivity while keeping
// mangled names readable.
char *mangle_method_symbol(const char *struct_name, const char *trait_name, const char *method_name)
{
    char tmp[MAX_MANGLED_NAME_LEN];
    if (trait_name)
    {
        snprintf(tmp, sizeof(tmp), "%s__%s__%s", struct_name, trait_name, method_name);
    }
    else
    {
        snprintf(tmp, sizeof(tmp), "%s__%s", struct_name, method_name);
    }
    if (method_name && method_name[0] == '_')
    {
        return xstrdup(tmp);
    }
    return merge_underscores(tmp);
}
void patch_and_fix_self(ParserContext *ctx, ASTNode *f, const char *full_struct_name)
{
    (void)ctx;
    char *na = patch_self_args(f->func.args, full_struct_name);
    zfree(f->func.args);
    f->func.args = na;
}
// Trait Parsing
