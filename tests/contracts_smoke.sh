#!/usr/bin/env bash
set -euo pipefail

NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-contracts.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

make_project() {
    local d="$1" contracts="${2:-}"
    mkdir -p "$d/.nift" "$d/content" "$d/templates" "$d/public" "$d/data"
    if [[ -n "$contracts" ]]; then
        cat > "$d/.nift/config.json" <<JSON
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified","contracts":$contracts}}
JSON
    else
        cat > "$d/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/template.html","build-threads":-1,"incremental-mode":"modified"}}
JSON
    fi
    cat > "$d/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"/","title":"Contract Test","template":"templates/template.html"}]}
JSON
    printf 'CONTENT\n' > "$d/content/index.html"
}

expect_build_failure() {
    local d="$1" expected="$2"
    if (cd "$d" && "$NIFT_BIN" build-all >log 2>&1); then
        echo "expected build failure containing: $expected" >&2
        exit 1
    fi
    grep -F "$expected" "$d/log" >/dev/null || {
        echo "missing expected error: $expected" >&2
        cat "$d/log" >&2
        exit 1
    }
}

expect_open_failure() {
    local d="$1" expected="$2"
    if (cd "$d" && "$NIFT_BIN" build-all >log 2>&1); then
        echo "expected project/config failure containing: $expected" >&2
        exit 1
    fi
    grep -F "$expected" "$d/log" >/dev/null || {
        echo "missing expected config error: $expected" >&2
        cat "$d/log" >&2
        exit 1
    }
}

# A configured contract is a project-level JSON namespace. Its source and the
# config declaration become dependencies only when the namespace is referenced.
D="$TMP/happy"
make_project "$D" '{"routes":".nift/routes.json","unused":".nift/unused.json"}'
cat > "$D/.nift/routes.json" <<'JSON'
{"users":{"list":"/api/users","details":"/api/users/{id}"},"enabled":true,"version":4}
JSON
printf '{"broken":' > "$D/.nift/unused.json"
cat > "$D/templates/template.html" <<'EOF_TEMPLATE'
LIST=$[routes.users.list]
DETAIL=$[routes.users.details]
ENABLED=$[routes.enabled]
VERSION=$[routes.version]
@content
EOF_TEMPLATE
(cd "$D" && "$NIFT_BIN" build-all >/dev/null)
grep -Fx 'LIST=/api/users' "$D/public/index.html" >/dev/null
grep -Fx 'DETAIL=/api/users/{id}' "$D/public/index.html" >/dev/null
grep -Fx 'ENABLED=true' "$D/public/index.html" >/dev/null
grep -Fx 'VERSION=4' "$D/public/index.html" >/dev/null
grep -F '".nift/routes.json"' "$D/.nift/public/index.info.json" >/dev/null
grep -F '".nift/config.json"' "$D/.nift/public/index.info.json" >/dev/null
if grep -F '".nift/unused.json"' "$D/.nift/public/index.info.json" >/dev/null; then
    echo "unused contract unexpectedly became a dependency" >&2
    exit 1
fi

# Contract-source changes invalidate dependent output.
printf '{"users":{"list":"/v2/users","details":"/api/users/{id}"},"enabled":true,"version":4}\n' > "$D/.nift/routes.json"
python3 - "$D/.nift/routes.json" <<'PY'
import os, sys, time
p=sys.argv[1]; t=time.time()+2; os.utime(p,(t,t))
PY
(cd "$D" && "$NIFT_BIN" build-updated >/dev/null)
grep -Fx 'LIST=/v2/users' "$D/public/index.html" >/dev/null

# Changing the config mapping also invalidates users because config is part of
# the contract dependency. The new source becomes the persisted dependency.
printf '{"users":{"list":"/v3/users","details":"/v3/users/{id}"},"enabled":false,"version":5}\n' > "$D/.nift/routes-v3.json"
python3 - "$D/.nift/config.json" <<'PY'
import json, os, sys, time
p=sys.argv[1]
d=json.load(open(p)); d['config']['contracts']['routes']='.nift/routes-v3.json'
open(p,'w').write(json.dumps(d,separators=(',',':'))+'\n')
t=time.time()+4; os.utime(p,(t,t))
PY
(cd "$D" && "$NIFT_BIN" build-updated >/dev/null)
grep -Fx 'LIST=/v3/users' "$D/public/index.html" >/dev/null
grep -F '".nift/routes-v3.json"' "$D/.nift/public/index.info.json" >/dev/null

# Contract values participate in the same expression/control-flow and parameter
# interpolation model as explicit JSON bindings.
D="$TMP/integration"
make_project "$D" '{"site":".nift/site.json"}'
printf '{"partial":"templates/a.html","enabled":true,"items":[{"name":"A"},{"name":"B"}]}\n' > "$D/.nift/site.json"
printf 'PARTIAL-A\n' > "$D/templates/a.html"
cat > "$D/templates/template.html" <<'EOF_INTEGRATION'
@input($[site.partial])
@if(site.enabled){ENABLED
}
@for(item : site.items){ITEM=$[item.name]
}
@content
EOF_INTEGRATION
(cd "$D" && "$NIFT_BIN" build-all >/dev/null)
grep -Fq 'PARTIAL-A' "$D/public/index.html"
grep -Fq 'ENABLED' "$D/public/index.html"
grep -Fq 'ITEM=A' "$D/public/index.html"
grep -Fq 'ITEM=B' "$D/public/index.html"
grep -F '".nift/site.json"' "$D/.nift/public/index.info.json" >/dev/null
grep -F '"templates/a.html"' "$D/.nift/public/index.info.json" >/dev/null

