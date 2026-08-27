#!/usr/bin/env bash
# Stage the native C/C++ embedding bundle for one target:
#   include/nift/*.h + lib/libnift_c.{a,so} + lib/pkgconfig/nift.pc +
#   install-embed.sh, then tar + SHA256SUMS.
# Usage: packaging/build-native-bundle.sh <os>-<arch> [out-dir]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${1:?usage: build-native-bundle.sh <os>-<arch>}"
OUT="${2:-$ROOT/dist}"
STAGE="$(mktemp -d /tmp/nift-embed-bundle.XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/include/nift" "$STAGE/lib/pkgconfig" "$STAGE/tools"
cp -r "$ROOT/include/nift/." "$STAGE/include/nift/"
cp "$ROOT/libnift_c.a" "$ROOT/libnift_c.so" "$STAGE/lib/"
cp "$ROOT/packaging/nift.pc" "$STAGE/lib/pkgconfig/nift.pc"
cp "$ROOT/packaging/install-embed.sh" "$STAGE/install-embed.sh"
mkdir -p "$OUT"
tar czf "$OUT/nift-embed-$TARGET.tar.gz" -C "$STAGE" include lib install-embed.sh
echo "staged $OUT/nift-embed-$TARGET.tar.gz"
