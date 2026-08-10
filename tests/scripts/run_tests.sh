#!/bin/bash
set -m # Enable job control for 'jobs' command to work properly in scripts

# Zen-C Test Suite Runner
# Usage: ./tests/scripts/run_tests.sh [options]
#
# Options:
#   --cpp                   Compile all tests in C++ mode
#   --cc <compiler>         Use a specific C compiler
#   --check                 Enable typechecking
#   --no-source             Don't print generated source code on failure
#   -j, --jobs <n>          Run <n> tests in parallel (default: 1)
#   -- file1.zc file2.zc    Any file listed after postfix `--` will be run
#                           If empty, scan for and run all tests
#
# Examples:
#   ./tests/scripts/run_tests.sh                                        # Test in C mode (default)
#   ./tests/scripts/run_tests.sh --cpp                                  # Test in C++ mode
#   ./tests/scripts/run_tests.sh -j 4                                   # Test with 4 parallel jobs
#   ./tests/scripts/run_tests.sh --cc clang                             # Test with clang
#   ./tests/scripts/run_tests.sh --cc clang --cpp                       # Test in C++ mode with clang
#   ./tests/scripts/run_tests.sh -- std/test_hash.zc std/test_arena.zc  # Test only these files

# Configuration
ZC="./zc"
if [ ! -f "$ZC" ]; then
    if [ -f "./zc.exe" ]; then
        ZC="./zc.exe"
    elif [ -f "./build/zc" ]; then
        ZC="./build/zc"
    elif [ -f "./build/zc.exe" ]; then
        ZC="./build/zc.exe"
    fi
fi
TEST_DIR="tests"
PASSED=0
FAILED=0
SKIPPED=0
FAILED_TESTS=""
JOBS=1
if [ -n "$ZC_TEST_JOBS" ]; then
    JOBS="$ZC_TEST_JOBS"
fi

# Parse arguments
CC_NAME="gcc (default)"
USE_TYPECHECK=0
USE_CPP=0
SHOW_SOURCE=1
if [ "$ZC_TEST_NO_SOURCE" = "1" ]; then
    SHOW_SOURCE=0
fi

TEST_FILES=()
sys_arch=$(uname -m)
zc_args=()

collect_files=0
prev_arg=""
for arg in "$@"; do
    if [ "$arg" = "--" ]; then
        # After `--`, only .zc files to test are listed
        collect_files=1
        continue
    fi

    if [ $collect_files -eq 1 ]; then
        TEST_FILES+=("$arg")
        continue
    fi

    if [ "$prev_arg" = "--cc" ]; then
        CC_NAME="$arg"
    fi
    if [ "$arg" = "--check" ]; then
        USE_TYPECHECK=1
        continue
    fi
    if [ "$arg" = "--cpp" ]; then
        USE_CPP=1
        zc_args+=("--cpp")
        continue
    fi
    if [ "$arg" = "--no-source" ]; then
        SHOW_SOURCE=0
        continue
    fi
    if [ "$arg" = "-j" ] || [ "$arg" = "--jobs" ]; then
        prev_arg="--jobs"
        continue
    fi
    if [ "$prev_arg" = "--jobs" ]; then
        JOBS="$arg"
        prev_arg=""
        continue
    fi

    zc_args+=("$arg")
    prev_arg="$arg"
done

# Also check ZC_FLAGS for --cpp (backwards compat)
if [[ "$ZC_FLAGS" == *"--cpp"* ]]; then
    USE_CPP=1
    read -r -a zc_flag_words <<< "$ZC_FLAGS"
    zc_args+=("${zc_flag_words[@]}")
fi

# Build mode label
MODE="C"
if [ $USE_CPP -eq 1 ]; then
    MODE="C++"
fi

if [ ! -f "$ZC" ]; then
    echo "Error: zc binary not found. Please build it first."
    exit 1
fi

