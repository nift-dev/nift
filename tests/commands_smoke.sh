#!/usr/bin/env bash
# CLI grammar regression test: `nift commands` must advertise the unified
# build/info grammar and must not advertise the removed historical verbs.
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-commands-test.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
cd "$TMP"
mkdir -p .nift content templates public
cat > .nift/config.json <<'JSON'
{"config":{"content-dir":"content/","output-dir":"public/","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}
JSON
cat > .nift/tracked.json <<'JSON'
{"tracked":[{"name":"/","title":"Index","template":"templates/template.html"}]}
JSON
echo '@content' > templates/template.html
printf '<p>home</p>\n' > content/index.html

COMMANDS="$("$NIFT_BIN" commands)"

# New unified grammar must be advertised.
for wanted in "build [names...]" "build --all" "build --auto" "build --repair" \
              "info [names...]" "info --all" "info --watching" "info --tracking" "info --names" \
              "status [-p]"; do
    if ! grep -qF -- "$wanted" <<<"$COMMANDS"; then
        echo "FAIL: 'nift commands' does not advertise '$wanted'" >&2
        exit 1
    fi
done

# Removed historical verbs must NOT be advertised.
for removed in "build-all" "build-updated" "build-names" "build-auto" \
               "info-all" "info-watching" "info-tracking" "info-names"; do
    if grep -qF -- "$removed" <<<"$COMMANDS"; then
        echo "FAIL: 'nift commands' still advertises removed verb '$removed'" >&2
        exit 1
    fi
done

# Removed verbs must error with the replacement hint and do nothing.
for pair in "build-all:build --all" "build-updated:build" "build-names:build <names...>" \
            "build-auto:build --auto" "info-all:info --all" \
            "info-watching:info --watching" "info-tracking:info --tracking" \
            "info-names:info --names"; do
    old="${pair%%:*}"; hint="${pair#*:}"
    if "$NIFT_BIN" "$old" >/dev/null 2>&1; then
        echo "FAIL: removed verb '$old' unexpectedly succeeded" >&2
        exit 1
    fi
    if ! out=$("$NIFT_BIN" "$old" 2>&1); then :; fi
    if ! grep -qF "has been removed; use 'nift $hint'" <<<"$out"; then
        echo "FAIL: removed verb '$old' lacks replacement hint (got: $out)" >&2
        exit 1
    fi
done

# Mode exclusivity is a hard error.
if "$NIFT_BIN" build / --all >/dev/null 2>&1; then echo "FAIL: build / --all succeeded" >&2; exit 1; fi
if "$NIFT_BIN" build --all --repair >/dev/null 2>&1; then echo "FAIL: build --all --repair succeeded" >&2; exit 1; fi
if "$NIFT_BIN" info / --all >/dev/null 2>&1; then echo "FAIL: info / --all succeeded" >&2; exit 1; fi
if "$NIFT_BIN" info about --names >/dev/null 2>&1; then echo "FAIL: info about --names succeeded" >&2; exit 1; fi

# Basic grammar works end to end.
"$NIFT_BIN" build >/dev/null
test -f public/index.html || { echo "FAIL: bare build did not produce index output" >&2; exit 1; }
"$NIFT_BIN" build --all >/dev/null
"$NIFT_BIN" build / >/dev/null
"$NIFT_BIN" build --repair >/dev/null
"$NIFT_BIN" info --names | grep -qF '"name"' || true
"$NIFT_BIN" info --names | grep -qF '"/"' || { echo "FAIL: info --names did not list the index name" >&2; exit 1; }
"$NIFT_BIN" info / | grep -qF '"name": "/"' || { echo "FAIL: info / did not show the index entry" >&2; exit 1; }

echo "CLI grammar smoke test passed"
