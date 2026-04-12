#include "cJSON.h"
#include "../constants.h"
#include "lsp_project.h" // Includes lsp_index.h, parser.h
#include "../plugins/plugin_manager.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct Diagnostic
{
    int line;
    int col;
    int len;
    int severity;
    int id;
    char *message;
    struct Diagnostic *next;
} Diagnostic;

typedef struct
{
    Diagnostic *head;
    Diagnostic *tail;
} DiagnosticList;

typedef enum
{
    CTX_GLOBAL,
    CTX_FUNCTION,
    CTX_LOOP,
    CTX_AFTER_IF,
    CTX_ASSIGNMENT
} LSPContext;

// Helper to send JSON response
static void send_json_response(cJSON *root)
{
    char *str = cJSON_PrintUnformatted(root);
    if (str)
    {
        fprintf(stderr, "zls: Sent: %s\n", str);
        fprintf(stdout, "Content-Length: %zu\r\n\r\n%s", strlen(str), str);
        fflush(stdout);
        free(str);
    }
    cJSON_Delete(root);
}

// Callback for parser errors (legacy fallback).
void lsp_on_error(void *data, Token t, const char *msg)
{
    (void)data;
    (void)t;
    (void)msg;
    // We do nothing here because lsp_on_diagnostic already captures everything via emit_json.
    // This function exists merely to satisfy the 'if (g_parser_ctx->on_error)' check
    // inside zpanic_at so the compiler recovers instead of exiting.
}

// Unified callback for diagnostics.
void lsp_on_diagnostic(void *data, Token t, int severity, const char *msg, int diag_id)
{
    DiagnosticList *list = (DiagnosticList *)data;
    Diagnostic *d = calloc(1, sizeof(Diagnostic));
    d->line = t.line > 0 ? t.line - 1 : 0;
    d->col = t.col > 0 ? t.col - 1 : 0;
    d->len = t.len;
    d->severity = severity;
    d->id = diag_id;
    d->message = msg ? strdup(msg) : strdup("Unknown error");
    d->next = NULL;

    if (!list->head)
    {
        list->head = d;
        list->tail = d;
    }
    else
    {
        list->tail->next = d;
        list->tail = d;
    }
}

void lsp_check_file(const char *uri, const char *json_src, int id)
{
    (void)id;
    if (!g_project)
    {
        char cwd[MAX_PATH_LEN];
        if (getcwd(cwd, sizeof(cwd)))
        {
            lsp_project_init(cwd);
        }
        else
        {
            lsp_project_init(".");
        }
    }

    // Setup error capture on the global project context
    DiagnosticList diagnostics = {0};

    // We attach the callback to 'g_project->ctx'.
    void *old_data = g_project->ctx->error_callback_data;
    void (*old_cb_error)(void *, Token, const char *) = g_project->ctx->on_error;
    void (*old_cb_diag)(void *, Token, int, const char *, int) = g_project->ctx->on_diagnostic;

    g_project->ctx->error_callback_data = &diagnostics;
    g_project->ctx->on_error = lsp_on_error;
    g_project->ctx->on_diagnostic = lsp_on_diagnostic;

    // Update and Parse
    lsp_project_update_file(uri, json_src);

    // Restore
    g_project->ctx->on_diagnostic = old_cb_diag;
    g_project->ctx->on_error = old_cb_error;
    g_project->ctx->error_callback_data = old_data;

    // Construct JSON Response (publishDiagnostics)
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", "textDocument/publishDiagnostics");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "uri", uri);

    cJSON *diag_array = cJSON_CreateArray();

    Diagnostic *d = diagnostics.head;
    while (d)
    {
        cJSON *diag = cJSON_CreateObject();

        cJSON *range = cJSON_CreateObject();
        cJSON *start = cJSON_CreateObject();
        cJSON_AddNumberToObject(start, "line", d->line);
        cJSON_AddNumberToObject(start, "character", d->col);

        cJSON *end = cJSON_CreateObject();
        cJSON_AddNumberToObject(end, "line", d->line);
        cJSON_AddNumberToObject(end, "character", d->col + d->len);

        cJSON_AddItemToObject(range, "start", start);
        cJSON_AddItemToObject(range, "end", end);

        cJSON_AddItemToObject(diag, "range", range);
        cJSON_AddNumberToObject(diag, "severity", d->severity);
        cJSON_AddStringToObject(diag, "message", d->message);
        if (d->id != DIAG_NONE)
        {
            char code_str[32];
            snprintf(code_str, sizeof(code_str), "W%d", d->id);
            cJSON_AddStringToObject(diag, "code", code_str);
        }

        cJSON_AddItemToArray(diag_array, diag);

        d = d->next;
    }

    cJSON_AddItemToObject(params, "diagnostics", diag_array);
    cJSON_AddItemToObject(root, "params", params);

    send_json_response(root);

    Diagnostic *cur = diagnostics.head;
    while (cur)
    {
        Diagnostic *next = cur->next;
        free(cur->message);
        free(cur);
        cur = next;
    }
}

void lsp_goto_definition(const char *uri, int line, int col, int id)
{
    ProjectFile *pf = lsp_project_get_file(uri);
    LSPIndex *idx = pf ? pf->index : NULL;

    if (!idx)
    {
        return;
    }

    LSPRange *r = lsp_find_at(idx, line, col);
    const char *target_uri = uri;
    int target_start_line = 0, target_start_col = 0;
    int target_end_line = 0, target_end_col = 0;
    int found = 0;

    if (r)
    {
        if (r->type == RANGE_DEFINITION)
        {
            target_start_line = r->start_line;
            target_start_col = r->start_col;
            target_end_line = r->end_line;
            target_end_col = r->end_col;
            found = 1;
        }
        else if (r->type == RANGE_REFERENCE && r->def_line >= 0)
        {
            LSPRange *def = lsp_find_at(idx, r->def_line, r->def_col);
            int is_local = 0;
            if (def && def->type == RANGE_DEFINITION)
            {
                is_local = 1;
            }

            if (is_local)
            {
                target_start_line = r->def_line;
                target_start_col = r->def_col;
                target_end_line = r->def_line;
                target_end_col = r->def_col; // approx
                found = 1;
            }
        }
    }

    if (!found && r && r->node)
    {
        char *name = NULL;
        if (r->node->type == NODE_EXPR_VAR)
        {
            name = r->node->var_ref.name;
        }
        else if (r->node->type == NODE_EXPR_CALL && r->node->call.callee->type == NODE_EXPR_VAR)
        {
            name = r->node->call.callee->var_ref.name;
        }

        if (name)
        {
            DefinitionResult def = lsp_project_find_definition(name);
            if (def.uri && def.range)
            {
                target_uri = def.uri;
                target_start_line = def.range->start_line;
                target_start_col = def.range->start_col;
                target_end_line = def.range->end_line;
                target_end_col = def.range->end_col;
                found = 1;
            }
        }
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", id);

    if (found)
    {
        cJSON *result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "uri", target_uri);

        cJSON *range = cJSON_CreateObject();
        cJSON *start = cJSON_CreateObject();
        cJSON_AddNumberToObject(start, "line", target_start_line);
        cJSON_AddNumberToObject(start, "character", target_start_col);

        cJSON *end = cJSON_CreateObject();
        cJSON_AddNumberToObject(end, "line", target_end_line);
        cJSON_AddNumberToObject(end, "character", target_end_col);

        cJSON_AddItemToObject(range, "start", start);
        cJSON_AddItemToObject(range, "end", end);
        cJSON_AddItemToObject(result, "range", range);

        cJSON_AddItemToObject(root, "result", result);
    }
    else
    {
        cJSON_AddNullToObject(root, "result");
    }

    send_json_response(root);
}