if [ ${#TEST_FILES[@]} -gt 0 ]; then
    mapfile -t TEST_LIST < <(printf "%s\n" "${TEST_FILES[@]}" | grep "$TEST_DIR"/)
else
    mapfile -t TEST_LIST < <(find "$TEST_DIR" -name "*.zc" -not -name "_*.zc" -not -path "*/backends/*" | sort)
    if [ $USE_CPP -eq 1 ] && [ -d "$TEST_DIR/backends/cpp" ]; then
        mapfile -t CPP_TESTS < <(find "$TEST_DIR/backends/cpp" -name "test_*.zc" | sort)
        TEST_LIST=("${TEST_LIST[@]}" "${CPP_TESTS[@]}")
    fi

fi

# Filter by ZC_TEST_FILTER env var if set (path/name substring match, case-insensitive)
if [ -n "$ZC_TEST_FILTER" ]; then
    mapfile -t FILTERED < <(printf "%s\n" "${TEST_LIST[@]}" | grep -i "$ZC_TEST_FILTER")
    TEST_LIST=("${FILTERED[@]}")
    if [ ${#TEST_LIST[@]} -eq 0 ]; then
        echo "** No tests match filter '$ZC_TEST_FILTER' **"
        exit 0
    fi
    echo "** Filtering by: $ZC_TEST_FILTER (${#TEST_LIST[@]} tests match) **"
fi

if [ ${#TEST_LIST[@]} -eq 0 ]; then
    echo "** Nothing to do **"
    exit 0
fi

echo "** Running Zen C test suite (mode: $MODE, compiler: $CC_NAME, jobs: $JOBS) **"

# Create temp dir for parallel results
RESULTS_DIR=$(mktemp -d)
trap 'rm -rf "$RESULTS_DIR"' EXIT

run_test() {
    local test_file="$1"
    local job_id="$2"
    local result_file="$RESULTS_DIR/$job_id"
    
    # Skip tests logic (duplicated for subprocess context)
    if [[ "$CC_NAME" == *"tcc"* ]]; then
        if [[ "$test_file" == *"test_intel.zc"* ]] || [[ "$test_file" == *"test_attributes.zc"* ]] || [[ "$test_file" == *"test_simd_native.zc"* ]]; then
            echo "SKIP" > "$result_file.status"
            return
        fi
    fi
    if [[ "$CC_NAME" == *"zig"* ]] && [[ "$test_file" == *"test_plugins_suite.zc"* ]]; then
        echo "SKIP" > "$result_file.status"
        return
    fi
    if [[ "$CC_NAME" == *"filcc"* ]]; then
        if [[ "$test_file" == *"test_asm"* ]] || [[ "$test_file" == *"test_intel"* ]] || [[ "$test_file" == *"test_volatile"* ]] || [[ "$test_file" == *"test_plugins_suite"* ]] || [[ "$test_file" == *"test_dynamic_plugin"* ]] || [[ "$test_file" == *"test_concurrency"* ]] || [[ "$test_file" == *"test_thread"* ]] || [[ "$test_file" == *"test_sync"* ]]; then
            echo "SKIP" > "$result_file.status"
            return
        fi
    fi
    if [ $USE_CPP -eq 1 ]; then
        if [[ "$test_file" == *"test_asm"* ]] || [[ "$test_file" == *"test_intel.zc"* ]] || [[ "$test_file" == *"test_simd_native.zc"* ]]; then
            echo "SKIP" > "$result_file.status"
            return
        fi
    fi
    if [[ "$sys_arch" != *"86"* && "$sys_arch" != "amd64" ]]; then
        if [[ "$test_file" == *"test_asm"* ]] || [[ "$test_file" == *"test_intel.zc"* ]] || [[ "$test_file" == *"test_simd_x86.zc"* ]]; then
            echo "SKIP" > "$result_file.status"
            return
        fi
    fi
    if [[ "$test_file" == *"_arm64.zc"* ]] || [[ "$test_file" == *"_aarch64.zc"* ]]; then
        if [[ "$sys_arch" != *"arm64"* && "$sys_arch" != "aarch64" ]]; then
            echo "SKIP" > "$result_file.status"
            return
        fi
    fi
    if [[ "$test_file" == *"tests/misra/"* ]]; then
        echo "SKIP" > "$result_file.status"
        return
    fi
    if grep -q "// REQUIRE: CHECK" "$test_file" && [ $USE_TYPECHECK -eq 0 ]; then
        echo "SKIP" > "$result_file.status"
        return
    fi
    if grep -q "// REQUIRE: CUDA" "$test_file"; then
        if ! echo "${zc_args[@]}" | grep -q -- "--cuda"; then
            echo "SKIP" > "$result_file.status"
            return
        fi
    fi
    if grep -q "// REQUIRE: OBJC" "$test_file"; then
        if ! echo "${zc_args[@]}" | grep -q -- "--objc"; then
            echo "SKIP" > "$result_file.status"
            return
        fi
    fi
    if grep -q "// REQUIRE: NOT WINDOWS" "$test_file"; then
        case "$(uname -s 2>/dev/null)" in
            MINGW*|MSYS*|CYGWIN*)
                echo "SKIP" > "$result_file.status"
                return ;;
        esac
    fi

    local tmp_out="test_out_parallel_${job_id}.out"
    local cmd_str="$ZC run \"$test_file\" -o \"$tmp_out\" -w --emit-c ${zc_args[*]}"

    run_single() {
        $ZC run "$test_file" -o "$tmp_out" -w --emit-c "${zc_args[@]}" 2>&1 | sed 's/\x0//g'
    }

    local output
    local exit_code
    output=$(set -o pipefail; run_single); exit_code=$?

    # Retry flaky failures (e.g., execvp race in parallel mode)
    if [ $exit_code -eq 127 ] && [ "$JOBS" -gt 1 ]; then
        sleep 0.2
        output=$(set -o pipefail; run_single); exit_code=$?
    fi

    if grep -q "// EXPECT: FAIL" "$test_file"; then
        if [ $exit_code -ne 0 ]; then
            echo "PASS_EXPECTED_FAIL" > "$result_file.status"
        else
            echo "FAIL_UNEXPECTED_SUCCESS" > "$result_file.status"
            {
                echo "FAIL (Unexpected Success)"
                echo "----------------------------------------"
                echo "Exit Code: $exit_code"
                echo "Command: $cmd_str"
                echo "Output:"
                echo "$output"
                echo "----------------------------------------"
            } > "$result_file.details"
        fi
    else
        if [ $exit_code -eq 0 ]; then
            echo "PASS" > "$result_file.status"
            rm -f "$tmp_out" "${tmp_out}.c" "${tmp_out}.cpp" "${tmp_out}.m" "${tmp_out}.cu"
        else
            echo "FAIL" > "$result_file.status"
            {
                echo "FAIL"
                echo "----------------------------------------"
                echo "Exit Code: $exit_code"
                echo "Command: $cmd_str"
                echo "Output:"
                echo "$output"
                if [ -f "$tmp_out" ]; then
                    echo "Program output preserved at: $tmp_out"
                fi
                if [ $exit_code -eq 127 ]; then
                    local bin_path=""
                    if [ -f "${tmp_out}" ]; then
                        bin_path="${tmp_out}"
                    elif [ -f "${tmp_out}.exe" ]; then
                        bin_path="${tmp_out}.exe"
                    fi
                    if [ -n "$bin_path" ]; then
                        local bin_sz
                        local magic
                        bin_sz=$(wc -c < "$bin_path" 2>/dev/null || echo "?")
                        magic=$(od -A n -t x1 -N 4 "$bin_path" 2>/dev/null | tr -d ' \n' || echo "?")
                        echo "Binary: $bin_path ($bin_sz bytes, header: $magic)"
                    fi
                    echo "(Exit 127 from the binary may indicate a missing runtime DLL or a platform limitation.)"
                fi
                if [ $SHOW_SOURCE -eq 1 ]; then
                    if [ -f "$tmp_out.c" ]; then
                        echo "Generated C source preserved at: $tmp_out.c"
                        echo "--- Source Begin ---"
                        cat "$tmp_out.c"
                        echo "--- Source End ---"
                    elif [ -f "$tmp_out.cpp" ]; then
                        echo "Generated C++ source preserved at: $tmp_out.cpp"
                        echo "--- Source Begin ---"
                        cat "$tmp_out.cpp"
                        echo "--- Source End ---"
                    fi
                else
                    if [ -f "$tmp_out.c" ]; then
                        echo "Generated C source preserved at: $tmp_out.c"
                    elif [ -f "$tmp_out.cpp" ]; then
                        echo "Generated C++ source preserved at: $tmp_out.cpp"
                    fi
                    echo "(Source printing suppressed. Log noise reduced.)"
                fi
                echo "----------------------------------------"
            } > "$result_file.details"
        fi
    fi
}

job_count=0
for test_file in "${TEST_LIST[@]}"; do
    [ -e "$test_file" ] || continue
    
    echo "$test_file" > "$RESULTS_DIR/$job_count.name"
    if [ "$JOBS" -le 1 ]; then
        run_test "$test_file" "$job_count"
    else
        run_test "$test_file" "$job_count" &
        # Batch wait to maintain job concurrency without complex 'jobs' logic
        if (( (job_count + 1) % JOBS == 0 )); then
            wait
        fi
    fi
    ((job_count++))
done

# Wait for any remaining background jobs
wait

# Aggregate results
for ((i=0; i<job_count; i++)); do
    status=$(cat "$RESULTS_DIR/$i.status" 2>/dev/null)
    test_file=$(cat "$RESULTS_DIR/$i.name" 2>/dev/null)
    [ -z "$test_file" ] && continue
    
    case "$status" in
        PASS)
            echo "Testing $test_file... PASS"
            ((PASSED++))
            ;;
        PASS_EXPECTED_FAIL)
            echo "Testing $test_file... PASS (Expected Failure)"
            ((PASSED++))
            ;;
        FAIL*)
            echo "Testing $test_file... FAIL"
            cat "$RESULTS_DIR/$i.details"
            ((FAILED++))
            if [ "$status" == "FAIL_UNEXPECTED_SUCCESS" ]; then
                FAILED_TESTS="$FAILED_TESTS\n- $test_file (Unexpected Success)"
            else
                FAILED_TESTS="$FAILED_TESTS\n- $test_file"
            fi
            ;;
        SKIP)
            ((SKIPPED++))
            ;;
        *)
            echo "Testing $test_file... ERROR (Unknown status: $status)"
            ((FAILED++))
            ;;
    esac
done

echo "----------------------------------------"
echo "Results ($MODE mode):"
echo "-> Passed:  $PASSED"
echo "-> Failed:  $FAILED"
echo "-> Skipped: $SKIPPED"
echo "----------------------------------------"

if [ $FAILED -ne 0 ]; then
    echo -e "Failed tests:$FAILED_TESTS"
    # Keep artifacts for CI or manual debugging
    exit 1
else
    echo "All tests passed!"
    rm -f test_out_*.out test_out_*.out.c test_out_*.out.cpp test_out_*.out.m out.c out.cpp out.m rule_*
    exit 0
fi
