// SPDX-License-Identifier: MIT

#include "parser.h"
#include "constants.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ast/ast.h"
#include "utils/format_expr.h"
#include "plugins/plugin_manager.h"
#include "zen/zen_facts.h"
#include "zprep_plugin.h"
#include "analysis/move_check.h"

static void try_parse_c_function_decl(ParserContext *ctx, const char *line)
{
    const char *p = line;
    while (*p && isspace(*p))
    {
        p++;
    }

    if (*p == '#' || *p == '/' || *p == '*' || *p == '\0')
    {
        return;
    }
    if (strncmp(p, "typedef", 7) == 0 && !isalnum(p[7]) && p[7] != '_')
    {
        return;
    }
    if (strncmp(p, "static", 6) == 0 && !isalnum(p[6]) && p[6] != '_')
    {
        return;
    }
    if (strncmp(p, "struct", 6) == 0 && !isalnum(p[6]) && p[6] != '_')
    {
        return;
    }
    if (strncmp(p, "union", 5) == 0 && !isalnum(p[5]) && p[5] != '_')
    {
        return;
    }
    if (strncmp(p, "enum", 4) == 0 && !isalnum(p[4]) && p[4] != '_')
    {
        return;
    }

    const char *lparen = (char *)strchr(p, '(');
    if (!lparen)
    {
        return;
    }

    const char *end = p + strlen(p) - 1;
    while (end > p && isspace(*end))
    {
        end--;
    }
    if (*end != ';')
    {
        return;
    }

    if (strchr(p, '{'))
    {
        return;
    }

    const char *name_end = lparen;
    while (name_end > p && isspace(*(name_end - 1)))
    {
        name_end--;
    }

    const char *name_start = name_end;
    while (name_start > p && (isalnum(*(name_start - 1)) || *(name_start - 1) == '_'))
    {
        name_start--;
    }

    int name_len = (int)(name_end - name_start);
    if (name_len <= 0)
    {
        return;
    }

    if ((name_len == 6 && strncmp(name_start, "return", 6) == 0) ||
        (name_len == 2 && strncmp(name_start, "if", 2) == 0) ||
        (name_len == 3 && strncmp(name_start, "for", 3) == 0) ||
        (name_len == 5 && strncmp(name_start, "while", 5) == 0))
    {
        return;
    }

    if (name_start == p)
    {
        return;
    }

    char *name = xmalloc((size_t)(name_len + 1));
    strncpy(name, name_start, (size_t)(name_len));
    name[name_len] = '\0';

    register_extern_symbol(ctx, name);
    zfree(name);
}

