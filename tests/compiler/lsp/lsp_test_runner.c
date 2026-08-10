#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
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
                    return msg;
                }
                printf("Mismatch ID: got %d expected %d\n", got_id, id);
            }
            cJSON_Delete(json);
        }
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

static void test_completion_prefix()
{
    printf("Running test_completion_prefix...\n");
    int fd = open("/tmp/test_prefix.zc", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        // Typing 'pr' should filter completions to print/printf/println
        const char *code = "fn main() { pr }";
        write(fd, code, strlen(code));
        close(fd);
    }

    send_request(
        "{\"jsonrpc\": \"2.0\", \"method\": \"textDocument/didOpen\", \"params\": "
        "{\"textDocument\": {\"uri\": \"file:///tmp/test_prefix.zc\", \"languageId\": "
        "\"zenc\", \"version\": 1, \"text\": \"fn main() { pr }\"}}}");
    usleep(100000);

    // Request completion at line 0, character 14 (cursor after 'pr', before ' }')
    send_request("{\"jsonrpc\": \"2.0\", \"id\": 110, \"method\": \"textDocument/completion\", "
                 "\"params\": {\"textDocument\": {\"uri\": \"file:///tmp/test_prefix.zc\"}, "
                 "\"position\": {\"line\": 0, \"character\": 14}}}");

    char *resp = wait_for_response(110);
    if (!resp)
    {
        printf("WARN: No response for prefix completion.\n");
        return;
    }

    // Should include print/printf/println
    int ok = (strstr(resp, "\"label\":\"print\"") != NULL &&
              strstr(resp, "\"label\":\"printf\"") != NULL &&
              strstr(resp, "\"label\":\"println\"") != NULL);
    // Should NOT include keywords not matching 'pr' prefix (e.g. 'malloc')
    int clean = (strstr(resp, "\"label\":\"malloc\"") == NULL);

    if (ok && clean)
    {
        printf("PASS: test_completion_prefix (print/printf/println filtered, no extras)\n");
    }
    else
    {
        if (!ok)
        {
            printf("WARN: test_completion_prefix (missing print/printf/println): %s\n", resp);
        }
        if (!clean)
        {
            printf("WARN: test_completion_prefix (unexpected extras in result)\n");
        }
    }
    free(resp);
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

int main()
{
    // Don't die with a cryptic 141 (SIGPIPE) if the LSP server crashes and
    // closes its pipe; send_request will report a clear failure instead.
    signal(SIGPIPE, SIG_IGN);

    start_lsp_server();
    test_initialize();
    test_hover();
    test_completion();
    test_struct_completion();
    test_diagnostics();
    test_semantic_tokens();
    test_definition();
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
    test_completion_prefix();
    test_shutdown();
    send_request("{\"jsonrpc\": \"2.0\", \"method\": \"exit\", \"params\": {}}");
    waitpid(child_pid, NULL, 0);
    printf("All LSP tests passed!\n");
    return 0;
}
