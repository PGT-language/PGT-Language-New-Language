set -u
cd "$(dirname "$0")"
ROOT="$(pwd)"
PGT="$(realpath "${1:-../build/pgt}")"
TMP="$(mktemp -d)"
PORT=18080
SERVER_PID=""
cleanup() {
    [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT

fail=0
check() {
    if [ "$2" = "$3" ]; then
        echo "ok   $1"
    else
        echo "FAIL $1: expected [$2] got [$3]"
        fail=1
    fi
}

if ! timeout 60 "$PGT" build ../examples/httpdemo -o "$TMP/demo" 2>&1; then
    echo "FAIL build httpdemo"
    exit 1
fi
if ! timeout 60 "$PGT" build net_http/client.pgt -o "$TMP/client" 2>&1; then
    echo "FAIL build client"
    exit 1
fi

cd ../examples/httpdemo
timeout 20 "$TMP/demo" $PORT > "$TMP/server.log" 2>&1 &
SERVER_PID=$!
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    curl -s -o /dev/null "http://127.0.0.1:$PORT/ping" && break
    sleep 0.25
done

U="http://127.0.0.1:$PORT"
check "GET / status" 200 "$(curl -s -o "$TMP/index" -w '%{http_code}' $U/)"
check "GET / content-type" "text/html; charset=utf-8" "$(curl -s -o /dev/null -w '%{content_type}' $U/)"
check "GET / body" "$(cat static/index.html)" "$(cat "$TMP/index")"
check "GET css content-type" "text/css; charset=utf-8" "$(curl -s -o /dev/null -w '%{content_type}' $U/static/style.css)"
check "GET css body" "$(cat static/style.css)" "$(curl -s $U/static/style.css)"
check "GET /api/hello" '{"hello": "PGT"}' "$(curl -s "$U/api/hello?name=PGT")"
check "GET /api/hello json type" "application/json; charset=utf-8" "$(curl -s -o /dev/null -w '%{content_type}' "$U/api/hello?name=PGT")"
check "GET /api/hello decode" '{"hello": "a b"}' "$(curl -s "$U/api/hello?name=a%20b")"
check "POST /api/echo" "hello body" "$(curl -s -d 'hello body' $U/api/echo)"
check "missing path" 404 "$(curl -s -o /dev/null -w '%{http_code}' $U/nothing)"
check "traversal" 403 "$(curl -s --path-as-is -o /dev/null -w '%{http_code}' $U/static/../main.pgt)"
check "encoded traversal" 403 "$(curl -s --path-as-is -o /dev/null -w '%{http_code}' "$U/static/%2e%2e/main.pgt")"
check "POST static route" 405 "$(curl -s -o /dev/null -w '%{http_code}' -d x $U/ping)"
check "PGT client" "$("$TMP/client" $PORT)" "$(cat "$ROOT/net_http/client.out")"

kill "$SERVER_PID" 2>/dev/null
SERVER_PID=""

if [ "$(id -u)" != 0 ]; then
    cat > "$TMP/priv.pgt" <<'PGT'
package main

#use (
    "http"
    "fmt"
)

fn main() {
    var* s = http.NewServer()
    fmt.Println(s.Listen("localhost", 80))
}
return 0
PGT
    timeout 60 "$PGT" build "$TMP/priv.pgt" -o "$TMP/priv" 2>&1
    check "port 80 non-root" "port 80 needs root: ports below 1024 are privileged, use 1024-65535" "$(timeout 10 "$TMP/priv")"
fi

[ $fail -eq 0 ]
