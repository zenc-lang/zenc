#!/bin/bash

ZC="${ZC:-./zc}"
EXAMPLES_DIR="${EXAMPLES_DIR:-examples}"
FAIL_COUNT=0
PASS_COUNT=0
SKIP_COUNT=0
EXT_SKIP_COUNT=0
# Set RUN_SMOKE=1 to also execute each built binary under a short timeout.
# A crash (signal) fails the run; a timeout or a normal non-zero exit is
# reported but does not fail (examples may be interactive / non-terminating /
# return non-zero by design).
RUN_SMOKE="${RUN_SMOKE:-0}"

if [ ! -f "$ZC" ]; then
    echo "Error: zc binary not found."
    exit 1
fi

# Examples that require external toolchains/libraries which are not portably
# available in the CI image or on typical dev machines. They are skipped rather
# than failed: the dependencies cannot be installed portably (CUDA/Objective-C
# are platform specific), and GUI examples would crash the run-smoke without a
# display server. Paths are relative to EXAMPLES_DIR.
ext_dep_reason() {
    local rel="$1"
    case "$rel" in
        "internal/gpu/cuda-benchmark.zc" | "internal/gpu/cuda_info.zc" | "internal/gpu/cuda_vector_add.zc")
            echo "requires CUDA toolkit (nvcc + GPU runtime)" ;;
        "internal/objc_interop.zc")
            echo "requires Apple Objective-C / Foundation (macOS only)" ;;
        "internal/games/raylib_emscripten.zc")
            echo "requires raylib + Emscripten" ;;
        "internal/games/zen_craft/main.zc" | "internal/graphics/raylib_demo.zc")
            echo "requires raylib" ;;
        "internal/cpp_interop.zc")
            echo "requires a C++ toolchain" ;;
        "internal/scripting/lua/lua.zc")
            echo "requires liblua headers and -llua" ;;
        "rosetta/Currency.zc" | "rosetta/Jacobsthal_numbers.zc")
            echo "requires GNU MP (gmp.h + -lgmp)" ;;
        "rosetta/Simple_windowed_application.zc")
            echo "requires GTK3" ;;
        *)
            return 1 ;;
    esac
}

# Allow specifying what tests to run as inputs to the script
# Example: run_example_build.sh examples/simd.zc examples/area_test.zc
TEST_FILES=("$@")

if [ ${#TEST_FILES[@]} -gt 0 ]; then
    TEST_LIST=$(printf "%s\n" "${TEST_FILES[@]}" | grep "$EXAMPLES_DIR"/)
else
    if [ ! -d "$EXAMPLES_DIR" ]; then
        echo "** Example directory '$EXAMPLES_DIR' not found; skipping."
        exit 0
    fi
    TEST_LIST=$(find "$EXAMPLES_DIR" -name "*.zc" | sort)
fi

if [ -z "$TEST_LIST" ]; then
    echo "** Nothing to do **"
    exit 0
fi

echo "Running Example Build Tests..."

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

while IFS= read -r file; do
    [ -e "$file" ] || continue

    # Module files (e.g. rat.zc) have no `fn main` and cannot be built
    # standalone; they are validated transitively when an importing program
    # is built. Skip them here.
    if ! grep -qE '^[[:space:]]*fn[[:space:]]+main[[:space:]]*\(' "$file"; then
        echo "Skipping $file (module, no main)"
        SKIP_COUNT=$((SKIP_COUNT + 1))
        continue
    fi

    # Examples requiring external toolchains/libraries (see ext_dep_reason).
    if reason=$(ext_dep_reason "${file#"$EXAMPLES_DIR"/}"); then
        echo "Skipping $file (external dependency: $reason)"
        EXT_SKIP_COUNT=$((EXT_SKIP_COUNT + 1))
        continue
    fi

    echo -n "Building $file... "

    if ! "$ZC" build "$file" -o "$WORKDIR/out" -w >/dev/null 2>&1; then
        echo "FAIL"
        "$ZC" build "$file" -o "$WORKDIR/out" -w 2>&1 | grep -E "error:" | head -3
        FAIL_COUNT=$((FAIL_COUNT + 1))
        continue
    fi
    echo "PASS"
    PASS_COUNT=$((PASS_COUNT + 1))

    if [ "$RUN_SMOKE" = "1" ]; then
        # Skip examples that read input: they cannot be meaningfully exercised
        # non-interactively (stdin is /dev/null), and unhandled EOF can crash.
        if grep -qE "\b(read|readln|readLine|getline|read_line|scanf|getchar|fgets|fgetc|cin|input|stdin|keyboard|console|args)\b" "$file"; then
            SKIP_COUNT=$((SKIP_COUNT + 1))
            continue
        fi
        # < /dev/null so the binary cannot consume the script's stdin (which
        # feeds the file list to the while loop) and truncate the run.
        timeout 3 "$WORKDIR/out" < /dev/null >/dev/null 2>&1
        rc=$?
        if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ]; then
            :
        elif [ "$rc" -gt 128 ]; then
            echo "  !! RUN CRASHED (signal $((rc - 128)))"
            FAIL_COUNT=$((FAIL_COUNT + 1))
        else
            echo "  (run exited $rc; not a crash, ignored)"
        fi
    fi
done <<< "$TEST_LIST"

echo "----------------------------------------"
echo "Summary:"
echo "-> Passed: $PASS_COUNT"
echo "-> Failed: $FAIL_COUNT"
if [ "$RUN_SMOKE" = "1" ]; then
    echo "-> Run-smoke skipped (interactive): $SKIP_COUNT"
fi
echo "-> Skipped (external dependencies): $EXT_SKIP_COUNT"
echo "----------------------------------------"

if [ $FAIL_COUNT -ne 0 ]; then
    exit 1
fi

exit 0