static const char *get_primitive_doc(const char *word)
{
    if (strcmp(word, "int") == 0)
    {
        return "**int**: 32-bit signed integer (guaranteed size).";
    }
    if (strcmp(word, "uint") == 0)
    {
        return "**uint**: 32-bit unsigned integer (guaranteed size).";
    }
    if (strcmp(word, "i8") == 0)
    {
        return "**i8**: 8-bit signed integer.";
    }
    if (strcmp(word, "u8") == 0)
    {
        return "**u8**: 8-bit unsigned integer (byte).";
    }
    if (strcmp(word, "i16") == 0)
    {
        return "**i16**: 16-bit signed integer.";
    }
    if (strcmp(word, "u16") == 0)
    {
        return "**u16**: 16-bit unsigned integer.";
    }
    if (strcmp(word, "i32") == 0)
    {
        return "**i32**: 32-bit signed integer.";
    }
    if (strcmp(word, "u32") == 0)
    {
        return "**u32**: 32-bit unsigned integer.";
    }
    if (strcmp(word, "i64") == 0)
    {
        return "**i64**: 64-bit signed integer.";
    }
    if (strcmp(word, "u64") == 0)
    {
        return "**u64**: 64-bit unsigned integer.";
    }
    if (strcmp(word, "f32") == 0)
    {
        return "**f32**: 32-bit floating point number.";
    }
    if (strcmp(word, "f64") == 0)
    {
        return "**f64**: 64-bit floating point number.";
    }
    if (strcmp(word, "bool") == 0)
    {
        return "**bool**: Boolean type representing `true` or `false`.";
    }
    if (strcmp(word, "rune") == 0)
    {
        return "**rune**: Unicode scalar value (32-bit UTF-32 code point).";
    }
    if (strcmp(word, "string") == 0)
    {
        return "**string**: Immutable sequence of UTF-8 characters.";
    }
    if (strcmp(word, "void") == 0)
    {
        return "**void**: The empty type.";
    }
    if (strcmp(word, "usize") == 0)
    {
        return "**usize**: Unsigned integer type of the same size as a pointer.";
    }
    if (strcmp(word, "isize") == 0)
    {
        return "**isize**: Signed integer type of the same size as a pointer.";
    }
    if (strcmp(word, "U0") == 0 || strcmp(word, "u0") == 0)
    {
        return "**u0**: Zero-sized unit type.";
    }
    if (strcmp(word, "c_int") == 0)
    {
        return "**c_int**: C-compatible `int` (size varies by platform).";
    }
    if (strcmp(word, "c_char") == 0)
    {
        return "**c_char**: C-compatible `char` (size varies by platform).";
    }
    if (strcmp(word, "c_long") == 0)
    {
        return "**c_long**: C-compatible `long` (size varies by platform).";
    }
    if (strcmp(word, "c_ulong") == 0)
    {
        return "**c_ulong**: C-compatible `unsigned long` (size varies by platform).";
    }
    if (strcmp(word, "c_longlong") == 0)
    {
        return "**c_longlong**: C-compatible `long long`.";
    }
    if (strcmp(word, "c_ulonglong") == 0)
    {
        return "**c_ulonglong**: C-compatible `unsigned long long`.";
    }
    return NULL;
}

static char *get_word_at(const char *src, int line, int col)
{
    if (!src)
    {
        return NULL;
    }
    int curr_line = 0;
    const char *p = src;
    while (curr_line < line && *p)
    {
        if (*p == '\n')
        {
            curr_line++;
        }
        p++;
    }
    if (curr_line != line)
    {
        return NULL;
    }
    p += col;

    // Boundary checks: move start to beginning of word
    const char *start = p;
    while (start > src && (isalnum(*(start - 1)) || *(start - 1) == '_'))
    {
        start--;
    }
    // move end to end of word
    const char *end = p;
    while (*end && (isalnum(*end) || *end == '_'))
    {
        end++;
    }

    if (start == end)
    {
        return NULL;
    }
    size_t len = end - start;
    char *word = malloc(len + 1);
    memcpy(word, start, len);
    word[len] = 0;
    return word;
}

