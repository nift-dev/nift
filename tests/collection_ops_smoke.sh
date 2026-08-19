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
python3 - "$OUT" <<'PYJSON'
import json,pathlib,sys
s=pathlib.Path(sys.argv[1]).read_text(); dec=json.JSONDecoder()
def val(label):
    p=s.index(label+"=")+len(label)+1
    return dec.raw_decode(s[p:].lstrip())[0]
assert val("SORT") == [1,2,2,3]
assert val("SORTDESC") == [3,2,2,1]
assert [x["title"] for x in val("FILTER")] == ["Old","Best"]
assert val("MAP") == ["Old","Draft","Best"]
assert val("MAPEXPR") == [10,198,20]
assert [x["title"] for x in val("SORTOBJ")] == ["Draft","Best","Old"]
assert val("SLICE") == [1,2]
assert val("DISTINCT") == ["beta","alpha"]
assert val("REVERSE") == [2,2,1,3]
assert val("FIND")["title"] == "Best"
assert val("FINDNONE") is None
assert val("SOME") is True and val("EVERY") is True and val("EMPTYEVERY") is True
assert val("NEST") == [3,2,2]
assert "JOIN=Old | Best" in s
assert s.index("FOR=Best") < s.index("FOR=Old")
PYJSON
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
