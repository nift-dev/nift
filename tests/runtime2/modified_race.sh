#!/usr/bin/env bash
# CP01: modified-mode incremental builds must not miss edits made
#  (a) immediately after a build finishes, and
#  (b) to a dependency while the page that read it is still rendering.
set -u -o pipefail
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

# (c) files Nift itself writes are not concurrent edits: a pre-build hook that
# regenerates an input must not cause rebuilds forever (review finding F1).
mkdir -p gen
printf 'sleep(20)\nr := cmd("sh","-c","echo CONST > gen/data.html").cwd(getenv("NIFT_HOOK_ROOT")).run()\n' > content/index.pre-build.f
printf '@input("gen/data.html")\nhello\n' > content/index.html
"$NIFT" build >/dev/null 2>&1; "$NIFT" build >/dev/null 2>&1
n=0; for i in 1 2 3 4 5; do "$NIFT" build 2>&1 | grep "built " >/dev/null && n=$((n+1)); done
if [ "$n" -eq 0 ]; then echo "PASS pre-build hook output is not racy"; else echo "FAIL pre-build hook output: $n/5 follow-up builds rebuilt"; fail=1; fi
rm -f content/index.pre-build.f

# (d) an output written by a prerequisite earlier in the same build is not a
# concurrent edit of its dependant (review finding F2). One conservative
# rebuild is allowed when both land in the same timestamp tick.
"$NIFT" track a a templates/template.html >/dev/null 2>&1
python3 - <<'PY'
import json
d=json.load(open('.nift/tracked.json'))
for t in d['tracked']:
    if t['name']=='/': t['depends']=['a']
json.dump(d,open('.nift/tracked.json','w'))
PY
echo A0 > content/a.html; printf '@input("public/a.html")\n' > content/index.html
"$NIFT" build --all >/dev/null 2>&1
extra=0
for i in $(seq 1 10); do
  echo "A$i" > content/a.html; "$NIFT" build >/dev/null 2>&1
  "$NIFT" build 2>&1 | grep "racy\|while the page was being built" >/dev/null && extra=$((extra+1))
  grep -q "A$i" public/index.html || { echo "FAIL depends: stale dependant at step $i"; fail=1; }
done
if [ "$extra" -eq 0 ]; then echo "PASS prerequisite outputs are not racy"; else echo "FAIL prerequisite outputs flagged racy in $extra/10 builds"; fail=1; fi
exit $fail
