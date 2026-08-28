// SPDX-License-Identifier: MIT
#include "repl_state.h"
#include "repl_jit.h"

static int cmd_help(ReplState *state, const char *args)
{
    (void)state;
    (void)args;
    repl_print_help();
    return REPL_HANDLED;
}

static int cmd_plot(ReplState *state, const char *args)
{
    if (!args || !args[0])
    {
        printf("Usage: :plot <expression>\n");
        return REPL_HANDLED;
    }

    char *global_code = NULL;
    char *main_code = NULL;
    repl_get_code(state->history, state->history_len, &global_code, &main_code);

    char *plot_logic = repl_generate_plot_code(args);

    size_t code_size = strlen(global_code) + strlen(main_code) + strlen(plot_logic) + 256;
    char *full_code = malloc(code_size);
    snprintf(full_code, code_size, "use std.io;\n%s\nfn main() { %s \n %s }", global_code,
             main_code, plot_logic);

    zfree(global_code);
    zfree(main_code);
    zfree(plot_logic);

    /* Execution */
    char *c_code = repl_transpile(full_code);
    if (c_code)
    {
        repl_jit_execute(c_code, state->config);
        zfree(c_code);
    }

    zfree(full_code);
    return REPL_HANDLED;
}

static int cmd_reload(ReplState *state, const char *args)
{
    (void)args;
    // For now, we clear the symbol table and re-run update_symbols
    // Since update_symbols re-parses the history, it will find 'import plugin' lines.
    // However, zptr_load_plugin doesn't actually 'unload' first.
    // We should explicitly find plugins and reload them.

    printf("Reloading plugins...\n");
    repl_update_symbols(state);
    printf("Done.\n");
    return REPL_HANDLED;
}

static int cmd_reset(ReplState *state, const char *args)
{
    (void)args;
    for (int i = 0; i < state->history_len; i++)
    {
        zfree(state->history[i]);
    }
    state->history_len = 0;
    printf("History cleared.\n");
    return REPL_HANDLED;
}

static int cmd_quit(ReplState *state, const char *args)
{
    (void)state;
    (void)args;
    return REPL_QUIT;
}

static int cmd_clear(ReplState *state, const char *args)
{
    (void)state;
    (void)args;
    printf("\033[2J\033[H");
    return REPL_HANDLED;
}

static int cmd_undo(ReplState *state, const char *args)
{
    (void)args;
    if (state->history_len > 0)
    {
        state->history_len = state->history_len - 1;
        zfree(state->history[state->history_len]);
        printf("Removed last entry.\n");
    }
    else
    {
        printf("History is empty.\n");
    }
    return REPL_HANDLED;
}

static int cmd_delete(ReplState *state, const char *args)
{
    int idx = (int)strtol(args, NULL, 10) - 1;
    if (idx >= 0 && idx < state->history_len)
    {
        zfree(state->history[idx]);
        for (int i = idx; i < state->history_len - 1; i++)
        {
            state->history[i] = state->history[i + 1];
        }
        state->history_len = state->history_len - 1;
        printf("Deleted entry %d.\n", idx + 1);
    }
    else
    {
        printf("Invalid index. Use :history to see valid indices.\n");
    }
    return REPL_HANDLED;
}

static int cmd_history(ReplState *state, const char *args)
{
    (void)args;
    printf("Session History:\n");
    for (int i = 0; i < state->history_len; i++)
    {
        printf("%4d  %s\n", i + 1, state->history[i]);
    }
    return REPL_HANDLED;
}

static int cmd_imports(ReplState *state, const char *args)
{
    (void)args;
    printf("Active Imports:\n");
    for (int i = 0; i < state->history_len; i++)
    {
        if (is_header_line(state->history[i]))
        {
            printf("  %s\n", state->history[i]);
        }
    }
    return REPL_HANDLED;
}

