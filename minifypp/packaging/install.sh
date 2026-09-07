#!/bin/sh
set -eu

repo="${MINIFY_GITHUB_REPOSITORY:-minify-cx/minify}"
custom_install_dir="${MINIFY_INSTALL_DIR:-}"
install_dir="${custom_install_dir:-$HOME/.local/bin}"
version="${MINIFY_VERSION:-}"

need() {
    command -v "$1" >/dev/null 2>&1 || { echo "minify installer: required command not found: $1" >&2; exit 1; }
}
need curl
need tar
need uname
need mktemp

if [ -z "$version" ]; then
    latest_url="$(curl -fsSL -o /dev/null -w '%{url_effective}' "https://github.com/$repo/releases/latest")"
    version="${latest_url##*/v}"
    case "$version" in
        ''|*[!0-9A-Za-z._-]*) echo "minify installer: could not determine latest release version" >&2; exit 1 ;;
    esac
fi
version="${version#v}"

os="$(uname -s)"
arch="$(uname -m)"
case "$os/$arch" in
    Linux/x86_64|Linux/amd64) platform="linux-x86_64" ;;
    Darwin/arm64|Darwin/aarch64) platform="macos-arm64" ;;
    Darwin/x86_64|Darwin/amd64) platform="macos-x86_64" ;;
    *) echo "minify installer: unsupported platform: $os/$arch" >&2; exit 1 ;;
esac

root="minify-$version-$platform"
archive="$root.tar.gz"
base="${MINIFY_RELEASE_BASE:-https://github.com/$repo/releases/download/v$version}"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/minify-install.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

curl --retry 5 --retry-all-errors --retry-delay 2 -fsSL "$base/$archive" -o "$tmp/$archive"
curl --retry 5 --retry-all-errors --retry-delay 2 -fsSL "$base/SHA256SUMS" -o "$tmp/SHA256SUMS"
expected="$(awk -v file="$archive" '$2 == file { print $1; exit }' "$tmp/SHA256SUMS")"
[ -n "$expected" ] || { echo "minify installer: checksum for $archive not found" >&2; exit 1; }

if command -v sha256sum >/dev/null 2>&1; then
    actual="$(sha256sum "$tmp/$archive" | awk '{print $1}')"
elif command -v shasum >/dev/null 2>&1; then
    actual="$(shasum -a 256 "$tmp/$archive" | awk '{print $1}')"
else
    echo "minify installer: sha256sum or shasum is required to verify the release" >&2
    exit 1
fi
[ "$actual" = "$expected" ] || { echo "minify installer: checksum verification failed for $archive" >&2; exit 1; }

tar -xzf "$tmp/$archive" -C "$tmp"
[ -f "$tmp/$root/minify" ] || { echo "minify installer: release archive did not contain minify" >&2; exit 1; }
mkdir -p "$install_dir"
# Stage inside install_dir so the final rename is on the same filesystem and
# atomic. mktemp creates the staging file exclusively (O_EXCL), so a stale
# file or symlink at any candidate name is never followed or overwritten, and
# rename(2) replaces an existing destination directory entry, so a
# pre-existing symlink at the target is replaced rather than followed.
stage="$(mktemp "$install_dir/.minify-install.XXXXXX")"
trap 'rm -f -- "$stage"; rm -rf "$tmp"' EXIT HUP INT TERM
cp "$tmp/$root/minify" "$stage"
chmod 0755 "$stage"
mv -f -- "$stage" "$install_dir/minify"

printf 'Installed Minify++ %s to %s/minify\n' "$version" "$install_dir"
case ":${PATH:-}:" in
    *":$install_dir:"*) ;;
    *) printf 'Add %s to PATH to run minify from any directory.\n' "$install_dir" ;;
esac
