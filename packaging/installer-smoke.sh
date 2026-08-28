#!/usr/bin/env bash
# Build-only end-to-end installer smoke for ONE target, served from a local HTTP
# server (never depends on an unpublished GitHub release).
# Usage: packaging/installer-smoke.sh <os>-<arch> <bundle-tgz>
#
# Exercises the real install-embed.sh path: host detection, curl download,
# bundle-name selection, SHA256SUMS retrieval/parsing, sha256sum vs
# shasum -a 256 (macOS), installation into a fresh PREFIX, installed nift.pc,
# and compilation + rendering from that prefix. Ends with a negative checksum
# test that MUST fail and must NOT install anything.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
TARGET="${1:?usage: installer-smoke.sh <os>-<arch> <bundle-tgz>}"
BUNDLE="${2:?usage: installer-smoke.sh <os>-<arch> <bundle-tgz>}"
OS="${TARGET%%-*}"
ARCH="${TARGET#*-}"
[ -f "$BUNDLE" ] || { echo "FAIL: bundle not found: $BUNDLE" >&2; exit 1; }

chk() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi; }
HAS_SHA256SUM=0; command -v sha256sum >/dev/null 2>&1 && HAS_SHA256SUM=1

SERVE="$(mktemp -d /tmp/nift-serve.XXXXXX)"
PREFIX="$(mktemp -d /tmp/nift-prefix.XXXXXX)"
CONSUMER="$(mktemp -d /tmp/nift-instcons.XXXXXX)"
PORT="$(( 22000 + (RANDOM % 10000) ))"
SERVER_PID=
cleanup() { [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null || true; rm -rf "$SERVE" "$PREFIX" "$CONSUMER"; }
trap cleanup EXIT

tarball="nift-embed-$OS-$ARCH.tar.gz"
cp "$BUNDLE" "$SERVE/$tarball"
( cd "$SERVE" && chk "$tarball" > SHA256SUMS )

python3 -m http.server "$PORT" -d "$SERVE" --bind 127.0.0.1 >/dev/null 2>&1 &
SERVER_PID=$!
for i in $(seq 1 50); do curl -fsS "http://127.0.0.1:$PORT/SHA256SUMS" >/dev/null 2>&1 && break; sleep 0.2; done
BASE="http://127.0.0.1:$PORT"

echo "=== installer smoke: positive (fresh prefix; real download + checksum) ==="
# On macOS, demonstrate the shasum -a 256 fallback by hiding sha256sum from
# PATH: install-embed.sh must take its else branch and still verify the bundle.
INSTALL_PATH="$PATH"
if [ "$OS" = "macos" ] && [ "$HAS_SHA256SUM" -eq 1 ]; then
  SHATOOL_DIR="$(dirname "$(command -v sha256sum)")"
  INSTALL_PATH="$(printf '%s' "$PATH" | tr ':' '\n' | grep -vx "$SHATOOL_DIR" | paste -sd: -)"
  echo "macOS installer run: sha256sum hidden (dropped ${SHATOOL_DIR}) -> shasum -a 256 fallback exercised"
else
  [ "$OS" = "macos" ] && echo "macOS installer run: sha256sum absent -> shasum -a 256 used"
fi
INSTALL_OUT="$(NIFT_EMBED_BASE="$BASE" PREFIX="$PREFIX" PATH="$INSTALL_PATH" bash packaging/install-embed.sh)"
echo "$INSTALL_OUT"
for f in include/nift/c_abi.h lib/libnift_c.a lib/pkgconfig/nift.pc; do
  [ -f "$PREFIX/$f" ] || { echo "FAIL: installer did not install $PREFIX/$f" >&2; exit 1; }
done
case "$OS" in
  linux) [ -f "$PREFIX/lib/libnift_c.so" ] || { echo "FAIL: installer missing libnift_c.so" >&2; exit 1; } ;;
  macos) [ -f "$PREFIX/lib/libnift_c.dylib" ] || { echo "FAIL: installer missing libnift_c.dylib" >&2; exit 1; } ;;
  *) echo "FAIL: installer smoke unsupported OS $OS" >&2; exit 2 ;;
esac

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
    printf("installer consumer render OK (%.*s)\n", (int)out.length, out.data);
    nift_render_result_free(r);
    nift_engine_free(e);
    return 0;
}
C
PC="$PREFIX/lib/pkgconfig/nift.pc"
pc_dir="$(dirname "$PC")"
pc_prefix="$(sed -n 's/^prefix=//p' "$PC" | sed "s|\${pcfiledir}|$pc_dir|g")"
pc_libdir="$(sed -n 's/^libdir=//p' "$PC" | sed "s|\${pcfiledir}|$pc_dir|g" | sed "s|\${prefix}|$pc_prefix|g")"
pc_incdir="$(sed -n 's/^includedir=//p' "$PC" | sed "s|\${pcfiledir}|$pc_dir|g" | sed "s|\${prefix}|$pc_prefix|g")"
NIFT_CFLAGS="-I$pc_incdir"
NIFT_LIBS="$(sed -n 's/^Libs: *//p' "$PC" | sed "s|\${libdir}|$pc_libdir|g")"
echo "--- compile from installed prefix (flags derived from installed nift.pc) ---"
echo "cflags: $NIFT_CFLAGS"
echo "libs: $NIFT_LIBS"
( cd "$CONSUMER" && ${CC:-gcc} consumer.c -o consumer $NIFT_CFLAGS $NIFT_LIBS )
"$CONSUMER/consumer"

echo "=== installer smoke: negative checksum (must fail; nothing installed) ==="
BADPREFIX="$(mktemp -d /tmp/nift-badprefix.XXXXXX)"
( cd "$SERVE" && chk "$tarball" | sed 's/^../00/' > SHA256SUMS )
if NIFT_EMBED_BASE="$BASE" PREFIX="$BADPREFIX" bash packaging/install-embed.sh >/dev/null 2>&1; then
  echo "FAIL: installer accepted a bad checksum" >&2; exit 1
fi
if [ -f "$BADPREFIX/lib/libnift_c.a" ] || [ -f "$BADPREFIX/include/nift/c_abi.h" ] || [ -f "$BADPREFIX/lib/pkgconfig/nift.pc" ]; then
  echo "FAIL: negative checksum test installed files into $BADPREFIX" >&2; exit 1
fi
echo "installer smoke PASS: negative checksum rejected, nothing installed"
echo "INSTALLER_SMOKE_EXECUTED=1"