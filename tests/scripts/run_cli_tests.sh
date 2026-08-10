#!/bin/bash
# Zen C CLI integration tests.
#
# Verifies the top-level `zc` command surface: version output, usage/help,
# and error handling for bad invocations, plus a compile+run smoke test.
#
# Usage: ./tests/scripts/run_cli_tests.sh
# Env:   ZC=path/to/zc          (default: ./zc)
#        ZC_ROOT=path/to/stdlib (set when running from outside the repo)

ZC="${ZC:-./zc}"
if [ ! -f "$ZC" ]; then
    echo "Error: zc binary not found ($ZC). Build it first." >&2
    exit 1
fi

PASSED=0
FAILED=0
FAILED_TESTS=""

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

# --- Version ---------------------------------------------------------------

out=$("$ZC" --version 2>&1)
if echo "$out" | grep -qE '^zc v[0-9]'; then
    check "--version prints version" 1
else
    check "--version prints version" 0
fi

out=$("$ZC" -V 2>&1)
if echo "$out" | grep -qE '^zc v[0-9]'; then
    check "-V prints version" 1
else
    check "-V prints version" 0
fi

# --- Usage / help ----------------------------------------------------------

out=$("$ZC" 2>&1)
if echo "$out" | grep -q "usage: zc"; then
    check "no arguments prints usage" 1
else
    check "no arguments prints usage" 0
fi

out=$("$ZC" help 2>&1)
if echo "$out" | grep -q "usage: zc"; then
    check "zc help prints usage" 1
else
    check "zc help prints usage" 0
fi

# --- Error paths -----------------------------------------------------------

out=$("$ZC" run 2>&1)
if echo "$out" | grep -q "no input file specified"; then
    check "zc run with no file reports error" 1
else
    check "zc run with no file reports error" 0
fi

out=$("$ZC" run "$ZC/does_not_exist.zc" 2>&1)
if echo "$out" | grep -q "could not read file"; then
    check "zc run with missing file reports error" 1
else
    check "zc run with missing file reports error" 0
fi

out=$("$ZC" --definitely-not-a-flag "$ZC" 2>&1)
if echo "$out" | grep -qiE "unknown|unrecognized|error"; then
    check "unknown flag reports error" 1
else
    check "unknown flag reports error" 0
fi

# --- Compile + run smoke test ----------------------------------------------

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT
printf 'fn main() {\n    println "cli-smoke-ok"\n}\n' > "$workdir/smoke.zc"

out=$("$ZC" run "$workdir/smoke.zc" -o "$workdir/smoke" 2>&1)
if echo "$out" | grep -q "cli-smoke-ok"; then
    check "zc run executes a program" 1
else
    check "zc run executes a program" 0
fi

out=$("$ZC" transpile "$workdir/smoke.zc" -o "$workdir/smoke_out.c" 2>&1)
if [ -s "$workdir/smoke_out.c" ]; then
    check "zc transpile emits C source" 1
else
    check "zc transpile emits C source" 0
fi

echo "----------------------------------------"
echo "Results (CLI):"
echo "-> Passed:  $PASSED"
echo "-> Failed:  $FAILED"
echo "----------------------------------------"

if [ "$FAILED" -ne 0 ]; then
    echo -e "Failed tests:$FAILED_TESTS"
    exit 1
fi
echo "All CLI tests passed!"
exit 0
