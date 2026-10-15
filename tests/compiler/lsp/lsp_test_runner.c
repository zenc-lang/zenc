#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include "../../src/utils/cJSON.h"
#include "../../src/platform/compiler.h"

#define MAX_BUFFER (2 * 1024 * 1024)

int pipe_in[2];
int pipe_out[2];
pid_t child_pid;

static ZC_NORETURN void fail(const char *msg)
{
    fprintf(stderr, "TEST FAIL: %s\n", msg);
    exit(1);
}

static void start_lsp_server()
{
    if (pipe(pipe_in) < 0 || pipe(pipe_out) < 0)
    {
        fail("Pipe creation failed");
    }
    child_pid = fork();
    if (child_pid < 0)
    {
        fail("Fork failed");
    }

    if (child_pid == 0)
    {
        dup2(pipe_in[0], STDIN_FILENO);
        dup2(pipe_out[1], STDOUT_FILENO);
        close(pipe_in[0]);
        close(pipe_in[1]);
        close(pipe_out[0]);
        close(pipe_out[1]);
        execl("./zc", "zc", "lsp", NULL);
        fail("Execl failed");
    }
    else
    {
        close(pipe_in[0]);
        close(pipe_out[1]);
    }
}

static void send_request(const char *json)
{
    char header[128];
    size_t len = strlen(json);
    int hdr_len = sprintf(header, "Content-Length: %d\r\n\r\n", (int)len);

    // If the server has died, writing to the pipe would raise SIGPIPE (killing
    // the runner with a cryptic 141). We ignore SIGPIPE (set in main) and
    // report the failure clearly instead.
    if (write(pipe_in[1], header, (size_t)hdr_len) != hdr_len ||
        write(pipe_in[1], json, len) != (ssize_t)len)
    {
        fprintf(stderr,
                "TEST FAIL: LSP server died (pipe closed). Check for a crash in `zc lsp`.\n");
        exit(1);
    }
}

char global_buf[MAX_BUFFER];
int global_len = 0;

static char *read_message()
{
    while (1)
    {
        // 1. Check if we have a complete header
        char *body_start = strstr(global_buf, "\r\n\r\n");
        if (body_start)
        {
            int header_len = (int)(body_start - global_buf) + 4;
            int content_len = 0;

            // Parse Content-Length
            char *cl_ptr = strstr(global_buf, "Content-Length: ");
            if (cl_ptr && cl_ptr < body_start)
            {
                content_len = atoi(cl_ptr + 16);
            }

            if (content_len > 0)
            {
                int total_msg_len = header_len + content_len;
                if (global_len >= total_msg_len)
                {
                    // We have a full message!
                    char *msg_body = malloc((size_t)content_len + 1);
                    memcpy(msg_body, body_start + 4, (size_t)content_len);
                    msg_body[content_len] = 0;

                    // Shift remaining data to start
                    int remaining = global_len - total_msg_len;
                    memmove(global_buf, global_buf + total_msg_len, (size_t)remaining);
                    global_len = remaining;
                    global_buf[global_len] = 0;

                    return msg_body;
                }
            }
        }

        // 2. Read more data
        if (global_len >= MAX_BUFFER - 1)
        {
            fail("Buffer overflow in read_message");
        }

        int n =
            (int)read(pipe_out[0], global_buf + global_len, (size_t)(MAX_BUFFER - 1 - global_len));
        if (n <= 0)
        {
            // EOF or error, but maybe we have a message pending verification?
            // If n=0 and we are waiting for data, it's bad.
            // But wait_for_response calls us in loop.
            // If we return NULL, wait_for_response aborts?
            // Wait, existing wait_for_response returns NULL on read_message NULL and loops?
            // No, existing wait_for_response returns NULL instantly if read_message returns NULL.
            // We should block here? read blocks.
            // If read returns 0, server closed pipe.
            return NULL;
        }
        global_len += n;
        global_buf[global_len] = 0;
    }
}

static char *wait_for_response(int id)
{
    // Loop until we get a response with the matching ID
    // or until timeout (implicit in blocking read, could add explicit timeout)
    // Increase loop count significantly because we might get many 'publishDiagnostics'
    for (int i = 0; i < 500; i++)
    {
        char *msg = read_message();
        if (!msg)
        {
            return NULL;
        }

        cJSON *json = cJSON_Parse(msg);
        if (json)
        {
            cJSON *id_item = cJSON_GetObjectItem(json, "id");
            if (id_item)
            {
                // Check both number and string just in case
                int got_id = -1;
                if (cJSON_IsNumber(id_item))
                {
                    got_id = id_item->valueint;
                }
                else if (cJSON_IsString(id_item))
                {
                    got_id = atoi(id_item->valuestring);
                }

                if (got_id == id)
                {
                    cJSON_Delete(json);
                    return msg; // Found it
                }
                else
                {
                    printf("Mismatch ID: got %d (type=%d) expected %d\n", got_id, id_item->type,
                           id);
                }
            }
            else
            {
                printf("No ID in message\n");
            }
            cJSON_Delete(json);
        }
        else
        {
            printf("JSON Parse Failed for: '%s'\n", msg);
        }
        printf("Ignored message (seq %d): %s\n", i, msg);
        free(msg);
    }
    return NULL;
}

static void test_initialize()
{
    printf("Running test_initialize...\n");
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"initialize\", \"params\": "
                 "{\"rootUri\": \"file:///tmp\"}}");
    char *resp = wait_for_response(1);
    if (!resp)
    {
        fail("No response for initialize");
    }

    // Validate basics
    if (!strstr(resp, "capabilities"))
    {
        fail("Init response missing capabilities");
    }
    printf("PASS: test_initialize\n");
    free(resp);
}

static void test_hover()
{
    printf("Running test_hover...\n");
    int fd = open("/tmp/test_lsp.zc", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        // Use 'fn' instead of 'def'
        write(fd, "fn main() { return 0; }", 23);
        close(fd);
    }

    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didOpen\", \"params\": "
                 "{\"textDocument\": {\"uri\": \"file:///tmp/test_lsp.zc\", \"languageId\": "
                 "\"zenc\", \"version\": 1, \"text\": \"fn main() { return 0; }\"}}}");

    // Wait a bit not strictly necessary if we use wait_for_response logic correctly,
    // but helps ensure server has processed the open event if it's async internal logic.
    usleep(100000);

    // Hover over 'main'
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 2, \"method\": \"textDocument/hover\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_lsp.zc\"}, "
                 "\"position\": {\"line\": 0, \"character\": 5}}}");

    char *resp = wait_for_response(2);
    if (!resp)
    {
        printf("WARN: No response for hover.\n");
        return;
    }

    if (strstr(resp, "contents"))
    {
        printf("PASS: test_hover\n");
    }
    else
    {
        printf("WARN: Hover response missing contents: %s\n", resp);
    }
    free(resp);
}

static void expect_signature_hover(int id, int line, int character, const char *expected)
{
    char req[512];
    snprintf(req, sizeof(req),
             "{\"jsonrpc\": \"2.0\", \"id\": %d, \"method\": \"textDocument/hover\", "
             "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_hover_sig.zc\"}, "
             "\"position\": {\"line\": %d, \"character\": %d}}}",
             id, line, character);
    send_request(req);
    char *resp = wait_for_response(id);
    if (!resp || !strstr(resp, expected))
    {
        printf("Expected hover \"%s\", got: %s\n", expected, resp ? resp : "(no response)");
        fail("Hover must show the function signature as written");
    }
    free(resp);
}

