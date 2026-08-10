#!/bin/bash
# Zen C REPL integration tests.
#
# Drives the interactive REPL both in single-shot mode (`zc repl -c <line>`)
# and as a piped session, verifying:
#   - the process does not crash on valid input (regression: ParserContext
#     used to be initialized without ->config, segfaulting on any input),
#   - a statement is emitted exactly once (regression: lines were emitted
#     twice, so `let x = 5;` failed with a redefinition),
#   - failed lines are rolled back from session history,
#   - commands (:help, :history, :vars, :type, :save, :load, :c) work,
#   - expression evaluation / auto-print works when an execution backend is
#     available (JIT, or the cc fallback build with ZC_HAS_JIT=0).
#
# Execution-dependent checks are skipped automatically when the REPL cannot
# execute code in this environment (e.g. libtcc built without --with-selinux).
#
# Usage: ./tests/scripts/run_repl_tests.sh
# Env:   ZC=path/to/zc          (default: ./zc)
#        ZC_TEST_NO_SOURCE=1    (unused, kept for parity with run_tests.sh)

ZC="${ZC:-./zc}"
if [ ! -f "$ZC" ]; then
    echo "Error: zc binary not found ($ZC). Build it first." >&2
    exit 1
fi

PASSED=0
FAILED=0
FAILED_TESTS=""
SAVE_FILE="/tmp/zenc_repl_save_test.$$.zc"

# Strip ANSI escapes, carriage returns, and line-editor redraw artifacts so
# the remaining output is banner + prompts + real program output, one item
# per line.
clean() {
    tr '\r' '\n' |
        sed 's/\x1b\[[0-9;]*[A-Za-z]//g' |
        sed 's/zenc >>>.*$//' |
        sed '/^[[:space:]]*$/d'
}

# Run a single REPL command (zc repl -c '<line>').
# Each invocation gets a fresh HOME so persisted history from a previous
# session cannot leak into (and break) the next one.
run_cmd() {
    local home
    home="$(mktemp -d)"
    HOME="$home" "$ZC" repl -c "$1" 2>&1 | clean
    rm -rf "$home"
}

# Run a full piped session (newline-separated input).
run_session() {
    local home
    home="$(mktemp -d)"
    printf '%s\n' "$1" | HOME="$home" "$ZC" repl 2>&1 | clean
    rm -rf "$home"
}

check() {
    local name="$1"
    local ok="$2"
    if [ "$ok" = "1" ]; then
        echo "  [PASS] $name"
        PASSED=$((PASSED + 1))
    else
        echo "  [FAIL] $name"
        FAILED=$((FAILED + 1))
        FAILED_TESTS="$FAILED_TESTS\n- $name"
    fi
}

