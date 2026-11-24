set -u
cd "$(dirname "$0")"
PGT="$(realpath "${1:-../build/pgt}")"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0
failed=()

ok() { pass=$((pass + 1)); }
bad() {
    fail=$((fail + 1))
    failed+=("$1")
    echo "FAIL $1"
    [ -n "${2:-}" ] && printf '%s\n' "$2" | head -20 | sed 's/^/     /'
}

check_output() {
    local name=$1 input=$2 expected=$3 stdin=$4 argsfile=$5
    local exe="$TMP/$(echo "$name" | tr '/' '_')"
    local log
    if ! log=$("$PGT" build "$input" -o "$exe" 2>&1); then
        bad "$name" "$log"
        return
    fi
    local args=()
    [ -f "$argsfile" ] && read -r -a args < "$argsfile"
    local out
    if [ -f "$input" ] && [ "$(head -n 1 "$input")" = "// memstats" ]; then
        export PGT_MEMSTATS=1
    else
        unset PGT_MEMSTATS
    fi
    if [ -f "$stdin" ]; then
        out=$("$exe" "${args[@]}" < "$stdin" 2>&1)
    else
        out=$("$exe" "${args[@]}" < /dev/null 2>&1)
    fi
    if [ "${UPDATE:-}" = 1 ]; then
        printf '%s\n' "$out" > "$expected"
        ok
        return
    fi
    if [ ! -f "$expected" ]; then
        bad "$name" "missing $expected (run with UPDATE=1 to create it)"
        return
    fi
    if diff -u "$expected" <(printf '%s\n' "$out") > "$TMP/diff"; then
        ok
    else
        bad "$name" "$(cat "$TMP/diff")"
    fi
}

for f in run/*.pgt; do
    base=${f%.pgt}
    check_output "$base" "$f" "$base.out" "$base.in" "$base.args"
done

for d in projects/*/; do
    d=${d%/}
    check_output "$d" "$d" "$d.out" "$d.in" "$d.args"
done

for f in debug/*.pgt; do
    base=${f%.pgt}
    out=$("$PGT" build - --debug < "$f" 2>&1)
    if [ "${UPDATE:-}" = 1 ]; then
        printf '%s\n' "$out" > "$base.out"
        ok
    elif diff -u "$base.out" <(printf '%s\n' "$out") > "$TMP/diff"; then
        ok
    else
        bad "$f" "$(cat "$TMP/diff")"
    fi
done

for f in errors/*.pgt; do
    first=$(head -n 1 "$f")
    exe="$TMP/err"
    if [[ $first == "// error: "* ]]; then
        want=${first#// error: }
        if log=$("$PGT" build "$f" -o "$exe" 2>&1); then
            bad "$f" "compiled, but expected the error: $want"
        elif [[ $log == *"$want"* ]]; then
            ok
        else
            bad "$f" "expected: $want"$'\n'"got:      $log"
        fi
    elif [[ $first == "// panic: "* ]]; then
        want=${first#// panic: }
        if ! log=$("$PGT" build "$f" -o "$exe" 2>&1); then
            bad "$f" "$log"
            continue
        fi
        err=$("$exe" < /dev/null 2>&1 >/dev/null)
        code=$?
        if [ $code -ne 0 ] && [[ $err == *"$want"* ]]; then
            ok
        else
            bad "$f" "expected a crash with: $want (exit $code)"$'\n'"got: $err"
        fi
    else
        bad "$f" "the first line must be '// error: ...' or '// panic: ...'"
    fi
done

echo "passed: $pass, failed: $fail"
[ $fail -eq 0 ]
