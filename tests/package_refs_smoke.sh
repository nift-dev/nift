#!/usr/bin/env bash
set -euo pipefail
NIFT="${NIFT:-./nift}"
# This contract creates a real local git repository; skip when git is
# unavailable (e.g. the Windows msys2 shell PATH lacks it) rather than fail.
if ! command -v git >/dev/null 2>&1; then
  echo "SKIP package refs/local lock (git unavailable)"
  exit 0
fi
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/pkg/src" "$tmp/site/.nift"
cat > "$tmp/pkg/manifest.json" <<'JSON'
{"name":"demo","version":"0.1.0","entry":"src/main.f"}
JSON
echo 'x := 1' > "$tmp/pkg/src/main.f"
git -C "$tmp/pkg" init -q; git -C "$tmp/pkg" config user.email a@b.c; git -C "$tmp/pkg" config user.name test; git -C "$tmp/pkg" add .; git -C "$tmp/pkg" commit -qm one; git -C "$tmp/pkg" tag v0.1.0
(cd "$tmp/site" && "$NIFT_ABS" add "$tmp/pkg")
grep -q '"commit": "local"' "$tmp/site/.nift/packages.lock.json"
echo PASS package refs/local lock
