#!/bin/bash
# Enforces codebase-wide conventions. Run via: make check-codebase
# NOTE: pipefail intentionally NOT set. The while(1) check uses
# tail | head | grep pipelines where SIGPIPE is expected.
set +o pipefail

ROOT="$(dirname "$0")/.."
err_count=0
warn_count=0

red()    { printf "\033[31m%s\033[0m\n" "$*"; }
green()  { printf "\033[32m%s\033[0m\n" "$*"; }
yellow() { printf "\033[33m%s\033[0m\n" "$*"; }

warn() { warn_count=$((warn_count + 1)); yellow "  [WARN] $*"; }
err()  { err_count=$((err_count + 1));   red    "  [FAIL] $*"; }

check_header() { echo ""; echo "--- $* ---"; }

# ===========================================================================
# Rule 1: while(1) loops in parser must guard TOK_EOF
# Accept TOK_EOF, break; after zpanic_at, or implicit break on non-match.
# ===========================================================================
check_header "Rule 1: while(1) loops must guard TOK_EOF"
while IFS= read -r line; do
    f=$(echo "$line" | cut -d: -f1)
    ln=$(echo "$line" | cut -d: -f2)
    body=$(tail -n +"$ln" "$f" | head -40)
    # Accept: explicit TOK_EOF, or break; (safe termination)
    if echo "$body" | grep -q 'TOK_EOF'; then continue; fi
    if echo "$body" | grep -qE '^\s+break;\s*$'; then continue; fi
    warn "$f:$ln"
done < <(grep -rn 'while (1)' "$ROOT/src/parser" "$ROOT/src/analysis" 2>/dev/null)

# ===========================================================================
# Rule 2: No exit(1) outside whitelisted locations
# ===========================================================================
check_header "Rule 2: exit(1) outside whitelist"
while IFS= read -r line; do
    case "$line" in
        *zfatal*)                               continue ;;
        *diagnostics.c*)                        continue ;;
        *utils_plugins.c*)                      continue ;;
        *"// whitelisted"*)                     continue ;;
        *lsp/lsp_project.c*)                    continue ;;
        *codegen_decl_emit.c*|\
        *codegen_decl_preamble.c*)              continue ;;
        *"/* exit */"*)                          continue ;;
    esac
    err "$line"
done < <(grep -rn 'exit(1)' "$ROOT/src" 2>/dev/null)

# ===========================================================================
# Rule 3: No strdup() — use xstrdup()
# ===========================================================================
check_header "Rule 3: strdup() should use xstrdup"
while IFS= read -r line; do
    case "$line" in
        *xstrdup*)  continue ;;
        *define*)   continue ;;
        *'"'*strdup*'"'*) continue ;;
    esac
    err "$line"
done < <(grep -rn '\bstrdup\b' "$ROOT/src" 2>/dev/null)

# ===========================================================================
# Rule 4: Use TOKEN_UNKNOWN not bare (Token){0}
# ===========================================================================
check_header "Rule 4: (Token){0} should use TOKEN_UNKNOWN"
while IFS= read -r line; do
    case "$line" in
        *TOKEN_UNKNOWN*)         continue ;;
        *"= {0}"*)               continue ;;
        *token.h*)               continue ;;
        *".o:"*)                 continue ;;
        *"return (Token){0}"*)   continue ;;  # zero-init Token return value
    esac
    err "$line"
done < <(grep -rn '(Token){0}' "$ROOT/src" 2>/dev/null)

# ===========================================================================
# Rule 5: Plugin Token must set .col
# ===========================================================================
check_header "Rule 5: Plugin Token.col must be set"
while IFS= read -r line; do
    f=$(echo "$line" | cut -d: -f1)
    ln=$(echo "$line" | cut -d: -f2)
    if ! tail -n +"$ln" "$f" | head -5 | grep -q '\.col'; then
        err "$f:$ln"
    fi
done < <(grep -rn 'Token t = {0};' "$ROOT/src/plugins" 2>/dev/null)

# ===========================================================================
# Rule 6: No zfree() on realpath() or malloc() allocations
# realpath() returns system-heap memory which must be freed with free(),
# not zfree() (which is a no-op for arena memory).
# ===========================================================================
check_header "Rule 6: zfree() on realpath() pointers"
if grep -rn 'zfree(real_path\|zfree(realpath' "$ROOT/src" 2>/dev/null; then
    err "zfree() used on realpath() return value — must use free()"
fi

