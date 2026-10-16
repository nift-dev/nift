#!/usr/bin/env bash
# CP01: modified-mode incremental builds must not miss edits made
#  (a) immediately after a build finishes, and
#  (b) to a dependency while the page that read it is still rendering.
set -u
NIFT="${NIFT_BIN:-$(pwd)/nift}"
CYCLES="${NIFT_RACE_CYCLES:-1000}"
fail=0
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cd "$T" && "$NIFT" init >/dev/null 2>&1 && "$NIFT" build >/dev/null 2>&1 || { echo "setup failed"; exit 2; }

# (a) immediate edits
miss=0
for i in $(seq 1 "$CYCLES"); do
  printf 'MARK-%s\n' "$i" > content/index.html
  "$NIFT" build >/dev/null 2>&1
  grep -q "MARK-$i" public/index.html || miss=$((miss+1))
done
if [ "$miss" -ne 0 ]; then echo "FAIL immediate edits: missed $miss/$CYCLES"; fail=1; else echo "PASS immediate edits: 0/$CYCLES missed"; fi

# (b) a dependency edited while the page is rendering
printf 'PART-OLD\n' > templates/part.html
printf '@input("templates/part.html")\n@script { sleep(400) }\n' > content/index.html
"$NIFT" build >/dev/null 2>&1
( sleep 0.15; printf 'PART-NEW\n' > templates/part.html ) &
printf '@input("templates/part.html")\n@script { sleep(400) }\nX\n' > content/index.html
"$NIFT" build >/dev/null 2>&1
wait
"$NIFT" build >/dev/null 2>&1
if grep -q PART-NEW public/index.html; then echo "PASS edit during render"; else echo "FAIL edit during render: stale output after a follow-up build"; fail=1; fi
exit $fail
