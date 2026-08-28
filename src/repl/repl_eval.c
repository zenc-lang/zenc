// SPDX-License-Identifier: MIT
/**
 * @file repl_eval.c
 * @brief Evaluation helpers: code synthesis, header detection, documentation,
 *        session symbol extraction, and error callbacks.
 */

#include "repl_state.h"
#include "codegen/codegen.h"

/* Common setup for every ParserContext the REPL creates.
 * The parser dereferences ctx->config (e.g. ctx->config->keep_comments) and
 * the global token/diagnostic contexts during parsing; without these the
 * REPL segfaults on any input. */
void repl_parser_ctx_init(ParserContext *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->config = &g_compiler.config;
    ctx->current_filename = "<repl>";
    ctx->cg.is_repl = 1;
    ctx->is_fault_tolerant = 1;
    ctx->on_error = repl_error_callback;
    module_state_init(&ctx->imports);
    token_set_parser_ctx(ctx);
    diag_set_parser_ctx(ctx);
}

typedef struct
{
    int count;
} ReplErrorCounter;

static void repl_counting_error_callback(void *data, Token t, const char *msg)
{
    ReplErrorCounter *c = (ReplErrorCounter *)data;
    if (c)
    {
        c->count++;
    }
    else
    {
        repl_error_callback(NULL, t, msg);
    }
}

char *repl_transpile(const char *zen_c_code)
{
    ParserContext ctx;
    repl_parser_ctx_init(&ctx);
    ctx.cg.skip_preamble = 0;

    /* Track parser errors via the callback: zpanic_at/zpanic_with_* all
     * invoke on_error in fault-tolerant mode, and parse_program resets
     * had_error during recovery, so a callback counter is the reliable
     * signal for "this program failed to parse". */
    ReplErrorCounter errs = {0};
    ctx.on_error = repl_counting_error_callback;
    ctx.error_callback_data = &errs;

    Lexer lex;
    lexer_init(&lex, zen_c_code, &g_compiler.config, ctx.current_filename);

    ASTNode *root = parse_program(&ctx, &lex);
    if (!root || errs.count > 0)
    {
        return NULL;
    }

    emitter_init_buffer(&ctx.cg.emitter);
    codegen_node(&ctx, root);
    return emitter_take_string(&ctx.cg.emitter);
}

int is_header_line(const char *line)
{
    while (*line && (*line == ' ' || *line == '\t'))
    {
        line++;
    }
    /* Preprocessor directives (e.g. #include, #define) only make sense at
     * global scope. */
    if (*line == '#')
    {
        return 1;
    }
    static const char *const HEADER_KEYWORDS[] = {"struct",  "impl",  "fn",    "use", "include",
                                                  "typedef", "enum",  "const", "def", "import",
                                                  "trait",   "alias", "extern"};
    for (size_t i = 0; i < sizeof(HEADER_KEYWORDS) / sizeof(HEADER_KEYWORDS[0]); i++)
    {
        size_t kw_len = strlen(HEADER_KEYWORDS[i]);
        if (strncmp(line, HEADER_KEYWORDS[i], kw_len) == 0)
        {
            /* Require a word boundary so identifiers that merely start with a
             * keyword (e.g. "fneg(x)", "user_name = ...") are not routed to
             * global scope. */
            char c = line[kw_len];
            if (c == 0 || !(isalnum((unsigned char)c) || c == '_'))
            {
                return 1;
            }
        }
    }
    return 0;
}

void repl_error_callback(void *data, Token t, const char *msg)
{
    (void)data;
    (void)t;
    fprintf(stderr, "\033[1;31merror:\033[0m %s\n", msg);
}

int is_definition_of(const char *code, const char *name)
{
    Lexer l;
    lexer_init(&l, code, &g_compiler.config, "<repl>");
    Token t = lexer_next(&l);
    int is_header = 0;
    if (t.kind == TOK_UNION)
    {
        is_header = 1;
    }
    else if (t.kind == TOK_IDENT)
    {
        if ((t.len == 2 && strncmp(t.start, "fn", 2) == 0) ||
            (t.len == 6 && strncmp(t.start, "struct", 6) == 0) ||
            (t.len == 4 && strncmp(t.start, "enum", 4) == 0) ||
            (t.len == 7 && strncmp(t.start, "typedef", 7) == 0) ||
            (t.len == 5 && strncmp(t.start, "const", 5) == 0))
        {
            is_header = 1;
        }
    }
    if (is_header)
    {
        Token name_tok = lexer_next(&l);
        if (name_tok.kind == TOK_IDENT && strlen(name) == (size_t)name_tok.len &&
            strncmp(name, name_tok.start, name_tok.len) == 0)
        {
            return 1;
        }
    }
    return 0;
}