static int cmd_show(ReplState *state, const char *args)
{
    const char *name = args;
    while (*name && isspace(*name))
    {
        name++;
    }

    int found = 0;
    printf("Source definition for '%s':\n", name);

    for (int i = state->history_len - 1; i >= 0; i--)
    {
        if (is_definition_of(state->history[i], name))
        {
            printf("  \033[90m// Found in history:\033[0m\n");
            printf("  ");
            repl_highlight(state->history[i], -1);
            printf("\n");
            found = 1;
            break;
        }
    }

    if (found)
    {
        return REPL_HANDLED;
    }

    printf("Source definition for '%s':\n", name);

    size_t show_code_size = 4096;
    for (int i = 0; i < state->history_len; i++)
    {
        show_code_size += strlen(state->history[i]) + 2;
    }
    char *show_code = malloc(show_code_size);
    show_code[0] = '\0';
    for (int i = 0; i < state->history_len; i++)
    {
        strcat(show_code, state->history[i]);
        strcat(show_code, "\n");
    }

    ParserContext ctx;
    repl_parser_ctx_init(&ctx);
    ctx.cg.skip_preamble = 1;
    ctx.is_fault_tolerant = 1;
    Lexer l;
    lexer_init(&l, show_code, &g_compiler.config, ctx.current_filename);
    ASTNode *nodes = parse_program(&ctx, &l);

    ASTNode *search = nodes;
    if (search && search->kind == NODE_ROOT)
    {
        search = search->root.children;
    }

    for (ASTNode *n = search; n; n = n->next)
    {
        if (n->kind == NODE_FUNCTION && 0 == strcmp(n->func.name, name))
        {
            printf("  fn %s(%s) -> %s\n", n->func.name, n->func.args ? n->func.args : "",
                   n->func.ret_type ? n->func.ret_type : "void");
            found = 1;
            break;
        }
        else if (n->kind == NODE_STRUCT && 0 == strcmp(n->strct.name, name))
        {
            printf("  struct %s {\n", n->strct.name);
            for (ASTNode *field = n->strct.fields; field; field = field->next)
            {
                if (field->kind == NODE_FIELD)
                {
                    printf("    %s: %s;\n", field->field.name, field->field.type);
                }
                else if (field->kind == NODE_VAR_DECL)
                {
                    printf("    %s: %s;\n", field->var_decl.name, field->var_decl.type_str);
                }
            }
            printf("  }\n");
            found = 1;
            break;
        }
    }
    if (!found)
    {
        printf("  (not found)\n");
    }
    zfree(show_code);
    return REPL_HANDLED;
}

static int cmd_edit(ReplState *state, const char *args)
{
    int idx = state->history_len - 1;
    if (args[0])
    {
        idx = (int)strtol(args, NULL, 10) - 1;
    }

    const char *editor = getenv("EDITOR");

    if (state->history_len == 0)
    {
        printf("History is empty.\n");

        if (!editor)
        {
            editor = "vi";
        }

        char edit_path[MAX_PATH_SIZE];
        const char *tmpdir = z_get_temp_dir();
        snprintf(edit_path, sizeof(edit_path), "%s/zprep_edit_%d.zc", tmpdir, rand());
        FILE *f = fopen(edit_path, "w");
        if (f)
        {
            fclose(f);

            char cmdbuf[4096];
#if ZC_OS_WINDOWS
            snprintf(cmdbuf, sizeof(cmdbuf), "\"%s \"%s\"\"", editor, edit_path);
#else
            snprintf(cmdbuf, sizeof(cmdbuf), "%s \"%s\"", editor, edit_path);
#endif
            int status = system(cmdbuf);

            if (0 == status)
            {
                FILE *fr = fopen(edit_path, "r");
                if (fr)
                {
                    fseek(fr, 0, SEEK_END);
                    long length = ftell(fr);
                    if (length < 0)
                    {
                        fclose(fr);
                        return REPL_HANDLED;
                    }
                    fseek(fr, 0, SEEK_SET);
                    char *buffer = malloc((size_t)(length + 1));
                    if (buffer)
                    {
                        size_t nread = fread(buffer, 1, (size_t)(length), fr);
                        if (nread != (size_t)(length))
                        {
                            zfree(buffer);
                            fclose(fr);
                            return REPL_HANDLED;
                        }
                        buffer[length] = 0;

                        while (length > 0 && buffer[length - 1] == '\n')
                        {
                            buffer[--length] = 0;
                        }

                        if (strlen(buffer) > 0)
                        {
                            printf("Running: %s\n", buffer);
                            repl_history_add(state, buffer);
                        }
                        else
                        {
                            zfree(buffer);
                        }
                    }
                    fclose(fr);
                }
            }
            remove(edit_path);
        }
        return REPL_HANDLED;
    }

    if (idx < 0 || idx >= state->history_len)
    {
        printf("Invalid index.\n");
        return REPL_HANDLED;
    }

    char edit_path[MAX_PATH_SIZE];
    const char *tmpdir = z_get_temp_dir();
    snprintf(edit_path, sizeof(edit_path), "%s/zprep_edit_%d.zc", tmpdir, rand());
    FILE *f = fopen(edit_path, "w");
    if (f)
    {
        fprintf(f, "%s", state->history[idx]);
        fclose(f);

        if (!editor)
        {
            editor = "nano";
        }

        char cmdbuf[4096];
#if ZC_OS_WINDOWS
        snprintf(cmdbuf, sizeof(cmdbuf), "\"%s \"%s\"\"", editor, edit_path);
#else
        snprintf(cmdbuf, sizeof(cmdbuf), "%s \"%s\"", editor, edit_path);
#endif
        int status = system(cmdbuf);

        if (0 == status)
        {
            FILE *fr = fopen(edit_path, "r");
            if (fr)
            {
                fseek(fr, 0, SEEK_END);
                long length = ftell(fr);
                if (length < 0)
                {
                    fclose(fr);
                    return REPL_HANDLED;
                }
                fseek(fr, 0, SEEK_SET);
                char *buffer = malloc((size_t)(length + 1));
                if (buffer)
                {
                    size_t nread = fread(buffer, 1, (size_t)(length), fr);
                    if (nread != (size_t)(length))
                    {
                        zfree(buffer);
                        fclose(fr);
                        return REPL_HANDLED;
                    }
                    buffer[length] = 0;

                    while (length > 0 && buffer[length - 1] == '\n')
                    {
                        buffer[--length] = 0;
                    }

                    if (strlen(buffer) > 0)
                    {
                        printf("Running: %s\n", buffer);
                        repl_history_add(state, buffer);
                    }
                    else
                    {
                        zfree(buffer);
                    }
                }
                fclose(fr);
            }
        }
        remove(edit_path);
    }
    return REPL_HANDLED;
}