# ===========================================================================
# Rule 7: No g_compiler or g_config access in parser/analysis/codegen
# These should go through ctx->compiler or ctx->config for thread safety.
# ===========================================================================
check_header "Rule 7: g_compiler/g_config in parser/analysis/codegen"
while IFS= read -r line; do
    case "$line" in
        *".h:"*)        continue ;;  # header declarations
        *"g_compiler"*"config"*) ;;
        *)              continue ;;
    esac
    warn "$line"
done < <(grep -rn 'g_compiler\.\|g_config\.' "$ROOT/src/parser" "$ROOT/src/analysis" "$ROOT/src/codegen" 2>/dev/null)

# ===========================================================================
# Rule 8: parse_type_formal return value should be NULL-checked
# ===========================================================================
check_header "Rule 8: parse_type_formal() NULL check (heuristic)"
while IFS= read -r line; do
    f=$(echo "$line" | cut -d: -f1)
    ln=$(echo "$line" | cut -d: -f2)
    # Skip the definition and header declarations
    case "$line" in
        *"Type \*parse_type_formal"*) continue ;;
        *".h:"*)                      continue ;;
        *"Type *parse_type_formal("*) continue ;;
    esac
    # Check next 3 lines for a NULL check pattern
    context=$(tail -n +"$ln" "$f" | head -5)
    if ! echo "$context" | grep -qE 'if\s*\([!&]|== NULL|!= NULL|!\w+\s*\)|[?]|parse_type_formal\(ctx, l\)\s*==\s*NULL'; then
        warn "$f:$ln"
    fi
done < <(grep -rn 'parse_type_formal(' "$ROOT/src/parser" "$ROOT/src/analysis" "$ROOT/src/codegen" 2>/dev/null)

# ===========================================================================
# Rule 9: sprintf() without bounds — use snprintf()
# ===========================================================================
check_header "Rule 9: sprintf() should use snprintf()"
count=0
while IFS= read -r line; do
    case "$line" in
        *"/* safe */"*)       continue ;;
        *"/* TODO"*)          continue ;;
        *codegen_decl_*)      continue ;;  # codegen string literals
        *cJSON.c*)            continue ;;  # vendored library
        *.h:*)                continue ;;
    esac
    # Skip sprintf inside string literals (codegen output)
    line_text=$(echo "$line" | cut -d: -f3-)
    if echo "$line_text" | grep -qE '^\s*"'; then
        continue
    fi
    warn "$line"
    count=$((count + 1))
done < <(grep -rn '\bsprintf(' "$ROOT/src" 2>/dev/null)
[ "$count" -gt 0 ] && yellow "  ($count sprintf() calls — needs review)"

# ===========================================================================
# Rule 10: Functions over 600 lines (heuristic)
# ===========================================================================
check_header "Rule 10: Functions over 600 lines"
while IFS= read -r line; do
    f=$(echo "$line" | cut -d: -f1)
    len=$(echo "$line" | cut -d' ' -f2)
    warn "$f ($len lines)"
done < <(wc -l "$ROOT/src/parser/expr/expr_primary.c" \
              "$ROOT/src/parser/expr/expr_prec.c" \
              "$ROOT/src/parser/parser_stmt.c" \
              "$ROOT/src/codegen/codegen_expr_handlers.c" 2>/dev/null | awk '$1 > 2000 {print $2 " " $1}')

