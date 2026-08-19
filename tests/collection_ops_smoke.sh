#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-collection-ops.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
D="$TMP/site"; mkdir -p "$D/.nift" "$D/content" "$D/templates" "$D/public" "$D/data"
cat >"$D/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","incremental-mode":"modified"}}
JSON
cat >"$D/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"/","title":"Collections","template":"templates/template.html"}]}
JSON
printf 'BODY\n' >"$D/content/index.html"
cat >"$D/data/data.json" <<'JSON'
{"nums":[3,1,2,2],"words":["beta","alpha","beta"],"posts":[{"title":"Old","score":5,"published":true},{"title":"Draft","score":99,"published":false},{"title":"Best","score":10,"published":true}]}
JSON
cat >"$D/templates/template.html" <<'EOF2'
@json("data/data.json", d)
SORT=@sort(d.nums)
SORTDESC=@sort(n : d.nums, n desc)
FILTER=@filter(p : d.posts, p.published && p.score >= 5)
MAP=@map(p : d.posts, p.title)
MAPEXPR=@map(p : d.posts, p.score * 2)
SORTOBJ=@sort(p : d.posts, p.score desc)
SLICE=@slice(d.nums, 1, 2)
DISTINCT=@distinct(d.words)
REVERSE=@reverse(d.nums)
FIND=@find(p : d.posts, p.published && p.score > 6)
FINDNONE=@find(p : d.posts, p.score > 100)
SOME=@some(p : d.posts, p.published && p.score == 10)
EVERY=@every(p : d.posts, p.score > 0)
EMPTYEVERY=@every(n : @slice(d.nums, 0, 0), n > 0)
NEST=@sort(n : @filter(n : d.nums, n >= 2), n desc)
JOIN=@join(@map(p : @filter(p : d.posts, p.published), p.title), " | ")
@for(p : @sort(p : @filter(p : d.posts, p.published), p.score desc)){
FOR=$[p.title]
}
@content
EOF2
(cd "$D" && "$NIFT_BIN" build >/dev/null)
OUT="$D/public/index.html"
grep -F 'SORT=[1,2,2,3]' "$OUT"
grep -F 'SORTDESC=[3,2,2,1]' "$OUT"
grep -F 'FILTER=[{"title":"Old","score":5,"published":true},{"title":"Best","score":10,"published":true}]' "$OUT"
grep -F 'MAP=["Old","Draft","Best"]' "$OUT"
grep -F 'MAPEXPR=[10,198,20]' "$OUT"
grep -F 'SORTOBJ=[{"title":"Draft","score":99,"published":false},{"title":"Best","score":10,"published":true},{"title":"Old","score":5,"published":true}]' "$OUT"
grep -F 'SLICE=[1,2]' "$OUT"
grep -F 'DISTINCT=["beta","alpha"]' "$OUT"
grep -F 'REVERSE=[2,2,1,3]' "$OUT"
grep -F 'FIND={"title":"Best","score":10,"published":true}' "$OUT"
grep -F 'FINDNONE=null' "$OUT"
grep -F 'SOME=true' "$OUT"
grep -F 'EVERY=true' "$OUT"
grep -F 'EMPTYEVERY=true' "$OUT"
grep -F 'NEST=[3,2,2]' "$OUT"
grep -F 'JOIN=Old | Best' "$OUT"
python3 - "$OUT" <<'PY'
import pathlib,sys
s=pathlib.Path(sys.argv[1]).read_text()
assert s.index('FOR=Best') < s.index('FOR=Old')
PY

# Invalid contracts are controlled failures.
for CASE in badsort badslice badpredicate; do
  X="$TMP/$CASE"; cp -a "$D" "$X"; rm -rf "$X/public"; mkdir "$X/public"
  case "$CASE" in
    badsort) printf '@json("data/data.json", d)\n@sort(p : d.posts, p)\n@content\n' >"$X/templates/template.html" ;;
    badslice) printf '@json("data/data.json", d)\n@slice(d.nums, -1, 2)\n@content\n' >"$X/templates/template.html" ;;
    badpredicate) printf '@json("data/data.json", d)\n@filter(p : d.posts, p.missing)\n@content\n' >"$X/templates/template.html" ;;
  esac
  if (cd "$X" && "$NIFT_BIN" build >out 2>err); then echo "$CASE unexpectedly succeeded" >&2; exit 1; fi
  test -s "$X/err"
done

echo 'collection ops smoke: PASS'
