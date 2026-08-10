#!/bin/bash

# Zen-C MISRA Compliance Test Runner
# Scans tests/misra/*.zc for // EXPECT: MISRA Rule X.Y and verifies diagnostics.

ZC="./zc"
if [ ! -f "$ZC" ]; then
    ZC="./zc.exe"
fi

TEST_DIR="tests/misra"
PASSED=0
FAILED=0
TOTAL=0

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}** Running Zen C MISRA Compliance Tests **${NC}"

for test_file in "$TEST_DIR"/*.zc; do
    [ -e "$test_file" ] || continue
    ((TOTAL++))
    
    echo -n "Testing $(basename "$test_file")... "
    
    # Extract expected rules
    expected_rules=$(grep "// EXPECT: " "$test_file" | grep -v "PASS" | sed -E 's|.*// EXPECT: (.*)|\1|')
    expect_pass=$(grep -c "// EXPECT: PASS" "$test_file")
    
    # Run compiler with --misra
    output=$($ZC build "$test_file" --misra -o /dev/null 2>&1)
    exit_code=$?
    
    file_failed=0
    missing_rules=""
    
    if [ "$expect_pass" -gt 0 ]; then
        if [ $exit_code -ne 0 ]; then
            file_failed=1
            error_msg="Expected PASS, but compiler returned error $exit_code"
        fi
    else
        # Verify diagnostics for each expected rule
        while IFS= read -r rule; do
            [ -z "$rule" ] && continue
            # Literal substring match (case avoids =~ quoting pitfalls)
            case "$output" in
                *"$rule"*) ;;
                *)
                    file_failed=1
                    missing_rules="$missing_rules $rule"
                    ;;
            esac
        done <<< "$expected_rules"
        
        if [ -z "$expected_rules" ] && [ $exit_code -ne 0 ]; then
             # No expected rules, but it failed - maybe it's just a general MISRA failure?
             # For now, we prefer explicit expectations.
             echo -n "(No expectations found) "
        fi
    fi
    
    if [ $file_failed -eq 0 ]; then
        echo -e "${GREEN}PASS${NC}"
        ((PASSED++))
    else
        echo -e "${RED}FAIL${NC}"
        if [ -n "$missing_rules" ]; then
            echo -e "  ${RED}Missing expected rules:${NC} $missing_rules"
        fi
        if [ -n "$error_msg" ]; then
            echo -e "  ${RED}Error:${NC} $error_msg"
        fi
        echo "----------------------------------------"
        echo "Output:"
        echo "$output"
        echo "----------------------------------------"
        ((FAILED++))
    fi
done

echo "----------------------------------------"
echo -e "Results: ${GREEN}Passed: $PASSED${NC}, ${RED}Failed: $FAILED${NC}, Total: $TOTAL"
echo "----------------------------------------"

if [ $FAILED -ne 0 ]; then
    exit 1
fi
exit 0
