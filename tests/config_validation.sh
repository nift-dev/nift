#!/usr/bin/env bash
# Config validation guard: `.nift/config.json` must fail loudly on unknown
# keys instead of silently ignoring them. An old-Nift key or a typo must never
# be accepted while the project believes it is honoured.
#
# GREEN control: a clean known-key config builds.
# RED: a legacy key (`script-ext`, `backup-scripts`, `paginate-threads`) and a
# typo key are both rejected with a clear `unknown config key '<name>'` error.
set -u

NIFT_BIN=${NIFT_BIN:-"$(pwd)/nift"}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

make_project() {
  local dir=$1 extra=$2
  mkdir -p "$TMP/$dir/.nift" "$TMP/$dir/content" "$TMP/$dir/templates" "$TMP/$dir/public"
  python3 - "$TMP/$dir/.nift/config.json" "$extra" <<'PY'
import json, sys
cfg = {
    "content-dir": "content/",
    "content-ext": ".html",
    "output-dir": "public/",
    "output-ext": ".html",
    "default-template": "templates/template.html",
    "incremental-mode": "modified",
}
extra = sys.argv[2].strip()
if extra:
    for part in extra.rstrip(',').split(','):
        part = part.strip()
        if not part:
            continue
        key, value = part.split(':', 1)
        cfg[key.strip().strip('"')] = json.loads(value.strip())
json.dump({"config": cfg}, open(sys.argv[1], "w"), indent="\t")
PY
  echo '{"tracked":[{"name":"/","title":"T","template":"templates/template.html"}]}' \
    > "$TMP/$dir/.nift/tracked.json"
  echo '<p>hi</p>' > "$TMP/$dir/content/index.html"
  echo '@content' > "$TMP/$dir/templates/template.html"
}

fail() { echo "config-validation FAIL: $*" >&2; exit 1; }

# GREEN control: a clean known-key config builds.
make_project "clean" ''
(cd "$TMP/clean" && "$NIFT_BIN" build-all >/dev/null 2>&1) \
  || fail "clean config did not build"

# RED: a legacy old-Nift key must be rejected loudly.
make_project "legacy" '"script-ext": ".f", "backup-scripts": true, "paginate-threads": -1,'
out=$(cd "$TMP/legacy" && "$NIFT_BIN" build-all 2>&1); rc=$?
[ "$rc" -eq 0 ] && fail "legacy key was silently accepted"
echo "$out" | grep -q "unknown config key 'script-ext'" \
  || fail "expected clear unknown-config-key error, got: $(echo "$out" | head -1)"

# RED: a typo key must also be rejected loudly.
make_project "typo" '"content-dri": "content/",'
out=$(cd "$TMP/typo" && "$NIFT_BIN" build-all 2>&1); rc=$?
[ "$rc" -eq 0 ] && fail "typo key was silently accepted"
echo "$out" | grep -q "unknown config key 'content-dri'" \
  || fail "expected clear error for typo key, got: $(echo "$out" | head -1)"

echo "config validation PASS: unknown config keys are rejected loudly"
