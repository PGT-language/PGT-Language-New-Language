set -u
cd "$(dirname "$0")/../.."
PGT="$PWD/build/pgt"
timeout 100 "$PGT" build compiler -o compiler/compiler || exit 1
exec python3 -I - "$PGT" "$PWD/compiler/compiler" <<'PY'
import json, os, subprocess, sys

pgt, stage1 = sys.argv[1], sys.argv[2]
files = []
for root in ("tests", "std", "test", "compiler", "editors"):
    for d, _, names in os.walk(root):
        for n in names:
            if n.endswith(".pgt"):
                files.append(os.path.join(d, n))
files.sort()


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, timeout=20)
    return p.returncode, p.stdout.decode("latin-1"), p.stderr.decode("latin-1")


same = 0
errs = 0
bad = []
for f in files:
    code0, out0, _ = run([pgt, "build", f, "--debug=json"])
    try:
        entry = json.loads(out0)["files"][0]
    except Exception:
        bad.append((f, "stage0 produced no JSON"))
        continue
    code1, out1, err1 = run([stage1, "tokens", f])
    if entry["tokens"] == [] and "error" in entry:
        e = entry["error"]
        want = "%s:%d:%d: error: %s" % (f, e["line"], e["col"], e["message"])
        got = err1.rstrip("\n")
        if e["line"] <= 0:
            want = "%s: error: %s" % (f, e["message"])
        if got == want and code1 == 1:
            same += 1
            errs += 1
        else:
            bad.append((f, "error mismatch\n  want: %s\n  got:  %s" % (want, got)))
        continue
    try:
        got = json.loads(out1)
    except Exception:
        bad.append((f, "stage1 output is not JSON: " + out1[:200]))
        continue
    if got == entry["tokens"]:
        same += 1
    else:
        detail = "token mismatch"
        for i, (a, b) in enumerate(zip(entry["tokens"], got)):
            if a != b:
                detail += "\n  at %d: want %s got %s" % (i, a, b)
                break
        else:
            detail += " (count %d vs %d)" % (len(entry["tokens"]), len(got))
        bad.append((f, detail))

print("%d files, %d identical (%d lexer errors), %d mismatches" % (len(files), same, errs, len(bad)))
for f, d in bad:
    print("MISMATCH", f, d)
sys.exit(1 if bad else 0)
PY