static int cmd_watch(ReplState *state, const char *args)
{
    char *expr = (char *)args;
    while (*expr == ' ')
    {
        expr++;
    }
    size_t l = strlen(expr);
    while (l > 0 && expr[l - 1] == ' ')
    {
        expr[--l] = 0;
    }

    if (l > 0)
    {
        if (state->watches_len < REPL_MAX_WATCHES)
        {
            state->watches[state->watches_len++] = xstrdup(expr);
            printf("Watching: %s\n", expr);
        }
        else
        {
            printf("Watch list full (max %d).\n", REPL_MAX_WATCHES);
        }
    }
    else
    {
        if (state->watches_len == 0)
        {
            printf("No active watches.\n");
        }
        else
        {
            for (int i = 0; i < state->watches_len; i++)
            {
                printf("%d: %s\n", i + 1, state->watches[i]);
            }
        }
    }
    return REPL_HANDLED;
}

static int cmd_unwatch(ReplState *state, const char *args)
{
    int idx = (int)strtol(args, NULL, 10) - 1;
    if (idx >= 0 && idx < state->watches_len)
    {
        zfree(state->watches[idx]);
        for (int i = idx; i < state->watches_len - 1; i++)
        {
            state->watches[i] = state->watches[i + 1];
        }
        state->watches_len--;
        printf("Removed watch %d.\n", idx + 1);
    }
    else
    {
        printf("Invalid index.\n");
    }
    return REPL_HANDLED;
}

static int cmd_save(ReplState *state, const char *args)
{
    const char *filename = args;
    FILE *f = fopen(filename, "w");
    if (f)
    {
        char *global_code = NULL;
        char *main_code = NULL;
        repl_get_code(state->history, state->history_len, &global_code, &main_code);

        fprintf(f, "%s\n", global_code);
        fprintf(f, "\nfn main() {\n%s\n}\n", main_code);

        zfree(global_code);
        zfree(main_code);
        fclose(f);
        printf("Session saved to %s\n", filename);
    }
    else
    {
        printf("Error: Cannot write to %s\n", filename);
    }
    return REPL_HANDLED;
}

static int cmd_load(ReplState *state, const char *args)
{
    const char *filename = args;
    FILE *f = fopen(filename, "r");
    if (f)
    {
        char buf[MAX_ERROR_MSG_LEN];
        int count = 0;
        int in_main_wrapper = 0;
        while (fgets(buf, sizeof(buf), f))
        {
            size_t l = strlen(buf);
            if (l > 0 && buf[l - 1] == '\n')
            {
                buf[--l] = 0;
            }
            if (l == 0)
            {
                continue;
            }
            /* Strip the `fn main() { ... }` wrapper emitted by :save so that
             * loading a saved session brings in the statements, not the
             * synthesized entry point (defining main again would break the
             * session). */
            if (!in_main_wrapper &&
                (strncmp(buf, "fn main() {", 11) == 0 || strncmp(buf, "fn main(){", 10) == 0))
            {
                in_main_wrapper = 1;
                continue;
            }
            if (in_main_wrapper && strcmp(buf, "}") == 0)
            {
                in_main_wrapper = 0;
                continue;
            }
            repl_history_add(state, buf);
            count++;
        }
        fclose(f);
        printf("Loaded %d lines from %s\n", count, filename);
    }
    else
    {
        printf("Error: Cannot read %s\n", filename);
    }
    return REPL_HANDLED;
}

