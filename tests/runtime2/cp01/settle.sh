#!/usr/bin/env bash
# settle.sh <base-dir> <label=nift>...: 20-page site; full build, then edit every
# content file (sequenced before the builds: ticks.py) and run 12 back-to-back
# ordinary builds (no pacing). Prints pages rebuilt per build and whether outputs
# carry the edit.
set -u -o pipefail; BASE="$1"; shift
HERE="$(cd "$(dirname "$0")" && pwd)"
for spec in "$@"; do L=${spec%%=*}; B=${spec#*=}
  T=$(mktemp -d -p "$BASE"); cd "$T"
  python3 -I -c "
import json,os
for d in ('.nift','content','templates','public'): os.makedirs(d,exist_ok=True)
json.dump({'config':{'content-dir':'content/','content-ext':'.html','output-dir':'public/','output-ext':'.html','default-template':'templates/template.html','build-threads':1,'incremental-mode':'modified','minify-exts':[]}},open('.nift/config.json','w'))
json.dump({'tracked':[{'name':f'p{i}','title':f'P{i}','template':'templates/template.html'} for i in range(20)]},open('.nift/tracked.json','w'))
open('templates/template.html','w').write('<html>@content</html>\n')
[open(f'content/p{i}.html','w').write(f'{i}\n') for i in range(20)]"
  cnt() { "$B" build 2>&1 | grep -oE '[0-9]+ files? (re)?built|^built ' | awk '/files?/{n=$1} /^built/{n++} END{print n+0}'; }
  init="$(cnt)"; r=""; for k in $(seq 12); do r="$r $(cnt)"; done
  for i in $(seq 0 19); do echo "E$i" > content/p$i.html; done
  # CP01 flakiness fix (2026-11-08): the edits are made before the builds, not
  # during the first one: ticks.py stamps them just before the filesystem clock
  # (T - 1 ns), so the first build's page-info is strictly newer whatever the
  # scheduling. Without it the last edits could share the first build's
  # timestamp tick and be rebuilt once more ([20 1 0 ...]), as v4.12 allows.
  # The edits must still be newer than the initial build's page-info.
  t=$(python3 -I "$HERE/ticks.py" content/p*.html) || { echo "$L harness: sequencing failed"; cd "$BASE"; rm -rf "$T"; continue; }
  newest=$(find .nift -name '*.info.json' -printf '%T@\n' | sort -n | tail -1)
  python3 -I -c "import sys; sys.exit(0 if int(sys.argv[1]) - 1 > float(sys.argv[2]) * 1e9 + 1e3 else 1)" "$t" "$newest" \
    || { echo "$L harness: edits not newer than the initial build's page-info"; cd "$BASE"; rm -rf "$T"; continue; }
  e=""; for k in $(seq 12); do e="$e $(cnt)"; done
  ok=$(grep -l E public/p*.html | wc -l)
  echo "$L fs=$(stat -f -c %T "$T") after-init:[$r ] after-edit-all:[$e ] outputs-with-edit=$ok/20"
  cd "$BASE"; rm -rf "$T"
done
