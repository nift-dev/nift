#!/usr/bin/env bash
# atomic_classify.sh <nift> <cycles> <label>
# Like classify.sh, but each edit is an editor-style atomic save (write a temp
# file, rename it over the source: a NEW inode), made right after the previous
# build returned. Counts misses and whether 3 more ordinary builds repair them.
set -u -o pipefail; NIFT="$1"; CYCLES="$2"; LABEL="$3"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cd "$T" && "$NIFT" init >/dev/null 2>&1 && "$NIFT" build >/dev/null 2>&1 || { echo setup failed; exit 2; }
miss=0; persistent=0; ex=""
for i in $(seq 1 "$CYCLES"); do
  printf 'MARK-%s\n' "$i" > content/.tmp-save && mv -f content/.tmp-save content/index.html
  "$NIFT" build >/dev/null 2>&1
  grep -q "MARK-$i" public/index.html && continue
  miss=$((miss+1)); fixed=0
  for k in 1 2 3; do "$NIFT" build >/dev/null 2>&1; grep -q "MARK-$i" public/index.html && { fixed=1; break; }; done
  if [ $fixed -eq 0 ]; then persistent=$((persistent+1))
    [ -z "$ex" ] && ex="cycle=$i content=$(stat -c %.9Y content/index.html) pageinfo=$(stat -c %.9Y .nift/public/index.info.json) status='$("$NIFT" status 2>&1 | grep -E 'up to date|need' | head -1)'"
    sleep 0.02; printf 'R-%s\n' "$i" > content/index.html; "$NIFT" build >/dev/null 2>&1; fi
done
echo "label=$LABEL atomic-save cycles=$CYCLES misses=$miss persistent=$persistent example: $ex"
[ "$persistent" -eq 0 ]