static int cmd_run(ReplState *state, const char *args)
{
    (void)args;
    char *global_code = NULL;
    char *main_code = NULL;
    repl_get_code(state->history, state->history_len, &global_code, &main_code);

    size_t code_size = strlen(global_code) + strlen(main_code) + 128;
    char *code = malloc(code_size);

    snprintf(code, code_size, "%s\nfn main() { %s }", global_code, main_code);
    zfree(global_code);
    zfree(main_code);

    char tmp_path[MAX_PATH_SIZE];
    snprintf(tmp_path, sizeof(tmp_path), "%s/zprep_repl_run_%d.zc", z_get_temp_dir(), rand());
    FILE *f = fopen(tmp_path, "w");
    if (f)
    {
        fprintf(f, "%s", code);
        fclose(f);
        char cmdbuf[2048];
#if ZC_OS_WINDOWS
        snprintf(cmdbuf, sizeof(cmdbuf), "\"\"%s\" run \"%s\"\"", state->compiler_path, tmp_path);
#else
        snprintf(cmdbuf, sizeof(cmdbuf), "\"%s\" run \"%s\"", state->compiler_path, tmp_path);
#endif
        {
            int z_ret = system(cmdbuf);
            (void)z_ret;
        }
    }
    zfree(code);
    return REPL_HANDLED;
}

