#!/usr/bin/env bash
# settle.sh <base-dir> <label=nift>...: 20-page site; full build, then edit every
# content file and run 12 back-to-back ordinary builds (no pacing). Prints pages
# rebuilt per build and whether outputs carry the edit.
set -u -o pipefail; BASE="$1"; shift
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
  e=""; for k in $(seq 12); do e="$e $(cnt)"; done
  ok=$(grep -l E public/p*.html | wc -l)
  echo "$L fs=$(stat -f -c %T "$T") after-init:[$r ] after-edit-all:[$e ] outputs-with-edit=$ok/20"
  cd "$BASE"; rm -rf "$T"
done