void lsp_hover(const char *uri, int line, int col, int id)
{
    (void)uri;
    ProjectFile *pf = lsp_project_get_file(uri);
    LSPIndex *idx = pf ? pf->index : NULL;

    if (!idx)
    {
        return;
    }

    LSPRange *r = lsp_find_at(idx, line, col);
    char *text = NULL;
    int is_primitive = 0;

    if (r)
    {
        if (r->type == RANGE_DEFINITION)
        {
            text = r->hover_text;
        }
        else if (r->type == RANGE_REFERENCE && r->def_line >= 0)
        {
            LSPRange *def = lsp_find_at(idx, r->def_line, r->def_col);
            if (def && def->type == RANGE_DEFINITION)
            {
                text = def->hover_text;
            }
        }

        // Plugin-Specific Hover Support
        if (r->node && r->node->type == NODE_PLUGIN)
        {
            ZPlugin *plugin = zptr_find_plugin(r->node->plugin_stmt.plugin_name);
            if (plugin && plugin->hover_fn)
            {
                // Calculate local offset inside the plugin block.
                // LSP line/col are 0-indexed.
                // node->plugin_stmt.start_line/col are 1-indexed.
                int local_line = (line + 1) - r->node->plugin_stmt.start_line;
                int local_col = 0;

                if (local_line == 0)
                {
                    // Same line as the opening brace: subtract the column offset of the block
                    // start.
                    local_col = col - (r->node->plugin_stmt.start_col - 1);
                }
                else
                {
                    // For subsequent lines, the column is relative to the start of the line.
                    local_col = col;
                }

                if (local_col >= 0)
                {
                    char *plugin_text =
                        (char *)plugin->hover_fn(r->node->plugin_stmt.body, local_line, local_col);
                    if (plugin_text)
                    {
                        text = plugin_text;
                    }
                }
            }
        }
    }

    if (!text && pf && pf->source)
    {
        char *word = get_word_at(pf->source, line, col);
        if (word)
        {
            const char *doc = get_primitive_doc(word);
            if (doc)
            {
                text = (char *)doc;
                is_primitive = 1;
            }
            free(word);
        }
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", id);

    if (text)
    {
        cJSON *result = cJSON_CreateObject();
        cJSON *contents = cJSON_CreateObject();
        cJSON_AddStringToObject(contents, "kind", "markdown");

        if (is_primitive)
        {
            cJSON_AddStringToObject(contents, "value", text);
        }
        else
        {
            // Need to wrap in ```zc code block (using zc for highlighting)
            size_t block_size = strlen(text) + 16;
            char *code_block = malloc(block_size);
            snprintf(code_block, block_size, "```zc\n%s\n```", text);
            cJSON_AddStringToObject(contents, "value", code_block);
            free(code_block);
        }

        cJSON_AddItemToObject(result, "contents", contents);
        cJSON_AddItemToObject(root, "result", result);
    }
    else
    {
        cJSON_AddNullToObject(root, "result");
    }

    send_json_response(root);
}

// Helper to find local in function
static ASTNode *find_local_in_func(ASTNode *func, const char *name)
{
    if (!func || !func->func.body)
    {
        return NULL;
    }

    ASTNode *queue[MAX_ERROR_MSG_LEN];
    int q_head = 0, q_tail = 0;
    queue[q_tail++] = func->func.body;

    // Also check params!
    if (func->func.param_names && func->func.arg_types)
    {
        for (int i = 0; i < func->func.arg_count; i++)
        {
            if (strcmp(func->func.param_names[i], name) == 0)
            {
                return NULL; // TODO: Args handled elsewhere
            }
        }
    }

    while (q_head < q_tail && q_tail < 1024)
    {
        ASTNode *curr = queue[q_head++];
        if (curr->type == NODE_VAR_DECL)
        {
            if (strcmp(curr->var_decl.name, name) == 0)
            {
                return curr;
            }
        }
        else if (curr->type == NODE_BLOCK)
        {
            ASTNode *stmt = curr->block.statements;
            while (stmt)
            {
                if (q_tail < 1024)
                {
                    queue[q_tail++] = stmt;
                }
                stmt = stmt->next;
            }
        }
        else if (curr->type == NODE_IF)
        {
            if (curr->if_stmt.then_body && q_tail < 1024)
            {
                queue[q_tail++] = curr->if_stmt.then_body;
            }
            if (curr->if_stmt.else_body && q_tail < 1024)
            {
                queue[q_tail++] = curr->if_stmt.else_body;
            }
        }
        else if (curr->type == NODE_WHILE)
        {
            if (curr->while_stmt.body && q_tail < 1024)
            {
                queue[q_tail++] = curr->while_stmt.body;
            }
        }
        else if (curr->type == NODE_FOR)
        {
            if (curr->for_stmt.init && q_tail < 1024)
            {
                queue[q_tail++] = curr->for_stmt.init;
            }
            if (curr->for_stmt.body && q_tail < 1024)
            {
                queue[q_tail++] = curr->for_stmt.body;
            }
        }
    }
    return NULL;
}

// Helper to resolve type of local var or arg
static Type *resolve_local_type(ASTNode *func, const char *name)
{
    if (!func)
    {
        return NULL;
    }
    // Check args
    if (func->func.param_names && func->func.arg_types)
    {
        for (int i = 0; i < func->func.arg_count; i++)
        {
            if (strcmp(func->func.param_names[i], name) == 0)
            {
                return func->func.arg_types[i];
            }
        }
    }
    // Check body vars
    ASTNode *decl = find_local_in_func(func, name);
    if (decl && decl->type == NODE_VAR_DECL)
    {
        return decl->var_decl.type_info;
    }
    return NULL;
}

// Helper to determine completion context
static LSPContext lsp_get_completion_context(const char *source, int line, int col,
                                             ASTNode *target_func)
{
    // Derive ptr to the start of the current line
    int cur_l = 0;
    const char *line_ptr = source;
    while (*line_ptr && cur_l < line)
    {
        if (*line_ptr == '\n')
        {
            cur_l++;
        }
        line_ptr++;
    }

    // Scan backward from cursor for assignment or recent block close
    int i = col - 1;
    int line_len = 0;
    while (line_ptr[line_len] && line_ptr[line_len] != '\n' && line_ptr[line_len] != '\r')
    {
        line_len++;
    }

    if (i >= line_len)
    {
        i = line_len - 1;
    }

    while (i >= 0 && (line_ptr[i] == ' ' || line_ptr[i] == '\t'))
    {
        i--;
    }

    if (i >= 0 && line_ptr[i] == '=')
    {
        return CTX_ASSIGNMENT;
    }
    if (i >= 0 && line_ptr[i] == '}')
    {
        return CTX_AFTER_IF; // Heuristic for else/else if
    }

    // Scan for balanced braces to see if we are still inside a block
    int depth = 0;
    const char *s = source;
    cur_l = 0;
    int cur_c = 0;
    while (*s && (cur_l < line || (cur_l == line && cur_c < col)))
    {
        if (*s == '{')
        {
            depth++;
        }
        else if (*s == '}')
        {
            depth--;
        }

        if (*s == '\n')
        {
            cur_l++;
            cur_c = 0;
        }
        else
        {
            cur_c++;
        }
        s++;
    }

    if (depth <= 0)
    {
        return CTX_GLOBAL;
    }
    if (!target_func)
    {
        return CTX_GLOBAL;
    }

    // AST-based check for loops
    ASTNode *queue[1024];
    int q_head = 0, q_tail = 0;
    if (target_func->func.body)
    {
        queue[q_tail++] = target_func->func.body;
    }

    while (q_head < q_tail)
    {
        ASTNode *curr = queue[q_head++];
        if (!curr)
        {
            continue;
        }

        // Approximate line check (Zen C AST stores line numbers in tokens)
        if (curr->token.line > 0 && curr->token.line <= line)
        {
            if (curr->type == NODE_WHILE || curr->type == NODE_FOR || curr->type == NODE_LOOP)
            {
                return CTX_LOOP;
            }
        }

        // Add children to queue
        if (curr->type == NODE_BLOCK)
        {
            ASTNode *it = curr->block.statements;
            while (it)
            {
                if (q_tail < 1024)
                {
                    queue[q_tail++] = it;
                }
                it = it->next;
            }
        }
        else if (curr->type == NODE_IF)
        {
            if (q_tail < 1024)
            {
                queue[q_tail++] = curr->if_stmt.then_body;
            }
            if (curr->if_stmt.else_body && q_tail < 1024)
            {
                queue[q_tail++] = curr->if_stmt.else_body;
            }
        }
    }

    return CTX_FUNCTION;
}

void lsp_completion(const char *uri, int line, int col, int id)
{
    ProjectFile *pf = lsp_project_get_file(uri);
    if (!g_project || !g_project->ctx || !pf)
    {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", id);
    cJSON *items = cJSON_CreateArray();
    fprintf(stderr, "zls: lsp_completion for %s at %d:%d\n", uri, line, col);

    ASTNode *target_func = NULL;
    int best_line = -1;
    if (pf->index)
    {
        LSPRange *r = pf->index->head;
        while (r)
        {
            if (r->type == RANGE_DEFINITION && r->node && r->node->type == NODE_FUNCTION)
            {
                if (r->start_line <= line && r->start_line > best_line)
                {
                    // Basic check: is the cursor AFTER the function start?
                    // With depth check in lsp_get_completion_context, this is safer.
                    best_line = r->start_line;
                    target_func = r->node;
                }
            }
            r = r->next;
        }
    }

    LSPContext context = lsp_get_completion_context(pf->source, line, col, target_func);
    if (context == CTX_GLOBAL)
    {
        target_func = NULL; // Reset if we are top-level
    }

    int dot_completed = 0;

    if (pf->source)
    {
        int cur_line = 0;
        char *ptr = pf->source;
        while (*ptr && cur_line < line)
        {
            if (*ptr == '\n')
            {
                cur_line++;
            }
            ptr++;
        }

        int line_len = 0;
        while (ptr[line_len] && ptr[line_len] != '\n' && ptr[line_len] != '\r')
        {
            line_len++;
        }

        if (col > 0 && col <= line_len &&
            (ptr[col - 1] == '.' || (col > 1 && ptr[col - 2] == ':' && ptr[col - 1] == ':')))
        {
            int i = col - 2;
            while (i >= 0 && (ptr[i] == ' ' || ptr[i] == '\t'))
            {
                i--;
            }
            if (i >= 0)
            {
                int end_ident = i;
                while (i >= 0 && (isalnum((unsigned char)ptr[i]) || ptr[i] == '_' ||
                                  ptr[i] == '.' || ptr[i] == '-' || ptr[i] == '>' || ptr[i] == '<'))
                {
                    i--;
                }
                int start_ident = i + 1;

                if (start_ident <= end_ident)
                {
                    int len = end_ident - start_ident + 1;
                    char expr_chain[MAX_VAR_NAME_LEN * 4];
                    if (len >= (int)sizeof(expr_chain))
                    {
                        len = sizeof(expr_chain) - 1;
                    }
                    strncpy(expr_chain, ptr + start_ident, len);
                    expr_chain[len] = 0;

                    // e.g. "a.b.c" -> parts: "a", "b", "c"
                    char *parts[32];
                    int part_count = 0;

                    // Handle Enum::Variant completion
                    int is_scoped = 0;
                    if (len >= 2 && expr_chain[len - 2] == ':' && expr_chain[len - 1] == ':')
                    {
                        is_scoped = 1;
                        expr_chain[len - 2] = 0; // Terminate early
                    }

                    // Replace '->' with '.' to simplify splitting
                    for (int c = 0; expr_chain[c]; c++)
                    {
                        if (expr_chain[c] == '-' && expr_chain[c + 1] == '>')
                        {
                            expr_chain[c] = '.';
                            expr_chain[c + 1] = '.'; // Will be skipped empty parts
                        }
                    }

                    char *copy = strdup(expr_chain);
                    char *t = strtok(copy, ".");
                    while (t && part_count < 32)
                    {
                        parts[part_count++] = t;
                        t = strtok(NULL, ".");
                    }

                    if (is_scoped && part_count == 1)
                    {
                        EnumVariantReg *ev = g_project->ctx->enum_variants;
                        while (ev)
                        {
                            if (strcmp(ev->enum_name, parts[0]) == 0)
                            {
                                cJSON *item = cJSON_CreateObject();
                                cJSON_AddStringToObject(item, "label", ev->variant_name);
                                cJSON_AddNumberToObject(item, "kind", 20); // EnumMember
                                cJSON_AddStringToObject(item, "detail", "enum variant");
                                cJSON_AddItemToArray(items, item);
                            }
                            ev = ev->next;
                        }
                        dot_completed = 1;
                    }

                    if (!is_scoped && part_count > 0)
                    {
                        char *base_name = parts[0];
                        Type *resolved_type = resolve_local_type(target_func, base_name);
                        char *type_name = NULL;

                        if (resolved_type)
                        {
                            type_name = type_to_string(resolved_type);
                        }
                        else
                        {
                            ASTNode *decl = find_local_in_func(target_func, base_name);
                            if (decl && decl->type == NODE_VAR_DECL && decl->var_decl.type_str)
                            {
                                type_name = strdup(decl->var_decl.type_str);
                            }
                            else
                            {
                                ZenSymbol *sym = find_symbol_in_all(g_project->ctx, base_name);
                                if (sym)
                                {
                                    if (sym->type_info)
                                    {
                                        type_name = type_to_string(sym->type_info);
                                    }
                                    else if (sym->type_name)
                                    {
                                        type_name = strdup(sym->type_name);
                                    }
                                }
                            }
                        }

                        // Now traverse properties
                        for (int p = 1; p < part_count && type_name; p++)
                        {
                            char clean_name[MAX_VAR_NAME_LEN];
                            char *src = type_name;
                            if (strncmp(src, "struct ", 7) == 0)
                            {
                                src += 7;
                            }
                            char *dst = clean_name;
                            while (*src && *src != '*' && *src != '<' && *src != '[')
                            {
                                *dst++ = *src++;
                            }
                            *dst = 0;

                            ASTNode *struct_node = find_struct_def(g_project->ctx, clean_name);
                            int found_field = 0;
                            if (struct_node && struct_node->type == NODE_STRUCT)
                            {
                                ASTNode *field = struct_node->strct.fields;
                                while (field)
                                {
                                    if (strcmp(field->field.name, parts[p]) == 0)
                                    {
                                        free(type_name);
                                        type_name =
                                            field->field.type ? strdup(field->field.type) : NULL;
                                        found_field = 1;
                                        break;
                                    }
                                    field = field->next;
                                }
                            }
                            if (!found_field)
                            {
                                free(type_name);
                                type_name = NULL;
                            }
                        }

                        if (type_name)
                        {
                            char clean_name[MAX_VAR_NAME_LEN];
                            char *src = type_name;
                            if (strncmp(src, "struct ", 7) == 0)
                            {
                                src += 7;
                            }

                            char *dst = clean_name;
                            while (*src && *src != '*' && *src != '<' && *src != '[')
                            {
                                *dst++ = *src++;
                            }
                            *dst = 0;

                            ASTNode *struct_node = find_struct_def(g_project->ctx, clean_name);
                            if (struct_node)
                            {
                                if (struct_node->type == NODE_STRUCT)
                                {
                                    ASTNode *field = struct_node->strct.fields;
                                    while (field)
                                    {
                                        cJSON *item = cJSON_CreateObject();
                                        cJSON_AddStringToObject(item, "label", field->field.name);
                                        cJSON_AddNumberToObject(item, "kind", 5); // Field
                                        cJSON_AddStringToObject(item, "detail", field->field.type);
                                        cJSON_AddItemToArray(items, item);
                                        field = field->next;
                                    }
                                }
                                else if (struct_node->type == NODE_ENUM)
                                {
                                    ASTNode *variant = struct_node->enm.variants;
                                    while (variant)
                                    {
                                        cJSON *item = cJSON_CreateObject();
                                        cJSON_AddStringToObject(item, "label",
                                                                variant->variant.name);
                                        cJSON_AddNumberToObject(item, "kind", 12); // EnumMember
                                        cJSON_AddItemToArray(items, item);
                                        variant = variant->next;
                                    }
                                }
                                dot_completed = 1;
                            }

                            // Show methods (Struct::Method)
                            FuncSig *fn_sig = g_project->ctx->func_registry;
                            char method_prefix[MAX_VAR_NAME_LEN + 4];
                            snprintf(method_prefix, sizeof(method_prefix), "%s::", clean_name);
                            while (fn_sig)
                            {
                                if (strncmp(fn_sig->name, method_prefix, strlen(method_prefix)) ==
                                    0)
                                {
                                    cJSON *item = cJSON_CreateObject();
                                    const char *method_name = fn_sig->name + strlen(method_prefix);
                                    cJSON_AddStringToObject(item, "label", method_name);
                                    cJSON_AddNumberToObject(item, "kind", 2); // Method
                                    char detail[MAX_SHORT_MSG_LEN];
                                    snprintf(detail, sizeof(detail), "fn %s", fn_sig->name);
                                    cJSON_AddStringToObject(item, "detail", detail);

                                    // Snippet format
                                    char snippet[MAX_VAR_NAME_LEN];
                                    if (fn_sig->total_args > 0)
                                    {
                                        snprintf(snippet, sizeof(snippet), "%s($1)", method_name);
                                    }
                                    else
                                    {
                                        snprintf(snippet, sizeof(snippet), "%s()", method_name);
                                    }
                                    cJSON_AddStringToObject(item, "insertText", snippet);
                                    cJSON_AddNumberToObject(item, "insertTextFormat", 2);

                                    cJSON_AddItemToArray(items, item);
                                }
                                fn_sig = fn_sig->next;
                            }
                            free(type_name);
                        }
                    }
                    free(copy);
                }
            }
        }
    }

    if (!dot_completed)
    {
        typedef struct
        {
            const char *label;
            const char *snippet;
            int context_mask; // 1: Global, 2: Function, 4: Loop, 8: AfterIf
        } KeywordSnippet;

        KeywordSnippet keywords[] = {
            {"fn", "fn ${1:name}(${2:args}) {\n    $0\n}", 1},
            {"struct", "struct ${1:Name} {\n    ${2:field}: ${3:type};\n}", 1},
            {"enum", "enum ${1:Name} {\n    ${2:Variant}\n}", 1},
            {"trait", "trait ${1:Name} {\n    fn ${2:method}(self);\n}", 1},
            {"impl", "impl ${1:Struct} {\n    $0\n}", 1},
            {"if", "if (${1:condition}) {\n    $0\n}", 2 | 4 | 8},
            {"else", "else {\n    $0\n}", 8},
            {"for", "for (${1:let i = 0}; ${2:i < 10}; ${3:i++}) {\n    $0\n}", 2 | 4 | 8},
            {"for..in", "for (${1:item} in ${2:iterator}) {\n    $0\n}", 2 | 4 | 8},
            {"while", "while (${1:condition}) {\n    $0\n}", 2 | 4 | 8},
            {"match", "match (${1:expression}) {\n    ${2:pattern} => ${3:action},\n    _ => $0\n}",
             2 | 4 | 8},
            {"defer", "defer {\n    $0\n}", 2 | 4 | 8},
            {"return", "return $0;", 2 | 4 | 8},
            {"let", "let ${1:var}: ${2:type} = ${3:value};", 2 | 4 | 8},
            {"mut", "mut ${1:var}: ${2:type} = ${3:value};", 2 | 4 | 8},
            {"def", "def ${1:CONST} = ${2:value};", 1 | 2 | 4 | 8},
            {"test", "test \"${1:description}\" {\n    $0\n}", 1},
            {"assert", "assert(${1:expression}, \"${2:error message}\");", 2 | 4 | 8},
            {"break", "break;", 4},
            {"continue", "continue;", 4},
            {NULL, NULL, 0}};

        int active_mask = 0;
        if (context == CTX_GLOBAL)
        {
            active_mask = 1;
        }
        else if (context == CTX_FUNCTION)
        {
            active_mask = 2;
        }
        else if (context == CTX_LOOP)
        {
            active_mask = 4;
        }
        else if (context == CTX_AFTER_IF)
        {
            active_mask = 8;
        }
        else if (context == CTX_ASSIGNMENT)
        {
            active_mask = 2 | 4 | 8; // Assignments usually in functions
        }

        // If we are after an if, we still want to allow 'if' keywords (else if)
        if (context == CTX_AFTER_IF)
        {
            active_mask |= 2;
        }

        for (int i = 0; keywords[i].label; i++)
        {
            if (!(keywords[i].context_mask & active_mask))
            {
                continue;
            }

            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "label", keywords[i].label);
            cJSON_AddNumberToObject(item, "kind", 14); // Keyword
            cJSON_AddStringToObject(item, "insertText", keywords[i].snippet);
            cJSON_AddNumberToObject(item, "insertTextFormat", 2); // Snippet
            cJSON_AddStringToObject(item, "sortText", "005");     // Keywords higher up
            cJSON_AddItemToArray(items, item);
        }

        // Add other keywords that don't need snippets
        const char *plain_keywords[] = {
            "alias",      "true",        "false",  "int",    "char",     "bool",   "string",
            "void",       "import",      "module", "defer",  "sizeof",   "opaque", "unsafe",
            "asm",        "u8",          "u16",    "u32",    "u64",      "i8",     "i16",
            "i32",        "i64",         "f32",    "f64",    "usize",    "isize",  "const",
            "rune",       "U0",          "u0",     "c_int",  "c_char",   "c_long", "c_ulong",
            "c_longlong", "c_ulonglong", "extern", "inline", "noreturn", NULL};

        for (int i = 0; plain_keywords[i]; i++)
        {
            // Simple type filtering or global check
            if (context == CTX_GLOBAL)
            {
                if (strcmp(plain_keywords[i], "import") != 0 &&
                    strcmp(plain_keywords[i], "module") != 0 &&
                    strcmp(plain_keywords[i], "alias") != 0)
                {
                    continue;
                }
            }

            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "label", plain_keywords[i]);
            cJSON_AddNumberToObject(item, "kind", 14);
            cJSON_AddStringToObject(item, "sortText", "008");
            cJSON_AddItemToArray(items, item);
        }

        StructRef *g = g_project->ctx->parsed_globals_list;
        while (g)
        {
            if (g->node)
            {
                cJSON *item = cJSON_CreateObject();
                char *name = g->node->var_decl.name;
                cJSON_AddStringToObject(item, "label", name);
                cJSON_AddNumberToObject(item, "kind", 21);
                cJSON_AddStringToObject(item, "sortText", "050"); // Globals in middle
                cJSON_AddItemToArray(items, item);
            }
            g = g->next;
        }

        StructDef *s = g_project->ctx->struct_defs;
        while (s)
        {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "label", s->name);
            cJSON_AddNumberToObject(item, "kind", 22);
            cJSON_AddStringToObject(item, "sortText", "060");
            cJSON_AddItemToArray(items, item);
            s = s->next;
        }

        FuncSig *f = g_project->ctx->func_registry;
        while (f)
        {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "label", f->name);
            cJSON_AddNumberToObject(item, "kind", 3); // Function
            cJSON_AddStringToObject(item, "sortText", "070");

            // Rich detail with signature
            char detail[1024];
            int offset = snprintf(detail, sizeof(detail), "fn %s(", f->name);
            for (int i = 0; i < f->total_args; i++)
            {
                char *tstr = type_to_string(f->arg_types[i]);
                offset += snprintf(detail + offset, sizeof(detail) - offset, "%s%s", tstr,
                                   (i < f->total_args - 1) ? ", " : "");
                free(tstr);
                if (offset >= (int)sizeof(detail))
                {
                    break;
                }
            }
            char *ret_str = type_to_string(f->ret_type);
            if (offset < (int)sizeof(detail))
            {
                snprintf(detail + offset, sizeof(detail) - offset, ") -> %s", ret_str);
            }
            free(ret_str);
            cJSON_AddStringToObject(item, "detail", detail);

            // Snippet to jump inside parens
            char snippet[MAX_VAR_NAME_LEN];
            if (f->total_args > 0)
            {
                snprintf(snippet, sizeof(snippet), "%s($1)", f->name);
            }
            else
            {
                snprintf(snippet, sizeof(snippet), "%s()", f->name);
            }
            cJSON_AddStringToObject(item, "insertText", snippet);
            cJSON_AddNumberToObject(item, "insertTextFormat", 2); // Snippet

            cJSON_AddItemToArray(items, item);
            f = f->next;
        }

        if (target_func)
        {
            if (target_func->func.param_names)
            {
                for (int i = 0; i < target_func->func.arg_count; i++)
                {
                    cJSON *item = cJSON_CreateObject();
                    cJSON_AddStringToObject(item, "label", target_func->func.param_names[i]);
                    cJSON_AddNumberToObject(item, "kind", 6);
                    cJSON_AddStringToObject(item, "detail", "argument");
                    cJSON_AddStringToObject(item, "sortText", "001"); // Arg priority
                    cJSON_AddItemToArray(items, item);
                }
            }
            ASTNode *queue[1024];
            int q_head = 0;
            int q_tail = 0;
            if (target_func->func.body)
            {
                queue[q_tail++] = target_func->func.body;
            }

            while (q_head < q_tail && q_tail < 1024)
            {
                ASTNode *curr = queue[q_head++];
                if (!curr)
                {
                    continue;
                }

                if (curr->type == NODE_VAR_DECL || curr->type == NODE_CONST)
                {
                    if (curr->token.line > 0 && (curr->token.line - 1) <= line)
                    {
                        cJSON *item = cJSON_CreateObject();
                        cJSON_AddStringToObject(item, "label", curr->var_decl.name);
                        cJSON_AddNumberToObject(
                            item, "kind",
                            curr->type == NODE_CONST ? 21 : 6);           // Constant or Variable
                        cJSON_AddStringToObject(item, "sortText", "002"); // Local priority
                        cJSON_AddItemToArray(items, item);
                    }
                }
                else if (curr->type == NODE_BLOCK)
                {
                    ASTNode *stmt = curr->block.statements;
                    while (stmt)
                    {
                        if (q_tail < 1024)
                        {
                            queue[q_tail++] = stmt;
                        }
                        stmt = stmt->next;
                    }
                }
                else if (curr->type == NODE_IF)
                {
                    if (curr->if_stmt.then_body && q_tail < 1024)
                    {
                        queue[q_tail++] = curr->if_stmt.then_body;
                    }
                    if (curr->if_stmt.else_body && q_tail < 1024)
                    {
                        queue[q_tail++] = curr->if_stmt.else_body;
                    }
                }
                else if (curr->type == NODE_WHILE)
                {
                    if (curr->while_stmt.body && q_tail < 1024)
                    {
                        queue[q_tail++] = curr->while_stmt.body;
                    }
                }
                else if (curr->type == NODE_FOR)
                {
                    if (curr->for_stmt.init && q_tail < 1024)
                    {
                        queue[q_tail++] = curr->for_stmt.init;
                    }
                    if (curr->for_stmt.body && q_tail < 1024)
                    {
                        queue[q_tail++] = curr->for_stmt.body;
                    }
                }
            }
        }
    }

    cJSON_AddItemToObject(root, "result", items);
    send_json_response(root);
}

