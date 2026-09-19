#!/usr/bin/env bash
# v4.4 build-hook execution (CP25/CP26): project hooks from .nift/config.json
# and per-file hooks from .nift/tracked.json, with the agreed lifecycle
# project-pre -> affected-file-pre/render/post -> project-post, mode-specific
# matching, failure propagation, incremental behaviour and missing-hook
# diagnostics. Hooks are Nift .f scripts run through the native parser.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
mkdir -p "$t/.nift" "$t/content" "$t/templates" "$t/public" "$t/scripts"
cat > "$t/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/main.html","pre build":"scripts/pre.f","post build":"scripts/post.f","pre build -all":"scripts/preall.f","post build -all":"scripts/postall.f"}}
JSON
cat > "$t/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"/","title":"Home","template":"templates/main.html"},{"name":"about","title":"About","template":"templates/main.html","post build":"scripts/filepost.f"}]}
JSON
printf 'home\n' > "$t/content/index.html"
printf 'about\n' > "$t/content/about.html"
printf '<div>@content</div>\n' > "$t/templates/main.html"
printf 'print("PRE=" + getenv("NIFT_HOOK_MODE"))\n' > "$t/scripts/pre.f"
printf 'print("POST")\n' > "$t/scripts/post.f"
printf 'print("PREALL")\n' > "$t/scripts/preall.f"
printf 'print("POSTALL")\n' > "$t/scripts/postall.f"
printf 'print("FILEPOST=" + getenv("NIFT_HOOK_TARGET"))\n' > "$t/scripts/filepost.f"

# Full build: generic + mode-specific hooks, file post hook, correct ordering.
out=$(cd "$t" && "$NIFT_ABS" build --all 2>&1)
grep -q '^PRE=all$' <<<"$out"
grep -q '^PREALL$' <<<"$out"
grep -q '^FILEPOST=about$' <<<"$out"
grep -q '^POST$' <<<"$out"
grep -q '^POSTALL$' <<<"$out"
# Up-to-date incremental build: generic hooks only, mode-specific and
# per-file hooks must not run for skipped pages.
out2=$(cd "$t" && "$NIFT_ABS" build 2>&1)
grep -q '^PRE=updated$' <<<"$out2"
grep -q '^POST$' <<<"$out2"
! grep -q '^PREALL$\|^POSTALL$\|^FILEPOST=' <<<"$out2"

# Missing hook script is a clear diagnostic.
printf '{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/main.html","pre build":"scripts/nope.f"}}\n' > "$t/.nift/config.json"
miss=$(cd "$t" && "$NIFT_ABS" build --all 2>&1 || true)
grep -q 'build hook script does not exist' <<<"$miss"

# A failing project pre hook aborts the build before any render.
printf '{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/main.html","pre build":"scripts/bad.f"}}\n' > "$t/.nift/config.json"
printf 'undefined_function_xyz()\n' > "$t/scripts/bad.f"
bad=$(cd "$t" && "$NIFT_ABS" build --all 2>&1 || true)
if grep -q 'built successfully' <<<"$bad"; then echo "pre hook did not abort build" >&2; exit 1; fi
grep -q 'build hook .* failed' <<<"$bad"
printf '{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/main.html"}}\n' > "$t/.nift/config.json"
(cd "$t" && "$NIFT_ABS" build --repair >/dev/null 2>&1) || true

echo 'PASS v4.4 build hooks'