static void test_hover_signature(void)
{
    printf("Running test_hover_signature...\n");
    // Line 0: struct Box { v: int; }
    // Line 1: impl Box {
    // Line 2:     fn get(self) -> int { return self.v; }
    // Line 3: }
    // Line 4: fn scale(value: int,
    // Line 5:     factor: f64) -> int
    // Line 6: {
    // Line 7:     return value;
    // Line 8: }
    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didOpen\", \"params\": "
                 "{\"textDocument\": {\"uri\": \"file:///tmp/test_hover_sig.zc\", "
                 "\"languageId\": \"zenc\", \"version\": 1, \"text\": "
                 "\"struct Box { v: int; }\\nimpl Box {\\n    fn get(self) -> int { return "
                 "self.v; }\\n}\\nfn scale(value: int,\\n    factor: f64) -> int\\n{\\n    "
                 "return value;\\n}\\n\"}}}");
    usleep(100000);

    // Zen C type names, not the C ones (int32_t, double), on one line.
    expect_signature_hover(4, 4, 3, "fn scale(value: int, factor: f64) -> int");
    // The method's own name, not the mangled one (Box__get).
    expect_signature_hover(5, 2, 7, "fn get(self) -> int");
    printf("PASS: test_hover_signature\n");
}

static void test_definition_partial_code()
{
    printf("Running test_definition_partial_code...\n");
    // Incomplete call: "foo(" without closing paren or args.
    // This creates a NODE_EXPR_CALL with NULL callee.
    int fd = open("/tmp/test_partial_def.zc", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        const char *code = "fn main() {\n    foo(\n}";
        write(fd, code, strlen(code));
        close(fd);
    }

    send_request(
        "{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didOpen\", \"params\": "
        "{\"textDocument\": {\"uri\": \"file:///tmp/test_partial_def.zc\", \"languageId\": "
        "\"zenc\", \"version\": 1, \"text\": \"fn main() {\\n    foo(\\n}\"}}}");
    usleep(100000);

    // Request definition at line 1, character 5 (on "foo")
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 100, \"method\": \"textDocument/definition\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_partial_def.zc\"}, "
                 "\"position\": {\"line\": 1, \"character\": 5}}}");

    char *resp = wait_for_response(100);
    if (!resp)
    {
        printf("WARN: No response for partial definition (no crash = good).\n");
        return;
    }
    printf("PASS: test_definition_partial_code (no crash)\n");
    free(resp);
}

static void test_references_partial_code()
{
    printf("Running test_references_partial_code...\n");
    // Same partial code as above — references on incomplete call
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 101, \"method\": \"textDocument/references\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_partial_def.zc\"}, "
                 "\"position\": {\"line\": 1, \"character\": 5}, "
                 "\"context\": {\"includeDeclaration\": true}}}");

    char *resp = wait_for_response(101);
    if (!resp)
    {
        printf("WARN: No response for partial references (no crash = good).\n");
        return;
    }
    printf("PASS: test_references_partial_code (no crash)\n");
    free(resp);
}

static void test_request_unopened_file()
{
    printf("Running test_request_unopened_file...\n");
    // Hover on a URI that was never didOpen'd — tests null-pointer guards.
    // The server may not respond, so verify it's still alive via a hover on
    // a known-opened file (test_lsp.zc from test_hover).
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 102, \"method\": \"textDocument/hover\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_nonexistent.zc\"}, "
                 "\"position\": {\"line\": 0, \"character\": 0}}}");

    // Hover on a known file to verify server is still alive
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 103, \"method\": \"textDocument/hover\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_lsp.zc\"}, "
                 "\"position\": {\"line\": 0, \"character\": 5}}}");

    char *resp = wait_for_response(103);
    if (resp && strstr(resp, "contents"))
    {
        printf("PASS: test_request_unopened_file (server alive)\n");
    }
    else
    {
        printf("WARN: test_request_unopened_file: %s\n", resp ? resp : "no response");
    }
    free(resp);
}

static void test_empty_source()
{
    printf("Running test_empty_source...\n");
    int fd = open("/tmp/test_empty.zc", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        write(fd, "", 0);
        close(fd);
    }

    // Open empty file
    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didOpen\", \"params\": "
                 "{\"textDocument\": {\"uri\": \"file:///tmp/test_empty.zc\", \"languageId\": "
                 "\"zenc\", \"version\": 1, \"text\": \"\"}}}");
    usleep(100000);

    // Hover on empty file — should not crash
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 103, \"method\": \"textDocument/hover\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_empty.zc\"}, "
                 "\"position\": {\"line\": 0, \"character\": 0}}}");

    char *resp = wait_for_response(103);
    if (resp)
    {
        printf("PASS: test_empty_source (hover on empty file)\n");
        free(resp);
    }
    else
    {
        printf("WARN: No response for empty source hover.\n");
    }

    // Completion on empty file
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 104, \"method\": \"textDocument/completion\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_empty.zc\"}, "
                 "\"position\": {\"line\": 0, \"character\": 0}}}");

    char *resp2 = wait_for_response(104);
    if (resp2)
    {
        printf("PASS: test_empty_source (completion on empty file)\n");
        free(resp2);
    }
    else
    {
        printf("WARN: No response for empty source completion.\n");
    }
}

static void test_did_change()
{
    printf("Running test_did_change...\n");
    // Open a file, then send didChange with modified content
    int fd = open("/tmp/test_change.zc", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        const char *code = "fn original() {}\nfn main() { original(); }";
        write(fd, code, strlen(code));
        close(fd);
    }

    send_request(
        "{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didOpen\", \"params\": "
        "{\"textDocument\": {\"uri\": \"file:///tmp/test_change.zc\", \"languageId\": "
        "\"zenc\", \"version\": 1, \"text\": \"fn original() {}\\nfn main() { original(); }\"}}}");
    usleep(100000);

    // Change: rename "original" to "updated"
    send_request(
        "{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didChange\", \"params\": "
        "{\"textDocument\": {\"uri\": \"file:///tmp/test_change.zc\", \"version\": 2}, "
        "\"contentChanges\": [{\"text\": \"fn updated() {}\\nfn main() { updated(); }\"}]}}}");
    usleep(100000);

    // Hover on "updated" should work after didChange
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 105, \"method\": \"textDocument/hover\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_change.zc\"}, "
                 "\"position\": {\"line\": 0, \"character\": 5}}}");

    char *resp = wait_for_response(105);
    if (resp && strstr(resp, "contents"))
    {
        printf("PASS: test_did_change (hover works after change)\n");
    }
    else
    {
        printf("WARN: test_did_change: %s\n", resp ? resp : "no response");
    }
    free(resp);

    // Verify definition on "updated()" function (line 0, character 3)
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 106, \"method\": \"textDocument/definition\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_change.zc\"}, "
                 "\"position\": {\"line\": 0, \"character\": 5}}}");

    char *resp2 = wait_for_response(106);
    if (resp2 && strstr(resp2, "\"line\":0"))
    {
        printf("PASS: test_did_change (definition works after change)\n");
    }
    else
    {
        printf("WARN: test_did_change definition: %s\n", resp2 ? resp2 : "no response");
    }
    free(resp2);
}

