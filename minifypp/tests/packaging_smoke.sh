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

cp "$root/packaging/install.sh" "$tmp/website/install"
MINIFY_VERSION="$version" MINIFY_RELEASE_BASE="file://$tmp/release" MINIFY_INSTALL_DIR="$tmp/bin" MINIFY_WEBSITE_BASE="file://$tmp/website" sh "$root/packaging/update.sh"
test "$("$tmp/bin/minify" --version)" = "Minify++ $version"

MINIFY_INSTALL_DIR="$tmp/bin" sh "$root/packaging/uninstall.sh"
test ! -e "$tmp/bin/minify"
echo "Minify++ packaging smoke passed"
