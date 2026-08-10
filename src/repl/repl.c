// SPDX-License-Identifier: MIT
/**
 * @file repl.c
 * @brief Main entry point and orchestration for the Zen C REPL.
 */

#include "repl_state.h"
#include "repl.h"
#include "repl_jit.h"
#include <ctype.h>

static int repl_process_line(ReplState *state, char *line_buf, int *brace_depth, int *paren_depth,
                             int *in_quote, int *escaped, char **input_buffer, size_t *input_len);

static const char *get_home(void)
{
    const char *h = getenv("HOME");
#if ZC_OS_WINDOWS
    if (!h)
    {
        h = getenv("USERPROFILE");
    }
#endif
    return h;
}

void repl_state_init(ReplState *state, const char *self_path, CompilerConfig *cfg)
{
    memset(state, 0, sizeof(*state));
    state->self_path = self_path;
    state->history_cap = 64;
    state->history = malloc((size_t)(state->history_cap) * sizeof(char *));
    state->history_len = 0;

    state->config = cfg;

    /* Ensure codegen knows we are using TCC for JIT */
    strncpy(cfg->cc, "tcc", sizeof(cfg->cc) - 1);
    cfg->cc[sizeof(cfg->cc) - 1] = '\0';

    const char *home = get_home();
    if (home)
    {
        snprintf(state->history_path, sizeof(state->history_path), "%s/.zenc_history", home);
    }
}

void repl_state_free(ReplState *state)
{
    for (int i = 0; i < state->history_len; i++)
    {
        zfree(state->history[i]);
    }
    zfree(state->history);

    for (int i = 0; i < state->watches_len; i++)
    {
        zfree(state->watches[i]);
    }

    for (int i = 0; i < state->symbol_count; i++)
    {
        zfree(state->symbols[i]);
    }
    zfree(state->symbols);
}

void repl_history_add(ReplState *state, const char *line)
{
    if (state->history_len >= state->history_cap)
    {
        state->history_cap *= 2;
        state->history = realloc(state->history, (size_t)(state->history_cap) * sizeof(char *));
    }
    state->history[state->history_len++] = xstrdup(line);
}

static void repl_load_history(ReplState *state)
{
    if (!state->history_path[0])
    {
        return;
    }
    FILE *hf = fopen(state->history_path, "r");
    if (!hf)
    {
        return;
    }
    char buf[MAX_ERROR_MSG_LEN];
    while (fgets(buf, sizeof(buf), hf))
    {
        size_t l = strlen(buf);
        if (l > 0 && buf[l - 1] == '\n')
        {
            buf[--l] = 0;
        }
        if (l > 0)
        {
            repl_history_add(state, buf);
        }
    }
    fclose(hf);
}

void repl_save_history(ReplState *state)
{
    if (!state->history_path[0])
    {
        return;
    }
    FILE *hf = fopen(state->history_path, "w");
    if (!hf)
    {
        return;
    }
    for (int i = 0; i < state->history_len; i++)
    {
        fprintf(hf, "%s\n", state->history[i]);
    }
    fclose(hf);
}

static void repl_load_init_script(ReplState *state)
{
    const char *home = get_home();
    if (!home)
    {
        return;
    }
    char path[MAX_PATH_SIZE];
    snprintf(path, sizeof(path), "%s/.zencrc", home);
    FILE *f = fopen(path, "r");
    if (!f)
    {
        return;
    }

    printf("Loading profile from %s...\n", path);
    char buf[MAX_ERROR_MSG_LEN];
    int b = 0, p = 0, q = 0, e = 0;
    char *ibuf = NULL;
    size_t ilen = 0;
    while (fgets(buf, sizeof(buf), f))
    {
        size_t l = strlen(buf);
        if (l > 0 && buf[l - 1] == '\n')
        {
            buf[--l] = 0;
        }
        repl_process_line(state, buf, &b, &p, &q, &e, &ibuf, &ilen);
    }
    fclose(f);
}

void run_repl(const char *self_path, int argc, char **argv)
{
    ReplState state;
    repl_state_init(&state, self_path, &g_compiler.config);
    repl_load_history(&state);

    char *cmd_to_run = NULL;
    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
        {
            cmd_to_run = argv[++i];
        }
    }

    if (cmd_to_run)
    {
        int b = 0, p = 0, q = 0, e = 0;
        char *buf = NULL;
        size_t blen = 0;
        repl_process_line(&state, cmd_to_run, &b, &p, &q, &e, &buf, &blen);
        repl_state_free(&state);
        return;
    }

    printf("\033[1;36mZen C REPL (%s)\033[0m\n", ZEN_VERSION);
    printf("Type 'exit' or 'quit' to leave.\n");
    printf("Type :help for commands.\n");

    repl_load_init_script(&state);

    int brace_depth = 0;
    int paren_depth = 0;
    int in_quote = 0;
    int escaped = 0;
    char *input_buffer = NULL;
    size_t input_len = 0;

    while (1)
    {
        const char *prompt = (brace_depth > 0 || paren_depth > 0) ? "      " : "zenc >>> ";
        char *line = repl_readline(&state, prompt, brace_depth);
        if (!line)
        {
            break;
        }

        int res = repl_process_line(&state, line, &brace_depth, &paren_depth, &in_quote, &escaped,
                                    &input_buffer, &input_len);
        zfree(line);
        if (res == REPL_QUIT)
        {
            break;
        }
    }

    repl_save_history(&state);
    repl_state_free(&state);
}