static void test_code_action()
{
    printf("Running test_code_action...\n");
    // Open a file with 'var' to trigger deprecation diagnostics
    int fd = open("/tmp/test_action.zc", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        const char *code = "fn main() { var x: int = 5; }";
        write(fd, code, strlen(code));
        close(fd);
    }

    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didOpen\", \"params\": "
                 "{\"textDocument\": {\"uri\": \"file:///tmp/test_action.zc\", \"languageId\": "
                 "\"zenc\", \"version\": 1, \"text\": \"fn main() { var x: int = 5; }\"}}}");
    usleep(100000);

    // Request code actions. The diagnostic is provided by the client
    // matching what the server would emit for 'var' deprecation.
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 107, \"method\": \"textDocument/codeAction\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_action.zc\"}, "
                 "\"context\": {\"diagnostics\": [{\"range\": {\"start\": {\"line\": 0, "
                 "\"character\": 12}, \"end\": {\"line\": 0, \"character\": 15}}, "
                 "\"code\": \"W42\", \"message\": \"'var' is deprecated for declarations. "
                 "Use 'let' instead.\"}]}}}");

    char *resp = wait_for_response(107);
    if (resp && strstr(resp, "Replace 'var' with 'let'"))
    {
        printf("PASS: test_code_action (var -> let action)\n");
    }
    else
    {
        printf("WARN: test_code_action: %s\n", resp ? resp : "no response");
    }
    free(resp);
}

static void on_unanswered_request(int signal_number)
{
    (void)signal_number;
    const char message[] = "TEST FAIL: no answer to an unknown request\n";
    write(STDERR_FILENO, message, sizeof(message) - 1);
    _exit(1);
}

static void test_unknown_method(void)
{
    printf("Running test_unknown_method...\n");
    // An unknown notification gets no answer; the unknown request after it gets an error
    // instead of leaving the client waiting (the alarm turns a hang into a failure).
    signal(SIGALRM, on_unanswered_request);
    alarm(5);
    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"$/unknownNotification\", \"params\": {}}");
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 900, \"method\": \"textDocument/unknownRequest\", "
                 "\"params\": {}}");
    char *response = wait_for_response(900);
    alarm(0);
    if (!response || !strstr(response, "-32601"))
    {
        fail("An unknown request must get a MethodNotFound error");
    }
    free(response);
    printf("PASS: test_unknown_method\n");
}

static void test_declaration(void)
{
    printf("Running test_declaration...\n");
    // test_definition opened /tmp/test_def.zc, where `target()` on line 2 is defined on line 0.
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 61, \"method\": \"textDocument/declaration\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_def.zc\"}, "
                 "\"position\": {\"line\": 2, \"character\": 6}}}");
    char *resp = wait_for_response(61);
    if (!resp || !strstr(resp, "\"line\":0"))
    {
        fail("Go to declaration must find target() on line 0");
    }
    free(resp);
    printf("PASS: test_declaration\n");
}

// The symbols named like `query` must include `expected` with that kind (and container).
static void expect_workspace_symbol(int id, const char *query, const char *expected, int kind,
                                    const char *container)
{
    char json[256];
    snprintf(json, sizeof(json),
             "{\"jsonrpc\": \"2.0\", \"id\": %d, \"method\": \"workspace/symbol\", "
             "\"params\": {\"query\": \"%s\"}}",
             id, query);
    send_request(json);
    char *response = wait_for_response(id);
    cJSON *parsed = response ? cJSON_Parse(response) : NULL;
    free(response);
    cJSON *results = cJSON_GetObjectItem(parsed, "result");
    int found = 0;
    for (int i = 0; i < cJSON_GetArraySize(results); i++)
    {
        cJSON *item = cJSON_GetArrayItem(results, i);
        cJSON *name = cJSON_GetObjectItem(item, "name");
        cJSON *item_kind = cJSON_GetObjectItem(item, "kind");
        cJSON *item_container = cJSON_GetObjectItem(item, "containerName");
        cJSON *uri = cJSON_GetObjectItem(cJSON_GetObjectItem(item, "location"), "uri");
        if (cJSON_IsString(name) && strcmp(name->valuestring, expected) == 0 &&
            cJSON_IsNumber(item_kind) && item_kind->valueint == kind && cJSON_IsString(uri) &&
            strstr(uri->valuestring, "test_ws_symbol.zc") &&
            (!container || (cJSON_IsString(item_container) &&
                            strcmp(item_container->valuestring, container) == 0)))
        {
            found = 1;
        }
    }
    if (!found)
    {
        printf("workspace/symbol '%s' without %s: %s\n", query, expected,
               results ? cJSON_PrintUnformatted(results) : "no result");
        fail("Missing workspace symbol");
    }
    cJSON_Delete(parsed);
}

static void test_workspace_symbol(void)
{
    printf("Running test_workspace_symbol...\n");
    const char *code = "struct WsShape {\n"
                       "    w: int;\n"
                       "}\n"
                       "\n"
                       "impl WsShape {\n"
                       "    fn ws_area(self) -> int {\n"
                       "        return self.w;\n"
                       "    }\n"
                       "}\n"
                       "\n"
                       "trait WsDrawable {\n"
                       "    fn ws_draw(self);\n"
                       "}\n"
                       "\n"
                       "fn ws_symbol_target() -> int {\n"
                       "    return 1;\n"
                       "}\n";
    int fd = open("/tmp/test_ws_symbol.zc", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        write(fd, code, strlen(code));
        close(fd);
    }
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "jsonrpc", "2.0");
    cJSON_AddStringToObject(message, "method", "textDocument/didOpen");
    cJSON *document =
        cJSON_AddObjectToObject(cJSON_AddObjectToObject(message, "params"), "textDocument");
    cJSON_AddStringToObject(document, "uri", "file:///tmp/test_ws_symbol.zc");
    cJSON_AddStringToObject(document, "languageId", "zenc");
    cJSON_AddNumberToObject(document, "version", 1);
    cJSON_AddStringToObject(document, "text", code);
    char *json = cJSON_PrintUnformatted(message);
    send_request(json);
    free(json);
    cJSON_Delete(message);

    expect_workspace_symbol(910, "ws_symbol", "ws_symbol_target", 12, NULL);
    expect_workspace_symbol(911, "WSSHAPE", "WsShape", 23, NULL);
    expect_workspace_symbol(912, "wsarea", "ws_area", 6, "WsShape");
    expect_workspace_symbol(913, "drawable", "WsDrawable", 11, NULL);
    printf("PASS: test_workspace_symbol\n");
}

static void write_index_fixture(const char *path, const char *content)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || write(fd, content, strlen(content)) != (ssize_t)strlen(content))
    {
        fail("Could not write an indexing fixture");
    }
    close(fd);
}

// The server lists the symbols of a file it was never sent only if the workspace scan indexed
// it: `expected` is its symbol, or NULL when the scan must have skipped the file.
static void expect_indexed(int id, const char *root, const char *relative_path,
                           const char *expected)
{
    char json[512];
    snprintf(json, sizeof(json),
             "{\"jsonrpc\": \"2.0\", \"id\": %d, \"method\": \"textDocument/documentSymbol\", "
             "\"params\": {\"textDocument\": {\"uri\": \"file://%s/%s\"}}}",
             id, root, relative_path);
    send_request(json);
    char *response = wait_for_response(id);
    cJSON *parsed = response ? cJSON_Parse(response) : NULL;
    cJSON *name =
        cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(parsed, "result"), 0), "name");
    int indexed = cJSON_IsString(name);
    if (expected ? !indexed || strcmp(name->valuestring, expected) != 0 : indexed)
    {
        printf("%s: expected %s, got %s\n", relative_path, expected ? expected : "not indexed",
               response ? response : "no response");
        fail("The workspace scan did not index or skip the file as expected");
    }
    cJSON_Delete(parsed);
    free(response);
}

