#!/bin/bash

# Codegen Verification Test Runner
ZC="./zc"
TEST_DIR="tests/compiler/codegen"
PASSED=0
FAILED=0

if [ ! -f "$ZC" ]; then
    echo "Error: zc binary not found."
    exit 1
fi

# Make forwards what tests to run as inputs to the script
# But this is not used by this script atm, since it only runs 1 cherry-picked file
# Consume the arguments and, if present, disable this script
# Example: run_codegen_tests.sh examples/simd.zc examples/area_test.zc
TEST_FILES=("$@")

if [ ${#TEST_FILES[@]} -gt 0 ]; then
    TEST_LIST=$(printf "%s\n" "${TEST_FILES[@]}" | grep "examples/")
else
    TEST_LIST=""
fi

# This script doesn't support running on set of target files currently
# But make passes the arguments forward, so treat it as wanting to disable this test
if [ -n "$TEST_LIST" ]; then
    echo "** Nothing to do **"
    exit 0
fi

echo "** Running Codegen Verification Tests **"

#
# Test 1: Duplicate Typedefs
#

TEST_NAME="test_dedup_typedefs.zc"
echo -n "Testing $TEST_DIR/$TEST_NAME (Duplicate Typedefs)... "

if ! "$ZC" "$TEST_DIR/$TEST_NAME" --emit-c > /dev/null 2>&1; then
    echo "FAIL (Compilation error)"
    ((FAILED++))
else
    # Check generated C file for duplicates
    # We expect "typedef struct Vec2f Vec2f;" to appear exactly once
    COUNT=$(grep -c "typedef struct Vec2f Vec2f;" "${TEST_NAME%.zc}.c")
    
    if [ "$COUNT" -eq 1 ]; then
        echo "PASS"
        ((PASSED++))
    else
        echo "FAIL (Found $COUNT typedefs for Vec2f, expected 1)"
        ((FAILED++))
    fi
fi

# Cleanup
rm -f "${TEST_NAME%.zc}.c" "${TEST_NAME%.zc}" a.out

#
# Test 2: Emit source mappings
#         zc check "file.zc" -g --warn-errors
#         will result in source mapping warnings if it fails and treat warnings as errors
#         thus returning a non-zero error code
#

TEST_NAME="test_emit_source_mapping.zc"
echo -n "Testing $TEST_DIR/$TEST_NAME (Source Mappings)... "

if ! "$ZC" check "$TEST_DIR/$TEST_NAME" -g --warn-errors > /dev/null 2>&1; then
    echo "FAIL"
    ((FAILED++))
else
    echo "PASS"
    ((PASSED++))
fi

#
# Test 3: Slice Instantiation Naming
#

TEST_NAME="test_slice_instantiation_offset.zc"
echo -n "Testing $TEST_DIR/$TEST_NAME (Slice Naming)... "

if ! "$ZC" "$TEST_DIR/$TEST_NAME" --emit-c > /dev/null 2>&1; then
    echo "FAIL (Compilation error)"
    ((FAILED++))
else
    # Verify that Slice__Inner is used and NOT Slice___Inner
    # Also check that typedef is correct
    TRIPLE_UNDERSCORE=$(grep -c "Slice___Inner" "${TEST_NAME%.zc}.c")
    DOUBLE_UNDERSCORE=$(grep -c "Slice__Inner" "${TEST_NAME%.zc}.c")
    
    if [ "$TRIPLE_UNDERSCORE" -eq 0 ] && [ "$DOUBLE_UNDERSCORE" -gt 0 ]; then
        echo "PASS"
        ((PASSED++))
    else
        echo "FAIL (Found $TRIPLE_UNDERSCORE mangled names, $DOUBLE_UNDERSCORE correct names)"
        ((FAILED++))
    fi
fi

# Cleanup
rm -f "${TEST_NAME%.zc}.c" "${TEST_NAME%.zc}" a.out

echo "----------------------------------------"
echo "Summary:"
echo "-> Passed: $PASSED"
echo "-> Failed: $FAILED"
echo "----------------------------------------"

if [ $FAILED -ne 0 ]; then
    exit 1
else
    exit 0
fi
