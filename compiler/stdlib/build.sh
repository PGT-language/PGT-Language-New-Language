set -eu
cd "$(dirname "$0")/../.."
PGT="${PGT:-$PWD/build/pgt}"
OUT="${1:-$PWD/build}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
timeout 100 "$PGT" build compiler/stdlib/bundle.pgt --emit-ir -o "$TMP/std.ll"
sed -i \
    -e 's/^define i32 @main(/define i32 @pgt_std_boot(/' \
    -e 's/^define internal void @pgt\.runtime\.panic/define void @pgt.runtime.panic/' \
    "$TMP/std.ll"
timeout 100 clang -O0 -w -c -x ir "$TMP/std.ll" -o "$TMP/pgtstd.o"
rm -f "$OUT/libpgtstd.a"
ar rcs "$OUT/libpgtstd.a" "$TMP/pgtstd.o"
echo "$OUT/libpgtstd.a"
