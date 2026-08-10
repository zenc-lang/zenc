#!/bin/bash

ZC="${ZC:-./zc}"
EXAMPLES_DIR="${EXAMPLES_DIR:-examples}"
FAIL_COUNT=0
PASS_COUNT=0

if [ ! -f "$ZC" ]; then
    echo "Error: zc binary not found."
    exit 1
fi

# Allow specifying what tests to run as inputs to the script
# Example: run_example_transpile.sh examples/simd.zc examples/area_test.zc
TEST_FILES=("$@")

if [ ${#TEST_FILES[@]} -gt 0 ]; then
    TEST_LIST=$(printf "%s\n" "${TEST_FILES[@]}" | grep "$EXAMPLES_DIR"/)
else
    if [ ! -d "$EXAMPLES_DIR" ]; then
        echo "** Example directory '$EXAMPLES_DIR' not found; skipping (clone awesome-zenc to enable)."
        exit 0
    fi
    TEST_LIST=$(find "$EXAMPLES_DIR" -name "*.zc" | sort)
fi

if [ -z "$TEST_LIST" ]; then
    echo "** Nothing to do **"
    exit 0
fi

echo "Running Example Transpilation Tests..."

while IFS= read -r file; do
    [ -e "$file" ] || continue
    echo -n "Transpiling $file... "
    
    OUTPUT=$($ZC transpile "$file" -o "$file.c" 2>&1)
    EXIT_CODE=$?
    
    if [ $EXIT_CODE -eq 0 ]; then
        echo "PASS"
        PASS_COUNT=$((PASS_COUNT + 1))
        [ -f "$file.c" ] && rm "$file.c"
        [ -f "a.out" ] && rm "a.out"
    else
        echo "FAIL"
        echo "$OUTPUT"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
done <<< "$TEST_LIST"

echo "----------------------------------------"
echo "Summary:"
echo "-> Passed: $PASS_COUNT"
echo "-> Failed: $FAIL_COUNT"
echo "----------------------------------------"

if [ $FAIL_COUNT -ne 0 ]; then
    exit 1
fi

exit 0
