set -u
cd "$(dirname "$0")"
PGT="$(realpath "${PGT:-../build/pgt}")"
OPT="${OPT:--O2}"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
for f in ${@:-*.pgt}; do
    n=${f%.pgt}
    "$PGT" build "$f" $OPT -o "$T/$n" || { echo "$n BUILD FAIL"; continue; }
    best=999999
    for k in 1 2 3; do
        s=$(date +%s%N)
        out=$("$T/$n" </dev/null 2>&1)
        e=$(date +%s%N)
        ms=$(( (e - s) / 1000000 ))
        [ $ms -lt $best ] && best=$ms
    done
    printf '%-10s %6d ms  %s\n' "$n" "$best" "$(echo "$out" | head -c 60)"
done