static int cmd_vars_funcs_structs(ReplState *state, const char *args)
{
    /* Determine which sub-command was invoked from args.
     * The caller passes the full command buffer so we re-check. */
    int want_vars = (strstr(args, "vars") != NULL);
    int want_funcs = (strstr(args, "funcs") != NULL);
    int want_structs = (strstr(args, "structs") != NULL);

    char *global_code = NULL;
    char *main_code = NULL;
    repl_get_code(state->history, state->history_len, &global_code, &main_code);

    size_t code_size = strlen(global_code) + strlen(main_code) + 128;
    char *code = malloc(code_size);
    snprintf(code, code_size, "%s\nfn main() { %s }", global_code, main_code);
    zfree(global_code);
    zfree(main_code);

    ParserContext ctx;
    repl_parser_ctx_init(&ctx);
    ctx.cg.skip_preamble = 1;
    ctx.is_fault_tolerant = 1;

    Lexer l;
    lexer_init(&l, code, &g_compiler.config, ctx.current_filename);
    ASTNode *nodes = parse_program(&ctx, &l);

    ASTNode *search = nodes;
    if (search && search->kind == NODE_ROOT)
    {
        search = search->root.children;
    }

    if (want_vars)
    {
        ASTNode *main_func = NULL;
        for (ASTNode *n = search; n; n = n->next)
        {
            if (n->kind == NODE_FUNCTION && 0 == strcmp(n->func.name, "main"))
            {
                main_func = n;
                break;
            }
        }

        /* Generate probe code to print values */
        char *probe_global_code = NULL;
        char *probe_main_code = NULL;
        repl_get_code(state->history, state->history_len, &probe_global_code, &probe_main_code);

        size_t probe_size = strlen(probe_global_code) + strlen(probe_main_code) + 4096;
        char *probe_code = malloc(probe_size);

        snprintf(probe_code, probe_size,
                 "%s\nfn main() { _z_suppress_stdout(); %s _z_restore_stdout(); "
                 "printf(\"Variables:\\n\"); ",
                 probe_global_code, probe_main_code);
        zfree(probe_global_code);
        zfree(probe_main_code);

        int found_vars = 0;
        if (main_func && main_func->func.body && main_func->func.body->kind == NODE_BLOCK)
        {
            for (ASTNode *s = main_func->func.body->block.statements; s; s = s->next)
            {
                if (s->kind == NODE_VAR_DECL)
                {
                    char *t = s->var_decl.type_str ? s->var_decl.type_str : "Inferred";
                    char fmt[64];
                    char val_expr[128];

                    if (s->var_decl.type_str)
                    {
                        if (strcmp(t, "int") == 0 || strcmp(t, "i32") == 0 ||
                            strcmp(t, "I32") == 0 || strcmp(t, "int32_t") == 0 ||
                            strcmp(t, "i16") == 0 || strcmp(t, "I16") == 0 ||
                            strcmp(t, "int16_t") == 0 || strcmp(t, "i8") == 0 ||
                            strcmp(t, "I8") == 0 || strcmp(t, "int8_t") == 0 ||
                            strcmp(t, "short") == 0 || strcmp(t, "rune") == 0)
                        {
                            strcpy(fmt, "%d");
                            strcpy(val_expr, s->var_decl.name);
                        }
                        else if (strcmp(t, "uint") == 0 || strcmp(t, "u32") == 0 ||
                                 strcmp(t, "U32") == 0 || strcmp(t, "uint32_t") == 0 ||
                                 strcmp(t, "u16") == 0 || strcmp(t, "U16") == 0 ||
                                 strcmp(t, "uint16_t") == 0 || strcmp(t, "u8") == 0 ||
                                 strcmp(t, "U8") == 0 || strcmp(t, "uint8_t") == 0 ||
                                 strcmp(t, "byte") == 0 || strcmp(t, "ushort") == 0)
                        {
                            strcpy(fmt, "%u");
                            strcpy(val_expr, s->var_decl.name);
                        }
                        else if (strcmp(t, "i64") == 0 || strcmp(t, "I64") == 0 ||
                                 strcmp(t, "int64_t") == 0 || strcmp(t, "long") == 0 ||
                                 strcmp(t, "isize") == 0 || strcmp(t, "ptrdiff_t") == 0)
                        {
                            strcpy(fmt, "%ld");
                            snprintf(val_expr, sizeof(val_expr), "(long)%s", s->var_decl.name);
                        }
                        else if (strcmp(t, "u64") == 0 || strcmp(t, "U64") == 0 ||
                                 strcmp(t, "uint64_t") == 0 || strcmp(t, "ulong") == 0 ||
                                 strcmp(t, "usize") == 0 || strcmp(t, "size_t") == 0)
                        {
                            strcpy(fmt, "%lu");
                            snprintf(val_expr, sizeof(val_expr), "(unsigned long)%s",
                                     s->var_decl.name);
                        }
                        else if (strcmp(t, "float") == 0 || strcmp(t, "double") == 0 ||
                                 strcmp(t, "f32") == 0 || strcmp(t, "f64") == 0 ||
                                 strcmp(t, "F32") == 0 || strcmp(t, "F64") == 0)
                        {
                            strcpy(fmt, "%f");
                            strcpy(val_expr, s->var_decl.name);
                        }
                        else if (strcmp(t, "bool") == 0)
                        {
                            strcpy(fmt, "%s");
                            snprintf(val_expr, sizeof(val_expr), "%s ? \"true\" : \"false\"",
                                     s->var_decl.name);
                        }
                        else if (strcmp(t, "string") == 0 || strcmp(t, "char*") == 0)
                        {
                            strcpy(fmt, "\\\"%s\\\"");
                            strcpy(val_expr, s->var_decl.name);
                        }
                        else if (strcmp(t, "char") == 0)
                        {
                            strcpy(fmt, "'%c'");
                            strcpy(val_expr, s->var_decl.name);
                        }
                        else
                        {
                            strcpy(fmt, "@%p");
                            snprintf(val_expr, sizeof(val_expr), "(void*)&%s", s->var_decl.name);
                        }
                    }
                    else
                    {
                        strcpy(fmt, "? @%p");
                        snprintf(val_expr, sizeof(val_expr), "(void*)&%s", s->var_decl.name);
                    }

                    char print_stmt[MAX_ERROR_MSG_LEN];
                    snprintf(print_stmt, sizeof(print_stmt), "printf(\"  %s (%s): %s\\n\", %s); ",
                             s->var_decl.name, t, fmt, val_expr);
                    strcat(probe_code, print_stmt);
                    found_vars = 1;
                }
            }
        }

        if (!found_vars)
        {
            strcat(probe_code, "printf(\"  (none)\\n\");");
        }

        strcat(probe_code, " }");

        /* Execute */
        char tmp_path[MAX_PATH_SIZE];
        snprintf(tmp_path, sizeof(tmp_path), "%s/zen_repl_vars_%d.zc", z_get_temp_dir(),
                 z_get_pid());
        FILE *f = fopen(tmp_path, "w");
        if (f)
        {
            fprintf(f, "%s", probe_code);
            fclose(f);
            char cmdbuf[4096];
#if ZC_OS_WINDOWS
            snprintf(cmdbuf, sizeof(cmdbuf), "\"\"%s\" run -q \"%s\"\"", state->compiler_path,
                     tmp_path);
#else
            snprintf(cmdbuf, sizeof(cmdbuf), "\"%s\" run -q \"%s\"", state->compiler_path,
                     tmp_path);
#endif
            {
                int z_ret = system(cmdbuf);
                (void)z_ret;
            }
            remove(tmp_path);
        }
        zfree(probe_code);
    }
    else if (want_funcs)
    {
        printf("Functions:\n");
        int found = 0;
        for (ASTNode *n = search; n; n = n->next)
        {
            if (n->kind == NODE_FUNCTION && 0 != strcmp(n->func.name, "main"))
            {
                printf("  fn %s()\n", n->func.name);
                found = 1;
            }
        }
        if (!found)
        {
            printf("  (none)\n");
        }
    }
    else if (want_structs)
    {
        printf("Structs:\n");
        int found = 0;
        for (ASTNode *n = search; n; n = n->next)
        {
            if (n->kind == NODE_STRUCT)
            {
                printf("  struct %s\n", n->strct.name);
                found = 1;
            }
        }
        if (!found)
        {
            printf("  (none)\n");
        }
    }

    zfree(code);
    return REPL_HANDLED;
}