static void test_index_gitignore(void)
{
    printf("Running test_index_gitignore...\n");
    const char *directories[] = {"/tmp/zc_lsp_index",
                                 "/tmp/zc_lsp_index/kept",
                                 "/tmp/zc_lsp_index/ignored",
                                 "/tmp/zc_lsp_index/nested",
                                 "/tmp/zc_lsp_index/nested/ignored",
                                 "/tmp/zc_lsp_index/anchored_dir",
                                 "/tmp/zc_lsp_index/nested/anchored_dir"};
    for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); i++)
    {
        mkdir(directories[i], 0755);
    }
    write_index_fixture("/tmp/zc_lsp_index/.gitignore", "# generated\n"
                                                        "ignored/\n"
                                                        "*.tmp.zc\n"
                                                        "/anchored_dir\n");
    write_index_fixture("/tmp/zc_lsp_index/kept/k.zc", "fn kept_fn() {}\n");
    write_index_fixture("/tmp/zc_lsp_index/ignored/i.zc", "fn ignored_fn() {}\n");
    write_index_fixture("/tmp/zc_lsp_index/nested/ignored/deep.zc", "fn deep_fn() {}\n");
    write_index_fixture("/tmp/zc_lsp_index/x.tmp.zc", "fn tmp_fn() {}\n");
    write_index_fixture("/tmp/zc_lsp_index/anchored_dir/a.zc", "fn anchored_fn() {}\n");
    write_index_fixture("/tmp/zc_lsp_index/nested/anchored_dir/b.zc",
                        "fn nested_anchored_fn() {}\n");

    // A server of its own, rooted there, which indexes the workspace on `initialized`.
    global_len = 0;
    global_buf[0] = 0;
    start_lsp_server();
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 1000, \"method\": \"initialize\", \"params\": "
                 "{\"rootUri\": \"file:///tmp/zc_lsp_index\"}}");
    free(wait_for_response(1000));
    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"initialized\", \"params\": {}}");

    expect_indexed(1001, "/tmp/zc_lsp_index", "kept/k.zc", "kept_fn");
    expect_indexed(1002, "/tmp/zc_lsp_index", "ignored/i.zc", NULL);
    expect_indexed(1003, "/tmp/zc_lsp_index", "nested/ignored/deep.zc", NULL);
    expect_indexed(1004, "/tmp/zc_lsp_index", "x.tmp.zc", NULL);
    expect_indexed(1005, "/tmp/zc_lsp_index", "anchored_dir/a.zc", NULL);
    expect_indexed(1006, "/tmp/zc_lsp_index", "nested/anchored_dir/b.zc", "nested_anchored_fn");

    send_request(
        "{\"jsonrpc\": \"2.0\", \"id\": 1007, \"method\": \"shutdown\", \"params\": null}");
    free(wait_for_response(1007));
    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"exit\", \"params\": {}}");
    waitpid(child_pid, NULL, 0);
    printf("PASS: test_index_gitignore\n");
}

// The name of the first symbol of /tmp/test_close.zc, as the server knows it.
static char *first_close_symbol(int id)
{
    char json[256];
    snprintf(json, sizeof(json),
             "{\"jsonrpc\": \"2.0\", \"id\": %d, \"method\": \"textDocument/documentSymbol\", "
             "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_close.zc\"}}}",
             id);
    send_request(json);
    char *response = wait_for_response(id);
    cJSON *parsed = response ? cJSON_Parse(response) : NULL;
    free(response);
    cJSON *name =
        cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(parsed, "result"), 0), "name");
    char *copy = cJSON_IsString(name) ? strdup(name->valuestring) : strdup("(none)");
    cJSON_Delete(parsed);
    return copy;
}

static void on_missing_diagnostics(int signal_number)
{
    (void)signal_number;
    const char message[] = "TEST FAIL: no diagnostics after closing a document\n";
    write(STDERR_FILENO, message, sizeof(message) - 1);
    _exit(1);
}

static void test_did_close(void)
{
    printf("Running test_did_close...\n");
    // On disk the file has one function; the client opens it with unsaved changes (and an
    // error, so that its diagnostics are not empty).
    int fd = open("/tmp/test_close.zc", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        const char *code = "fn disk_version() {}\n";
        write(fd, code, strlen(code));
        close(fd);
    }
    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didOpen\", \"params\": "
                 "{\"textDocument\": {\"uri\": \"file:///tmp/test_close.zc\", \"languageId\": "
                 "\"zenc\", \"version\": 1, \"text\": \"fn memory_version() {}\\nfn broken() { "
                 "let x: int = ; }\\n\"}}}");
    char *symbol = first_close_symbol(950);
    if (strcmp(symbol, "memory_version") != 0)
    {
        printf("Open document: first symbol %s\n", symbol);
        fail("The open document must be the client's version");
    }
    free(symbol);

    // Closing it clears its diagnostics (the alarm turns a missing message into a failure)...
    signal(SIGALRM, on_missing_diagnostics);
    alarm(5);
    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didClose\", \"params\": "
                 "{\"textDocument\": {\"uri\": \"file:///tmp/test_close.zc\"}}}");
    int cleared = 0;
    while (!cleared)
    {
        char *message = read_message();
        if (!message)
        {
            break;
        }
        cleared = strstr(message, "publishDiagnostics") && strstr(message, "test_close.zc") &&
                  strstr(message, "\"diagnostics\":[]");
        free(message);
    }
    alarm(0);
    if (!cleared)
    {
        fail("Closing a document must clear its diagnostics");
    }

    // ...and the server goes back to the file on disk.
    symbol = first_close_symbol(951);
    if (strcmp(symbol, "disk_version") != 0)
    {
        printf("Closed document: first symbol %s\n", symbol);
        fail("A closed document must go back to its contents on disk");
    }
    free(symbol);
    printf("PASS: test_did_close\n");
}

/* --- C headers: hover and go to definition --- */

#define C_TEST_DIR "/tmp/zc_lsp_c"

static void write_file(const char *path, const char *content)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || write(fd, content, strlen(content)) != (ssize_t)strlen(content))
    {
        fail("Could not write a test file");
    }
    close(fd);
}

// Writes `path` and opens it in the server.
static void open_document(const char *path, const char *text)
{
    write_file(path, text);
    char uri[512];
    snprintf(uri, sizeof(uri), "file://%s", path);

    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "jsonrpc", "2.0");
    cJSON_AddStringToObject(message, "method", "textDocument/didOpen");
    cJSON *params = cJSON_AddObjectToObject(message, "params");
    cJSON *document = cJSON_AddObjectToObject(params, "textDocument");
    cJSON_AddStringToObject(document, "uri", uri);
    cJSON_AddStringToObject(document, "languageId", "zenc");
    cJSON_AddNumberToObject(document, "version", 1);
    cJSON_AddStringToObject(document, "text", text);
    char *json = cJSON_PrintUnformatted(message);
    send_request(json);
    free(json);
    cJSON_Delete(message);
}

// Sends a position request and returns the whole response; the caller deletes it.
static cJSON *position_request(int id, const char *method, const char *path, int line,
                               int character)
{
    char json[1024];
    snprintf(json, sizeof(json),
             "{\"jsonrpc\": \"2.0\", \"id\": %d, \"method\": \"%s\", \"params\": "
             "{\"textDocument\": {\"uri\": \"file://%s\"}, \"position\": {\"line\": %d, "
             "\"character\": %d}}}",
             id, method, path, line, character);
    send_request(json);
    char *response = wait_for_response(id);
    if (!response)
    {
        fail("No response for a position request");
    }
    cJSON *parsed = cJSON_Parse(response);
    free(response);
    if (!parsed)
    {
        fail("Unparsable response for a position request");
    }
    return parsed;
}

