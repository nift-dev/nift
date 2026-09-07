#!/bin/sh
set -eu

repo="${MINIFY_GITHUB_REPOSITORY:-minify-cx/minify}"
version="${MINIFY_VERSION:-}"
download_dir="${MINIFY_DOWNLOAD_DIR:-$PWD}"

need() {
    command -v "$1" >/dev/null 2>&1 || { echo "minify download: required command not found: $1" >&2; exit 1; }
}
need curl
need uname
need mktemp

if [ -z "$version" ]; then
    latest_url="$(curl --retry 5 --retry-all-errors --retry-delay 2 -fsSL -o /dev/null -w '%{url_effective}' "https://github.com/$repo/releases/latest")"
    version="${latest_url##*/v}"
    case "$version" in
        ''|*[!0-9A-Za-z._-]*) echo "minify download: could not determine latest release version" >&2; exit 1 ;;
    esac
fi
version="${version#v}"

os="$(uname -s)"
arch="$(uname -m)"
case "$os/$arch" in
    Linux/x86_64|Linux/amd64) platform="linux-x86_64"; extension="tar.gz" ;;
    Darwin/arm64|Darwin/aarch64) platform="macos-arm64"; extension="tar.gz" ;;
    Darwin/x86_64|Darwin/amd64) platform="macos-x86_64"; extension="tar.gz" ;;
    *) echo "minify download: unsupported platform: $os/$arch" >&2; exit 1 ;;
esac

archive="minify-$version-$platform.$extension"
base="${MINIFY_RELEASE_BASE:-https://github.com/$repo/releases/download/v$version}"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/minify-download.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

curl --retry 5 --retry-all-errors --retry-delay 2 -fsSL "$base/$archive" -o "$tmp/$archive"
curl --retry 5 --retry-all-errors --retry-delay 2 -fsSL "$base/SHA256SUMS" -o "$tmp/SHA256SUMS"
expected="$(awk -v file="$archive" '$2 == file { print $1; exit }' "$tmp/SHA256SUMS")"
[ -n "$expected" ] || { echo "minify download: checksum for $archive not found" >&2; exit 1; }
if command -v sha256sum >/dev/null 2>&1; then
    actual="$(sha256sum "$tmp/$archive" | awk '{print $1}')"
elif command -v shasum >/dev/null 2>&1; then
    actual="$(shasum -a 256 "$tmp/$archive" | awk '{print $1}')"
else
    echo "minify download: sha256sum or shasum is required" >&2
    exit 1
fi
[ "$actual" = "$expected" ] || { echo "minify download: checksum verification failed for $archive" >&2; exit 1; }

mkdir -p "$download_dir"
cp "$tmp/$archive" "$download_dir/$archive"
printf '%s  %s\n' "$expected" "$archive" > "$download_dir/$archive.sha256"
printf 'Downloaded and verified %s in %s\n' "$archive" "$download_dir"