static cJSON *ast_to_symbol(ASTNode *node)
{
    if (!node)
    {
        return NULL;
    }
    char *name = NULL;
    int kind = 0;

    if (node->type == NODE_FUNCTION)
    {
        name = node->func.name;
        kind = 12;
    }
    else if (node->type == NODE_STRUCT)
    {
        name = node->strct.name;
        kind = 23;
    }
    else if (node->type == NODE_VAR_DECL)
    {
        name = node->var_decl.name;
        kind = 13;
    }
    else if (node->type == NODE_CONST)
    {
        name = node->var_decl.name;
        kind = 14;
    }
    else if (node->type == NODE_ENUM)
    {
        name = node->enm.name;
        kind = 10;
    }
    else if (node->type == NODE_FIELD)
    {
        name = node->field.name;
        kind = 8;
    }

    if (!name)
    {
        return NULL;
    }

    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "name", name);
    cJSON_AddNumberToObject(item, "kind", kind);

    cJSON *range = cJSON_CreateObject();
    cJSON *start = cJSON_CreateObject();
    cJSON_AddNumberToObject(start, "line", node->token.line > 0 ? node->token.line - 1 : 0);
    cJSON_AddNumberToObject(start, "character", node->token.col > 0 ? node->token.col - 1 : 0);

    cJSON *end = cJSON_CreateObject();
    cJSON_AddNumberToObject(end, "line", node->token.line > 0 ? node->token.line - 1 : 0);
    cJSON_AddNumberToObject(end, "character",
                            (node->token.col > 0 ? node->token.col - 1 : 0) + node->token.len);

    cJSON_AddItemToObject(range, "start", start);
    cJSON_AddItemToObject(range, "end", end);

    cJSON_AddItemToObject(item, "range", range);
    cJSON *selRange = cJSON_Duplicate(range, 1);
    cJSON_AddItemToObject(item, "selectionRange", selRange);

    if (node->type == NODE_STRUCT && node->strct.fields)
    {
        cJSON *children = cJSON_CreateArray();
        ASTNode *f = node->strct.fields;
        while (f)
        {
            cJSON *child = ast_to_symbol(f);
            if (child)
            {
                cJSON_AddItemToArray(children, child);
            }
            f = f->next;
        }
        cJSON_AddItemToObject(item, "children", children);
    }

    return item;
}