static int cmd_type(ReplState *state, const char *args)
{
    const char *expr = args;

    char *global_code = NULL;
    char *main_code = NULL;
    repl_get_code(state->history, state->history_len, &global_code, &main_code);

    size_t probe_size = strlen(global_code) + strlen(main_code) + strlen(expr) + 4096;
    char *probe_code = malloc(probe_size);

    snprintf(probe_code, probe_size, "%s\nfn main() { _z_suppress_stdout(); %s", global_code,
             main_code);
    zfree(global_code);
    zfree(main_code);

    strcat(probe_code, " raw { typedef struct { int _u; } __REVEAL_TYPE__; } ");
    strcat(probe_code, " let _z_type_probe: __REVEAL_TYPE__; _z_type_probe = (");
    strcat(probe_code, expr);
    strcat(probe_code, "); }");

    char tmp_path[MAX_PATH_SIZE];
    const char *tmpdir = z_get_temp_dir();
    snprintf(tmp_path, sizeof(tmp_path), "%s/zprep_repl_type_%d.zc", tmpdir, rand());
    FILE *f = fopen(tmp_path, "w");
    if (f)
    {
        fprintf(f, "%s", probe_code);
        fclose(f);

        char cmdbuf[2048];
#if ZC_OS_WINDOWS
        snprintf(cmdbuf, sizeof(cmdbuf), "\"\"%s\" run -q \"%s\" 2>&1\"", state->compiler_path,
                 tmp_path);
#else
        snprintf(cmdbuf, sizeof(cmdbuf), "\"%s\" run -q \"%s\" 2>&1", state->compiler_path,
                 tmp_path);
#endif

        FILE *p = popen(cmdbuf, "r");
        if (p)
        {
            char buf[MAX_ERROR_MSG_LEN];
            int found = 0;
            while (fgets(buf, sizeof(buf), p))
            {
                char *start = (char *)strstr(buf, "from type ");
                char quote = 0;
                if (!start)
                {
                    start = strstr(buf, "incompatible type ");
                }

                if (start)
                {
                    char *q = (char *)strchr(start, '\'');
                    if (!q)
                    {
                        q = strstr(start, "\xe2\x80\x98");
                    }

                    if (q)
                    {
                        if (*q == '\'')
                        {
                            start = q + 1;
                            quote = '\'';
                        }
                        else
                        {
                            start = q + 3;
                            quote = 0;
                        }

                        char *end = NULL;
                        if (quote)
                        {
                            end = strchr(start, quote);
                        }
                        else
                        {
                            end = strstr(start, "\xe2\x80\x99");
                        }

                        if (end)
                        {
                            *end = 0;
                            printf("\033[1;36mType: %s\033[0m\n", start);
                            found = 1;
                            break;
                        }
                    }
                }
            }
            pclose(p);
            if (!found)
            {
                printf("Type: <unknown>\n");
            }
        }
    }
    zfree(probe_code);
    return REPL_HANDLED;
}

static int cmd_time(ReplState *state, const char *args)
{
    if (z_is_windows())
    {
        printf("Command ':time' is not supported on Windows yet.\n");
        return REPL_HANDLED;
    }
    const char *expr = args;

    char *global_code = NULL;
    char *main_code = NULL;
    repl_get_code(state->history, state->history_len, &global_code, &main_code);

    size_t code_size = strlen(global_code) + strlen(main_code) + strlen(expr) + 4096;
    char *code = malloc(code_size);

    snprintf(code, code_size,
             "%s\ninclude \"time.h\"\nfn main() { _z_suppress_stdout();\n%s "
             "_z_restore_stdout();\n",
             global_code, main_code);
    zfree(global_code);
    zfree(main_code);

    strcat(code, "raw { clock_t _start = clock(); }\n");
    strcat(code, "for _i in 0..1000 { ");
    strcat(code, expr);
    strcat(code, "; }\n");
    strcat(code, "raw { clock_t _end = clock(); double _elapsed = (double)(_end - "
                 "_start) / CLOCKS_PER_SEC; printf(\"1000 iterations: %.4fs "
                 "(%.6fs/iter)\\n\", _elapsed, _elapsed/1000); }\n");
    strcat(code, "}");

    char tmp_path[MAX_PATH_SIZE];
    const char *tmpdir = z_get_temp_dir();
    snprintf(tmp_path, sizeof(tmp_path), "%s/zprep_repl_time_%d.zc", tmpdir, rand());
    FILE *f = fopen(tmp_path, "w");
    if (f)
    {
        fprintf(f, "%s", code);
        fclose(f);
        char cmdbuf[2048];
#if ZC_OS_WINDOWS
        snprintf(cmdbuf, sizeof(cmdbuf), "\"\"%s\" run -q \"%s\"\"", state->compiler_path,
                 tmp_path);
#else
        snprintf(cmdbuf, sizeof(cmdbuf), "\"%s\" run -q \"%s\"", state->compiler_path, tmp_path);
#endif
        {
            int z_ret = system(cmdbuf);
            (void)z_ret;
        }
    }
    zfree(code);
    return REPL_HANDLED;
}