static void try_parse_c_struct_decl(ParserContext *ctx, const char *line)
{
    const char *p = line;
    while (*p && isspace(*p))
    {
        p++;
    }

    if (*p == '#' || *p == '/' || *p == '*' || *p == '\0')
    {
        return;
    }

    int is_typedef = 0;
    int is_union = 0;

    if (strncmp(p, "typedef", 7) == 0 && !isalnum(p[7]) && p[7] != '_')
    {
        is_typedef = 1;
        p += 7;
        while (*p && isspace(*p))
        {
            p++;
        }
    }

    if (strncmp(p, "struct", 6) == 0 && !isalnum(p[6]) && p[6] != '_')
    {
        p += 6;
    }
    else if (strncmp(p, "union", 5) == 0 && !isalnum(p[5]) && p[5] != '_')
    {
        p += 5;
        is_union = 1;
    }
    else if (is_typedef)
    {
        return;
    }
    else
    {
        if (*p == '}')
        {
            p++;
            while (*p && isspace(*p))
            {
                p++;
            }
            const char *name_start = p;
            while (*p && (isalnum(*p) || *p == '_'))
            {
                p++;
            }
            int name_len = (int)(p - name_start);
            while (*p && isspace(*p))
            {
                p++;
            }
            if (name_len > 0 && *p == ';')
            {
                char *name = xmalloc((size_t)(name_len + 1));
                strncpy(name, name_start, (size_t)(name_len));
                name[name_len] = '\0';
                register_type_alias(ctx, name, name, NULL, 1, NULL, TOKEN_UNKNOWN, 0);
                register_extern_symbol(ctx, name);
                zfree(name);
            }
        }
        return;
    }

    while (*p && isspace(*p))
    {
        p++;
    }

    const char *tag_start = p;
    while (*p && (isalnum(*p) || *p == '_'))
    {
        p++;
    }
    int tag_len = (int)(p - tag_start);

    if (tag_len <= 0)
    {
        return;
    }

    char *tag_name = xmalloc((size_t)((size_t)(tag_len) + 1));
    strncpy(tag_name, tag_start, (size_t)(tag_len));
    tag_name[tag_len] = '\0';

    while (*p && isspace(*p))
    {
        p++;
    }

    if (*p == '{' || *p == ';')
    {
        const char *c_keyword = is_union ? "union" : "struct";
        char *c_type = xmalloc((size_t)(strlen(c_keyword) + 1 + (size_t)(tag_len) + 1));
        sprintf(c_type, "%s %s", c_keyword, tag_name); /* safe */
        register_type_alias(ctx, tag_name, c_type, NULL, 1, NULL, TOKEN_UNKNOWN, 0);
        register_extern_symbol(ctx, tag_name);
        zfree(c_type);
    }

    zfree(tag_name);
}

static void scan_c_header_contents(ParserContext *ctx, const char *path, int depth)
{
    if (depth > 16)
    {
        return;
    }

    if (is_file_imported(ctx, path))
    {
        return;
    }
    mark_file_imported(ctx, path);

    char *src = load_file(path, ctx->current_filename);
    if (!src)
    {
        return;
    }

    char header_dir[MAX_PATH_LEN];
    header_dir[0] = 0;
    const char *last_slash = z_path_last_sep(path);
    if (last_slash)
    {
        int dir_len = (int)(last_slash - path);
        if (dir_len >= (int)sizeof(header_dir))
        {
            dir_len = (int)sizeof(header_dir) - 1;
        }
        strncpy(header_dir, path, (size_t)(dir_len));
        header_dir[dir_len] = 0;
    }

    char *ptr = src;
    while (*ptr)
    {
        char *line_start = ptr;
        char *line_end = ptr;
        while (*line_end)
        {
            if (*line_end == '\n')
            {
                if (line_end > line_start && *(line_end - 1) == '\\')
                {
                    line_end++;
                    continue;
                }
                break;
            }
            line_end++;
        }

        ptrdiff_t len = line_end - line_start;
        if (len > 0)
        {
            char stack_buf[4096];
            char *line_buf = stack_buf;
            int heap_alloced = 0;
            if (len >= (int)sizeof(stack_buf))
            {
                line_buf = xmalloc((size_t)(len + 1));
                heap_alloced = 1;
            }
            memcpy(line_buf, line_start, (size_t)(len));
            line_buf[len] = 0;

            char *p = line_buf;
            while (*p && isspace(*p))
            {
                p++;
            }
            if (*p == '#')
            {
                try_parse_macro_const(ctx, line_buf);

                const char *inc = p + 1;
                while (*inc && isspace(*inc))
                {
                    inc++;
                }
                if (strncmp(inc, "include", 7) == 0 && !isalnum(inc[7]) && inc[7] != '_')
                {
                    inc += 7;
                    while (*inc && isspace(*inc))
                    {
                        inc++;
                    }
                    if (*inc == '"')
                    {
                        inc++;
                        const char *end_quote = (char *)strchr(inc, '"');
                        if (end_quote && end_quote > inc)
                        {
                            int inc_len = (int)(end_quote - inc);
                            if (inc_len > 255)
                            {
                                inc_len = 255;
                            }
                            char inc_name[MAX_VAR_NAME_LEN];
                            memcpy(inc_name, inc, (size_t)(inc_len));
                            inc_name[inc_len] = '\0';

                            char nested_path[MAX_PATH_LEN * 2];
                            if (header_dir[0])
                            {
                                snprintf(nested_path, sizeof(nested_path), "%s/%s", header_dir,
                                         inc_name);
                            }
                            else
                            {
                                snprintf(nested_path, sizeof(nested_path), "%s", inc_name);
                            }

                            scan_c_header_contents(ctx, nested_path, depth + 1);
                        }
                    }
                }
            }
            else
            {
                try_parse_c_function_decl(ctx, line_buf);
                try_parse_c_struct_decl(ctx, line_buf);
            }
            if (heap_alloced)
            {
                zfree(line_buf);
            }
        }

        ptr = line_end;
        if (*ptr == '\n')
        {
            ptr++;
        }
    }
    zfree(src);
}