void lsp_document_symbol(const char *uri, int id)
{
    ProjectFile *pf = lsp_project_get_file(uri);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", id);

    if (!pf || !pf->ast)
    {
        cJSON_AddNullToObject(root, "result");
        send_json_response(root);
        return;
    }

    cJSON *items = cJSON_CreateArray();
    ASTNode *node = pf->ast;

    // Unwrap ROOT node if present
    if (node && node->type == NODE_ROOT)
    {
        node = node->root.children;
    }

    while (node)
    {
        cJSON *s = ast_to_symbol(node);
        if (s)
        {
            cJSON_AddItemToArray(items, s);
        }
        node = node->next; // Top level siblings
    }

    cJSON_AddItemToObject(root, "result", items);
    send_json_response(root);
}

void lsp_references(const char *uri, int line, int col, int id)
{
    ProjectFile *pf = lsp_project_get_file(uri);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", id);
    cJSON *items = cJSON_CreateArray();

    if (pf && pf->index)
    {
        LSPRange *r = lsp_find_at(pf->index, line, col);
        if (r && r->node)
        {
            char *name = NULL;
            if (r->node->type == NODE_FUNCTION)
            {
                name = r->node->func.name;
            }
            else if (r->node->type == NODE_VAR_DECL)
            {
                name = r->node->var_decl.name;
            }
            else if (r->node->type == NODE_CONST)
            {
                name = r->node->var_decl.name;
            }
            else if (r->node->type == NODE_STRUCT)
            {
                name = r->node->strct.name;
            }
            else if (r->node->type == NODE_EXPR_VAR)
            {
                name = r->node->var_ref.name;
            }
            else if (r->node->type == NODE_EXPR_CALL && r->node->call.callee->type == NODE_EXPR_VAR)
            {
                name = r->node->call.callee->var_ref.name;
            }

            if (name)
            {
                ReferenceResult *refs = lsp_project_find_references(name);
                ReferenceResult *curr = refs;
                while (curr)
                {
                    cJSON *item = cJSON_CreateObject();
                    cJSON_AddStringToObject(item, "uri", curr->uri);
                    cJSON *range = cJSON_CreateObject();
                    cJSON *start = cJSON_CreateObject();
                    cJSON_AddNumberToObject(start, "line", curr->range->start_line);
                    cJSON_AddNumberToObject(start, "character", curr->range->start_col);

                    cJSON *end = cJSON_CreateObject();
                    cJSON_AddNumberToObject(end, "line", curr->range->end_line);
                    cJSON_AddNumberToObject(end, "character", curr->range->end_col);

                    cJSON_AddItemToObject(range, "start", start);
                    cJSON_AddItemToObject(range, "end", end);
                    cJSON_AddItemToObject(item, "range", range);
                    cJSON_AddItemToArray(items, item);

                    ReferenceResult *next = curr->next;
                    free(curr);
                    curr = next;
                }
            }
        }
    }

    cJSON_AddItemToObject(root, "result", items);
    send_json_response(root);
}

