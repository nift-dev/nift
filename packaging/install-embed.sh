#!/usr/bin/env bash
# Nift Embedded native installer (optional request-time rendering library).
# Usage:
#   curl -fsSL https://nift.dev/install-embed.sh | sh              # per-user
#   curl -fsSL https://nift.dev/install-embed.sh | sudo sh -s -- --system
#
# Downloads the matching native bundle for the host OS/arch, verifies its
# SHA-256 before installing, and reports any PATH / library-path /
# PKG_CONFIG_PATH configuration required. Never silently builds or downloads an
# unverified artifact.
set -euo pipefail
BUNDLE_BASE="${NIFT_EMBED_BASE:-https://github.com/nift-dev/nift/releases/latest/download}"
PREFIX="${PREFIX:-$HOME/.local}"
SYSTEM=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --system) SYSTEM=1; PREFIX=/usr/local ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

uname_s="$(uname -s)"
uname_m="$(uname -m)"
case "$uname_s" in
  Linux) os="linux" ;;
  Darwin) os="macos" ;;
  *) echo "unsupported OS: $uname_s" >&2; exit 2 ;;
esac
case "$uname_m" in
  x86_64|amd64) arch="x86_64" ;;
  aarch64|arm64) arch="arm64" ;;
  *) echo "unsupported arch: $uname_m" >&2; exit 2 ;;
esac
tarball="nift-embed-$os-$arch.tar.gz"
url="$BUNDLE_BASE/$tarball"
tmp="$(mktemp -d /tmp/nift-embed-install.XXXXXX)"
trap 'rm -rf "$tmp"' EXIT
echo "downloading $url"
curl -fsSL "$url" -o "$tmp/$tarball"
curl -fsSL "$BUNDLE_BASE/SHA256SUMS" -o "$tmp/SHA256SUMS"
expected="$(awk -v f="$tarball" '$2==f {print $1}' "$tmp/SHA256SUMS")"
[ -n "$expected" ] || { echo "no checksum for $tarball in SHA256SUMS" >&2; exit 2; }
if command -v sha256sum >/dev/null 2>&1; then
  actual="$(sha256sum "$tmp/$tarball" | cut -d' ' -f1)"
else
  actual="$(shasum -a 256 "$tmp/$tarball" | cut -d' ' -f1)"
fi
[ "$actual" = "$expected" ] || { echo "checksum mismatch for $tarball" >&2; exit 2; }
tar xzf "$tmp/$tarball" -C "$tmp"
mkdir -p "$PREFIX/include/nift" "$PREFIX/lib/pkgconfig"
cp -r "$tmp/include/nift/." "$PREFIX/include/nift/"
# Target-correct native library files: static archive always; shared library is
# .so on Linux, .dylib on macOS, .dll on Windows (the bundle ships what the
# target produced).
# Fail-fast: every required native file for the selected platform must install.
[ -f "$tmp/lib/libnift_c.a" ] || { echo "install: bundle missing libnift_c.a" >&2; exit 1; }
cp "$tmp/lib/libnift_c.a" "$PREFIX/lib/"
case "$os" in
  linux)
    [ -f "$tmp/lib/libnift_c.so" ] || { echo "install: bundle missing libnift_c.so" >&2; exit 1; }
    cp "$tmp/lib/libnift_c.so" "$PREFIX/lib/" ;;
  macos)
    # Target-correct macOS layout: only a Mach-O .dylib is accepted.
    [ -f "$tmp/lib/libnift_c.dylib" ] || { echo "install: bundle missing libnift_c.dylib" >&2; exit 1; }
    cp "$tmp/lib/libnift_c.dylib" "$PREFIX/lib/" ;;
esac
cp "$tmp/lib/pkgconfig/nift.pc" "$PREFIX/lib/pkgconfig/nift.pc"
echo "installed Nift Embed native library to $PREFIX"
echo "if needed, export PKG_CONFIG_PATH=\"\$PKG_CONFIG_PATH:$PREFIX/lib/pkgconfig\""
echo "if needed, export LD_LIBRARY_PATH=\"\$LD_LIBRARY_PATH:$PREFIX/lib\""
