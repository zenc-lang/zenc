// SPDX-License-Identifier: MIT
#include "json_rpc.h"
#include "../utils/cJSON.h"
#include "lsp_project.h"
#include "lsp_formatter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Prototype

// Decode percent-encoded URI in-place (e.g. "file%20name" -> "file name")
static void uri_decode(char *s)
{
    if (!s)
    {
        return;
    }
    char *r = s;
    while (*s)
    {
        if (*s == '%' && s[1] && s[2])
        {
            char hi = (char)toupper((unsigned char)s[1]);
            char lo = (char)toupper((unsigned char)s[2]);
            if ((hi >= '0' && hi <= '9') || (hi >= 'A' && hi <= 'F'))
            {
                int v = (hi >= 'A' ? hi - 'A' + 10 : hi - '0') * 16;
                v += (lo >= 'A' ? lo - 'A' + 10 : lo - '0');
                *r++ = (char)v;
                s += 3;
                continue;
            }
        }
        *r++ = *s++;
    }
    *r = 0;
}

// Helper to extract textDocument params
static void get_params(cJSON *root, char **uri, int *line, int *col)
{
    cJSON *params = cJSON_GetObjectItem(root, "params");

    if (!params)
    {
        return;
    }

    cJSON *doc = cJSON_GetObjectItem(params, "textDocument");
    if (doc)
    {
        cJSON *u = cJSON_GetObjectItem(doc, "uri");
        if (u && u->valuestring)
        {
            *uri = xstrdup(u->valuestring);
            uri_decode(*uri);
        }
    }

    cJSON *pos = cJSON_GetObjectItem(params, "position");
    if (pos)
    {
        cJSON *l = cJSON_GetObjectItem(pos, "line");
        cJSON *c = cJSON_GetObjectItem(pos, "character");
        if (l)
        {
            *line = l->valueint;
        }
        if (c)
        {
            *col = c->valueint;
        }
    }
}