void lsp_signature_help(const char *uri, int line, int col, int id)
{
    ProjectFile *pf = lsp_project_get_file(uri);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", id);

    if (!g_project || !g_project->ctx || !pf || !pf->source)
    {
        cJSON_AddNullToObject(root, "result");
        send_json_response(root);
        return;
    }

    // ... [Scan backwards logic same as before] ...
    char *ptr = pf->source;
    int cur_line = 0;
    while (*ptr && cur_line < line)
    {
        if (*ptr == '\n')
        {
            cur_line++;
        }
        ptr++;
    }
    if (col > 0)
    {
        ptr += col;
    }

    if (ptr > pf->source + strlen(pf->source))
    {
        cJSON_AddNullToObject(root, "result");
        send_json_response(root);
        return;
    }

    int found = 0;
    char *p = ptr - 1;
    while (p >= pf->source)
    {
        if (*p == ')')
        {
            break;
        }
        if (*p == '(')
        {
            // Found open paren
            char *ident_end = p - 1;
            while (ident_end >= pf->source && isspace(*ident_end))
            {
                ident_end--;
            }
            if (ident_end < pf->source)
            {
                break;
            }
            char *ident_start = ident_end;
            while (ident_start >= pf->source && (isalnum(*ident_start) || *ident_start == '_'))
            {
                ident_start--;
            }
            ident_start++;

            int len = ident_end - ident_start + 1;
            if (len > 0 && len < MAX_FUNC_NAME_LEN - 1)
            {
                char func_name[MAX_FUNC_NAME_LEN];
                strncpy(func_name, ident_start, len);
                func_name[len] = 0;
                // Lookup
                FuncSig *fn = g_project->ctx->func_registry;
                while (fn)
                {
                    if (strcmp(fn->name, func_name) == 0)
                    {
                        // Found it
                        cJSON *result = cJSON_CreateObject();
                        cJSON *sigs = cJSON_CreateArray();
                        cJSON *sig = cJSON_CreateObject();

                        char label[2048];
                        char params[MAX_ERROR_MSG_LEN] = "";
                        int first = 1;
                        for (int i = 0; i < fn->total_args; i++)
                        {
                            if (!first)
                            {
                                strncat(params, ", ", sizeof(params) - strlen(params) - 1);
                            }
                            char *tstr = type_to_string(fn->arg_types[i]);
                            if (tstr)
                            {
                                strncat(params, tstr, sizeof(params) - strlen(params) - 1);
                                free(tstr);
                            }
                            else
                            {
                                strncat(params, "unknown", sizeof(params) - strlen(params) - 1);
                            }
                            first = 0;
                        }
                        char *ret_str = type_to_string(fn->ret_type);
                        snprintf(label, sizeof(label), "fn %s(%s) -> %s", fn->name, params,
                                 ret_str ? ret_str : "void");
                        if (ret_str)
                        {
                            free(ret_str);
                        }

                        cJSON_AddStringToObject(sig, "label", label);

                        cJSON *params_array = cJSON_CreateArray();
                        char *p_ptr = params;
                        while (*p_ptr)
                        {
                            char *p_end = strstr(p_ptr, ", ");
                            char param_label[MAX_VAR_NAME_LEN];
                            if (p_end)
                            {
                                int p_len = p_end - p_ptr;
                                if (p_len > 255)
                                {
                                    p_len = 255;
                                }
                                strncpy(param_label, p_ptr, p_len);
                                param_label[p_len] = 0;
                                p_ptr = p_end + 2;
                            }
                            else
                            {
                                strcpy(param_label, p_ptr);
                                p_ptr += strlen(p_ptr);
                            }
                            cJSON *p_obj = cJSON_CreateObject();
                            cJSON_AddStringToObject(p_obj, "label", param_label);
                            cJSON_AddItemToArray(params_array, p_obj);
                        }
                        cJSON_AddItemToObject(sig, "parameters", params_array);
                        cJSON_AddItemToArray(sigs, sig);

                        cJSON_AddItemToObject(result, "signatures", sigs);

                        // Calculate Active Parameter
                        int active = 0;
                        char *active_p = ptr - 1;
                        while (active_p > p)
                        {
                            if (*active_p == ',')
                            {
                                active++;
                            }
                            active_p--;
                        }
                        cJSON_AddNumberToObject(result, "activeSignature", 0);
                        cJSON_AddNumberToObject(result, "activeParameter", active);

                        cJSON_AddItemToObject(root, "result", result);
                        found = 1;
                        break;
                    }
                    fn = fn->next;
                }
            }
            break;
        }
        p--;
    }

    if (!found)
    {
        cJSON_AddNullToObject(root, "result");
    }

    send_json_response(root);
}

