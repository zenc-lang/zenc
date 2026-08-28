// SPDX-License-Identifier: MIT
#include "json_rpc.h"
#include "../constants.h"
#include "zprep.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int g_lsp_request_is_readonly = 1;

// Simple Main Loop for LSP.
int lsp_main(int argc, char **argv)
{
    int verbose = 0;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0)
        {
            verbose = 1;
        }
    }

    // Redirect stderr to /dev/null unless --verbose is passed.
    // This prevents the stderr pipe buffer from filling up and blocking the
    // LSP's main loop, which previously caused "cannot resume dead coroutine"
    // errors in Neovim's RPC client.
    if (!verbose)
    {
        if (!freopen("/dev/null", "w", stderr))
        {
        }
    }

    g_config.mode_lsp = 1;
    g_config.json_output = 1;

    // Initialize root path from executable to find std/
    char self_path[MAX_PATH_LEN];
    z_get_executable_path(self_path, sizeof(self_path));
    if (self_path[0])
    {
        g_config.root_path = xstrdup(self_path);
    }

    while (1)
    {
        // Read headers
        int content_len = 0;
        char line[MAX_MANGLED_NAME_LEN];
        while (fgets(line, sizeof(line), stdin))
        {
            if (0 == strcmp(line, "\r\n"))
            {
                break; // End of headers
            }
            if (0 == strncmp(line, "Content-Length: ", 16))
            {
                content_len = (int)strtol(line + 16, NULL, 10);
            }
        }

        if (content_len <= 0)
        {
            if (feof(stdin))
            {
                break;
            }
            continue;
        }

        if (content_len > 10 * 1024 * 1024)
        {
            fprintf(stderr, "zls: Content-Length too large (%d)\n", content_len);
            break;
        }

        // Read body (use libc malloc/free to avoid arena leak for long-running LSP).
        char *body = (char *)libc_malloc((size_t)content_len + 1);
        if (!body || fread(body, 1, (size_t)content_len, stdin) != (size_t)content_len)
        {
            fprintf(stderr, "zls: Error reading body\n");
            libc_free(body);
            break;
        }
        body[content_len] = 0;

        // Save arena mark before processing. For read-only requests (hover,
        // goto-def, etc.) we restore the mark after, preventing the arena from
        // growing unboundedly. Write requests (didOpen, didChange, etc.)
        // modify persistent project data and keep the arena growth.
        ZarenaMark arena_mark = zarena_save(&g_compiler.arena);
        g_lsp_request_is_readonly = 1;

        // Process JSON-RPC.
        handle_request(body);

        if (g_lsp_request_is_readonly)
        {
            zarena_restore(&g_compiler.arena, arena_mark);
            clear_registered_traits();
        }

        libc_free(body);
    }

    return 0;
}