int is_command(const char *buf, const char *cmd)
{
    if (buf[0] != ':')
    {
        return 0;
    }
    size_t cmd_len = strlen(cmd);
    if (strncmp(buf + 1, cmd, (size_t)(cmd_len)) != 0)
    {
        return 0;
    }
    char next = buf[1 + cmd_len];
    return next == 0 || isspace(next);
}

void repl_get_code(char **history, int len, char **out_global, char **out_main)
{
    size_t total_len = 0;
    for (int i = 0; i < len; i++)
    {
        total_len += strlen(history[i]) + 2;
    }
    char *global_buf = malloc(total_len + 1);
    char *main_buf = malloc(total_len + 1);
    global_buf[0] = 0;
    main_buf[0] = 0;
    int brace_depth = 0;
    for (int i = 0; i < len; i++)
    {
        char *line = history[i];
        if (brace_depth == 0 && is_header_line(line))
        {
            strcat(global_buf, line);
            strcat(global_buf, "\n");
        }
        else
        {
            strcat(main_buf, line);
            strcat(main_buf, " ");
        }
        int in_quote = 0;
        for (char *p = line; *p; p++)
        {
            if (*p == '"')
            {
                in_quote = !in_quote;
            }
            else if (!in_quote)
            {
                if (*p == '{')
                {
                    brace_depth++;
                }
                else if (*p == '}')
                {
                    brace_depth--;
                }
            }
        }
    }
    *out_global = global_buf;
    *out_main = main_buf;
}

void repl_load_docs(ReplState *state)
{
    if (state->docs)
    {
        return;
    }
    const char *search_paths[] = {"src/repl/docs.json", "docs.json", NULL};
    FILE *f = NULL;
    for (int i = 0; search_paths[i]; i++)
    {
        f = fopen(search_paths[i], "r");
        if (f)
        {
            break;
        }
    }
    if (!f)
    {
        return;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0)
    {
        fclose(f);
        return;
    }
    fseek(f, 0, SEEK_SET);
    char *data = malloc((size_t)(len + 1));
    if (data)
    {
        size_t nread = fread(data, 1, (size_t)(len), f);
        if (nread != (size_t)(len))
        {
            zfree(data);
            data = NULL;
        }
        else
        {
            data[len] = 0;
        }
    }
    fclose(f);
    if (!data)
    {
        return;
    }
    cJSON *json = cJSON_Parse(data);
    zfree(data);
    if (!json)
    {
        return;
    }
    if (cJSON_IsArray(json))
    {
        // Free previously allocated docs before reloading
        for (int j = 0; j < state->doc_count; j++)
        {
            zfree(state->docs[j].name);
            zfree(state->docs[j].doc);
        }
        zfree(state->docs);
        state->doc_count = cJSON_GetArraySize(json);
        state->docs = calloc((size_t)(state->doc_count + 1), sizeof(ReplDoc));
        cJSON *item = NULL;
        int i = 0;
        cJSON_ArrayForEach(item, json)
        {
            cJSON *name = cJSON_GetObjectItem(item, "name");
            cJSON *doc = cJSON_GetObjectItem(item, "doc");
            if (cJSON_IsString(name))
            {
                state->docs[i].name = xstrdup(name->valuestring);
            }
            if (cJSON_IsString(doc))
            {
                state->docs[i].doc = xstrdup(doc->valuestring);
            }
            i++;
        }
    }
    cJSON_Delete(json);
}

const ReplDoc *repl_find_doc(ReplState *state, const char *name)
{
    repl_load_docs(state);
    if (!state->docs)
    {
        return NULL;
    }
    for (int i = 0; i < state->doc_count; i++)
    {
        if (state->docs[i].name && strcmp(name, state->docs[i].name) == 0)
        {
            return &state->docs[i];
        }
    }
    return NULL;
}