static int cmd_c(ReplState *state, const char *args)
{
    if (z_is_windows())
    {
        printf("Command ':c' (transpile inspection) is not supported on Windows yet.\n");
        return REPL_HANDLED;
    }
    size_t expr_buf_size = 8192;
    char *expr_buf = malloc(expr_buf_size);
    snprintf(expr_buf, expr_buf_size, "%s", args);

    int cmd_brace_depth = 0;
    for (char *p = expr_buf; *p; p++)
    {
        if (*p == '{')
        {
            cmd_brace_depth++;
        }
        else if (*p == '}')
        {
            cmd_brace_depth--;
        }
    }

    while (cmd_brace_depth > 0)
    {
        char *more = repl_readline(state, "... ", cmd_brace_depth);
        if (!more)
        {
            break;
        }
        size_t needed = strlen(expr_buf) + strlen(more) + 2;
        if (needed >= expr_buf_size)
        {
            expr_buf_size = needed * 2;
            expr_buf = realloc(expr_buf, expr_buf_size);
        }
        strcat(expr_buf, "\n");
        strcat(expr_buf, more);
        for (char *p = more; *p; p++)
        {
            if (*p == '{')
            {
                cmd_brace_depth++;
            }
            else if (*p == '}')
            {
                cmd_brace_depth--;
            }
        }
        zfree(more);
    }

    char *global_code = NULL;
    char *main_code = NULL;
    repl_get_code(state->history, state->history_len, &global_code, &main_code);

    size_t code_size = strlen(global_code) + strlen(main_code) + strlen(expr_buf) + 128;
    char *code = malloc(code_size);

    snprintf(code, code_size, "%s\nfn main() { %s %s }", global_code, main_code, expr_buf);
    zfree(global_code);
    zfree(main_code);
    zfree(expr_buf);

    char tmp_path[MAX_PATH_SIZE];
    snprintf(tmp_path, sizeof(tmp_path), "%s/zprep_repl_c_%d.zc", z_get_temp_dir(), rand());
    char out_path[MAX_PATH_SIZE];
    snprintf(out_path, sizeof(out_path), "%s/zprep_repl_out", z_get_temp_dir());
    FILE *f = fopen(tmp_path, "w");
    if (f)
    {
        fprintf(f, "%s", code);
        fclose(f);
        char cmdbuf[4096];
        snprintf(cmdbuf, sizeof(cmdbuf),
                 "\"%s\" build -q --emit-c -o \"%s\" \"%s\" > /dev/null 2>&1", state->compiler_path,
                 out_path, tmp_path);
        if (system(cmdbuf) == 0)
        {
            char full_out_path[MAX_PATH_SIZE + 4];
            snprintf(full_out_path, sizeof(full_out_path), "%s.c", out_path);
            repl_extract_c_code(full_out_path);
            remove(full_out_path);
        }

        remove(tmp_path);
    }
    zfree(code);
    return REPL_HANDLED;
}

static int cmd_doc(ReplState *state, const char *args)
{
    char sym_buf[MAX_VAR_NAME_LEN];
    strncpy(sym_buf, args, sizeof(sym_buf) - 1);
    sym_buf[sizeof(sym_buf) - 1] = 0;

    char *sym = sym_buf;
    while (*sym == ' ')
    {
        sym++;
    }
    size_t symlen = strlen(sym);
    while (symlen > 0 && sym[symlen - 1] == ' ')
    {
        sym[--symlen] = 0;
    }

    const ReplDoc *doc = repl_find_doc(state, sym);
    if (doc)
    {
        printf("\033[1;36m%s\033[0m\n%s\n", doc->name, doc->doc);
        return REPL_HANDLED;
    }

    /* Fallback: try man page (POSIX only) */
    if (z_is_windows())
    {
        printf("No documentation for '%s'. (Man pages not available on Windows)\n", sym);
        return REPL_HANDLED;
    }

    /* Sanitize symbol name */
    char safe_sym[MAX_VAR_NAME_LEN];
    size_t slen = strlen(sym);
    if (slen > 255)
    {
        slen = 255;
    }
    strncpy(safe_sym, sym, (size_t)(slen));
    safe_sym[slen] = 0;
    for (int i = 0; safe_sym[i]; i++)
    {
        if (!isalnum((unsigned char)safe_sym[i]) && safe_sym[i] != '_' && safe_sym[i] != ':' &&
            safe_sym[i] != '.')
        {
            safe_sym[i] = '_';
        }
    }

    char man_cmd[MAX_MANGLED_NAME_LEN];
    snprintf(man_cmd, sizeof(man_cmd),
             "man 3 %s 2>/dev/null | sed -n '/^SYNOPSIS/,/^[A-Z]/p' | head -10", safe_sym);
    FILE *mp = popen(man_cmd, "r");
    if (mp)
    {
        char buf[MAX_SHORT_MSG_LEN];
        int lines = 0;
        while (fgets(buf, sizeof(buf), mp) && lines < 8)
        {
            printf("%s", buf);
            lines++;
        }
        int status = pclose(mp);
        if (0 == status && lines > 0)
        {
            printf("\033[90m(man 3 %s)\033[0m\n", sym);
            return REPL_HANDLED;
        }
    }

    printf("No documentation for '%s'.\n", sym);
    return REPL_HANDLED;
}

