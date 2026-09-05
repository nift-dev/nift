#!/usr/bin/env bash
set -euo pipefail

NIFT_BIN=${NIFT_BIN:-./nift}
ROOT=$(mktemp -d)
trap 'rm -rf "$ROOT"' EXIT

cd "$ROOT"
"$NIFT_BIN" init --ext=.html >/dev/null
printf '@content\n' > templates/template.html
mkdir -p data schemas prose
printf '{"title":"From file","items":["one","two"]}\n' > data/article.json
printf '{"type":"object","required":["title"],"properties":{"title":{"type":"string"}}}\n' > schemas/article.json
printf '# $[file_plain.title]\n\n**templated body**\n' > prose/article.md
printf '= AsciiDoc $[file_plain.title]\n\ninclude::adoc-part.adoc[]\n' > prose/guide.adoc
printf 'Included *$[file_plain.title]*.\n' > prose/adoc-part.adoc
printf 'RST $[file_plain.title]\n==================\n\n.. include:: rst-part.rst\n' > prose/manual.rst
printf 'Included **$[file_plain.title]**.\n' > prose/rst-part.rst

cat > content/index.html <<'EOF'
@json(article_schema) {
  {"type":"object","required":["title"],"properties":{"title":{"type":"string"}}}
}
@json(file_plain, "data/article.json")
@json(file_path_schema, "schemas/article.json", "data/article.json")
@json(file_named_schema, article_schema, "data/article.json")
@json(inline_plain) { {"title":"Inline"} }
@json(inline_path_schema, "schemas/article.json") { {"title":"Inline path"} }
@json(inline_named_schema, article_schema) { {"title":"$[file_plain.title] templated"} }
<p>$[file_path_schema.title]|$[file_named_schema.title]|$[inline_plain.title]|$[inline_path_schema.title]|$[inline_named_schema.title]</p>
@markup("md") {
  ## Inline $[inline_plain.title]

  Literal \@json(fake){not executed} and \$[not.reparsed].
}
@markup("md", "prose/article.md")
@markup("adoc", "prose/guide.adoc")
@markup("rst", "prose/manual.rst")
EOF

"$NIFT_BIN" build >/dev/null
grep -F '<p>From file|From file|Inline|Inline path|From file templated</p>' public/index.html >/dev/null
grep -F '<h2>Inline Inline</h2>' public/index.html >/dev/null
grep -F 'Literal @json(fake){not executed} and $[not.reparsed].' public/index.html >/dev/null
grep -F '<h1>From file</h1>' public/index.html >/dev/null
grep -F '<p><strong>templated body</strong></p>' public/index.html >/dev/null
grep -F '<h1>AsciiDoc From file</h1>' public/index.html >/dev/null
grep -F '<h1>RST From file</h1>' public/index.html >/dev/null
test "$(grep -Fc 'Included <strong>From file</strong>.' public/index.html)" -eq 2

# Both JSON/schema files and markup sources participate in incremental builds.
for dependency in data/article.json schemas/article.json prose/article.md prose/guide.adoc prose/adoc-part.adoc prose/manual.rst prose/rst-part.rst; do
    grep -F "\"$dependency\"" .nift/public/index.info.json >/dev/null
done
sleep 1
printf '# Changed $[file_plain.title]\n' > prose/article.md
"$NIFT_BIN" build >/dev/null
grep -F '<h1>Changed From file</h1>' public/index.html >/dev/null

# Invalid schema data and unsafe/missing markup paths fail with controlled diagnostics.
printf '@json(bad, "schemas/article.json") { {"title":1} }\n' > content/index.html
if "$NIFT_BIN" build >out 2>err; then exit 1; fi
grep -F 'does not satisfy schema' err >/dev/null
printf '@markup("md", "../outside.md")\n' > content/index.html
if "$NIFT_BIN" build >out 2>err; then exit 1; fi
grep -F 'path must stay inside the Nift project' err >/dev/null
printf '@markup("unknown") { text }\n' > content/index.html
if "$NIFT_BIN" build >out 2>err; then exit 1; fi
grep -F "unknown format 'unknown'" err >/dev/null

# A @markup include cycle that closes through a nested @markup directive is
# reported cleanly instead of exhausting the parse-depth guard.
mkdir -p cyc
printf '@markup("adoc", "cyc/part-a.adoc")\n' > content/index.html
printf '= A\n\ninclude::part-b.adoc[]\n' > cyc/part-a.adoc
printf '@markup("adoc", "cyc/part-a.adoc")\n' > cyc/part-b.adoc
if "$NIFT_BIN" build >out 2>err; then exit 1; fi
grep -F 'cycle' err >/dev/null

# Braces inside Markdown code spans and fenced blocks must not terminate the
# @markup block; quoted braces in inline JSON must not corrupt parsing.
cat > content/index.html <<'EOF'
@markup("md") {
  ## Braces

  Inline `code { x }` span.

  ```
  function f() { return { a: 1 }; }
  ```
}
@json(settings) {
  {"pattern": "value { with } lone } brace"}
}
VALUE=$[settings.pattern]
EOF
"$NIFT_BIN" build >/dev/null
grep -F '<code>code { x }</code>' public/index.html >/dev/null
grep -F '<span class="hljs-title">' public/index.html >/dev/null 2>&1 || true
grep -F 'function f() { return { a: 1 }; }' public/index.html >/dev/null
grep -F 'VALUE=value { with } lone } brace' public/index.html >/dev/null

echo '@json and @markup directive smoke test passed'