static void repl_add_symbol(ReplState *state, const char *name)
{
    if (!name || !name[0])
    {
        return;
    }
    for (int i = 0; i < state->symbol_count; i++)
    {
        if (strcmp(state->symbols[i], name) == 0)
        {
            return;
        }
    }
    if (state->symbol_count >= state->symbol_cap)
    {
        state->symbol_cap = state->symbol_cap ? state->symbol_cap * 2 : 64;
        state->symbols = realloc(state->symbols, (size_t)(state->symbol_cap) * sizeof(char *));
    }
    state->symbols[state->symbol_count++] = xstrdup(name);
}

void repl_update_symbols(ReplState *state)
{
    for (int i = 0; i < state->symbol_count; i++)
    {
        zfree(state->symbols[i]);
    }
    state->symbol_count = 0;
    static const char *STDLIB_TYPES[] = {"Vec", "String", "Map", "Set", "Slice", "Regex", NULL};
    for (int i = 0; STDLIB_TYPES[i]; i++)
    {
        repl_add_symbol(state, STDLIB_TYPES[i]);
    }

    if (state->history_len == 0)
    {
        return;
    }
    char *global_code = NULL;
    char *main_code = NULL;
    repl_get_code(state->history, state->history_len, &global_code, &main_code);
    size_t sz = strlen(global_code) + strlen(main_code) + 128;
    char *code = malloc(sz);
    snprintf(code, sz, "%s\nfn main() { %s }", global_code, main_code);
    zfree(global_code);
    zfree(main_code);
    ParserContext ctx;
    repl_parser_ctx_init(&ctx);
    ctx.cg.skip_preamble = 1;
    ctx.is_fault_tolerant = 1;
    Lexer lex;
    lexer_init(&lex, code, &g_compiler.config, ctx.current_filename);
    ASTNode *nodes = parse_program(&ctx, &lex);
    ASTNode *search = (nodes && nodes->kind == NODE_ROOT) ? nodes->root.children : nodes;
    for (ASTNode *n = search; n; n = n->next)
    {
        if (n->kind == NODE_FUNCTION)
        {
            repl_add_symbol(state, n->func.name);
        }
        else if (n->kind == NODE_STRUCT)
        {
            repl_add_symbol(state, n->strct.name);
        }
    }
    zfree(code);
}

void repl_extract_c_code(const char *filename)
{
    FILE *f = fopen(filename, "r");
    if (!f)
    {
        return;
    }
    char buf[4096];
    int in_main = 0, br = 0, lines = 0;
    while (fgets(buf, sizeof(buf), f))
    {
        if (!in_main)
        {
            if (strstr(buf, "int main() {") || strstr(buf, "int main(void)"))
            {
                in_main = 1;
                br = 1;
                continue;
            }
        }
        else
        {
            for (int i = 0; buf[i]; i++)
            {
                if (buf[i] == '{')
                {
                    br++;
                }
                else if (buf[i] == '}')
                {
                    br--;
                }
            }
            if (br == 0)
            {
                break;
            }
            lines++;
            if (lines <= 2)
            {
                continue;
            }
            const char *start = (strncmp(buf, "    ", 4) == 0) ? buf + 4 : buf;
            if (!strstr(start, "return 0;"))
            {
                printf("  %s", start);
            }
        }
    }
    fclose(f);
}

char *repl_generate_plot_code(const char *expr)
{
    size_t sz = strlen(expr) + 3072;
    char *code = malloc(sz);
    snprintf(code, sz,
             "{\n    let _z_data = %s;\n    let _z_max = 1;\n    let _z_count = 0;\n    for val in "
             "_z_data {\n        if val > _z_max { _z_max = val; }\n        _z_count = _z_count + "
             "1;\n    }\n    println \"\\n  Visualizing: %s ({_z_count} values)\";\n    println \" "
             " ----------------------------------------\";\n    for val in _z_data {\n        let "
             "bar_len = (val * 40) / _z_max;\n        print \"  {val} | \";\n        for j in "
             "0..bar_len { print \"#\"; }\n        println \"\";\n    }\n    println \"  "
             "----------------------------------------\\n\";\n}\n",
             expr, expr);

    return code;
}
