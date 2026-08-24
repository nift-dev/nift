#!/usr/bin/env bash
# Pagination ordering invariant: within a page's build epoch the generated
# output(s) are written BEFORE the stale pagination cleanup, and .info.json is
# written LAST. This ordering is load-bearing for the recovery model: a torn
# .info.json therefore implies the page's stale pagination cleanup already ran.
#
# The trace forces the .info.json to actually change (pagination shrinks), so
# the atomic replacement order is observable: output replacements, then the
# stale page removal, then the .info.json replacement LAST.
#
# Uses strace on Linux; skips when strace is unavailable (the lifecycle
# behaviour remains covered by pagination_smoke.sh and
# pagination_incremental_equivalence.py).
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
if ! command -v strace >/dev/null 2>&1; then
    echo "Pagination ordering smoke test SKIPPED (strace unavailable)"
    exit 0
fi
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-pagination-order.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
cd "$TMP"
mkdir -p .nift content templates public
cat > .nift/config.json <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":1,"incremental-mode":"modified"}}
JSON
cat > .nift/tracked.json <<'JSON'
{"tracked":[{"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":1}}]}
JSON
echo '@content' > templates/template.html
cat > content/blog.html <<'EOF'
@item{one}@item{two}@item{three}@paginate
EOF
cat > content/blog.paginate.html <<'EOF'
<section>$[paginate.items]</section>
EOF
"$NIFT_BIN" build --all >/dev/null 2>&1
test -f public/blog.html -a -f public/blog-2.html -a -f public/blog-3.html

# Shrink pagination: 3 pages -> 1 page (blog-3 becomes stale and is removed).
cat > .nift/tracked.json <<'JSON'
{"tracked":[{"name":"blog","title":"Blog","template":"templates/template.html","paginate":{"items-per-page":3}}]}
JSON
strace -f -e trace=rename,unlink,unlinkat -o trace.txt "$NIFT_BIN" build --all >/dev/null 2>&1

# Completed replacements/removals involving the blog page, in syscall order.
grep -E 'rename\(|unlink(\(|at\()' trace.txt | grep 'blog' > ops.txt
output_seq=$(grep -nE 'blog\.html"' ops.txt | grep -v 'blog-[0-9]' | tail -1 | cut -d: -f1)
stale_seq=$(grep -nE 'blog-3\.html"' ops.txt | grep -vE 'rename' | tail -1 | cut -d: -f1)
info_seq=$(grep -nE 'blog\.info\.json"' ops.txt | tail -1 | cut -d: -f1)

for v in output_seq stale_seq info_seq; do
    [ -n "${!v}" ] || { echo "FAIL: pagination ordering trace missing $v" >&2; exit 1; }
done
# outputs written -> stale cleanup -> .info.json LAST
if [ "$output_seq" -ge "$info_seq" ]; then
    echo "FAIL: .info.json not written last (output $output_seq >= info $info_seq)" >&2
    exit 1
fi
if [ -n "$stale_seq" ] && [ "$stale_seq" -ge "$info_seq" ]; then
    echo "FAIL: stale pagination cleanup not before .info.json (stale $stale_seq >= info $info_seq)" >&2
    exit 1
fi
test ! -f public/blog-3.html || { echo "FAIL: stale blog-3.html remains after build" >&2; exit 1; }
echo "Pagination ordering smoke test passed (output $output_seq < cleanup $stale_seq < info $info_seq)"
