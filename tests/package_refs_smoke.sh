#!/usr/bin/env bash
set -euo pipefail
NIFT="${NIFT:-./nift}"
# This contract creates a real local git repository; skip when git is
# unavailable (e.g. the Windows msys2 shell PATH lacks it) rather than fail.
if ! command -v git >/dev/null 2>&1; then
  echo "SKIP package refs/local lock (git unavailable)"
  exit 77
fi
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/pkg/src" "$tmp/site/.nift"
cat > "$tmp/pkg/manifest.json" <<'JSON'
{"name":"demo","version":"0.1.0","entry":"src/main.f"}
JSON
echo 'x := 1' > "$tmp/pkg/src/main.f"
git -C "$tmp/pkg" init -q; git -C "$tmp/pkg" config user.email a@b.c; git -C "$tmp/pkg" config user.name test; git -C "$tmp/pkg" add .; git -C "$tmp/pkg" commit -qm one; git -C "$tmp/pkg" tag v0.1.0; git -C "$tmp/pkg" tag v9.0.0
printf 'ignored\n' > "$tmp/pkg/ignored.txt"; git -C "$tmp/pkg" add .; git -C "$tmp/pkg" commit -qm two; git -C "$tmp/pkg" tag vbanana
printf 'prerelease\n' >> "$tmp/pkg/ignored.txt"; git -C "$tmp/pkg" add .; git -C "$tmp/pkg" commit -qm three; git -C "$tmp/pkg" tag v10.0.0-alpha
printf 'stable\n' >> "$tmp/pkg/ignored.txt"; git -C "$tmp/pkg" add .; git -C "$tmp/pkg" commit -qm four; git -C "$tmp/pkg" tag v10.0.0
latest_stable=$(git -C "$tmp/pkg" rev-parse HEAD)
(cd "$tmp/site" && "$NIFT_ABS" add "$tmp/pkg")
grep -q '"commit": "local"' "$tmp/site/.nift/packages.lock.json"

commit=$(git -C "$tmp/pkg" rev-parse HEAD)
mkdir -p "$tmp/remote-site"
(cd "$tmp/remote-site" && "$NIFT_ABS" add "file://$tmp/pkg" "--ref=$commit")
grep -q "\"requested\": \"$commit\"" "$tmp/remote-site/.nift/packages.lock.json"
grep -q "\"commit\": \"$commit\"" "$tmp/remote-site/.nift/packages.lock.json"
rm -rf "$tmp/remote-site/.nift/packages/demo"
(cd "$tmp/remote-site" && "$NIFT_ABS" install >/dev/null)
grep -q "\"requested\": \"$commit\"" "$tmp/remote-site/.nift/packages.lock.json"

printf '{"demo":{"source":"file://%s/pkg","requested":"%s","commit":"abc"}}\n' "$tmp" "$commit" > "$tmp/remote-site/.nift/packages.lock.json"
if (cd "$tmp/remote-site" && "$NIFT_ABS" install >/dev/null 2>&1); then
  echo 'malformed exact commit unexpectedly accepted' >&2
  exit 1
fi

mkdir -p "$tmp/tag-site"
(cd "$tmp/tag-site" && "$NIFT_ABS" add "file://$tmp/pkg" --ref=latest-tag >/dev/null)
grep -q "\"commit\": \"$latest_stable\"" "$tmp/tag-site/.nift/packages.lock.json"
echo PASS package refs/local lock