ASTNode *parse_include(ParserContext *ctx, Lexer *l)
{
    lexer_next(l);
    Token t = lexer_next(l);
    char *path = NULL;
    int is_system = 0;

    if (t.kind == TOK_LANGLE)
    {
        is_system = 1;
        char buf[MAX_SHORT_MSG_LEN];
        buf[0] = 0;
        while (1)
        {
            Token i = lexer_next(l);
            if (i.kind == TOK_EOF)
            {
                zpanic_at(i, "Unexpected EOF in include path, expected '>'");
                break;
            }
            if (i.kind == TOK_RANGLE)
            {
                break;
            }
            strncat(buf, i.start, i.len);
        }
        path = xstrdup(buf);

        ctx->has_external_includes = 1;
    }
    else
    {
        is_system = 0;
        path = token_get_string_content(t);
    }

    ASTNode *n = ast_create(NODE_INCLUDE);
    n->include.path = path;
    n->include.is_system = is_system;

    if (!is_system && path)
    {
        scan_c_header_contents(ctx, path, 0);
    }

    return n;
}

ASTNode *parse_import(ParserContext *ctx, Lexer *l, int is_re_export)
{
    lexer_next(l);

    Token next = lexer_peek(l);
    if (next.kind == TOK_IDENT && next.len == 6 && strncmp(next.start, "plugin", 6) == 0)
    {
        lexer_next(l);

        Token plugin_tok = lexer_next(l);
        if (plugin_tok.kind != TOK_STRING)
        {
            zpanic_at(plugin_tok, "Expected string literal after 'import plugin'");
            return NULL;
        }

        char *plugin_name = token_get_string_content(plugin_tok);

        if (plugin_name[0] == '.' &&
            (plugin_name[1] == '/' || (plugin_name[1] == '.' && plugin_name[2] == '/')))
        {
            char *current_dir = xstrdup(ctx->current_filename);
            char *last_slash = z_path_last_sep(current_dir);
            if (last_slash)
            {
                *last_slash = 0;
                char resolved_path[MAX_PATH_LEN];
                snprintf(resolved_path, sizeof(resolved_path), "%s/%s", current_dir, plugin_name);
                zfree(plugin_name);
                plugin_name = xstrdup(resolved_path);
            }
            zfree(current_dir);
        }

        char *alias = NULL;
        Token as_tok = lexer_peek(l);
        if (as_tok.kind == TOK_IDENT && as_tok.len == 2 && strncmp(as_tok.start, "as", 2) == 0)
        {
            lexer_next(l);
            Token alias_tok = lexer_next(l);
            if (alias_tok.kind != TOK_IDENT)
            {
                zpanic_at(alias_tok, "Expected identifier after 'as'");
                return NULL;
            }
            alias = token_strdup(alias_tok);
        }

#if ZC_HAS_PLUGINS
        register_plugin(ctx, plugin_name, alias);
#endif

        if (lexer_peek(l).kind == TOK_SEMICOLON)
        {
            lexer_next(l);
        }

        return NULL;
    }

    int is_selective = 0;
    zvec_Str symbols = {0};
    zvec_Str aliases = {0};

    if (lexer_peek(l).kind == TOK_LBRACE)
    {
        is_selective = 1;
        lexer_next(l);

        while (1)
        {
            if (lexer_peek(l).kind == TOK_RBRACE)
            {
                break;
            }
            if (lexer_peek(l).kind == TOK_EOF)
            {
                zpanic_at(lexer_peek(l), "Unexpected end of file in selective import");
                break;
            }
            if (symbols.length > 0 && lexer_peek(l).kind == TOK_COMMA)
            {
                lexer_next(l);
            }

            Token sym_tok = lexer_next(l);
            if (sym_tok.kind != TOK_IDENT)
            {
                zpanic_at(sym_tok, "Expected identifier in selective import");
                for (size_t _c = 0; _c < symbols.length; _c++)
                {
                    zfree(symbols.data[_c]);
                    if (aliases.data[_c])
                    {
                        zfree(aliases.data[_c]);
                    }
                }
                zvec_free_Str(&symbols);
                zvec_free_Str(&aliases);
                return NULL;
            }

            char *sym = xmalloc(sym_tok.len + 1);
            strncpy(sym, sym_tok.start, sym_tok.len);
            sym[sym_tok.len] = 0;
            zvec_push_Str(&symbols, sym);

            Token inner_next = lexer_peek(l);
            if (inner_next.kind == TOK_IDENT && inner_next.len == 2 &&
                strncmp(inner_next.start, "as", 2) == 0)
            {
                lexer_next(l);
                Token alias_tok = lexer_next(l);
                if (alias_tok.kind != TOK_IDENT)
                {
                    zpanic_at(alias_tok, "Expected identifier after 'as'");
                    for (size_t _c = 0; _c < symbols.length; _c++)
                    {
                        zfree(symbols.data[_c]);
                        if (aliases.data[_c])
                        {
                            zfree(aliases.data[_c]);
                        }
                    }
                    zvec_free_Str(&symbols);
                    zvec_free_Str(&aliases);
                    return NULL;
                }

                char *als = xmalloc(alias_tok.len + 1);
                strncpy(als, alias_tok.start, alias_tok.len);
                als[alias_tok.len] = 0;
                zvec_push_Str(&aliases, als);
            }
            else
            {
                zvec_push_Str(&aliases, NULL);
            }
        }

        lexer_next(l);

        Token from_tok = lexer_next(l);
        if (from_tok.kind != TOK_IDENT || from_tok.len != 4 ||
            strncmp(from_tok.start, "from", 4) != 0)
        {
            zpanic_at(from_tok, "Expected 'from' after selective import list, got type=%d",
                      (int)from_tok.kind);
            for (size_t _c = 0; _c < symbols.length; _c++)
            {
                zfree(symbols.data[_c]);
                if (aliases.data[_c])
                {
                    zfree(aliases.data[_c]);
                }
            }
            zvec_free_Str(&symbols);
            zvec_free_Str(&aliases);
            return NULL;
        }
    }

    Token t = lexer_next(l);
    if (t.kind != TOK_STRING && t.kind != TOK_RAW_STRING)
    {
        zpanic_at(t, "Expected string (filename) after 'from' in selective import, got type %d",
                  (int)t.kind);
        for (size_t _c = 0; _c < symbols.length; _c++)
        {
            zfree(symbols.data[_c]);
            if (aliases.data[_c])
            {
                zfree(aliases.data[_c]);
            }
        }
        zvec_free_Str(&symbols);
        zvec_free_Str(&aliases);
        return NULL;
    }
    char *fn = token_get_string_content(t);

    char *resolved = z_resolve_path(fn, ctx->current_filename, ctx->config);
    if (!resolved)
    {
        if (strlen(fn) > 2 && strcmp(fn + strlen(fn) - 2, ".h") == 0)
        {
            resolved = xstrdup(fn);
        }
        else
        {
            zpanic_at(t, "Could not find module: %s", fn);
            for (size_t _c = 0; _c < symbols.length; _c++)
            {
                zfree(symbols.data[_c]);
                if (aliases.data[_c])
                {
                    zfree(aliases.data[_c]);
                }
            }
            zvec_free_Str(&symbols);
            zvec_free_Str(&aliases);
            zfree(fn);
            return NULL;
        }
    }
    zfree(fn);
    fn = resolved;

    if (!fn)
    {
        ASTNode *dummy = ast_create(NODE_BLOCK);
        dummy->block.statements = NULL;
        return dummy;
    }

    if (is_file_imported(ctx, fn))
    {
        for (size_t _c = 0; _c < symbols.length; _c++)
        {
            zfree(symbols.data[_c]);
            if (aliases.data[_c])
            {
                zfree(aliases.data[_c]);
            }
        }
        zvec_free_Str(&symbols);
        zvec_free_Str(&aliases);
        zfree(fn);
        return NULL;
    }

    if (zmap_get(&ctx->imports.currently_parsing, fn))
    {
        zpanic_at(t, "Circular import detected: '%s'", fn);
        for (size_t _c = 0; _c < symbols.length; _c++)
        {
            zfree(symbols.data[_c]);
            if (aliases.data[_c])
            {
                zfree(aliases.data[_c]);
            }
        }
        zvec_free_Str(&symbols);
        zvec_free_Str(&aliases);
        zfree(fn);
        return NULL;
    }
    zmap_put(&ctx->imports.currently_parsing, fn, fn);

    char *module_base_name = NULL;
    if (is_selective)
    {
        module_base_name = extract_module_name(fn);
        for (size_t i = 0; i < symbols.length; i++)
        {
            register_selective_import(ctx, symbols.data[i], aliases.data[i], module_base_name);
        }
    }

    char *alias = NULL;
    int is_wildcard = 0;
    if (!is_selective)
    {
        Token next_tok = lexer_peek(l);
        if (next_tok.kind == TOK_IDENT && next_tok.len == 2 &&
            strncmp(next_tok.start, "as", 2) == 0)
        {
            lexer_next(l);
            Token alias_tok = lexer_next(l);
            if (alias_tok.kind != TOK_IDENT && alias_tok.kind != TOK_OP)
            {
                zpanic_at(alias_tok, "Expected identifier after 'as'");
                return NULL;
            }

            if (alias_tok.len == 1 && *alias_tok.start == '*')
            {
                is_wildcard = 1;
            }
            else
            {
                alias = xmalloc(alias_tok.len + 1);
                strncpy(alias, alias_tok.start, alias_tok.len);
                alias[alias_tok.len] = 0;
            }

            if (!is_wildcard)
            {
                int is_header = 0;
                if (strlen(fn) > 2 && strcmp(fn + strlen(fn) - 2, ".h") == 0)
                {
                    is_header = 1;
                }

                if (!zmap_get(&ctx->imports.modules, alias))
                {
                    char *mod_base = extract_module_name(fn);
                    Module *m = xmalloc(sizeof(Module));
                    m->alias = xstrdup(alias);
                    m->path = xstrdup(fn);
                    m->base_name = mod_base;
                    m->is_c_header = is_header;
                    m->is_re_export = is_re_export;
                    zmap_put(&ctx->imports.modules, alias, m);
                }
            }
        }
    }

    if (strlen(fn) > 2 && strcmp(fn + strlen(fn) - 2, ".h") == 0)
    {
        // Headers are turned into NODE_INCLUDE without parsing their contents,
        // so they must be released from the cycle-tracking set here (the
        // currently_parsing removal below is only reached for .zc modules).
        // Otherwise a second import of the same header (e.g. via a std module)
        // would be misreported as a circular import.
        zmap_remove(&ctx->imports.currently_parsing, fn);
        mark_file_imported(ctx, fn);
        ASTNode *n = ast_create(NODE_INCLUDE);
        n->include.path = xstrdup(fn);
        n->include.is_system = 0;
        for (size_t _c = 0; _c < symbols.length; _c++)
        {
            zfree(symbols.data[_c]);
            if (aliases.data[_c])
            {
                zfree(aliases.data[_c]);
            }
        }
        zvec_free_Str(&symbols);
        zvec_free_Str(&aliases);
        return n;
    }

    char *src = load_file(fn, ctx->current_filename);
    if (!src)
    {
        if (!src)
        {
            if (ctx->config->mode_lsp)
            {
                zwarn_at(t, "LSP: Import not found: %s", fn);
                for (size_t _c = 0; _c < symbols.length; _c++)
                {
                    zfree(symbols.data[_c]);
                    if (aliases.data[_c])
                    {
                        zfree(aliases.data[_c]);
                    }
                }
                zvec_free_Str(&symbols);
                zvec_free_Str(&aliases);
                ASTNode *dummy = ast_create(NODE_BLOCK);
                dummy->block.statements = NULL;
                return dummy;
            }
            zpanic_at(t, "Not found: %s", fn);
            return NULL;
        }
    }

    Lexer i;
    lexer_init(&i, src, ctx->config, ctx->current_filename);

    char *prev_module_prefix = ctx->imports.current_module_prefix;
    char *temp_module_prefix = NULL;

    if (alias)
    {
        temp_module_prefix = extract_module_name(fn);
        ctx->imports.current_module_prefix = temp_module_prefix;
    }
    else if (is_selective || is_wildcard)
    {
        temp_module_prefix = extract_module_name(fn);
        ctx->imports.current_module_prefix = temp_module_prefix;
    }
    else
    {
        temp_module_prefix = NULL;
        ctx->imports.current_module_prefix = NULL;

        char *mod_base = extract_module_name(fn);
        if (!zmap_get(&ctx->imports.modules, mod_base))
        {
            Module *m = xmalloc(sizeof(Module));
            m->alias = mod_base;
            m->path = xstrdup(fn);
            m->base_name = mod_base;
            m->is_c_header = 0;
            m->is_re_export = is_re_export;
            zmap_put(&ctx->imports.modules, m->alias, m);
        }
    }

    if (is_re_export && alias && prev_module_prefix)
    {
        char *prop_base = extract_module_name(fn);
        re_export_propagated(ctx, alias, prev_module_prefix, prop_base);
        zfree(prop_base);
    }

    const char *saved_fn = ctx->current_filename;
    ctx->current_filename = fn;

    ASTNode *import_node = ast_create(NODE_IMPORT);
    skip_comments(&i);
    ATTACH_DOC_COMMENT(ctx, import_node);

    ASTNode *r = parse_program_nodes(ctx, &i);

    zmap_remove(&ctx->imports.currently_parsing, fn);
    mark_file_imported(ctx, fn);

    if (is_wildcard && temp_module_prefix)
    {
        re_export_wildcard_symbols(ctx, temp_module_prefix);
        zmap_put(&ctx->imports.wildcard_imports, temp_module_prefix, temp_module_prefix);
    }

    ctx->current_filename = (char *)saved_fn;

    ctx->imports.current_module_prefix = prev_module_prefix;
    if (temp_module_prefix)
    {
        zfree(temp_module_prefix);
    }

    for (size_t k = 0; k < symbols.length; k++)
    {
        zfree(symbols.data[k]);
        if (aliases.data[k])
        {
            zfree(aliases.data[k]);
        }
    }
    zvec_free_Str(&symbols);
    zvec_free_Str(&aliases);

    if (alias)
    {
        zfree(alias);
    }

    if (module_base_name)
    {
        zfree(module_base_name);
    }

    import_node->token = t;
    import_node->import_stmt.path = xstrdup(fn);
    import_node->import_stmt.module_root = r;

    return import_node;
}
