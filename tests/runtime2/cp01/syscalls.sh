#!/usr/bin/env bash
# syscalls.sh <pages> <nav-links> <label=binary>...
# strace -f syscall counts for full / no-op / one-page-edit / shared-template-edit
# builds of a generated modified-mode site (1 build thread). Same fixture as the
# prior overhead.sh (template + @input head + 1 content file per page); with
# nav-links > 0 the head also holds that many @pathto links (CP03 attribution).
# Output: one line per (binary, phase) with totals and per-syscall/per-path counts.
set -u
N="$1"; NAV="$2"; shift 2
HERE="$(mktemp -d)"
trap 'rm -rf "$HERE"' EXIT
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
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
for spec in "$@"; do
  label="${spec%%=*}"; B="${spec#*=}"
  P="$W/$label"; mk "$P"; cd "$P"
  for phase in full noop edit tpl noop2; do
    case $phase in
      full) args="build --all";;
      noop|noop2) args="build";;
      edit) sleep 0.05; echo "<p>edited</p>" > content/p7.html; args="build";;
      tpl) sleep 0.05; printf '<html><head>@input("templates/head.html")</head><body>X @content</body></html>\n' > templates/template.html; args="build";;
    esac
    strace -f -qq -o "$W/t.txt" "$B" $args >/dev/null 2>&1
    total=$(grep -vc "resumed>" "$W/t.txt")
    sc() { grep -c -E "^[0-9]+ +($1)\(" "$W/t.txt"; }
    pf() { grep -c -F "$1" "$W/t.txt"; }
    echo "$label n=$N nav=$NAV $phase total=$total openat=$(sc openat) stat=$(sc 'newfstatat|statx|fstat|lstat|stat') read=$(sc read) write=$(sc write) close=$(sc close) utimensat=$(sc utimensat) rename=$(sc 'rename|renameat|renameat2') mkdir=$(sc mkdir) unlink=$(sc 'unlink|unlinkat') readlink=$(sc readlink) getdents=$(sc getdents64) futex=$(sc futex) path.clock=$(pf '.nift/clock') path.lock=$(pf '.nift/.lock"') path.failed=$(pf 'failed.json') path.unfinished=$(pf '.unfinished') path.tmp=$(pf '.nift-tmp')"
  done
  echo "$label files-under-.nift=$(find .nift -type f | wc -l) clock-files=$(find .nift/clock -type f 2>/dev/null | wc -l) public=$(ls public | wc -l)"
  cd "$W"
done