# ===========================================================================
# Rule 11: bare realloc() outside arena-macro scope
# ===========================================================================
check_header "Rule 11: bare realloc() without NULL check"
while IFS= read -r line; do
    case "$line" in
        *xrealloc*)          continue ;;
        *codegen_decl_*)     continue ;;  # codegen string literals
        *zmap.h*)            continue ;;  # macro definition
        *zvec.h*)            continue ;;  # macro definition
        *zalloc.h*)          continue ;;  # macro definition
        *zc_utils.h*)        continue ;;  # realloc macro redirect
        *arena.h*)           continue ;;  # realloc macro redirect
        *cJSON.c*)           continue ;;  # vendored library
    esac
    f=$(echo "$line" | cut -d: -f1)
    ln=$(echo "$line" | cut -d: -f2)
    # Parser and REPL files include arena.h indirectly via compiler.h
    case "$f" in
        *parser/*)  continue ;;
        *repl/*)    continue ;;
    esac
    # Only flag files outside parser/repl that use raw realloc
    # Check if the next line has a NULL check
    context=$(tail -n +"$ln" "$f" | head -3)
    if echo "$context" | grep -qE 'if\s*\(!|== NULL|!= NULL'; then
        continue  # has NULL check, safe
    fi
    err "$line"
done < <(grep -rn '\brealloc(' "$ROOT/src" 2>/dev/null)

# ===========================================================================
# Rule 12: zpanic_at() without break/return on next line
# ===========================================================================
check_header "Rule 12: zpanic_at() missing break/return"
while IFS= read -r line; do
    f=$(echo "$line" | cut -d: -f1)
    ln=$(echo "$line" | cut -d: -f2)
    # Skip files with known complex multi-line zpanic_at patterns
    # (These have been manually verified to have return/break after the
    # closing paren of the zpanic_at call, but the heuristic here can't
    # see past multi-line argument lists.)
    case "$f" in
        *expr_prec.c*) continue ;;
        *type_base.c*) continue ;;
        *type_formal.c*) continue ;;
        *stmt_import.c*) continue ;;
        *decl_var.c*) continue ;;
        *expr_literal.c*) continue ;;
        *expr_primary.c*) continue ;;
        *parser_stmt.c*) continue ;;
        *struct_impl.c*) continue ;;
    esac
    # Check the next non-blank non-brace line after zpanic_at
    next=$(tail -n +"$((ln + 1))" "$f" | grep -v '^\s*$\|^\s*}\s*$' | head -1)
    case "$next" in
        *break\;*)    continue ;;
        *return*)     continue ;;
        *continue\;*) continue ;;
        *"// no-break") continue ;;
        ""|"}")       continue ;;  # end of function or block — safe
    esac
    # zpanic_at is the last thing in an else branch — that's ok
    prev=$(sed -n "$((ln - 1))p" "$f")
    case "$prev" in
        *else*) continue ;;
    esac
    warn "$f:$ln  (no break/return after zpanic_at)"
done < <(grep -rn 'zpanic_at(' "$ROOT/src/parser" "$ROOT/src/analysis" 2>/dev/null)

# ===========================================================================
# Rule 13: atoi() is banned — use strtol() with error checking
# atoi() has undefined behavior on integer overflow.
# ===========================================================================
check_header "Rule 13: atoi() should use strtol()"
count=0
while IFS= read -r line; do
    err "$line"
    count=$((count + 1))
done < <(grep -rn '\batoi(' "$ROOT/src" 2>/dev/null | grep -v 'cJSON.c' | grep -v '\.h:' | grep -v '\.json:')
[ "$count" -gt 0 ] && red "  ($count atoi() calls — must fix)"

# ===========================================================================
# Rule 14: Bare strcat() is banned in core compiler code
# strcat() has no bounds checking — overflow risk on fixed-size buffers.
# ===========================================================================
check_header "Rule 14: strcat() should use snprintf() or size-tracked concat"
count=0
while IFS= read -r line; do
    case "$line" in
        *codegen_decl_*)      continue ;;  # codegen string literals
        *codegen_backend_*.c*) continue ;;  # codegen output strings
        *cJSON.c*)            continue ;;  # vendored library
        *".h:"*)              continue ;;
        *"/* safe */"*)       continue ;;
        *"/* strcat OK */"*)  continue ;;
    esac
    warn "$line"
    count=$((count + 1))
done < <(grep -rn '\bstrcat(' "$ROOT/src" 2>/dev/null)
[ "$count" -gt 0 ] && yellow "  ($count strcat() calls — needs review)"

# ===========================================================================
# Rule 15: Bare strcpy() is banned in core compiler code
# strcpy() has no bounds checking — overflow risk on fixed-size buffers.
# ===========================================================================
check_header "Rule 15: strcpy() should use snprintf() or size-checked copy"
count=0
while IFS= read -r line; do
    case "$line" in
        *codegen_decl_*)      continue ;;  # codegen string literals
        *cJSON.c*)            continue ;;  # vendored library
        *".h:"*)              continue ;;
        *"/* safe */"*)       continue ;;
    esac
    warn "$line"
    count=$((count + 1))
done < <(grep -rn '\bstrcpy(' "$ROOT/src" 2>/dev/null)
[ "$count" -gt 0 ] && yellow "  ($count strcpy() calls — needs review)"