// Go to definition at (line, character) must land on a single location in a file ending with
// `file_suffix`, at `expected_line`; with a NULL `file_suffix`, it must find nothing.
static void expect_definition(int id, const char *what, const char *path, int line, int character,
                              const char *file_suffix, int expected_line)
{
    cJSON *response = position_request(id, "textDocument/definition", path, line, character);
    cJSON *result = cJSON_GetObjectItem(response, "result");
    if (!file_suffix)
    {
        if (result && !cJSON_IsNull(result) &&
            !(cJSON_IsArray(result) && cJSON_GetArraySize(result) == 0))
        {
            printf("Unexpected definition for %s: %s\n", what, cJSON_PrintUnformatted(result));
            fail("A missing C symbol has a definition");
        }
        cJSON_Delete(response);
        return;
    }

    if (cJSON_IsArray(result) && cJSON_GetArraySize(result) != 1)
    {
        printf("%s: %d locations\n", what, cJSON_GetArraySize(result));
        fail("Expected exactly one location");
    }
    cJSON *location = cJSON_IsArray(result) ? cJSON_GetArrayItem(result, 0) : result;
    cJSON *uri = location ? cJSON_GetObjectItem(location, "uri") : NULL;
    cJSON *start =
        location ? cJSON_GetObjectItem(cJSON_GetObjectItem(location, "range"), "start") : NULL;
    cJSON *start_line = start ? cJSON_GetObjectItem(start, "line") : NULL;
    size_t uri_length = cJSON_IsString(uri) ? strlen(uri->valuestring) : 0;
    size_t suffix_length = strlen(file_suffix);
    if (!cJSON_IsString(uri) || uri_length < suffix_length ||
        strcmp(uri->valuestring + uri_length - suffix_length, file_suffix) != 0 ||
        !cJSON_IsNumber(start_line) || start_line->valueint != expected_line)
    {
        printf("%s: expected %s:%d, got %s\n", what, file_suffix, expected_line,
               result ? cJSON_PrintUnformatted(result) : "nothing");
        fail("Wrong definition for a C symbol");
    }
    cJSON_Delete(response);
}

// The hover at (line, character) must contain `expected`.
static void expect_hover(int id, const char *what, const char *path, int line, int character,
                         const char *expected)
{
    cJSON *response = position_request(id, "textDocument/hover", path, line, character);
    cJSON *contents = cJSON_GetObjectItem(cJSON_GetObjectItem(response, "result"), "contents");
    cJSON *value = contents ? cJSON_GetObjectItem(contents, "value") : NULL;
    if (!cJSON_IsString(value) || !strstr(value->valuestring, expected))
    {
        printf("%s: hover without '%s': %s\n", what, expected,
               cJSON_IsString(value) ? value->valuestring : "nothing");
        fail("Wrong hover for a C symbol");
    }
    cJSON_Delete(response);
}

static void write_c_header_fixtures(void)
{
    mkdir(C_TEST_DIR, 0755);
    mkdir(C_TEST_DIR "/include", 0755);
    mkdir(C_TEST_DIR "/include_mod", 0755);
    write_file(C_TEST_DIR "/include/fake.h", "#ifndef FAKE_H\n"
                                             "#define FAKE_H\n"
                                             "#define FAKE_LIMIT 8 // Upper bound\n"
                                             "/* Adds two numbers. */\n"
                                             "int fake_add(int a,\n"
                                             "             int b);\n"
                                             "typedef struct FakePoint\n"
                                             "{\n"
                                             "    int x;\n"
                                             "} FakePoint;\n"
                                             "typedef enum { FAKE_ON = 1, FAKE_OFF } FakeMode;\n"
                                             "_FAKE_MACRO()\n"
                                             "int fake_after_macro(void);\n"
                                             "#include \"fake_nested.h\"\n"
                                             "#endif\n");
    write_file(C_TEST_DIR "/include/fake_nested.h", "int fake_nested(void); // nested\n");
    write_file(C_TEST_DIR "/include_mod/modonly.h", "int mod_function(void); // from the module\n");
}

static void test_c_header_symbols(void)
{
    printf("Running test_c_header_symbols...\n");
    write_c_header_fixtures();
    const char *main_path = C_TEST_DIR "/main.zc";
    open_document(main_path, "//> include: " C_TEST_DIR "/include\n"
                             "import \"fake.h\" as fk;\n"
                             "\n"
                             "fn main() {\n"
                             "    fk::fake_add(1, 2);\n"
                             "    let p: fk::FakePoint;\n"
                             "    let m = fk::FAKE_ON;\n"
                             "    let n = fk::fake_nested();\n"
                             "    let a = fk::fake_after_macro();\n"
                             "    let l = fk::FAKE_LIMIT;\n"
                             "    let z = fk::fake_missing();\n"
                             "}\n");

    expect_definition(800, "multi-line prototype", main_path, 4, 10, "/include/fake.h", 4);
    expect_definition(801, "typedef struct", main_path, 5, 17, "/include/fake.h", 6);
    expect_definition(802, "enum value", main_path, 6, 18, "/include/fake.h", 10);
    expect_definition(803, "nested include", main_path, 7, 18, "/include/fake_nested.h", 0);
    expect_definition(804, "after a macro call", main_path, 8, 18, "/include/fake.h", 12);
    expect_definition(805, "#define", main_path, 9, 18, "/include/fake.h", 2);
    expect_definition(806, "missing symbol", main_path, 10, 18, NULL, 0);
    expect_hover(807, "prototype", main_path, 4, 10, "int fake_add(int a,");
    expect_hover(808, "leading comment", main_path, 4, 10, "Adds two numbers.");
    expect_hover(809, "trailing comment", main_path, 9, 18, "Upper bound");
    printf("PASS: test_c_header_symbols\n");

    expect_definition(810, "alias", main_path, 4, 5, "/main.zc", 1);
    expect_hover(811, "alias", main_path, 4, 5, "alias of the C header `fake.h`");
    expect_definition(812, "header name", main_path, 1, 10, "/include/fake.h", 0);

    // The hover highlights the whole header name, which a client splits at the dot.
    cJSON *response = position_request(813, "textDocument/hover", main_path, 1, 10);
    cJSON *range = cJSON_GetObjectItem(cJSON_GetObjectItem(response, "result"), "range");
    cJSON *start = cJSON_GetObjectItem(cJSON_GetObjectItem(range, "start"), "character");
    cJSON *end = cJSON_GetObjectItem(cJSON_GetObjectItem(range, "end"), "character");
    if (!cJSON_IsNumber(start) || !cJSON_IsNumber(end) || start->valueint != 8 ||
        end->valueint != 14)
    {
        fail("The hover on a header name must cover the whole name");
    }
    cJSON_Delete(response);
    printf("PASS: test_c_header_symbols (alias and header name)\n");
}