/* Special handler IDs for :vars/:funcs/:structs which share implementation */
static int cmd_vars(ReplState *state, const char *args)
{
    (void)args;
    return cmd_vars_funcs_structs(state, "vars");
}
static int cmd_funcs(ReplState *state, const char *args)
{
    (void)args;
    return cmd_vars_funcs_structs(state, "funcs");
}
static int cmd_structs(ReplState *state, const char *args)
{
    (void)args;
    return cmd_vars_funcs_structs(state, "structs");
}

static const ReplCommand command_table[] = {
    {"help", "Show this help", 0, cmd_help},
    {"reset", "Clear history", 0, cmd_reset},
    {"quit", "Exit REPL", 0, cmd_quit},
    {"clear", "Clear screen", 0, cmd_clear},
    {"undo", "Remove last command", 0, cmd_undo},
    {"delete", "Remove command at index n", 1, cmd_delete},
    {"history", "Show command history", 0, cmd_history},
    {"imports", "Show active imports", 0, cmd_imports},
    {"show", "Show source definition", 1, cmd_show},
    {"edit", "Edit command n in $EDITOR", 0, cmd_edit},
    {"watch", "Watch expression output", 0, cmd_watch},
    {"unwatch", "Remove watch n", 1, cmd_unwatch},
    {"save", "Save session to file", 1, cmd_save},
    {"load", "Load file into session", 1, cmd_load},
    {"run", "Execute full session", 0, cmd_run},
    {"vars", "Show active variables", 0, cmd_vars},
    {"funcs", "Show user functions", 0, cmd_funcs},
    {"structs", "Show user structs", 0, cmd_structs},
    {"type", "Show type of expression", 1, cmd_type},
    {"time", "Benchmark expression (1000 iters)", 1, cmd_time},
    {"c", "Show generated C code", 1, cmd_c},
    {"doc", "Show documentation for symbol", 1, cmd_doc},
    {"reload", "Hot-reload session plugins", 0, cmd_reload},
    {"plot", "Render bar chart of data", 1, cmd_plot},
    {NULL, NULL, 0, NULL}};

void repl_print_help(void)
{
    printf("REPL Commands:\n");
    for (int i = 0; command_table[i].name; i++)
    {
        if (command_table[i].takes_arg)
        {
            printf("  :%-10s <arg>  %s\n", command_table[i].name, command_table[i].help);
        }
        else
        {
            printf("  :%-16s  %s\n", command_table[i].name, command_table[i].help);
        }
    }
    printf("  ! <cmd>             Run shell command\n");
    printf("\nShortcuts:\n");
    printf("  Up/Down     History navigation\n");
    printf("  Tab         Completion\n");
    printf("  Ctrl+A      Go to start\n");
    printf("  Ctrl+E      Go to end\n");
    printf("  Ctrl+L      Clear screen\n");
    printf("  Ctrl+U      Clear line to start\n");
    printf("  Ctrl+K      Clear line to end\n");
}

int repl_dispatch_command(ReplState *state, const char *cmd_buf)
{
    if (cmd_buf[0] != ':')
    {
        return REPL_UNKNOWN;
    }

    const char *cmd_start = cmd_buf + 1;

    for (int i = 0; command_table[i].name; i++)
    {
        size_t name_len = strlen(command_table[i].name);
        if (strncmp(cmd_start, command_table[i].name, (size_t)(name_len)) == 0)
        {
            char next = cmd_start[name_len];
            if (next == 0 || isspace(next))
            {
                /* Extract argument (everything after the command name + whitespace) */
                const char *args = cmd_start + name_len;
                while (*args && isspace(*args))
                {
                    args++;
                }
                return command_table[i].handler(state, args);
            }
        }
    }

    printf("Unknown command: %s\n", cmd_buf);
    printf("Type :help for available commands.\n");
    return REPL_HANDLED;
}
