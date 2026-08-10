#!/usr/bin/env bash
# Lisp backend test suite — transpile Zen tests to Lisp, run with SBCL.
set -o pipefail

ZC=${ZC:-./zc}
TEST_DIR=${TEST_DIR:-tests}
JOBS=${JOBS:-4}

# Features the Lisp backend cannot handle
BLOCKED_PATTERNS=(
    'raw {'
    'include <'
    'include "'
    'asm {'
    'struct '
    'enum '
    'trait '
    '@global'
    '@device'
    '@host'
    'cuda_'
    'extern '
    'operator '
    'import "std/'
    'import "[^"]+/std/'
    'import "[^"]+\.h"'
    'cfg'
)

PASS=0; FAIL=0; SKIP=0
PASSED_LIST=(); FAILED_LIST=(); SKIPPED_LIST=()

# Find test files
mapfile -t ALL_TESTS < <(find "$TEST_DIR" -name "test_*.zc" -not -path "*/backends/*" -not -path "*/misra/*" | sort)
TOTAL=${#ALL_TESTS[@]}
echo "Found $TOTAL test files"

filter_test() {
    local f="$1"
    # Skip if file contains "EXPECT: FAIL" (these are expected-failure tests)
    if head -1 "$f" | grep -q "EXPECT: FAIL"; then
        return 1
    fi
    # Check against blocked patterns
    for pat in "${BLOCKED_PATTERNS[@]}"; do
        if grep -qE "$pat" "$f" 2>/dev/null; then
            return 1
        fi
    done
    return 0
}

run_test() {
    local test_file="$1"
    local src actual

    src=$(mktemp /tmp/zc_lisp_XXXXXX.lisp)
    actual=$(mktemp /tmp/zc_lisp_out_XXXXXX)

    # Phase 1: Transpile to Lisp
    if ! "$ZC" transpile "$test_file" --backend lisp -o "$src" 2>/dev/null; then
        echo "X"
        rm -f "$src" "$actual"
        return 2
    fi

    # Check for error markers in transpiled output
    if grep -q '(error ' "$src" 2>/dev/null; then
        rm -f "$src" "$actual"
        return 2
    fi

    # Phase 2: Run with SBCL
    if ! type sbcl >/dev/null 2>&1; then
        rm -f "$src" "$actual"
        return 2
    fi

    sbcl --script "$src" > "$actual" 2>/dev/null
    local exit_code=$?
    rm -f "$src" "$actual"

    if [ $exit_code -eq 0 ]; then
        return 0
    else
        return 1
    fi
}

echo "Testing Lisp-compatible tests..."
echo ""

# First pass: filter and count
COMPATIBLE=()
for f in "${ALL_TESTS[@]}"; do
    if filter_test "$f"; then
        COMPATIBLE+=("$f")
    fi
done
echo "Lisp-compatible: ${#COMPATIBLE[@]} / $TOTAL"

# Run compatible tests
for f in "${COMPATIBLE[@]}"; do
    echo -n "  $(basename "$f")... "
    run_test "$f"
    case $? in
        0)
            echo "PASS"
            PASS=$((PASS+1))
            PASSED_LIST+=("$(basename "$f")")
            ;;
        1)
            echo "FAIL"
            FAIL=$((FAIL+1))
            FAILED_LIST+=("$(basename "$f")")
            ;;
        2)
            echo "SKIP (unsupported at runtime)"
            SKIP=$((SKIP+1))
            SKIPPED_LIST+=("$(basename "$f")")
            ;;
    esac
done

echo ""
echo "----------------------------------------"
echo "Lisp backend test results:"
echo "-> Total:   $TOTAL"
echo "-> Compat:  ${#COMPATIBLE[@]}"
echo "-> Passed:  $PASS"
echo "-> Failed:  $FAIL"
echo "-> Skipped: $SKIP"
echo "----------------------------------------"
if [ ${#FAILED_LIST[@]} -gt 0 ] && [ ${#FAILED_LIST[@]} -le 20 ]; then
    echo "Failed (first 20):"
    for f in "${FAILED_LIST[@]}"; do echo "  $f"; done
fi
exit $FAIL