static void test_c_header_reexports(void)
{
    printf("Running test_c_header_reexports...\n");
    write_c_header_fixtures();

    // A module that re-exports a header, found only through the module's own directive.
    write_file(C_TEST_DIR "/gfx_a.zc", "//> include: " C_TEST_DIR "/include_mod\n"
                                       "export import \"modonly.h\" as md;\n");
    const char *use_path = C_TEST_DIR "/use_a.zc";
    open_document(use_path, "import \"gfx_a.zc\"\n"
                            "\n"
                            "fn main() {\n"
                            "    md::mod_function();\n"
                            "}\n");

    // The directive of gfx_a.zc does not apply when building use_a.zc.
    int warned = 0;
    for (int i = 0; i < 20 && !warned; i++)
    {
        char *message = read_message();
        if (!message)
        {
            break;
        }
        if (strstr(message, "publishDiagnostics") && strstr(message, "use_a.zc") &&
            strstr(message, "modonly.h"))
        {
            warned = 1;
        }
        free(message);
        if (!warned && i > 0)
        {
            break;
        }
    }
    if (!warned)
    {
        fail("No warning for a header only found through an imported module's directive");
    }

    expect_definition(820, "re-exported name", use_path, 3, 10, "/include_mod/modonly.h", 0);
    expect_hover(821, "re-exported name", use_path, 3, 10, "Re-exported by `gfx_a.zc`");
    expect_definition(822, "re-exported alias", use_path, 3, 5, "/gfx_a.zc", 1);

    // An `extern fn` that declares a function of an included header.
    const char *extern_path = C_TEST_DIR "/gfx_b.zc";
    open_document(extern_path, "//> include: " C_TEST_DIR "/include\n"
                               "include <fake.h>\n"
                               "\n"
                               "extern fn fake_add(a: int, b: int) -> int;\n");
    expect_definition(823, "extern fn", extern_path, 3, 12, "/include/fake.h", 4);
    expect_hover(824, "extern fn", extern_path, 3, 12, "int fake_add(int a,");
    printf("PASS: test_c_header_reexports\n");
}

// Completion at (line, character) must offer `expected` with `kind`, and not the generic
// keyword list.
static void expect_completion(int id, const char *path, int line, int character,
                              const char *expected, int kind)
{
    cJSON *response = position_request(id, "textDocument/completion", path, line, character);
    cJSON *items = cJSON_GetObjectItem(response, "result");
    int found = 0;
    int keywords = 0;
    for (int i = 0; i < cJSON_GetArraySize(items); i++)
    {
        cJSON *label = cJSON_GetObjectItem(cJSON_GetArrayItem(items, i), "label");
        cJSON *item_kind = cJSON_GetObjectItem(cJSON_GetArrayItem(items, i), "kind");
        if (cJSON_IsString(label) && strcmp(label->valuestring, expected) == 0 &&
            cJSON_IsNumber(item_kind) && item_kind->valueint == kind)
        {
            found = 1;
        }
        if (cJSON_IsString(label) && strcmp(label->valuestring, "if") == 0)
        {
            keywords = 1;
        }
    }
    if (!found || keywords)
    {
        printf("Completion at %d:%d without %s (kind %d) or with keywords\n", line, character,
               expected, kind);
        fail("Wrong completion after a C header alias");
    }
    cJSON_Delete(response);
}

static void test_c_header_completion(void)
{
    printf("Running test_c_header_completion...\n");
    write_c_header_fixtures();
    const char *path = C_TEST_DIR "/complete.zc";
    open_document(path, "//> include: " C_TEST_DIR "/include\n"
                        "import \"fake.h\" as fk;\n"
                        "\n"
                        "fn main() {\n"
                        "    fk::fake_\n"
                        "    fk::FAKE_O\n"
                        "    fk::\n"
                        "}\n");
    expect_completion(830, path, 4, 13, "fake_add", 3);
    expect_completion(831, path, 4, 13, "fake_nested", 3);
    expect_completion(832, path, 5, 14, "FAKE_ON", 20);
    expect_completion(833, path, 6, 8, "FakePoint", 22);
    // A header re-exported by an imported module (use_a.zc, from test_c_header_reexports).
    expect_completion(834, C_TEST_DIR "/use_a.zc", 3, 11, "mod_function", 3);
    printf("PASS: test_c_header_completion\n");
}

static void test_shutdown()
{
    printf("Running test_shutdown...\n");
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 3, \"method\": \"shutdown\", \"params\": {}}");
    char *resp = wait_for_response(3);
    if (resp)
    {
        printf("PASS: test_shutdown\n");
        free(resp);
    }
    else
    {
        printf("PASS: test_shutdown (no response)\n");
    }
}

static void test_completion()
{
    printf("Running test_completion...\n");
    int fd = open("/tmp/test_compl.zc", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        // Function with local variable 'local_idx' and argument 'arg_val'
        const char *code = "fn test_func(arg_val: int) { let local_idx = 10; \n    \n }";
        write(fd, code, strlen(code));
        close(fd);
    }

    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didOpen\", \"params\": "
                 "{\"textDocument\": {\"uri\": \"file:///tmp/test_compl.zc\", \"languageId\": "
                 "\"zenc\", \"version\": 1, \"text\": \"fn test_func(arg_val: int) { let local_idx "
                 "= 10; \\n    \\n }\"}}}");
    usleep(100000);

    // Request completion inside the function body
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 10, \"method\": \"textDocument/completion\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_compl.zc\"}, "
                 "\"position\": {\"line\": 1, \"character\": 4}}}");

    char *resp = wait_for_response(10);
    if (!resp)
    {
        printf("WARN: No response for completion.\n");
        return;
    }

    // Check for 'local_idx'
    if (strstr(resp, "local_idx"))
    {
        printf("PASS: test_completion (found local_idx)\n");
    }
    else
    {
        printf("FAIL: test_completion (local_idx not found): %s\n", resp);
    }

    // Check for 'arg_val'
    if (strstr(resp, "arg_val"))
    {
        printf("PASS: test_completion (found arg_val)\n");
    }
    else
    {
        printf("WARN: test_completion (arg_val not found): %s\n", resp);
    }
    free(resp);
}

static void test_struct_completion()
{
    printf("Running test_struct_completion...\n");
    int fd = open("/tmp/test_struct.zc", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        const char *code = "struct Point { x: int; y: int; }\n"
                           "fn main() {\n"
                           "    let my_point: Point;\n"
                           "    my_point.\n"
                           "}";
        write(fd, code, strlen(code));
        close(fd);
    }

    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didOpen\", \"params\": "
                 "{\"textDocument\": {\"uri\": \"file:///tmp/test_struct.zc\", \"languageId\": "
                 "\"zenc\", \"version\": 1, \"text\": \"struct Point { x: int; y: int; }\\nfn "
                 "main() {\\n    let my_point: Point;\\n    my_point.\\n}\"}}}");
    usleep(100000);

    // Request completion at 'my_point.' (line 3, character 13)
    // 4 spaces + 8 chars + 1 dot = 13
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 20, \"method\": \"textDocument/completion\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_struct.zc\"}, "
                 "\"position\": {\"line\": 3, \"character\": 13}}}");

    char *resp = wait_for_response(20);
    if (!resp)
    {
        printf("WARN: No response for struct completion.\n");
        return;
    }

    // Check for 'x'
    if (strstr(resp, "\"label\":\"x\""))
    {
        printf("PASS: test_struct_completion (found field x)\n");
    }
    else
    {
        printf("FAIL: test_struct_completion (field x not found): %s\n", resp);
    }
    free(resp);
}

static void test_diagnostics()
{
    printf("Running test_diagnostics...\n");
    // Create a file with a syntax error
    // "fn main() { let x: int = ; }" -> Syntax error
    int fd = open("/tmp/test_error.zc", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        const char *code = "fn main() { let x: int = ; }";
        write(fd, code, strlen(code));
        close(fd);
    }

    // Open file - should trigger publishDiagnostics
    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didOpen\", \"params\": "
                 "{\"textDocument\": {\"uri\": \"file:///tmp/test_error.zc\", \"languageId\": "
                 "\"zenc\", \"version\": 1, \"text\": \"fn main() { let x: int = ; }\"}}}");

    // Check for notification
    int found = 0;
    // We might get other messages, so loop briefly?
    // But typical server sends it immediately.
    for (int i = 0; i < 5; i++)
    {
        char *msg = read_message();
        if (msg)
        {
            if (strstr(msg, "textDocument/publishDiagnostics") && strstr(msg, "diagnostics"))
            {
                printf("PASS: test_diagnostics (received diagnostics)\n");
                found = 1;
                free(msg);
                break;
            }
            free(msg);
        }
        else
        {
            break;
        }
    }

    if (!found)
    {
        printf("FAIL: test_diagnostics. No diagnostics received.\n");
    }
}

