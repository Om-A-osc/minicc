#!/usr/bin/env bash
# MiniCC golden-output test runner.
#
#   tests/parse/*.dcf   parsed with --parse-only --dump-ast; stdout+stderr+exit code
#                       compared against the .expected file
#   tests/sema/*.dcf    checked with --check; diagnostics compared
#   tests/opt/*.dcf     --dump-ir -O output compared (golden IR)
#   tests/run/*.dcf     compiled with every backend at -O0 and -O, executed with
#                       the matching .in file on stdin (if any); stdout+stderr+exit
#                       code compared
#
# Environment:
#   MINICC     compiler binary (default build/minicc[.exe])
#   BACKENDS   backends to exercise for run tests (default: x86, plus llvm if clang is found)
#   UPDATE=1   rewrite the .expected files from the current output
set -u
cd "$(dirname "$0")/.."

MINICC=${MINICC:-build/minicc}
if [ ! -x "$MINICC" ] && [ -x "$MINICC.exe" ]; then MINICC="$MINICC.exe"; fi
if [ ! -x "$MINICC" ]; then echo "compiler not found: $MINICC (run make first)"; exit 2; fi

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) EXE=.exe ;;
    *) EXE= ;;
esac

if [ -z "${BACKENDS:-}" ]; then
    BACKENDS=x86
    if command -v "${MINICC_CLANG:-clang}" > /dev/null 2>&1; then BACKENDS="x86 llvm"; fi
fi

tmp=build/test-tmp
rm -rf "$tmp"
mkdir -p "$tmp"
pass=0
fail=0

check() {   # check <name> <expected-file> <actual-file>
    local name=$1 expected=$2 actual=$3
    if [ "${UPDATE:-0}" = 1 ]; then
        cp "$actual" "$expected"
        echo "UPDATED $name"
        return
    fi
    if [ ! -f "$expected" ]; then
        echo "FAIL    $name (missing $expected; run with UPDATE=1 to create it)"
        fail=$((fail + 1))
    elif diff -u --strip-trailing-cr "$expected" "$actual" > "$tmp/diff" 2>&1; then
        echo "PASS    $name"
        pass=$((pass + 1))
    else
        echo "FAIL    $name"
        sed 's/^/        /' "$tmp/diff"
        fail=$((fail + 1))
    fi
}

for f in tests/parse/*.dcf; do
    [ -e "$f" ] || continue
    "$MINICC" --parse-only --dump-ast "$f" > "$tmp/out" 2>&1
    echo "exit: $?" >> "$tmp/out"
    check "parse/$(basename "$f")" "${f%.dcf}.expected" "$tmp/out"
done

for f in tests/sema/*.dcf; do
    [ -e "$f" ] || continue
    "$MINICC" --check "$f" > "$tmp/out" 2>&1
    echo "exit: $?" >> "$tmp/out"
    check "sema/$(basename "$f")" "${f%.dcf}.expected" "$tmp/out"
done

for f in tests/opt/*.dcf; do
    [ -e "$f" ] || continue
    "$MINICC" --dump-ir -O "$f" > "$tmp/out" 2>&1
    echo "exit: $?" >> "$tmp/out"
    check "opt/$(basename "$f")" "${f%.dcf}.expected" "$tmp/out"
done

for f in tests/run/*.dcf; do
    [ -e "$f" ] || continue
    input=/dev/null
    [ -e "${f%.dcf}.in" ] && input="${f%.dcf}.in"
    for be in $BACKENDS; do
        for o in -O0 -O; do
            exe="$tmp/prog$EXE"
            rm -f "$exe"
            if ! "$MINICC" --backend="$be" "$o" "$f" -o "$exe" > "$tmp/out" 2>&1; then
                echo "exit: compile failed" >> "$tmp/out"
            else
                "$exe" < "$input" > "$tmp/out" 2>&1
                echo "exit: $?" >> "$tmp/out"
            fi
            check "run/$(basename "$f") [$be $o]" "${f%.dcf}.expected" "$tmp/out"
        done
    done
done

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