static char *get_symbol_at(ProjectFile *pf, int line, int col)
{
    if (!pf || !pf->index)
    {
        return NULL;
    }
    LSPRange *r = pf->index->head;
    while (r)
    {
        int over_start = (line > r->start_line) || (line == r->start_line && col >= r->start_col);
        int under_end = (line < r->end_line) || (line == r->end_line && col <= r->end_col);

        if (over_start && under_end && r->node)
        {
            if (r->node->type == NODE_FUNCTION)
            {
                return strdup(r->node->func.name);
            }
            if (r->node->type == NODE_VAR_DECL)
            {
                return strdup(r->node->var_decl.name);
            }
            if (r->node->type == NODE_CONST)
            {
                return strdup(r->node->var_decl.name);
            }
            if (r->node->type == NODE_STRUCT)
            {
                return strdup(r->node->strct.name);
            }
            if (r->node->type == NODE_EXPR_VAR)
            {
                return strdup(r->node->var_ref.name);
            }
            if (r->node->type == NODE_EXPR_CALL)
            {
                if (r->node->call.callee && r->node->call.callee->type == NODE_EXPR_VAR)
                {
                    return strdup(r->node->call.callee->var_ref.name);
                }
            }
        }
        r = r->next;
    }
    return NULL;
}

void lsp_rename(const char *uri, int line, int col, const char *new_name, int id)
{
    ProjectFile *pf = lsp_project_get_file(uri);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(root, "id", id);

    char *name = get_symbol_at(pf, line, col);
    if (!name)
    {
        cJSON_AddNullToObject(root, "result");
        send_json_response(root);
        return;
    }

    ReferenceResult *refs = lsp_project_find_references(name);
    free(name);

    if (!refs)
    {
        cJSON_AddNullToObject(root, "result");
        send_json_response(root);
        return;
    }

    cJSON *result = cJSON_CreateObject();
    cJSON *changes = cJSON_CreateObject();

    ReferenceResult *cur = refs;
    while (cur)
    {
        cJSON *edits = cJSON_GetObjectItem(changes, cur->uri);
        if (!edits)
        {
            edits = cJSON_CreateArray();
            cJSON_AddItemToObject(changes, cur->uri, edits);
        }

        cJSON *edit = cJSON_CreateObject();
        cJSON *range = cJSON_CreateObject();
        cJSON *start = cJSON_CreateObject();
        cJSON *end = cJSON_CreateObject();

        cJSON_AddNumberToObject(start, "line", cur->range->start_line);
        cJSON_AddNumberToObject(start, "character", cur->range->start_col);
        cJSON_AddNumberToObject(end, "line", cur->range->end_line);
        cJSON_AddNumberToObject(end, "character", cur->range->end_col);

        cJSON_AddItemToObject(range, "start", start);
        cJSON_AddItemToObject(range, "end", end);
        cJSON_AddItemToObject(edit, "range", range);
        cJSON_AddStringToObject(edit, "newText", new_name);

        cJSON_AddItemToArray(edits, edit);

        ReferenceResult *next = cur->next;
        free(cur);
        cur = next;
    }

    cJSON_AddItemToObject(result, "changes", changes);
    cJSON_AddItemToObject(root, "result", result);
    send_json_response(root);
}

