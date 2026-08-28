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

# --- Robustness: pathological inputs must exit cleanly (never crash) -------

# A crash (signal) yields exit code > 128; a clean compile or a normal
# diagnostic error stays <= 128. This guards the parser/codegen overflow
# fixes against regressions.
robust_check() {
    local name="$1"
    local code="$2"
    printf '%s\n' "$code" > "$workdir/robust.zc"
    "$ZC" build "$workdir/robust.zc" -o "$workdir/robust_out" >/dev/null 2>&1
    local rc=$?
    if [ "$rc" -le 128 ]; then
        check "$name" 1
    else
        check "$name" 0
    fi
}

robust_check "20-arg ?() scan does not crash" 'fn main() {
    let x = ?("prompt", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20);
    println "x";
}'

long_ident=$(python3 -c "print('A' * 1500)")
robust_check "very long qualified identifier does not crash" "fn main() {
    let x = Foo::$long_ident;
    println \"x\";
}"

robust_check "enum variant with 40 type args does not crash" "fn gen_enum() {
    return 0;
}
enum E { V(int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int) }
fn main() {
    let e = E::V(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40);
    println \"e\";
}"

robust_check "@derive on a struct with many long fields does not crash" "fn gen_derive() {
    return 0;
}
@derive(Eq)
struct S {
    field_0_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_1_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_2_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_3_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_4_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_5_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_6_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_7_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_8_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_9_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_10_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_11_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_12_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_13_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_14_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_15_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_16_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_17_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_18_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_19_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_20_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_21_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_22_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_23_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_24_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_25_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_26_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_27_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_28_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
    field_29_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: int;
}
fn main() {
    println \"ok\";
}"

garg=$(python3 -c "print('B' * 1500)")
robust_check "generic call with a very long type argument does not crash" "fn f<T>(x: T) -> T {
    return x;
}
fn main() {
    let y = f<$garg>(1);
    println \"y\";
}"

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
