#!/usr/bin/env bash
# syscalls.sh <pages> <nav-links> <label=binary>...
# Deterministic strace counts for full / no-op / one-page-edit / template-edit
# builds. A missing tracer is a skip (77); a failed tracer, failed workload,
# empty or malformed trace is a hard failure, never a zero-count PASS.
set -u -o pipefail
N="$1"; NAV="$2"; shift 2
HERE="$(mktemp -d)"; trap 'rm -rf "$HERE"' EXIT
TRACE="${STRACE:-strace}"
if ! command -v "$TRACE" >/dev/null 2>&1; then
  echo "SKIP syscalls: tracer '$TRACE' is unavailable"
  exit 77
fi
mk() {
python3 -I - "$1" "$N" "$NAV" <<'PY'
import json, os, sys
root, n, nav = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
for d in (".nift", "content", "templates", "public"): os.makedirs(os.path.join(root, d), exist_ok=True)
json.dump({"config": {"content-dir": "content/", "content-ext": ".html", "output-dir": "public/", "output-ext": ".html",
  "default-template": "templates/template.html", "build-threads": 1, "incremental-mode": "modified", "minify-exts": []}},
  open(f"{root}/.nift/config.json", "w"))
json.dump({"tracked": [{"name": f"p{i}", "title": f"Page {i}", "template": "templates/template.html"} for i in range(n)]},
  open(f"{root}/.nift/tracked.json", "w"))
links = "".join(f'<a href="@pathto(p{j})">{j}</a>' for j in range(min(nav, n)))
open(f"{root}/templates/head.html", "w").write("<title>$[title]</title>" + links + "\n")
open(f"{root}/templates/template.html", "w").write('<html><head>@input("templates/head.html")</head><body>@content</body></html>\n')
for i in range(n): open(f"{root}/content/p{i}.html", "w").write(f"<p>page {i}</p>\n")
PY
}
fail() { echo "FAIL syscalls ($label/$phase): $*" >&2; exit 1; }
for spec in "$@"; do
  label="${spec%%=*}"; B="${spec#*=}"
  P="$HERE/$label"; mk "$P"; cd "$P"
  for phase in full noop edit tpl noop2; do
    case $phase in
      full) args="build --all";;
      noop|noop2) args="build";;
      edit) sleep 0.05; echo "<p>edited</p>" > content/p0.html; args="build";;
      tpl) sleep 0.05; printf '<html><head>@input("templates/head.html")</head><body>X @content</body></html>\n' > templates/template.html; args="build";;
    esac
    : > "$HERE/t.txt"; : > "$HERE/out.txt"; : > "$HERE/err.txt"
    # shellcheck disable=SC2086
    "$TRACE" -f -qq -o "$HERE/t.txt" "$B" $args >"$HERE/out.txt" 2>"$HERE/err.txt"; trc=$?
    [ "$trc" -eq 0 ] || fail "tracer/workload exit $trc; stderr: $(tail -2 "$HERE/err.txt" | tr '\n' ' ')"
    if grep -Eqi 'ptrace|operation not permitted|strace:' "$HERE/err.txt"; then
      fail "tracer failed: $(tail -2 "$HERE/err.txt" | tr '\n' ' ')"
    fi
    [ -s "$HERE/t.txt" ] || fail "empty trace"
    grep -Eq '^[0-9]+ +[A-Za-z_]+\(' "$HERE/t.txt" || fail "malformed trace (no syscall lines)"
    case $phase in
      full) [ "$(find public -type f | wc -l)" -eq "$N" ] || fail "full build did not produce $N pages";;
      noop|noop2) grep -q 'up to date' "$HERE/out.txt" || fail "no-op build did not report up to date";;
      edit) grep -q '<p>edited</p>' public/p0.html || fail "edited page was not rebuilt";;
      tpl) grep -q 'X ' public/p0.html || fail "template edit was not rebuilt";;
    esac
    total=$(grep -vc "resumed>" "$HERE/t.txt")
    sc() { grep -c -E "^[0-9]+ +($1)\(" "$HERE/t.txt" || true; }
    pf() { grep -c -F "$1" "$HERE/t.txt" || true; }
    echo "$label n=$N nav=$NAV $phase total=$total openat=$(sc openat) stat=$(sc 'newfstatat|statx|fstat|lstat|stat') read=$(sc read) write=$(sc write) close=$(sc close) utimensat=$(sc utimensat) rename=$(sc 'rename|renameat|renameat2') mkdir=$(sc mkdir) unlink=$(sc 'unlink|unlinkat') readlink=$(sc readlink) getdents=$(sc getdents64) futex=$(sc futex) path.clock=$(pf '.nift/clock') path.lock=$(pf '.nift/.lock"') path.failed=$(pf 'failed.json') path.unfinished=$(pf '.unfinished') path.tmp=$(pf '.nift-tmp')"
  done
  echo "$label files-under-.nift=$(find .nift -type f | wc -l) clock-files=$(find .nift/clock -type f 2>/dev/null | wc -l) public=$(ls public | wc -l)"
  cd "$HERE"
done
