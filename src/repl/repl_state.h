// SPDX-License-Identifier: MIT
/**
#ifndef ZC_ALLOW_INTERNAL
#error "repl/repl_state.h is internal to Zen C. Include the appropriate public header instead."
#endif

 * @file repl_state.h
 * @brief Shared state and types for the REPL subsystem.
 *
 * This is the internal header used by repl_*.c modules.
 * The public API remains in repl.h.
 */

#ifndef REPL_STATE_H
#define REPL_STATE_H

#include "repl.h"
#include "ast.h"
#include "parser/parser.h"
#include "../platform/os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../utils/cJSON.h"
#include "constants.h"

/* Forward declaration */

#define REPL_MAX_WATCHES 16
#define REPL_MAX_SYMBOLS 512

#define REPL_HANDLED 0  /**< Command was handled, continue loop.     */
#define REPL_QUIT 1     /**< User requested exit.                    */
#define REPL_UNKNOWN -1 /**< Not a recognized command.               */

typedef struct
{
    char *name;
    char *doc;
} ReplDoc;

typedef struct
{
    /* History */
    char **history;
    int history_len;
    int history_cap;

    /* Watches */
    char *watches[REPL_MAX_WATCHES];
    int watches_len;

    /* Persistence */
    char history_path[MAX_PATH_LEN];

    /* Compiler self-path (for subprocess re-invocations) */
    const char *self_path;

    /* Session symbols for tab completion */
    char **symbols;
    int symbol_count;
    int symbol_cap;

    /* Documentation database (lazy-loaded) */
    ReplDoc *docs;
    int doc_count;

    int aborted; /**< Flag set if user hit Ctrl+C to abort line. */

    /* Compiler configuration for JIT and subprocess calls */
    struct CompilerConfig *config;
} ReplState;

typedef struct CompilerConfig CompilerConfig;

typedef struct
{
    const char *name; /**< Command name without leading ':'.        */
    const char *help; /**< One-line description for :help.          */
    int takes_arg;    /**< 1 if command expects an argument.        */
    int (*handler)(ReplState *state, const char *args);
} ReplCommand;

void repl_highlight(const char *buf, int cursor_pos);
int get_visible_length(const char *str);

char *repl_readline(ReplState *state, const char *prompt, int indent_level);
char *repl_complete(ReplState *state, const char *buf, int pos);

int is_header_line(const char *line);
void repl_parser_ctx_init(ParserContext *ctx);
int is_definition_of(const char *code, const char *name);
int is_command(const char *buf, const char *cmd);
void repl_get_code(char **history, int len, char **out_global, char **out_main);
void repl_error_callback(void *data, Token t, const char *msg);
void repl_load_docs(ReplState *state);
const ReplDoc *repl_find_doc(ReplState *state, const char *name);
void repl_update_symbols(ReplState *state);
void repl_extract_c_code(const char *filename);
char *repl_generate_plot_code(const char *expr);
char *repl_transpile(const char *zen_c_code);

int repl_dispatch_command(ReplState *state, const char *cmd_buf);
void repl_print_help(void);

void repl_state_init(ReplState *state, const char *self_path, CompilerConfig *cfg);
void repl_state_free(ReplState *state);
void repl_save_history(ReplState *state);
void repl_history_add(ReplState *state, const char *line);

#endif /* REPL_STATE_H */
