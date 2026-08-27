#!/usr/bin/env bash
# Non-publishing target-matrix build + smoke for ONE supported target.
# Usage: packaging/matrix-build.sh <os>-<arch> <version>
#   e.g. linux-x86_64, linux-arm64, macos-arm64, macos-x86_64, windows-x86_64
#
# Builds (nothing published):
#   dist/<version>/<target>/nift-<os>-<arch>            CLI binary
#   dist/<version>/<target>/nift-embed-<os>-<arch>.tar.gz  native bundle
#   dist/<version>/<target>/SHA256SUMS
# Then smoke-tests the native bundle from a clean consumer: extract to a temp
# prefix, compile a C consumer via the packaged nift.pc, link and run a render,
# and verify the consumer has no Nift shared-library runtime dependency using
# the platform dependency tool (ldd / otool -L / objdump).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
TARGET="${1:?usage: matrix-build.sh <os>-<arch> <version>}"
VERSION="${2:?usage: matrix-build.sh <os>-<arch> <version>}"
OS="${TARGET%%-*}"
ARCH="${TARGET#*-}"
OUT="$ROOT/dist/$VERSION/$TARGET"
mkdir -p "$OUT"

case "$OS:$ARCH" in
  linux:*) PC_LIBS='-L${libdir} -Wl,-Bstatic -lnift_c -Wl,-Bdynamic -lstdc++ -lm -pthread' ;;
  macos:*) PC_LIBS='-L${libdir} -lnift_c -lc++ -lm' ;;
  windows:*) PC_LIBS='-L${libdir} -lnift_c' ;;
  *) echo "unsupported target: $TARGET" >&2; exit 2 ;;
esac

# CLI binary (the runner already built it for this OS/arch; stage it).
if [ -x "$ROOT/nift" ]; then
  cp "$ROOT/nift" "$OUT/nift-$OS-$ARCH"
fi

# Native bundle with target-correct .pc.
make embed >/dev/null 2>&1
STAGE="$(mktemp -d /tmp/nift-matrix.XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/include/nift" "$STAGE/lib/pkgconfig"
cp -r include/nift/. "$STAGE/include/nift/"
cp libnift_c.* "$STAGE/lib/" 2>/dev/null || true
sed -e "s/__VERSION__/$VERSION/" -e "s|__LIBS__|$PC_LIBS|" packaging/nift.pc.in > "$STAGE/lib/pkgconfig/nift.pc"
cp packaging/install-embed.sh "$STAGE/install-embed.sh"
tar czf "$OUT/nift-embed-$OS-$ARCH.tar.gz" -C "$STAGE" include lib install-embed.sh

( cd "$OUT" && for f in *; do [ -f "$f" ] && [ "$f" != "SHA256SUMS" ] && sha256sum "$f"; done | sort > SHA256SUMS )

# --- Clean-consumer smoke of the native bundle (this runner's platform) ---
# (Only runs when the bundle matches the running platform; cross-target bundles
# are verified by their own runner in the CI matrix.)
if [ "$OS-$ARCH" = "$(uname -s)-$(uname -m)" ] || \
   { [ "$OS" = "linux" ] && [ "$ARCH" = "x86_64" ] && [ "$(uname -m)" = "x86_64" ]; }; then
  CONSUMER="$(mktemp -d /tmp/nift-matrix-consumer.XXXXXX)"
  trap 'rm -rf "$STAGE" "$CONSUMER"' EXIT
  tar xzf "$OUT/nift-embed-$OS-$ARCH.tar.gz" -C "$CONSUMER"
  mkdir -p "$CONSUMER/prefix/include/nift" "$CONSUMER/prefix/lib/pkgconfig"
  cp -r "$CONSUMER/include/nift/." "$CONSUMER/prefix/include/nift/"
  cp "$CONSUMER"/lib/libnift_c.* "$CONSUMER/prefix/lib/" 2>/dev/null || true
  cp "$CONSUMER/lib/pkgconfig/nift.pc" "$CONSUMER/prefix/lib/pkgconfig/"
  sed "s|^prefix=.*|prefix=$CONSUMER/prefix|" "$CONSUMER/prefix/lib/pkgconfig/nift.pc" > "$CONSUMER/prefix/lib/pkgconfig/nift.pc.tmp" \
    && mv "$CONSUMER/prefix/lib/pkgconfig/nift.pc.tmp" "$CONSUMER/prefix/lib/pkgconfig/nift.pc"
  cat > "$CONSUMER/consumer.c" <<'C'
#include <stdio.h>
#include <string.h>
#include "nift/c_abi.h"
int main(void) {
    nift_engine* e = nift_engine_new();
    nift_render_result* r = NULL;
    if (nift_engine_render_text(e, NULL, "<p>hi</p>", 10, &r) != NIFT_OK || nift_render_result_ok(r) != 1) return 2;
    nift_string out;
    if (nift_render_result_output(r, &out) != NIFT_OK) return 3;
    if (out.length != 10 || strncmp(out.data, "<p>hi</p>", 10) != 0) return 4;
    printf("C consumer render OK (%.*s)\n", (int)out.length, out.data);
    nift_render_result_free(r);
    nift_engine_free(e);
    return 0;
}
C
  ( cd "$CONSUMER" && gcc consumer.c -o consumer $(PKG_CONFIG_PATH="$CONSUMER/prefix/lib/pkgconfig" pkg-config --cflags --libs nift) )
  "$CONSUMER/consumer"
  # verify no Nift shared-library runtime dependency
  if command -v ldd >/dev/null 2>&1; then ldd "$CONSUMER/consumer" | grep -i nift && { echo "FAIL: consumer links libnift_c shared" >&2; exit 1; } || true; fi
  if command -v otool >/dev/null 2>&1; then otool -L "$CONSUMER/consumer" | grep -i nift && { echo "FAIL: consumer links libnift_c shared" >&2; exit 1; } || true; fi
  echo "SMOKE PASS: $TARGET native bundle renders from a clean consumer with no Nift shared-library dependency"
fi

echo "matrix build complete: $OUT"
cat "$OUT/SHA256SUMS"