#!/usr/bin/env python3
"""Minimal LSP smoke test.

Starts `zc lsp`, exchanges a JSON-RPC initialize/shutdown conversation over
stdio (Content-Length framing), and verifies the server responds with valid
JSON-RPC responses.
"""

import argparse
import json
import select
import subprocess
import sys
import time


def frame(payload):
    body = payload.encode("utf-8")
    return b"Content-Length: %d\r\n\r\n%s" % (len(body), body)


def recv_message(proc, timeout=10.0):
    """Read one Content-Length framed JSON message from proc.stdout."""
    deadline = time.time() + timeout
    buf = b""
    while b"\r\n\r\n" not in buf:
        if time.time() > deadline:
            raise TimeoutError("timed out reading LSP message headers")
        ready, _, _ = select.select([proc.stdout], [], [], 0.5)
        if not ready:
            continue
        # read1() reads from the fd directly without filling the internal
        # buffer (read() would read ahead and break select on later chunks).
        chunk = proc.stdout.read1(4096)
        if not chunk:
            raise RuntimeError("LSP closed stdout")
        buf += chunk

    header, body = buf.split(b"\r\n\r\n", 1)
    content_length = None
    for line in header.split(b"\r\n"):
        if line.lower().startswith(b"content-length:"):
            content_length = int(line.split(b":", 1)[1].strip())
    if content_length is None:
        raise RuntimeError("LSP response missing Content-Length header")

    while len(body) < content_length:
        if time.time() > deadline:
            raise TimeoutError("timed out reading LSP message body")
        ready, _, _ = select.select([proc.stdout], [], [], 0.5)
        if not ready:
            continue
        chunk = proc.stdout.read1(content_length - len(body))
        if not chunk:
            raise RuntimeError("LSP closed stdout during message body")
        body += chunk
    return json.loads(body[:content_length])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--zc", default="./zc", help="path to the zc binary")
    args = ap.parse_args()

    proc = subprocess.Popen(
        [args.zc, "lsp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    try:
        req = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {"processId": None, "rootUri": None, "capabilities": {}},
        }
        proc.stdin.write(frame(json.dumps(req)))
        proc.stdin.flush()

        resp = recv_message(proc)
        if resp.get("id") != 1 or "result" not in resp:
            print("FAIL: initialize response invalid: %s" % json.dumps(resp)[:200])
            return 1
        print("initialize response OK")

        proc.stdin.write(
            frame(json.dumps({"jsonrpc": "2.0", "id": 2, "method": "shutdown", "params": None}))
        )
        proc.stdin.flush()
        resp = recv_message(proc)
        if resp.get("id") != 2 or "result" not in resp:
            print("FAIL: shutdown response invalid: %s" % json.dumps(resp)[:200])
            return 1
        print("shutdown response OK")

        proc.stdin.write(frame(json.dumps({"jsonrpc": "2.0", "method": "exit", "params": None})))
        proc.stdin.flush()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            print("FAIL: LSP did not exit after 'exit' notification")
            return 1
        return 0
    except (TimeoutError, RuntimeError, json.JSONDecodeError, OSError) as exc:
        print("FAIL: %s" % exc)
        return 1
    finally:
        if proc.poll() is None:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