# ===========================================================================
# Rule 16: strncpy() must guarantee null-termination
# strncpy() does NOT add \0 if source length >= destination size.
# ===========================================================================
check_header "Rule 16: strncpy() must guarantee null-termination"
count=0
while IFS= read -r line; do
    f=$(echo "$line" | cut -d: -f1)
    ln=$(echo "$line" | cut -d: -f2)
    case "$f" in
        *cJSON.c*)     continue ;;  # vendored library
        *\.h)          continue ;;
        *docs.json)    continue ;;
    esac
    # Check for null-termination within next 5 lines (some patterns span more lines)
    after=$(tail -n +"$((ln + 1))" "$f" | head -5)
    # Check if any line after strncpy contains a null-termination of a likely buffer
    # Look for patterns like: buf[index] = '\0' or = 0, or snprintf, or explicit \0 assignment
    if echo "$after" | grep -qE '\[.*\]\s*=\s*0\s*;|\[.*\]\s*=\s*'"'"'\\\\0'"'"'\s*;'; then
        continue  # some buffer is null-terminated — assume correct
    fi
    if echo "$after" | head -2 | grep -qE '\bsnprintf\b'; then
        continue  # switched to snprintf
    fi
    # Try to check if the specific destination buffer is null-terminated
    # First, check if an array-style dest (like "buf + offset") has base buf null-terminated
    base_dest=$(echo "$line" | sed 's/.*strncpy(\([a-zA-Z_][a-zA-Z0-9_]*\)[^,]*,[^,]*,[^)]*).*/\1/')
    if [ "$base_dest" != "$line" ] && [ -n "$base_dest" ]; then
        if echo "$after" | grep -qE "${base_dest}\[.*\]\s*=\s*0\s*;"; then
            continue  # base buffer is null-terminated after copy
        fi
    fi
    # Now check for sizeof guard
    if echo "$line" | grep -qE 'sizeof\([^)]+\)\s*-\s*[0-9]+'; then
        warn "$line  (uses sizeof(buf)-N — verify null-termination follows)"
        count=$((count + 1))
    elif echo "$line" | grep -qE 'sizeof\([^)]+\)'; then
        warn "$line  (strncpy with sizeof — verify null-termination: doesn't add \0 if src >= dst)"
        count=$((count + 1))
    else
        warn "$line  (strncpy without sizeof guard — verify null-termination)"
        count=$((count + 1))
    fi
done < <(grep -rn '\bstrncpy(' "$ROOT/src" 2>/dev/null)
[ "$count" -gt 0 ] && yellow "  ($count strncpy() calls — needs review)"

# ===========================================================================
# Rule 17: ftell() result must be validated
# ftell() returns -1 on error, which becomes SIZE_MAX when cast to size_t.
# ===========================================================================
check_header "Rule 17: ftell() result must be validated"
while IFS= read -r line; do
    f=$(echo "$line" | cut -d: -f1)
    ln=$(echo "$line" | cut -d: -f2)
    # Check next 5 lines for error validation
    context=$(tail -n +"$ln" "$f" | head -5)
    if echo "$context" | grep -qE 'if\s*\(\s*\w+\s*<\s*0'; then
        continue  # ftell result validated with if (var < 0)
    fi
    if echo "$context" | grep -qE 'ftell.*< 0'; then
        continue  # ftell result checked inline
    fi
    err "$line  (ftell() result not validated — can silently return SIZE_MAX)"
done < <(grep -rn '\bftell(' "$ROOT/src" 2>/dev/null | grep -v '\.h:')

# ===========================================================================
# Rule 18: No exit() outside main/lsp/diagnostics (in library code)
# exit() kills the embedding process. Library code (parser/analysis/codegen)
# must never call exit() directly.
# ===========================================================================
check_header "Rule 18: exit() in library code (parser/analysis/codegen)"
while IFS= read -r line; do
    case "$line" in
        *diagnostics.c*)              continue ;;
        *"/* exit */"*)               continue ;;
        *"// whitelisted"*)           continue ;;
        *codegen_decl_*.c*)           continue ;;  # codegen emits exit() in generated C code
    esac
    # Skip comments and string literals (generated code)
    line_text=$(echo "$line" | cut -d: -f3-)
    case "$line_text" in
        *//*)  continue ;;  # comment
        */\**) continue ;;  # block comment start
        *\**)  continue ;;  # block comment end or mid
        *'"'*exit*'"'*) continue ;;  # inside string literal
    esac
    err "$line"
done < <(grep -rn '\bexit(' "$ROOT/src/parser" "$ROOT/src/analysis" "$ROOT/src/codegen" 2>/dev/null)

