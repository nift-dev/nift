#!/usr/bin/env bash
# Unreadable-source contract: a tracked content file, an @input file, or a
# template that becomes unreadable must fail the build with a clear
# "not readable" diagnostic and must leave the previously successful output and
# page metadata byte-identical (the read is the authority; a failed read must
# never silently render as empty content). An empty but readable source is
# still a valid, distinct state and must build successfully.
set -u
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-unreadable.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

fail() { echo "unreadable-source FAIL: $*" >&2; exit 1; }

scaffold() {
  local dir="$1"
  mkdir -p "$dir/.nift" "$dir/content" "$dir/templates" "$dir/public"
  cat >"$dir/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":1,"incremental-mode":"modified"}}
JSON
  cat >"$dir/.nift/tracked.json" <<'JSON'
{"tracked":[
  {"name":"/","title":"Home","template":"templates/template.html"}
]}
JSON
  printf '<main>@content</main>\n' >"$dir/templates/template.html"
  printf '<p>BASE</p>\n' >"$dir/content/index.html"
  (cd "$dir" && "$NIFT_BIN" build --all >/dev/null 2>&1) || fail "base build failed"
}

preserve_pair() {
  local dir="$1"
  rm -f "$TMP/out.before" "$TMP/meta.before"
  cp "$dir/public/index.html" "$TMP/out.before"
  cp "$dir/.nift/public/index.info.json" "$TMP/meta.before"
}

assert_preserved() {
  local dir="$1" label="$2"
  cmp -s "$dir/public/index.html" "$TMP/out.before" || fail "$label changed previously successful output"
  cmp -s "$dir/.nift/public/index.info.json" "$TMP/meta.before" || fail "$label changed page metadata"
}

expect_not_readable() {
  local dir="$1" label="$2"
  local out rc
  out="$(cd "$dir" && "$NIFT_BIN" build --all 2>&1)"
  rc=$?
  [ "$rc" -ne 0 ] || fail "$label: build unexpectedly succeeded"
  printf '%s' "$out" | grep -q "not readable" || fail "$label: missing 'not readable' diagnostic (output: $out)"
  assert_preserved "$dir" "$label"
  chmod 644 "$dir/$3" 2>/dev/null || true
}

# 1. Unreadable tracked content must fail and preserve last good output.
P="$TMP/content"
scaffold "$P"
printf '<p>NEW</p>\n' >"$P/content/index.html"
preserve_pair "$P"
chmod 000 "$P/content/index.html"
expect_not_readable "$P" "unreadable content" "content/index.html"

# 2. Unreadable @input source must fail and preserve last good output.
P="$TMP/input"
scaffold "$P"
printf '<head>@input("templates/head.html")</head><main>@content</main>\n' >"$P/templates/template.html"
printf '<meta charset="utf-8">\n' >"$P/templates/head.html"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null 2>&1) || fail "input base build failed"
printf '<link>new</link>\n' >"$P/templates/head.html"
preserve_pair "$P"
chmod 000 "$P/templates/head.html"
expect_not_readable "$P" "unreadable @input" "templates/head.html"

# 3. Unreadable template must fail with the template-readable diagnostic and
#    preserve last good output (not the misleading downstream @content error).
P="$TMP/template"
scaffold "$P"
printf '<section>@content</section>\n' >"$P/templates/template.html"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null 2>&1) || fail "template base build failed"
preserve_pair "$P"
chmod 000 "$P/templates/template.html"
out="$(cd "$P" && "$NIFT_BIN" build --all 2>&1)"; rc=$?
[ "$rc" -ne 0 ] || fail "unreadable template: build unexpectedly succeeded"
printf '%s' "$out" | grep -q "template file is not readable" || fail "unreadable template: wrong diagnostic (output: $out)"
assert_preserved "$P" "unreadable template"
chmod 644 "$P/templates/template.html"

# 4. Empty-but-readable and unreadable are distinct: an empty readable content
#    file builds successfully and emits empty content; only the unreadable
#    state fails. This pins the semantic difference that a failed read must not
#    be conflated with an empty file.
P="$TMP/empty-vs-unreadable"
scaffold "$P"
printf '' >"$P/content/index.html"
(cd "$P" && "$NIFT_BIN" build --all >/dev/null 2>&1) || fail "empty readable content must build"
[ "$(cat "$P/public/index.html")" = "<main></main>" ] || fail "empty readable content rendered wrong (got: $(cat "$P/public/index.html"))"
preserve_pair "$P"
chmod 000 "$P/content/index.html"
expect_not_readable "$P" "empty-but-unreadable must still fail" "content/index.html"

echo "unreadable-source smoke test passed: unreadable content/@input/template fail cleanly and preserve last good output"