void handle_request(const char *json_str)
{
    cJSON *json = cJSON_Parse(json_str);
    if (!json)
    {
        return;
    }

    int id = 0;
    cJSON *id_item = cJSON_GetObjectItem(json, "id");
    if (id_item)
    {
        id = id_item->valueint;
    }

    cJSON *method_item = cJSON_GetObjectItem(json, "method");
    if (!method_item || !method_item->valuestring)
    {
        cJSON_Delete(json);
        return;
    }
    char *method = method_item->valuestring;

    if (strcmp(method, "initialize") == 0)
    {
        g_lsp_request_is_readonly = 0;
        cJSON *params = cJSON_GetObjectItem(json, "params");
        char *root = NULL;
        if (params)
        {
            cJSON *rp = cJSON_GetObjectItem(params, "rootPath");
            if (rp && rp->valuestring)
            {
                root = xstrdup(rp->valuestring);
                uri_decode(root);
            }
            else
            {
                cJSON *ru = cJSON_GetObjectItem(params, "rootUri");
                if (ru && ru->valuestring)
                {
                    root = xstrdup(ru->valuestring);
                }
            }

            // Try workspaceFolders if rootPath/rootUri weren't set
            if (!root)
            {
                cJSON *wfs = cJSON_GetObjectItem(params, "workspaceFolders");
                if (wfs && cJSON_GetArraySize(wfs) > 0)
                {
                    cJSON *wf = cJSON_GetArrayItem(wfs, 0);
                    cJSON *wf_uri = cJSON_GetObjectItem(wf, "uri");
                    if (wf_uri && wf_uri->valuestring)
                    {
                        root = xstrdup(wf_uri->valuestring);
                    }
                }
            }
        }

        if (root && strncmp(root, "file://", 7) == 0)
        {
            char *clean = xstrdup(root + 7);
            zfree(root);
            root = clean;
            uri_decode(root);
        }

        lsp_project_init(root ? root : ".");
        if (root)
        {
            zfree(root);
        }

        const char *response =
            "{\"jsonrpc\":\"2.0\",\"id\":0,\"result\":{"
            "\"serverInfo\":{\"name\":\"ZenC LS\",\"version\": \"1.0.0\"},"
            "\"capabilities\":{\"textDocumentSync\":{\"openClose\":true,\"change\":1},"
            "\"definitionProvider\":true,\"hoverProvider\":true,"
            "\"referencesProvider\":true,\"documentSymbolProvider\":true,"
            "\"renameProvider\":true,\"codeActionProvider\":true,"
            "\"documentFormattingProvider\":true,"
            "\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\"]},"
            "\"completionProvider\":{"
            "\"triggerCharacters\":[\".\"]},"
            "\"semanticTokensProvider\":{\"legend\":{\"tokenTypes\":[\"variable\",\"function\","
            "\"struct\",\"keyword\",\"string\",\"number\",\"comment\",\"type\",\"enum\",\"member\","
            "\"operator\",\"parameter\",\"macro\",\"typeParameter\"],\"tokenModifiers\":["
            "\"declaration\",\"definition\",\"readonly\","
            "\"static\",\"deprecated\",\"abstract\",\"async\",\"modification\",\"documentation\","
            "\"defaultLibrary\"]},\"full\":true}"
            "}}}";

        // Dynamically construct response with correct ID
        cJSON *res_json = cJSON_Parse(response);
        if (!res_json)
        {
            fprintf(stderr, "zls: Failed to construct initialize response\n");
            return;
        }
        cJSON_DeleteItemFromObject(res_json, "id");
        cJSON_AddNumberToObject(res_json, "id", id);

        char *str = cJSON_PrintUnformatted(res_json);
        fprintf(stdout, "Content-Length: %zu\r\n\r\n%s", strlen(str), str);
        fflush(stdout);
        zfree(str);
        cJSON_Delete(res_json);
        fflush(stdout);
    }
    else if (strcmp(method, "initialized") == 0)
    {
        g_lsp_request_is_readonly = 0;
        lsp_project_index_workspace();
    }
    else if (strcmp(method, "textDocument/didOpen") == 0 ||
             strcmp(method, "textDocument/didChange") == 0)
    {
        g_lsp_request_is_readonly = 0;
        cJSON *params = cJSON_GetObjectItem(json, "params");
        if (params)
        {
            cJSON *doc = cJSON_GetObjectItem(params, "textDocument");
            if (doc)
            {
                cJSON *uri = cJSON_GetObjectItem(doc, "uri");
                cJSON *text = cJSON_GetObjectItem(doc, "text");
                // For didChange, text is inside contentChanges
                if (!text && strcmp(method, "textDocument/didChange") == 0)
                {
                    cJSON *changes = cJSON_GetObjectItem(params, "contentChanges");
                    if (changes && cJSON_GetArraySize(changes) > 0)
                    {
                        cJSON *change = cJSON_GetArrayItem(changes, 0);
                        text = cJSON_GetObjectItem(change, "text");
                    }
                }

                if (uri && uri->valuestring && text && text->valuestring)
                {
                    uri_decode(uri->valuestring);
                    lsp_check_file(uri->valuestring, text->valuestring, id);
                }
            }
        }
    }
    else if (strcmp(method, "textDocument/definition") == 0)
    {
        char *uri = NULL;
        int line = 0, col = 0;
        get_params(json, &uri, &line, &col);
        if (uri)
        {
            lsp_goto_definition(uri, line, col, id);
            zfree(uri);
        }
    }
    else if (strcmp(method, "textDocument/hover") == 0)
    {
        char *uri = NULL;
        int line = 0, col = 0;
        get_params(json, &uri, &line, &col);
        if (uri)
        {
            lsp_hover(uri, line, col, id);
            zfree(uri);
        }
    }
    else if (strcmp(method, "textDocument/completion") == 0)
    {
        char *uri = NULL;
        int line = 0, col = 0;
        get_params(json, &uri, &line, &col);
        if (uri)
        {
            lsp_completion(uri, line, col, id);
            zfree(uri);
        }
    }
    else if (strcmp(method, "textDocument/documentSymbol") == 0)
    {
        char *uri = NULL;
        int line = 0, col = 0; // Unused for outline
        get_params(json, &uri, &line, &col);
        if (uri)
        {
            lsp_document_symbol(uri, id);
            zfree(uri);
        }
    }
    else if (strcmp(method, "textDocument/references") == 0)
    {
        char *uri = NULL;
        int line = 0, col = 0;
        get_params(json, &uri, &line, &col);
        if (uri)
        {
            lsp_references(uri, line, col, id);
            zfree(uri);
        }
    }
    else if (strcmp(method, "textDocument/signatureHelp") == 0)
    {
        char *uri = NULL;
        int line = 0, col = 0;
        get_params(json, &uri, &line, &col);
        if (uri)
        {
            lsp_signature_help(uri, line, col, id);
            zfree(uri);
        }
    }
    else if (strcmp(method, "textDocument/codeAction") == 0)
    {
        cJSON *params = cJSON_GetObjectItem(json, "params");
        if (params)
        {
            cJSON *uri_obj = cJSON_GetObjectItem(params, "textDocument");
            cJSON *uri = cJSON_GetObjectItem(uri_obj, "uri");
            cJSON *context = cJSON_GetObjectItem(params, "context");
            cJSON *diagnostics = cJSON_GetObjectItem(context, "diagnostics");

            if (uri && diagnostics)
            {
                uri_decode(uri->valuestring);
                lsp_code_action(uri->valuestring, diagnostics, id);
            }
        }
    }
    else if (strcmp(method, "textDocument/semanticTokens/full") == 0)
    {
        cJSON *params = cJSON_GetObjectItem(json, "params");
        cJSON *doc = cJSON_GetObjectItem(params, "textDocument");
        if (doc)
        {
            cJSON *uri_item = cJSON_GetObjectItem(doc, "uri");
            if (uri_item && uri_item->valuestring)
            {
                uri_decode(uri_item->valuestring);
                char *resp = lsp_semantic_tokens_full(uri_item->valuestring);
                if (resp)
                {
                    cJSON *res_json = cJSON_CreateObject();
                    cJSON_AddStringToObject(res_json, "jsonrpc", "2.0");
                    cJSON_AddNumberToObject(res_json, "id", id);
                    cJSON *result = cJSON_Parse(resp);
                    if (result)
                    {
                        cJSON_AddItemToObject(res_json, "result", result);
                    }
                    else
                    {
                        // fallback empty
                        cJSON_AddItemToObject(res_json, "result", cJSON_CreateObject());
                    }
                    zfree(resp);

                    char *str = cJSON_PrintUnformatted(res_json);
                    fprintf(stdout, "Content-Length: %zu\r\n\r\n%s", strlen(str), str);
                    fflush(stdout);
                    zfree(str);
                    cJSON_Delete(res_json);
                }
            }
        }
    }
    else if (strcmp(method, "textDocument/rename") == 0)
    {
        char *uri = NULL;
        int line = 0, col = 0;
        get_params(json, &uri, &line, &col);

        cJSON *params = cJSON_GetObjectItem(json, "params");
        cJSON *nn = cJSON_GetObjectItem(params, "newName");
        char *new_name = nn ? nn->valuestring : NULL;

        if (uri && new_name)
        {
            lsp_rename(uri, line, col, new_name, id);
            zfree(uri);
        }
    }
    else if (strcmp(method, "textDocument/formatting") == 0)
    {
        cJSON *params = cJSON_GetObjectItem(json, "params");
        cJSON *doc = cJSON_GetObjectItem(params, "textDocument");
        if (doc)
        {
            cJSON *uri_item = cJSON_GetObjectItem(doc, "uri");
            if (uri_item && uri_item->valuestring)
            {
                uri_decode(uri_item->valuestring);
                ProjectFile *pf = lsp_project_get_file(uri_item->valuestring);
                if (pf && pf->source)
                {
                    char *formatted = lsp_format_source(pf->source);
                    if (formatted)
                    {
                        cJSON *res_json = cJSON_CreateObject();
                        cJSON_AddStringToObject(res_json, "jsonrpc", "2.0");
                        cJSON_AddNumberToObject(res_json, "id", id);

                        cJSON *result = cJSON_CreateArray();
                        cJSON *edit = cJSON_CreateObject();

                        // Replace whole document
                        cJSON *range = cJSON_CreateObject();
                        cJSON *start = cJSON_CreateObject();
                        cJSON_AddNumberToObject(start, "line", 0);
                        cJSON_AddNumberToObject(start, "character", 0);

                        // Count lines in source
                        int lines = 0;
                        const char *p = pf->source;
                        while (*p)
                        {
                            if (*p == '\n')
                            {
                                lines++;
                            }
                            p++;
                        }

                        cJSON *end = cJSON_CreateObject();
                        cJSON_AddNumberToObject(end, "line", lines > 0 ? lines : 1);
                        cJSON_AddNumberToObject(end, "character", 0);

                        cJSON_AddItemToObject(range, "start", start);
                        cJSON_AddItemToObject(range, "end", end);
                        cJSON_AddItemToObject(edit, "range", range);
                        cJSON_AddStringToObject(edit, "newText", formatted);

                        cJSON_AddItemToArray(result, edit);
                        cJSON_AddItemToObject(res_json, "result", result);

                        char *str = cJSON_PrintUnformatted(res_json);
                        fprintf(stdout, "Content-Length: %zu\r\n\r\n%s", strlen(str), str);
                        fflush(stdout);
                        zfree(str);
                        cJSON_Delete(res_json);
                        libc_free(formatted);
                    }
                }
            }
        }
    }
    else if (strcmp(method, "shutdown") == 0)
    {
        cJSON *res_json = cJSON_CreateObject();
        cJSON_AddStringToObject(res_json, "jsonrpc", "2.0");
        cJSON_AddNumberToObject(res_json, "id", id);
        cJSON_AddNullToObject(res_json, "result");

        char *str = cJSON_PrintUnformatted(res_json);
        fprintf(stdout, "Content-Length: %zu\r\n\r\n%s", strlen(str), str);
        fflush(stdout);
        zfree(str);
        cJSON_Delete(res_json);
    }
    else if (strcmp(method, "exit") == 0)
    {
        // For notification, no ID. Just exit.
        exit(0);
    }

    cJSON_Delete(json);
}