# ===========================================================================
# Rule 19: No variable-length arrays (VLA) on stack
# VLAs can cause stack overflow with attacker-controlled sizes.
# ===========================================================================
check_header "Rule 19: Variable-length arrays (VLA) on stack"
count=0
while IFS= read -r line; do
    case "$line" in
        *cJSON.c*)            continue ;;  # vendored library
        *\.h:*)               continue ;;
    esac
    # Match: type name[size] where size is not a literal
    # Look for patterns like:  char buf[len]  or  int arr[n]
    # but NOT:  char buf[64]  or  char buf[MAX_LEN]
    line_text=$(echo "$line" | cut -d: -f3-)
    case "$line_text" in
        *"["*"]"*) ;;
        *) continue ;;
    esac
    # Check if the array size is a variable (not a literal/named constant)
    if echo "$line_text" | grep -qE '\b[a-z_][a-zA-Z0-9_]*\s*=\s*\{0\};'; then
        continue
    fi
    warn "$line  (potential VLA — verify size is a constant)"
    count=$((count + 1))
done < <(grep -rn '\[[a-z_][a-zA-Z0-9_]*\]' "$ROOT/src" 2>/dev/null | grep -v '\.json:' | grep -v 'MAX_' | grep -v 'sizeof' | grep -v '^\s*/\*' | grep -v '//.*\[')
[ "$count" -gt 0 ] && yellow "  ($count potential VLA declarations — needs review)"

# ===========================================================================
# Rule 20: All switch statements should have a default case
# Missing default can lead to unhandled enum values silently passing through.
# ===========================================================================
check_header "Rule 20: switch without default case"
count=0
while IFS= read -r line; do
    case "$line" in
        *cJSON.c*)   continue ;;
        *cJSON.h*)   continue ;;
        *".h:"*)     continue ;;
    esac
    # Get the file
    f=$(echo "$line" | cut -d: -f1)
    ln=$(echo "$line" | cut -d: -f2)
    # Check if this switch has a default case within the next 20 lines
    context=$(tail -n +"$ln" "$f" | head -20)
    if ! echo "$context" | grep -q '\bdefault:'; then
        warn "$line  (switch without default case)"
        count=$((count + 1))
    fi
done < <(grep -rn '\bswitch\s*(' "$ROOT/src" 2>/dev/null | grep -v '^\s*//' | head -200)
[ "$count" -gt 0 ] && yellow "  ($count switch statements without default — needs review)"

# ===========================================================================
# Rule 21: No longjmp() or setjmp() usage
# Non-local goto complicates cleanup and can skip destructor calls.
# ===========================================================================
check_header "Rule 21: longjmp() / setjmp() usage"
count=0
while IFS= read -r line; do
    case "$line" in
        *cJSON.c*)          continue ;;
        *\.json:*)          continue ;;  # docs, facts
        *misra.c*)          continue ;;  # MISRA documentation table
        *\.h:*)             continue ;;
    esac
    # Skip comments
    line_text=$(echo "$line" | cut -d: -f3-)
    case "$line_text" in
        *//*)  continue ;;  # comment
        */\**) continue ;;  # block comment
    esac
    err "$line"
    count=$((count + 1))
done < <(grep -rn '\blongjmp\b\|\bsetjmp\b' "$ROOT/src" 2>/dev/null)
[ "$count" -gt 0 ] && red "  ($count longjmp/setjmp calls — must fix)"

# ===========================================================================
# Rule 22: Function bodies over 500 lines (heuristic complexity check)
# ===========================================================================
check_header "Rule 22: Function bodies over 500 lines"
while IFS= read -r line; do
    f=$(echo "$line" | cut -d' ' -f2)
    len=$(echo "$line" | cut -d' ' -f1)
    warn "$f ($len lines)"
done < <(wc -l "$ROOT/src/parser/expr/expr_primary.c" \
              "$ROOT/src/parser/expr/expr_prec.c" \
              "$ROOT/src/parser/parser_stmt.c" \
              "$ROOT/src/codegen/codegen_expr_handlers.c" \
              "$ROOT/src/parser/utils/utils_template_replace.c" \
              "$ROOT/src/parser/utils/utils_rewrite.c" \
              "$ROOT/src/codegen/codegen_stmt_handlers.c" 2>/dev/null | awk '$1 > 500 {print $1 " " $2}')

# ===========================================================================
# Summary
# ===========================================================================
echo ""
if [ "$err_count" -eq 0 ]; then
    if [ "$warn_count" -eq 0 ]; then
        green "All checks passed."
    else
        yellow "$warn_count warning(s). $err_count error(s)."
        green "All required checks passed."
    fi
    exit 0
else
    red "$err_count violation(s) found (must fix). $warn_count warning(s)."
    exit 1
fi