static void test_semantic_tokens()
{
    printf("Running test_semantic_tokens...\n");
    int fd = open("/tmp/test_semantic.zc", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        // fn hello() { let x = 10; }
        // fn: function (modifier?) or keyword? Actually keyword 'fn'.
        // hello: function
        // let: keyword
        // x: variable
        // 10: number
        const char *code = "fn hello() { let x = 10; }";
        write(fd, code, strlen(code));
        close(fd);
    }

    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didOpen\", \"params\": "
                 "{\"textDocument\": {\"uri\": \"file:///tmp/test_semantic.zc\", \"languageId\": "
                 "\"zenc\", \"version\": 1, \"text\": \"fn hello() { let x = 10; }\"}}}");
    usleep(100000);

    send_request(
        "{\"jsonrpc\": \"2.0\", \"id\": 50, \"method\": \"textDocument/semanticTokens/full\", "
        "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_semantic.zc\"}}}");

    char *resp = wait_for_response(50);
    if (!resp)
    {
        printf("WARN: No response for semantic tokens.\n");
        return;
    }

    // Check for "data" array
    if (strstr(resp, "\"data\":["))
    {
        printf("PASS: test_semantic_tokens (received data)\n");
        // Optional: Check existence of some numbers, e.g. token types
    }
    else
    {
        printf("FAIL: test_semantic_tokens (no data): %s\n", resp);
    }
    free(resp);
}

static void test_definition()
{
    printf("Running test_definition...\n");
    int fd = open("/tmp/test_def.zc", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        // Line 0: fn target() {}
        // Line 1: fn main() {
        // Line 2:     target();
        // Line 3: }
        const char *code = "fn target() {}\nfn main() {\n    target();\n}";
        write(fd, code, strlen(code));
        close(fd);
    }

    send_request(
        "{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didOpen\", \"params\": "
        "{\"textDocument\": {\"uri\": \"file:///tmp/test_def.zc\", \"languageId\": \"zenc\", "
        "\"version\": 1, \"text\": \"fn target() {}\\nfn main() {\\n    target();\\n}\"}}}");
    usleep(100000);

    // Request definition at line 2, character 6 (start of "target")
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 60, \"method\": \"textDocument/definition\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_def.zc\"}, "
                 "\"position\": {\"line\": 2, \"character\": 6}}}");

    char *resp = wait_for_response(60);
    if (!resp)
    {
        printf("WARN: No response for definition.\n");
        return;
    }

    // Check for correct range (Line 0)
    // Expect: "uri":..., "range": { "start": { "line": 0 ...
    if (strstr(resp, "\"line\":0"))
    {
        printf("PASS: test_definition (found target at line 0)\n");
    }
    else
    {
        printf("FAIL: test_definition (wrong location): %s\n", resp);
    }
    free(resp);
}

static void test_references()
{
    printf("Running test_references...\n");
    // Reuse test_def.zc
    // Request references for "target" at line 0
    send_request(
        "{\"jsonrpc\": \"2.0\", \"id\": 70, \"method\": \"textDocument/references\", \"params\": "
        "{\"textDocument\": {\"uri\": \"file:///tmp/test_def.zc\"}, \"position\": {\"line\": 0, "
        "\"character\": 5}, \"context\": {\"includeDeclaration\": true}}}");

    char *resp = wait_for_response(70);
    if (!resp)
    {
        printf("WARN: No response for references.\n");
        return;
    }

    // Check for array result
    // Expect at least 2 refs: line 0 (decl) and line 2 (call)
    // "uri":..., "range": ... "line": 0
    // "uri":..., "range": ... "line": 2
    if (strstr(resp, "\"line\":0") && strstr(resp, "\"line\":2"))
    {
        printf("PASS: test_references (found decl and call)\n");
    }
    else
    {
        printf("FAIL: test_references (missing refs): %s\n", resp);
    }
    free(resp);
}

static void test_rename()
{
    printf("Running test_rename...\n");
    // Reuse test_def.zc
    // Rename "target" to "renamed_target"
    send_request(
        "{\"jsonrpc\": \"2.0\", \"id\": 80, \"method\": \"textDocument/rename\", \"params\": "
        "{\"textDocument\": {\"uri\": \"file:///tmp/test_def.zc\"}, \"position\": {\"line\": 0, "
        "\"character\": 5}, \"newName\": \"renamed_target\"}}");

    char *resp = wait_for_response(80);
    if (!resp)
    {
        printf("WARN: No response for rename.\n");
        return;
    }

    // Check for changes
    if (strstr(resp, "changes") && strstr(resp, "renamed_target"))
    {
        printf("PASS: test_rename (received edits)\n");
    }
    else
    {
        printf("FAIL: test_rename (missing edits): %s\n", resp);
    }
    free(resp);
}

static void test_outline()
{
    printf("Running test_outline...\n");
    int fd = open("/tmp/test_outline.zc", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        // struct Point { x: i32; y: i32; }
        // fn main() {}
        const char *code = "struct Point { x: i32; y: i32; }\nfn main() {}";
        write(fd, code, strlen(code));
        close(fd);
    }

    send_request(
        "{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didOpen\", \"params\": "
        "{\"textDocument\": {\"uri\": \"file:///tmp/test_outline.zc\", \"languageId\": \"zenc\", "
        "\"version\": 1, \"text\": \"struct Point { x: i32; y: i32; }\\nfn main() {}\"}}}");
    usleep(100000);

    send_request("{\"jsonrpc\": \"2.0\", \"id\": 90, \"method\": \"textDocument/documentSymbol\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_outline.zc\"}}}");

    char *resp = wait_for_response(90);
    if (!resp)
    {
        printf("WARN: No response for outline.\n");
        return;
    }

    // Check for hierarchy or existence
    // Expect "Point", "main". Maybe "x", "y" if improved.
    if (strstr(resp, "Point") && strstr(resp, "main"))
    {
        if (strstr(resp, "\"children\"") || strstr(resp, "\"name\":\"x\""))
        {
            printf("PASS: test_outline (found structure and children/fields)\n"); // Improved
        }
        else
        {
            printf("PASS: test_outline (found top-level only)\n"); // Basic
        }
    }
    else
    {
        printf("FAIL: test_outline (missing symbols): %s\n", resp);
    }
    free(resp);
}

static void test_formatting()
{
    printf("Running test_formatting...\n");
    int fd = open("/tmp/test_format.zc", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        const char *code = "fn my_func(a: int) {}\nfn main() {\n  let x = 1;\n    if (x == 1) {\n  "
                           "    my_func(1);\n    }\n}\n";
        write(fd, code, strlen(code));
        close(fd);
    }

    send_request(
        "{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didOpen\", \"params\": "
        "{\"textDocument\": {\"uri\": \"file:///tmp/test_format.zc\", \"languageId\": \"zenc\", "
        "\"version\": 1, \"text\": \"fn my_func(a: int) {}\\nfn main() {\\n  let x = 1;\\n    if "
        "(x == 1) {\\n      "
        "my_func(1);\\n    }\\n}\\n\"}}}");
    usleep(100000);

    send_request("{\"jsonrpc\": \"2.0\", \"id\": 95, \"method\": \"textDocument/formatting\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_format.zc\"}}}");

    char *resp = wait_for_response(95);
    if (resp && strstr(resp, "newText"))
    {
        printf("PASS: test_formatting (received edits)\n");
    }
    else
    {
        printf("FAIL: test_formatting (missing edits): %s\n", resp);
    }
    free(resp);
}