# An unresolved ordinary JSON namespace remains an ordinary unknown value: Nift
# must not guess that every unknown root is a contract.
D="$TMP/unconfigured"
make_project "$D"
printf '%s\n' 'VALUE=$[routes.users.list]' '@content' > "$D/templates/template.html"
(cd "$D" && "$NIFT_BIN" build-all >/dev/null)
grep -F '$[routes.users.list]' "$D/public/index.html" >/dev/null

# The same namespace remains valid as an explicit local @json binding when no
# configured contract reserves it.
D="$TMP/local-json"
make_project "$D"
printf '{"users":{"list":"/local"}}\n' > "$D/data/routes.json"
printf '%s\n' '@json("data/routes.json", routes)' 'VALUE=$[routes.users.list]' '@content' > "$D/templates/template.html"
(cd "$D" && "$NIFT_BIN" build-all >/dev/null)
grep -Fx 'VALUE=/local' "$D/public/index.html" >/dev/null

# Contract-specific failures are clear and occur only for declared namespaces.
D="$TMP/missing-file"
make_project "$D" '{"routes":".nift/routes.json"}'
printf '%s\n' 'VALUE=$[routes.users.list]' > "$D/templates/template.html"
expect_build_failure "$D" "contract 'routes': file does not exist: .nift/routes.json"

D="$TMP/malformed"
make_project "$D" '{"routes":".nift/routes.json"}'
printf '{"users":' > "$D/.nift/routes.json"
printf '%s\n' 'VALUE=$[routes.users.list]' > "$D/templates/template.html"
expect_build_failure "$D" "contract 'routes': failed to parse .nift/routes.json"

D="$TMP/missing-entry"
make_project "$D" '{"routes":".nift/routes.json"}'
printf '{"users":{"list":"/api/users"}}\n' > "$D/.nift/routes.json"
printf '%s\n' 'VALUE=$[routes.users.details]' > "$D/templates/template.html"
expect_build_failure "$D" "contract 'routes' has no entry 'users.details'"

D="$TMP/render-object"
make_project "$D" '{"routes":".nift/routes.json"}'
printf '{"users":{"list":"/api/users"}}\n' > "$D/.nift/routes.json"
printf '%s\n' 'VALUE=$[routes.users]' > "$D/templates/template.html"
expect_build_failure "$D" 'cannot render JSON object $[routes.users]'

# Configured contract names are reserved project-wide and cannot be shadowed by
# local @json or loop bindings.
D="$TMP/json-shadow"
make_project "$D" '{"routes":".nift/routes.json"}'
printf '{}\n' > "$D/.nift/routes.json"
printf '{}\n' > "$D/data/local.json"
printf '%s\n' '@json("data/local.json", routes)' > "$D/templates/template.html"
expect_build_failure "$D" "json: name 'routes' conflicts with configured contract namespace"

D="$TMP/array-loop-shadow"
make_project "$D" '{"routes":".nift/routes.json"}'
printf '{"items":[1]}\n' > "$D/.nift/routes.json"
printf '%s\n' '@for(routes : routes.items){$[routes]}' > "$D/templates/template.html"
expect_build_failure "$D" "@for binding 'routes' conflicts with configured contract namespace"

D="$TMP/object-loop-shadow"
make_project "$D" '{"routes":".nift/routes.json"}'
printf '{"items":{"a":1}}\n' > "$D/.nift/routes.json"
printf '%s\n' '@for((routes, value) : routes.items){$[value]}' > "$D/templates/template.html"
expect_build_failure "$D" '@for bindings cannot conflict with configured contract namespaces'

# Contract declarations themselves have a small, strict config contract.
D="$TMP/config-not-object"
make_project "$D" '[]'
expect_open_failure "$D" 'contracts must be an object mapping names to project-relative JSON paths'

D="$TMP/config-bad-name"
make_project "$D" '{"bad-name":".nift/bad.json"}'
expect_open_failure "$D" "contract name 'bad-name' must be an identifier"

D="$TMP/config-reserved"
make_project "$D" '{"title":".nift/title.json"}'
expect_open_failure "$D" "contract name 'title' conflicts with built-in metadata/reserved bindings"

D="$TMP/config-bad-path-type"
make_project "$D" '{"routes":42}'
expect_open_failure "$D" "contract 'routes' must map to a non-empty JSON path string"

D="$TMP/config-traversal"
make_project "$D" '{"routes":"../outside.json"}'
expect_open_failure "$D" "contract 'routes' path must stay inside the Nift project"

D="$TMP/config-symlink"
make_project "$D" '{"routes":"data/link.json"}'
printf '{}\n' > "$TMP/outside.json"
ln -s "$TMP/outside.json" "$D/data/link.json"
expect_open_failure "$D" "contract 'routes' path must stay inside the Nift project"

echo "Project contracts smoke test passed"
