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
{"nums":[3,1,2,2],"words":["beta","alpha","beta"],"triples":[[1,2,3],[4,5,6]],"products":[{"price":10,"quantity":2},{"price":5,"quantity":3}],"posts":[{"title":"Old","score":5,"published":true},{"title":"Draft","score":99,"published":false},{"title":"Best","score":10,"published":true}]}
JSON
cat >"$D/templates/template.html" <<'EOF2'
@json("data/data.json", d)
SORT=@sort(d.nums)
SORTDESC=@sort(n : d.nums => n desc)
FILTER=@filter(p : d.posts => p.published && p.score >= 5)
MAP=@map(p : d.posts => p.title)
MAPEXPR=@map(p : d.posts => p.score * 2)
SORTOBJ=@sort(p : d.posts => p.score desc)
SLICE=@slice(d.nums, 1, 2)
DISTINCT=@distinct(d.words)
REVERSE=@reverse(d.nums)
SUM=@sum(d.nums)
PROD=@prod(d.nums)
MIN=@min(d.nums)
MAX=@max(d.nums)
MINWORD=@min(d.words)
MAXWORD=@max(d.words)
SUMEXPR=@sum(p : d.products => p.price * p.quantity)
MAXEXPR=@max(p : d.posts => p.score)
TUPLESUM=@sum((a,b,c) : d.triples => a + b + c)
TUPLEPROD=@prod((a,b,c) : d.triples => a + b + c)
REDUCE=@reduce(n : d.nums & acc = 0 => acc + n)
REDUCEEXPR=@reduce(p : d.products & total = 5 => total + p.price * p.quantity)
REDUCEEMPTY=@reduce(n : @slice(d.nums, 0, 0) & acc = 42 => acc + n)
REDUCETUPLE=@reduce((a,b,c) : d.triples & acc = 0 => acc + a + b + c)
FIND=@find(p : d.posts => p.published && p.score > 6)
FINDNONE=@find(p : d.posts => p.score > 100)
SOME=@some(p : d.posts => p.published && p.score == 10)
EVERY=@every(p : d.posts => p.score > 0)
EMPTYEVERY=@every(n : @slice(d.nums, 0, 0) => n > 0)
NEST=@sort(n : @filter(n : d.nums => n >= 2) => n desc)
JOIN=@join(@map(p : @filter(p : d.posts => p.published) => p.title), " | ")
@for(p : @sort(p : @filter(p : d.posts => p.published) => p.score desc)){
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
assert val("SUM") == 8
assert val("PROD") == 12
assert val("MIN") == 1 and val("MAX") == 3
assert "MINWORD=alpha" in s and "MAXWORD=beta" in s
assert val("SUMEXPR") == 35
assert val("MAXEXPR") == 99
assert val("TUPLESUM") == 21
assert val("TUPLEPROD") == 90
assert val("REDUCE") == 8
assert val("REDUCEEXPR") == 40
assert val("REDUCEEMPTY") == 42
assert val("REDUCETUPLE") == 21
assert val("FIND")["title"] == "Best"
assert val("FINDNONE") is None
assert val("SOME") is True and val("EVERY") is True and val("EMPTYEVERY") is True
assert val("NEST") == [3,2,2]
assert "JOIN=Old | Best" in s
assert s.index("FOR=Best") < s.index("FOR=Old")
PYJSON
# Invalid contracts are controlled failures.
for CASE in badsort badslice badpredicate legacycomma badreduce badsum emptymin badtuple badacc; do
  X="$TMP/$CASE"; cp -a "$D" "$X"; rm -rf "$X/public"; mkdir "$X/public"
  case "$CASE" in
    badsort) printf '@json("data/data.json", d)\n@sort(p : d.posts => p)\n@content\n' >"$X/templates/template.html" ;;
    badslice) printf '@json("data/data.json", d)\n@slice(d.nums, -1, 2)\n@content\n' >"$X/templates/template.html" ;;
    badpredicate) printf '@json("data/data.json", d)\n@filter(p : d.posts => p.missing)\n@content\n' >"$X/templates/template.html" ;;
    legacycomma) printf '@json("data/data.json", d)\n@map(p : d.posts, p.title)\n@content\n' >"$X/templates/template.html" ;;
    badreduce) printf '@json("data/data.json", d)\n@reduce(n : d.nums & acc = 0 => acc + missing)\n@content\n' >"$X/templates/template.html" ;;
    badsum) printf '@json("data/data.json", d)\n@sum(d.words)\n@content\n' >"$X/templates/template.html" ;;
    emptymin) printf '@json("data/data.json", d)\n@min(@slice(d.nums, 0, 0))\n@content\n' >"$X/templates/template.html" ;;
    badtuple) printf '@json("data/data.json", d)\n@sum((a,b) : d.triples => a + b)\n@content\n' >"$X/templates/template.html" ;;
    badacc) printf '@json("data/data.json", d)\n@reduce(n : d.nums & n = 0 => n + 1)\n@content\n' >"$X/templates/template.html" ;;

  esac
  if (cd "$X" && "$NIFT_BIN" build >out 2>err); then echo "$CASE unexpectedly succeeded" >&2; exit 1; fi
  test -s "$X/err"
done

echo 'collection ops smoke: PASS'
