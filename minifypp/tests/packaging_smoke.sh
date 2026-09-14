#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/minify-packaging-test.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

for script in install download update uninstall; do sh -n "$root/packaging/$script.sh"; done

version="$("$root/minify" --version | sed -E 's/^Minify\+\+ ([^ ]+)$/\1/')"
case "$(uname -s)/$(uname -m)" in
  Linux/x86_64|Linux/amd64) platform="linux-x86_64" ;;
  Darwin/arm64|Darwin/aarch64) platform="macos-arm64" ;;
  Darwin/x86_64|Darwin/amd64) platform="macos-x86_64" ;;
  *) echo "unsupported packaging-smoke platform" >&2; exit 1 ;;
esac
archive="minify-$version-$platform.tar.gz"
stage="$tmp/release/minify-$version-$platform"
mkdir -p "$stage" "$tmp/bin" "$tmp/downloads" "$tmp/website"
cp "$root/minify" "$stage/minify"
cp "$root/README.md" "$root/LICENSE" "$stage/"
tar -czf "$tmp/release/$archive" -C "$tmp/release" "minify-$version-$platform"
(cd "$tmp/release" && if command -v sha256sum >/dev/null 2>&1; then sha256sum "$archive"; else shasum -a 256 "$archive"; fi > SHA256SUMS)

MINIFY_VERSION="$version" MINIFY_RELEASE_BASE="file://$tmp/release" MINIFY_INSTALL_DIR="$tmp/bin" sh "$root/packaging/install.sh"
test "$("$tmp/bin/minify" --version)" = "Minify++ $version"

MINIFY_VERSION="$version" MINIFY_RELEASE_BASE="file://$tmp/release" MINIFY_DOWNLOAD_DIR="$tmp/downloads" sh "$root/packaging/download.sh"
test -f "$tmp/downloads/$archive"
test -f "$tmp/downloads/$archive.sha256"

cp "$root/packaging/install.sh" "$tmp/website/install.sh"
MINIFY_VERSION="$version" MINIFY_RELEASE_BASE="file://$tmp/release" MINIFY_INSTALL_DIR="$tmp/bin" MINIFY_WEBSITE_BASE="file://$tmp/website" sh "$root/packaging/update.sh"
test "$("$tmp/bin/minify" --version)" = "Minify++ $version"

MINIFY_INSTALL_DIR="$tmp/bin" sh "$root/packaging/uninstall.sh"
test ! -e "$tmp/bin/minify"
echo "Minify++ packaging smoke passed"

# Negative coverage: a tampered or missing checksum must fail closed.
mkdir -p "$tmp/badrelease/minify-$version-$platform"
cp "$root/minify" "$tmp/badrelease/minify-$version-$platform/minify"
tar -czf "$tmp/badrelease/$archive" -C "$tmp/badrelease" "minify-$version-$platform"
printf '0' > "$tmp/badrelease/SHA256SUMS"   # wrong format: no matching archive line
if MINIFY_VERSION="$version" MINIFY_RELEASE_BASE="file://$tmp/badrelease" MINIFY_INSTALL_DIR="$tmp/bin" sh "$root/packaging/install.sh" >/dev/null 2>&1; then
  echo "install must reject a missing checksum entry" >&2; exit 1
fi
printf '%s  %s\n' "0000000000000000000000000000000000000000000000000000000000000000" "$archive" > "$tmp/badrelease/SHA256SUMS"
if MINIFY_VERSION="$version" MINIFY_RELEASE_BASE="file://$tmp/badrelease" MINIFY_INSTALL_DIR="$tmp/bin" sh "$root/packaging/install.sh" >/dev/null 2>&1; then
  echo "install must reject a checksum mismatch" >&2; exit 1
fi
echo "Minify++ packaging negative checks passed"

# Safe staged replacement: overwriting an existing executable succeeds, an
# existing destination symlink is replaced as a directory entry (referent
# unchanged), a stale candidate staging file is never followed or
# overwritten, and no staged installer file remains.
printf 'old\n' > "$tmp/referent"
ln -s "$tmp/referent" "$tmp/bin/minify"
printf 'stale-referent\n' > "$tmp/stale-referent"
ln -s "$tmp/stale-referent" "$tmp/bin/.minify-install.zzzzzz"
MINIFY_VERSION="$version" MINIFY_RELEASE_BASE="file://$tmp/release" MINIFY_INSTALL_DIR="$tmp/bin" sh "$root/packaging/install.sh"
test ! -L "$tmp/bin/minify"
test "$(cat "$tmp/referent")" = "old"
test -L "$tmp/bin/.minify-install.zzzzzz"
test "$(cat "$tmp/stale-referent")" = "stale-referent"
test "$("$tmp/bin/minify" --version)" = "Minify++ $version"
test "$(find "$tmp/bin" -name '.minify-install.*' ! -name '.minify-install.zzzzzz' | wc -l)" -eq 0
echo "Minify++ safe staged replacement passed"