static int repl_process_line(ReplState *state, char *line_buf, int *brace_depth, int *paren_depth,
                             int *in_quote, int *escaped, char **input_buffer, size_t *input_len)
{
    /* Exit check */
    if (strcmp(line_buf, "exit") == 0 || strcmp(line_buf, "quit") == 0)
    {
        return REPL_QUIT;
    }

    /* Shell escape */
    if (line_buf[0] == '!')
    {
        int ret = system(line_buf + 1);
        (void)ret;
        return REPL_HANDLED;
    }

    /* Command dispatch */
    if (line_buf[0] == ':')
    {
        int result = repl_dispatch_command(state, line_buf);
        if (result == REPL_HANDLED || result == REPL_QUIT)
        {
            return result;
        }
    }

    /* Multi-line tracking */
    for (int i = 0; line_buf[i]; i++)
    {
        char c = line_buf[i];
        if (*escaped)
        {
            *escaped = 0;
            continue;
        }
        if (c == '\\')
        {
            *escaped = 1;
            continue;
        }
        if (c == '"')
        {
            *in_quote = !(*in_quote);
            continue;
        }
        if (!(*in_quote))
        {
            if (c == '{')
            {
                (*brace_depth)++;
            }
            else if (c == '}')
            {
                if (*brace_depth > 0)
                {
                    (*brace_depth)--;
                }
            }
            else if (c == '(')
            {
                (*paren_depth)++;
            }
            else if (c == ')')
            {
                if (*paren_depth > 0)
                {
                    (*paren_depth)--;
                }
            }
        }
    }

    size_t len = strlen(line_buf);
    *input_buffer = realloc(*input_buffer, *input_len + len + 2);
    memcpy(*input_buffer + *input_len, line_buf, (size_t)(len));
    (*input_len) += len;
    (*input_buffer)[(*input_len)++] = '\n';
    (*input_buffer)[*input_len] = '\0';

    if (*brace_depth > 0 || *paren_depth > 0)
    {
        return REPL_HANDLED;
    }

    if (*input_len > 0 && (*input_buffer)[*input_len - 1] == '\n')
    {
        (*input_buffer)[--(*input_len)] = 0;
    }
    if (*input_len == 0)
    {
        zfree(*input_buffer);
        *input_buffer = NULL;
        *input_len = 0;
        return REPL_HANDLED;
    }

    repl_history_add(state, *input_buffer);
    char *raw_input = xstrdup(*input_buffer);
    if (!raw_input)
    {
        zfree(*input_buffer);
        *input_buffer = NULL;
        *input_len = 0;
        return -1;
    }
    zfree(*input_buffer);
    *input_buffer = NULL;
    *input_len = 0;

    /* Synthesize program.
     * The current line is already in history. Header lines (fn/struct/etc.)
     * are reconstructed into global scope so they get compiled; everything
     * else is excluded from the session body here and appended exactly once
     * below (as a statement or auto-print), avoiding duplicate emission. */
    int cur_is_header = is_header_line(raw_input);
    char *global_code = NULL;
    char *main_code = NULL;
    repl_get_code(state->history, cur_is_header ? state->history_len : state->history_len - 1,
                  &global_code, &main_code);

    size_t total_size = strlen(global_code) + strlen(main_code) + strlen(raw_input) + 1024;
    char *full_code = malloc(total_size);
    if (!full_code)
    {
        state->history_len--;
        return REPL_HANDLED;
    }
    snprintf(full_code, total_size, "%s\nfn main() { _z_suppress_stdout(); %s", global_code,
             main_code);
    zfree(global_code);
    zfree(main_code);
    strcat(full_code, "_z_restore_stdout(); ");

    /* Auto-print detection */
    if (!is_header_line(raw_input))
    {
        char *check_buf = malloc(strlen(raw_input) + 2);
        sprintf(check_buf, "%s;", raw_input); /* TODO: check buffer size */

        ParserContext pctx;
        repl_parser_ctx_init(&pctx);
        pctx.cg.skip_preamble = 1;
        pctx.suppress_errors = 1;
        Lexer l;
        lexer_init(&l, check_buf, &g_compiler.config, pctx.current_filename);
        ASTNode *node = parse_statement(&pctx, &l);
        zfree(check_buf);

        if (node &&
            ((node->type >= NODE_EXPR_BINARY && node->type <= NODE_EXPR_SLICE) ||
             node->type == NODE_TERNARY || node->type == NODE_LAMBDA) &&
            /* Splicing quotes or braces into the "{...}" string literal would
             * produce malformed code; fall back to evaluating as a statement. */
            !strpbrk(raw_input, "\"{}"))
        {
            strcat(full_code, "println \"{");
            strcat(full_code, raw_input);
            strcat(full_code, "}\";");
        }
        else
        {
            strcat(full_code, raw_input);
        }
    }
    else
    {
        strcat(full_code, " ");
    }
    strcat(full_code, " }");

    /* Execution */
    char *c_code = repl_transpile(full_code);
    if (c_code)
    {
        if (repl_jit_execute(c_code, state->config) != 0)
        {
            /* zfree() is a no-op under the arena allocator, so the decrement
             * must happen explicitly rather than inside a zfree() argument. */
            state->history_len--;
        }
        else
        {
            repl_update_symbols(state);
        }
        zfree(c_code);
    }
    else
    {
        state->history_len--;
    }
    printf("\n");
    zfree(full_code);
    zfree(raw_input);
    return REPL_HANDLED;
}