void lsp_code_action(const char *uri, cJSON *diagnostics, int id)
{
    cJSON *actions = cJSON_CreateArray();

    int diag_count = cJSON_GetArraySize(diagnostics);
    for (int i = 0; i < diag_count; i++)
    {
        cJSON *diag = cJSON_GetArrayItem(diagnostics, i);
        cJSON *code = cJSON_GetObjectItem(diag, "code");
        if (!code || !code->valuestring)
        {
            continue;
        }

        cJSON *range = cJSON_GetObjectItem(diag, "range");
        if (!range)
        {
            continue;
        }

        // Note: The code string is "W" + DiagnosticID enum value.
        // We'll check for the specific ones we care about.
        // For robustness, we check the message too or use a lookup.
        cJSON *msg = cJSON_GetObjectItem(diag, "message");
        int is_var = (msg && strstr(msg->valuestring, "'var' is deprecated"));
        int is_const = (msg && strstr(msg->valuestring, "'const' for declarations is deprecated"));

        if (is_var)
        {
            cJSON *action = cJSON_CreateObject();
            cJSON_AddStringToObject(action, "title", "Replace 'var' with 'let'");
            cJSON_AddStringToObject(action, "kind", "quickfix");

            cJSON *edit = cJSON_CreateObject();
            cJSON *changes = cJSON_CreateObject();
            cJSON *edits = cJSON_CreateArray();

            cJSON *text_edit = cJSON_CreateObject();
            cJSON_AddItemToObject(text_edit, "range", cJSON_Duplicate(range, 1));
            cJSON_AddStringToObject(text_edit, "newText", "let");
            cJSON_AddItemToArray(edits, text_edit);

            cJSON_AddItemToObject(changes, uri, edits);
            cJSON_AddItemToObject(edit, "changes", changes);
            cJSON_AddItemToObject(action, "edit", edit);

            cJSON_AddItemToArray(actions, action);
        }
        else if (is_const)
        {
            cJSON *action = cJSON_CreateObject();
            cJSON_AddStringToObject(action, "title", "Replace 'const' with 'def'");
            cJSON_AddStringToObject(action, "kind", "quickfix");

            cJSON *edit = cJSON_CreateObject();
            cJSON *changes = cJSON_CreateObject();
            cJSON *edits = cJSON_CreateArray();

            cJSON *text_edit = cJSON_CreateObject();
            cJSON_AddItemToObject(text_edit, "range", cJSON_Duplicate(range, 1));
            cJSON_AddStringToObject(text_edit, "newText", "def");
            cJSON_AddItemToArray(edits, text_edit);

            cJSON_AddItemToObject(changes, uri, edits);
            cJSON_AddItemToObject(edit, "changes", changes);
            cJSON_AddItemToObject(action, "edit", edit);

            cJSON_AddItemToArray(actions, action);
        }
    }

    cJSON *res_json = cJSON_CreateObject();
    cJSON_AddStringToObject(res_json, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(res_json, "id", id);
    cJSON_AddItemToObject(res_json, "result", actions);

    char *str = cJSON_PrintUnformatted(res_json);
    fprintf(stdout, "Content-Length: %zu\r\n\r\n%s", strlen(str), str);
    fflush(stdout);
    free(str);
    cJSON_Delete(res_json);
}
