#!/bin/bash

# Move semantics test runner
ZC="./zc"
if [ ! -f "$ZC" ]; then
    ZC="./build/zc"
fi

TEST_DIR="tests/move"
PASSED=0
FAILED=0
FAILED_TESTS=""

if [ ! -f "$ZC" ]; then
    echo "Error: zc binary not found. Please build it first."
    exit 1
fi

if [ ! -d "$TEST_DIR" ]; then
    echo "Error: $TEST_DIR does not exist."
    exit 1
fi

echo "** Running Move Semantics Tests **"

while IFS= read -r test_file; do
    [ -e "$test_file" ] || continue

    echo -n "Testing $test_file... "

    output=$($ZC run "$test_file" 2>&1)
    exit_code=$?

    if grep -q "^// EXPECT: FAIL" "$test_file"; then
        if [ $exit_code -ne 0 ]; then
            echo "PASS (Expected Failure)"
            PASSED=$((PASSED + 1))
        else
            echo "FAIL (Unexpected Success)"
            FAILED=$((FAILED + 1))
            FAILED_TESTS="$FAILED_TESTS\n- $test_file (Unexpected Success)"
        fi
    else
        if [ $exit_code -eq 0 ]; then
            echo "PASS"
            PASSED=$((PASSED + 1))
        else
            echo "FAIL"
            FAILED=$((FAILED + 1))
            err_line=$(echo "$output" | grep -m1 -E "error:|C compilation failed|panic" || true)
            FAILED_TESTS="$FAILED_TESTS\n- $test_file ${err_line:+($err_line)}"
        fi
    fi
done < <(find "$TEST_DIR" -name "*.zc" -not -name "_*.zc" | sort)

echo "----------------------------------------"
echo "Summary:"
echo "-> Passed: $PASSED"
echo "-> Failed: $FAILED"
echo "----------------------------------------"

rm -f a.out out.c

if [ $FAILED -ne 0 ]; then
    echo -e "Failed tests:$FAILED_TESTS"
    exit 1
fi

echo "All move tests passed!"
exit 0
