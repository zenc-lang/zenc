// SPDX-License-Identifier: MIT
#include "parser.h"
#include "constants.h"
#include "ast/ast.h"
#include "analysis/move_check.h"
#include "plugins/plugin_manager.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "analysis/const_fold.h"
#include "utils/utils.h"
#include "ast/primitives.h"

ASTNode *parse_program_nodes(ParserContext *ctx, Lexer *l)
{
    ASTNode *h = 0, *tl = 0;
    while (1)
    {
        skip_comments(l);

        // Fault-tolerant recovery: if a sub-parser encountered an error and
        // returned (instead of exit()ing), skip tokens until we find a likely
        // top-level keyword so we can resume parsing cleanly.
        if (ctx->is_fault_tolerant && ctx->had_error)
        {
            ctx->had_error = 0;
            while (1)
            {
                Token recovery = lexer_peek(l);
                if (recovery.kind == TOK_EOF)
                {
                    return h;
                }
                // Resync on tokens that typically start top-level declarations
                if (recovery.kind == TOK_IDENT)
                {
                    if ((recovery.len == 2 && strncmp(recovery.start, "fn", 2) == 0) ||
                        (recovery.len == 6 && strncmp(recovery.start, "struct", 6) == 0) ||
                        (recovery.len == 4 && strncmp(recovery.start, "enum", 4) == 0) ||
                        (recovery.len == 4 && strncmp(recovery.start, "impl", 4) == 0) ||
                        (recovery.len == 5 && strncmp(recovery.start, "trait", 5) == 0) ||
                        (recovery.len == 6 && strncmp(recovery.start, "import", 6) == 0) ||
                        (recovery.len == 6 && strncmp(recovery.start, "extern", 6) == 0) ||
                        (recovery.len == 4 && strncmp(recovery.start, "test", 4) == 0) ||
                        (recovery.len == 5 && strncmp(recovery.start, "alias", 5) == 0))
                    {
                        break; // Found a top-level keyword, resume parsing
                    }
                }
                lexer_next(l); // Skip this token
            }
            continue; // Re-enter loop with peeked top-level keyword
        }

        skip_comments(l);
        Token t = lexer_peek(l);
        if (t.kind == TOK_EOF)
        {
            break;
        }

        if (t.kind == TOK_COMPTIME)
        {
            ASTNode *body = parse_comptime_body(ctx, l);
            ASTNode *ct = ast_create(NODE_COMPTIME);
            ct->comptime.body = body;
            ct->comptime.generated = NULL;
            if (!h)
            {
                h = ct;
            }
            else
            {
                tl->next = ct;
            }
            tl = ct;
            continue;
        }

        ASTNode *s = 0;
        DeclarationAttributes attrs = parse_attributes(ctx, l);
        t = lexer_peek(l);

        // Standalone @link("path") directive
        if (attrs.link_path)
        {
            // Push to config.c_files immediately (module AST may be freed later)
            const char *path = attrs.link_path;
            char *resolved = realpath(path, NULL);
            if (!resolved && ctx->config->root_path)
            {
                // The path may be relative to the workspace/stdlib root
                // (ZC_ROOT) rather than the current working directory.
                char alt[MAX_PATH_LEN];
                snprintf(alt, sizeof(alt), "%s/%s", ctx->config->root_path, path);
                resolved = realpath(alt, NULL);
            }
            if (!resolved && ctx->config->std_root[0])
            {
                // Installed compilers resolve the std root from an actual std
                // import; @link paths inside std (e.g. the vendored TRE unity
                // build) are relative to that root.
                char alt[MAX_PATH_LEN];
                snprintf(alt, sizeof(alt), "%s/%s", ctx->config->std_root, path);
                resolved = realpath(alt, NULL);
            }
            zvec_push_Str(&ctx->config->c_files, xstrdup(resolved ? resolved : path));
            if (resolved)
            {
                libc_free(resolved);
            }

            s = ast_create(NODE_LINK);
            s->link.path = attrs.link_path;
            attrs.link_path = NULL;
            goto add_node;
        }

        if (t.kind == TOK_PREPROC)
        {
            lexer_next(l);
            char *content = xmalloc(t.len + 2);
            strncpy(content, t.start, t.len);
            content[t.len] = '\n';
            content[t.len + 1] = 0;
            s = ast_create(NODE_PREPROC_DIRECTIVE);
            s->token = t;
            s->raw_stmt.content = content;

            // Audit and potentially deprecate preprocessor directives
            parser_audit_preprocessor(ctx, t);
        }
        else if (t.kind == TOK_DEF)
        {
            s = parse_def(ctx, l, attrs.is_export);
        }
        else if (t.kind == TOK_IDENT)
        {
            // Inline function: inline fn name(...) { }
            if (0 == strncmp(t.start, "inline", 6) && 6 == t.len)
            {
                lexer_next(l);
                Token next = lexer_peek(l);
                if (next.kind == TOK_IDENT && 2 == next.len && 0 == strncmp(next.start, "fn", 2))
                {
                    s = parse_function(ctx, l, 0, 0, attrs.link_name, attrs.is_export);
                    attrs.is_inline = 1;
                }
                else
                {
                    zpanic_at(next, "Expected 'fn' after 'inline'");
                    return NULL;
                }
            }
            else if (0 == strncmp(t.start, "fn", 2) && 2 == t.len)
            {
                s = parse_function(ctx, l, 0, 0, attrs.link_name, attrs.is_export);
            }
            else if (0 == strncmp(t.start, "struct", 6) && 6 == t.len)
            {
                s = parse_struct(ctx, l, 0, 0, 0, attrs.link_name, attrs.is_export);
                if (s && s->kind == NODE_STRUCT)
                {
                    s->strct.is_packed = attrs.is_packed;
                    s->strct.align = attrs.align;
                    s->strct.attributes = attrs.custom_attributes;
                    if (attrs.crepr_c_type)
                    {
                        s->strct.crepr_c_type = xstrdup(attrs.crepr_c_type);
                    }

                    if (attrs.vector_size > 0)
                    {
                        s->type_info->kind = TYPE_VECTOR;
                        s->type_info->array_size = attrs.vector_size;
                    }
                }
            }
            else if (0 == strncmp(t.start, "enum", 4) && 4 == t.len)
            {
                s = parse_enum(ctx, l, attrs.link_name, attrs.is_export);
            }
            else if (t.len == 4 && strncmp(t.start, "impl", 4) == 0)
            {
                s = parse_impl(ctx, l);
            }
            else if (t.len == 5 && strncmp(t.start, "trait", 5) == 0)
            {
                s = parse_trait(ctx, l);
            }
            else if (t.len == 7 && strncmp(t.start, "include", 7) == 0)
            {
                s = parse_include(ctx, l);
            }
            else if (t.len == 6 && strncmp(t.start, "export", 6) == 0)
            {
                lexer_next(l); // eat 'export'
                Token next = lexer_peek(l);
                if (next.len == 6 && strncmp(next.start, "import", 6) == 0)
                {
                    s = parse_import(ctx, l, 1);
                }
            }
            else if (t.len == 6 && strncmp(t.start, "import", 6) == 0)
            {
                s = parse_import(ctx, l, 0);
            }
            else if (t.len == 6 && strncmp(t.start, "static", 6) == 0)
            {
                lexer_next(l); // eat static
                Token next = lexer_peek(l);
                if (next.kind == TOK_IDENT && next.len == 3 && strncmp(next.start, "let", 3) == 0)
                {
                    s = parse_var_decl(ctx, l, attrs.is_export);
                    if (s)
                    {
                        s->var_decl.is_static = 1;
                        if (attrs.is_thread_local)
                        {
                            s->var_decl.is_thread_local = 1;
                        }
                    }
                }
                else
                {
                    zpanic_at(next, "Expected 'let' after 'static' in global scope");
                    return NULL;
                }
            }
            else if (t.len == 3 && strncmp(t.start, "let", 3) == 0)
            {
                s = parse_var_decl(ctx, l, attrs.is_export);
            }
            else if (t.len == 6 && strncmp(t.start, "extern", 6) == 0)
            {
                lexer_next(l);

                Token peek = lexer_peek(l);
                if (peek.kind == TOK_IDENT && peek.len == 2 && strncmp(peek.start, "fn", 2) == 0)
                {
                    s = parse_function(ctx, l, 0, 1, attrs.link_name, attrs.is_export);
                }
                else if (peek.kind == TOK_IDENT && peek.len == 6 &&
                         strncmp(peek.start, "struct", 6) == 0)
                {
                    // extern struct Name; -> opaque struct declaration
                    s = parse_struct(ctx, l, 0, 1, 1, attrs.link_name, attrs.is_export);
                    if (s && s->kind == NODE_STRUCT)
                    {
                        register_extern_symbol(ctx, s->strct.name);
                    }
                }
                else if ((peek.kind == TOK_IDENT && peek.len == 5 &&
                          strncmp(peek.start, "union", 5) == 0) ||
                         peek.kind == TOK_UNION)
                {
                    // extern union Name; -> opaque union declaration
                    s = parse_struct(ctx, l, 1, 1, 1, attrs.link_name, attrs.is_export);
                    if (s && s->kind == NODE_STRUCT)
                    {
                        register_extern_symbol(ctx, s->strct.name);
                    }
                }
                else
                {
                    if (lexer_peek(l).kind == TOK_IDENT && lexer_peek(l).len == 3 &&
                        strncmp(lexer_peek(l).start, "let", 3) == 0)
                    {
                        lexer_next(l); // eat let
                    }

                    while (1)
                    {
                        Token sym = lexer_next(l);
                        if (sym.kind != TOK_IDENT)
                        {
                            break;
                        }

                        char *name = token_strdup(sym);
                        register_extern_symbol(ctx, name);

                        Token next = lexer_peek(l);
                        if (next.kind == TOK_COMMA)
                        {
                            lexer_next(l);
                        }
                        else
                        {
                            break;
                        }
                    }

                    if (lexer_peek(l).kind == TOK_SEMICOLON)
                    {
                        lexer_next(l);
                    }
                    continue;
                }
            }
            else if (0 == strncmp(t.start, "type", 4) && 4 == t.len)
            {
                s = parse_type_alias(ctx, l, 0, attrs.is_export);
            }
            else if (0 == strncmp(t.start, "raw", 3) && 3 == t.len)
            {
                lexer_next(l);
                if (lexer_peek(l).kind != TOK_LBRACE)
                {
                    zpanic_at(lexer_peek(l), "Expected { after raw");
                    return NULL;
                }
                lexer_next(l);

                const char *start = l->src + l->pos;

                int depth = 1;
                while (depth > 0)
                {
                    Token inner_t = lexer_next(l);
                    if (inner_t.kind == TOK_EOF)
                    {
                        zpanic_at(inner_t, "Unexpected EOF in raw block");
                        return NULL;
                    }
                    if (inner_t.kind == TOK_LBRACE)
                    {
                        depth++;
                    }
                    if (inner_t.kind == TOK_RBRACE)
                    {
                        depth--;
                    }
                }

                const char *end = l->src + l->pos - 1;
                size_t len = (size_t)(end - start);

                char *content = xmalloc(len + 1);
                memcpy(content, start, (size_t)(len));
                content[len] = 0;

                s = ast_create(NODE_RAW_STMT);
                s->token = t;
                s->raw_stmt.content = normalize_raw_content(content);
                zfree(content);
            }
            else
            {
                lexer_next(l);
            }
        }
        else if (t.kind == TOK_OPAQUE)
        {
            lexer_next(l); // eat opaque
            Token next = lexer_peek(l);
            if (0 == strncmp(next.start, "struct", 6) && 6 == next.len)
            {
                s = parse_struct(ctx, l, 0, 1, 0, attrs.link_name, attrs.is_export);
                if (s && s->kind == NODE_STRUCT)
                {
                    s->strct.is_packed = attrs.is_packed;
                    s->strct.align = attrs.align;
                }
            }
            else if (next.kind == TOK_ALIAS)
            {
                s = parse_type_alias(ctx, l, 1, attrs.is_export);
            }
            else
            {
                zpanic_at(next, "Expected 'struct' or 'alias' after 'opaque'");
                return NULL;
            }
        }
        else if (t.kind == TOK_ALIAS)
        {
            s = parse_type_alias(ctx, l, 0, attrs.is_export);
        }
        else if (t.kind == TOK_ASYNC)
        {
            lexer_next(l);
            Token next = lexer_peek(l);
            if (0 == strncmp(next.start, "fn", 2) && 2 == next.len)
            {
                s = parse_function(ctx, l, 1, 0, attrs.link_name, attrs.is_export);
            }
            else
            {
                zpanic_at(next, "Expected 'fn' after 'async'");
                return NULL;
            }
        }
        else if (t.kind == TOK_UNION)
        {
            s = parse_struct(ctx, l, 1, 0, 0, attrs.link_name, attrs.is_export);
        }
        else if (t.kind == TOK_TRAIT)
        {
            s = parse_trait(ctx, l);
        }
        else if (t.kind == TOK_IMPL)
        {
            s = parse_impl(ctx, l);
        }
        else if (t.kind == TOK_TEST)
        {
            s = parse_test(ctx, l);
        }
        else
        {
            lexer_next(l);
        }

        if (s && s->kind == NODE_FUNCTION)
        {
            s->func.required = attrs.is_required;
            s->func.is_inline = attrs.is_inline || s->func.is_inline;
            s->func.noinline = attrs.is_noinline;
            s->func.constructor = attrs.is_constructor;
            s->func.destructor = attrs.is_destructor;
            s->func.unused = attrs.is_unused;
            s->func.weak = attrs.is_weak;
            s->func.is_export = attrs.is_export;
            s->func.cold = attrs.is_cold;
            s->func.hot = attrs.is_hot;
            s->func.noreturn = attrs.is_noreturn;
            s->func.pure = attrs.is_pure;
            s->func.section = attrs.section;
            s->func.is_comptime = attrs.is_comptime;
            s->func.cuda_global = attrs.cuda_global;
            s->func.cuda_device = attrs.cuda_device;
            s->func.cuda_host = attrs.cuda_host;
            s->func.attributes = attrs.custom_attributes;

            if (attrs.is_deprecated && s->func.name)
            {
                register_deprecated_func(ctx, s->func.name, attrs.deprecated_msg);
            }

            if ((attrs.is_required || attrs.is_pure) && s->func.name)
            {
                FuncSig *sig = find_func(ctx, s->func.name);
                if (sig)
                {
                    if (attrs.is_required)
                    {
                        sig->required = 1;
                    }
                    if (attrs.is_pure)
                    {
                        sig->is_pure = 1;
                    }
                }
            }
        }
        else if (s && s->kind == NODE_STRUCT)
        {
            s->strct.is_export = attrs.is_export;
            s->strct.attributes = attrs.custom_attributes;
            s->strct.is_packed = attrs.is_packed || s->strct.is_packed;
            if (attrs.align)
            {
                s->strct.align = attrs.align;
            }
            if (attrs.is_deprecated && s->strct.name)
            {
                register_deprecated_func(ctx, s->strct.name, attrs.deprecated_msg);
            }
            if (attrs.derived_count > 0)
            {
                ASTNode *impls =
                    generate_derive_impls(ctx, s, attrs.derived_traits, attrs.derived_count);
                s->next = impls;
            }
        }
        else if (s && s->kind == NODE_ENUM)
        {
            if (attrs.derived_count > 0)
            {
                ASTNode *impls =
                    generate_derive_impls(ctx, s, attrs.derived_traits, attrs.derived_count);
                s->next = impls;
            }
        }

        if (s)
        {
        add_node:
            ATTACH_DOC_COMMENT(ctx, s);
            s->cfg_condition = attrs.cfg_condition;
            s->link_name = attrs.link_name;

            if (!h)
            {
                h = s;
            }
            else
            {
                tl->next = s;
            }
            tl = s;
            while (tl->next)
            {
                tl = tl->next;
            }
        }
        else
        {
            // Cleanup attributes if no node was created to receive them
            if (attrs.cfg_condition)
            {
                zfree(attrs.cfg_condition);
            }
            if (attrs.link_name)
            {
                zfree(attrs.link_name);
            }
            if (attrs.deprecated_msg)
            {
                zfree(attrs.deprecated_msg);
            }
            if (attrs.section)
            {
                zfree(attrs.section);
            }
            // ... potentially more cleanup needed for derived_traits and custom_attributes
        }
    }
    return h;
}

ASTNode *parse_program(ParserContext *ctx, Lexer *l)
{

    if (!ctx->global_scope)
    {
        ctx->global_scope = symbol_scope_create(NULL, "Global");
        ctx->current_scope = ctx->global_scope;
        register_builtins(ctx);
    }
    else
    {
        ctx->current_scope = ctx->global_scope;
    }

    ASTNode *r = ast_create(NODE_ROOT);
    skip_comments(l);
    ATTACH_DOC_COMMENT(ctx, r);
    r->root.children = parse_program_nodes(ctx, l);

    // Synchronize linkage overrides across all type references
    sync_all_link_names(ctx, r);

    return r;
}