# --- Capability probe: can the REPL actually execute code here? ------------
probe_out=$(run_session '1+1
quit')
if echo "$probe_out" | grep -qx "2"; then
    CAN_EXEC=1
else
    CAN_EXEC=0
    echo "** REPL execution backend unavailable (JIT/cc fallback) in this environment."
    echo "** Execution checks will be skipped; structural checks still run."
fi

# --- Structural checks (never require execution) ---------------------------

out=$(run_cmd "let x = 5;")
if echo "$out" | grep -qiE "signal 11|segmentation"; then
    check "no crash on 'let x = 5;'" 0
else
    check "no crash on 'let x = 5;'" 1
fi

out=$(run_cmd "fn add(a: int, b: int) -> int { return a + b; }")
if echo "$out" | grep -qiE "signal 11|segmentation"; then
    check "no crash on function definition" 0
else
    check "no crash on function definition" 1
fi

out=$(run_cmd "let x = 5;")
if echo "$out" | grep -qiE "redefinition|redeclaration"; then
    check "statement emitted exactly once" 0
else
    check "statement emitted exactly once" 1
fi

out=$(run_cmd "1+1")
if echo "$out" | grep -qiE "signal 11|segmentation"; then
    check "no crash on expression input" 0
else
    check "no crash on expression input" 1
fi

out=$(run_session ':help
quit')
if echo "$out" | grep -q ":reset"; then
    check ":help lists commands" 1
else
    check ":help lists commands" 0
fi

out=$(run_session 'let x = 5;
let z = ;
:history
quit')
# The bad line must not appear as a numbered history entry.
if echo "$out" | grep -qE '^ *[0-9]+ +let z = ;'; then
    check "failed line rolled back from history" 0
else
    check "failed line rolled back from history" 1
fi

out=$(run_session ':type 1 + 1
quit')
if echo "$out" | grep -qiE "Type:.*int"; then
    check ":type reports expression type" 1
else
    check ":type reports expression type" 0
fi

# :c transpiles via a subprocess (zc build --emit-c), so it does not depend on
# the REPL execution backend being available.
out=$(run_session ':c 1 + 1
quit')
if echo "$out" | grep -q "(1 + 1);"; then
    check ":c shows generated C" 1
else
    check ":c shows generated C" 0
fi

# --- Execution checks (only when a backend is available) -------------------

if [ "$CAN_EXEC" = "1" ]; then
    out=$(run_session 'let x = 5;
let z = ;
:history
quit')
    if echo "$out" | grep -qE '^ *[0-9]+ +let x = 5;'; then
        check "successful line kept in history" 1
    else
        check "successful line kept in history" 0
    fi

    out=$(run_session 'let x = 5;
:vars
quit')
    if echo "$out" | grep -q "x (int): 5"; then
        check ":vars lists session variables" 1
    else
        check ":vars lists session variables" 0
    fi

    out=$(run_session "let x = 5;
let y = 7;
:save $SAVE_FILE
:reset
:load $SAVE_FILE
:vars
quit")
    if echo "$out" | grep -q "x (int): 5" && echo "$out" | grep -q "y (int): 7"; then
        check ":save / :reset / :load round-trip" 1
    else
        check ":save / :reset / :load round-trip" 0
    fi
    rm -f "$SAVE_FILE"

    out=$(run_cmd "1+1")
    if echo "$out" | grep -qx "2"; then
        check "auto-print evaluates '1+1'" 1
    else
        check "auto-print evaluates '1+1'" 0
    fi

    out=$(run_session 'let x = 5;
x + 1
quit')
    if echo "$out" | grep -qx "6"; then
        check "variables persist across lines" 1
    else
        check "variables persist across lines" 0
    fi

    out=$(run_session 'fn add(a: int, b: int) -> int { return a + b; }
add(2, 3)
quit')
    if echo "$out" | grep -qx "5"; then
        check "function definition + call" 1
    else
        check "function definition + call" 0
    fi

    out=$(run_session 'fn fib(n: int) -> int {
    if n < 2 { return n; }
    return fib(n - 1) + fib(n - 2);
}
fib(10)
quit')
    if echo "$out" | grep -qx "55"; then
        check "multi-line function definition" 1
    else
        check "multi-line function definition" 0
    fi

    out=$(run_session 'let x = 5;
println "x={x}"
quit')
    if echo "$out" | grep -qx "x=5"; then
        check "println interpolation" 1
    else
        check "println interpolation" 0
    fi

    out=$(run_session '"hello"
quit')
    if echo "$out" | grep -qx "hello"; then
        check "string literal auto-print" 1
    else
        check "string literal auto-print" 0
    fi

    out=$(run_session 'struct Point { x: int, y: int }
let p = Point { x: 3, y: 4 }
println "p.x={p.x}"
quit')
    if echo "$out" | grep -qx "p.x=3"; then
        check "struct definition + field access" 1
    else
        check "struct definition + field access" 0
    fi
fi

echo "----------------------------------------"
echo "Results (REPL):"
echo "-> Passed:  $PASSED"
echo "-> Failed:  $FAILED"
echo "----------------------------------------"

if [ "$FAILED" -ne 0 ]; then
    echo -e "Failed tests:$FAILED_TESTS"
    exit 1
fi
echo "All REPL tests passed!"
exit 0