static void test_signature_help()
{
    printf("Running test_signature_help...\n");
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 96, \"method\": \"textDocument/signatureHelp\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_format.zc\"}, "
                 "\"position\": {\"line\": 4, \"character\": 14}}}"); // Inside my_func(

    char *resp = wait_for_response(96);
    if (resp && strstr(resp, "signatures"))
    {
        printf("PASS: test_signature_help\n");
    }
    else
    {
        printf("FAIL: test_signature_help: %s\n", resp);
    }
    free(resp);
}

static void write_config_fixture(const char *path, const char *content)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || write(fd, content, strlen(content)) != (ssize_t)strlen(content))
    {
        fail("Could not write a config fixture");
    }
    close(fd);
}

// main.zc imports a library that lives outside the workspace root, so the import resolves only
// through the include_paths of zenc.server.json.
static void test_project_config(void)
{
    printf("Running test_project_config...\n");
    mkdir("/tmp/zc_lsp_config", 0755);
    mkdir("/tmp/zc_lsp_config/ws", 0755);
    mkdir("/tmp/zc_lsp_config/libs", 0755);
    write_config_fixture("/tmp/zc_lsp_config/ws/zenc.server.json",
                         "{\"include_paths\": [\"../libs\"]}\n");
    write_config_fixture("/tmp/zc_lsp_config/libs/mathx.zc",
                         "fn mathx_double(x: int) -> int { return x * 2; }\n");
    const char *main_src = "import \\\"mathx.zc\\\"\\nfn main() { let r = mathx_double(4); }\\n";
    write_config_fixture("/tmp/zc_lsp_config/ws/main.zc",
                         "import \"mathx.zc\"\nfn main() { let r = mathx_double(4); }\n");

    global_len = 0;
    global_buf[0] = 0;
    start_lsp_server();
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 1100, \"method\": \"initialize\", \"params\": "
                 "{\"rootUri\": \"file:///tmp/zc_lsp_config/ws\"}}");
    free(wait_for_response(1100));
    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"initialized\", \"params\": {}}");

    char json[1024];
    snprintf(json, sizeof(json),
             "{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didOpen\", \"params\": "
             "{\"textDocument\": {\"uri\": \"file:///tmp/zc_lsp_config/ws/main.zc\", "
             "\"languageId\": \"zenc\", \"version\": 1, \"text\": \"%s\"}}}",
             main_src);
    send_request(json);

    // The diagnostics of main.zc report the import as missing unless include_paths resolved it.
    // A request after didOpen guarantees they were flushed; they arrive before its response.
    send_request(
        "{\"jsonrpc\": \"2.0\", \"id\": 1101, \"method\": \"textDocument/documentSymbol\", "
        "\"params\": {\"textDocument\": {\"uri\": "
        "\"file:///tmp/zc_lsp_config/ws/main.zc\"}}}");
    char *diagnostics = NULL;
    for (int i = 0; i < 50; i++)
    {
        char *msg = read_message();
        if (!msg)
        {
            break;
        }
        if (strstr(msg, "textDocument/publishDiagnostics") &&
            strstr(msg, "zc_lsp_config/ws/main.zc"))
        {
            diagnostics = msg;
        }
        else if (strstr(msg, "\"id\":1101"))
        {
            free(msg);
            break;
        }
        else
        {
            free(msg);
        }
    }
    if (!diagnostics)
    {
        fail("No diagnostics received for main.zc");
    }
    if (strstr(diagnostics, "Could not find module"))
    {
        printf("diagnostics: %s\n", diagnostics);
        fail("An import was not resolved through include_paths of zenc.server.json");
    }
    free(diagnostics);

    send_request(
        "{\"jsonrpc\": \"2.0\", \"id\": 1102, \"method\": \"shutdown\", \"params\": null}");
    free(wait_for_response(1102));
    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"exit\", \"params\": {}}");
    waitpid(child_pid, NULL, 0);
    printf("PASS: test_project_config\n");
}

// With `use_gitignore: false` in zenc.server.json, the scan skips only what `exclude` lists.
static void test_index_exclude(void)
{
    printf("Running test_index_exclude...\n");
    const char *directories[] = {"/tmp/zc_lsp_exclude", "/tmp/zc_lsp_exclude/vendor",
                                 "/tmp/zc_lsp_exclude/generated", "/tmp/zc_lsp_exclude/src"};
    for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); i++)
    {
        mkdir(directories[i], 0755);
    }
    write_index_fixture("/tmp/zc_lsp_exclude/.gitignore", "vendor/\n");
    write_index_fixture(
        "/tmp/zc_lsp_exclude/zenc.server.json",
        "{\"use_gitignore\": false, \"exclude\": [\"generated/\", \"*.tmp.zc\"]}\n");
    write_index_fixture("/tmp/zc_lsp_exclude/vendor/v.zc", "fn vendor_fn() {}\n");
    write_index_fixture("/tmp/zc_lsp_exclude/generated/g.zc", "fn generated_fn() {}\n");
    write_index_fixture("/tmp/zc_lsp_exclude/x.tmp.zc", "fn tmp_fn() {}\n");
    write_index_fixture("/tmp/zc_lsp_exclude/src/s.zc", "fn src_fn() {}\n");

    global_len = 0;
    global_buf[0] = 0;
    start_lsp_server();
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 1200, \"method\": \"initialize\", \"params\": "
                 "{\"rootUri\": \"file:///tmp/zc_lsp_exclude\"}}");
    free(wait_for_response(1200));
    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"initialized\", \"params\": {}}");

    expect_indexed(1201, "/tmp/zc_lsp_exclude", "src/s.zc", "src_fn");
    expect_indexed(1202, "/tmp/zc_lsp_exclude", "vendor/v.zc", "vendor_fn");
    expect_indexed(1203, "/tmp/zc_lsp_exclude", "generated/g.zc", NULL);
    expect_indexed(1204, "/tmp/zc_lsp_exclude", "x.tmp.zc", NULL);

    send_request(
        "{\"jsonrpc\": \"2.0\", \"id\": 1205, \"method\": \"shutdown\", \"params\": null}");
    free(wait_for_response(1205));
    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"exit\", \"params\": {}}");
    waitpid(child_pid, NULL, 0);
    printf("PASS: test_index_exclude\n");
}

int main()
{
    // Don't die with a cryptic 141 (SIGPIPE) if the LSP server crashes and
    // closes its pipe; send_request will report a clear failure instead.
    signal(SIGPIPE, SIG_IGN);

    start_lsp_server();
    test_initialize();
    test_hover();
    test_hover_signature();
    test_completion();
    test_struct_completion();
    test_diagnostics();
    test_semantic_tokens();
    test_definition();
    test_declaration();
    test_references();
    test_rename();
    test_outline();
    test_formatting();
    test_signature_help();
    test_definition_partial_code();
    test_references_partial_code();
    test_request_unopened_file();
    test_empty_source();
    test_did_change();
    test_code_action();
    test_unknown_method();
    test_workspace_symbol();
    test_did_close();
    test_c_header_symbols();
    test_c_header_reexports();
    test_c_header_completion();
    test_shutdown();
    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"exit\", \"params\": {}}");
    waitpid(child_pid, NULL, 0);
    test_index_gitignore();
    test_project_config();
    test_index_exclude();
    printf("All LSP tests passed!\n");
    return 0;
}
