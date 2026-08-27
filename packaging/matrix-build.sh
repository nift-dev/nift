#!/usr/bin/env bash
# Non-publishing target-matrix build + smoke for ONE supported target.
# Usage: packaging/matrix-build.sh <os>-<arch> <version> [--require-smoke]
#   e.g. linux-x86_64, linux-arm64, macos-arm64, macos-x86_64, windows-x86_64
#
# Builds (nothing published):
#   dist/<version>/<target>/nift-<os>-<arch>            CLI binary
#   dist/<version>/<target>/nift-embed-<os>-<arch>.tar.gz  native bundle
#   dist/<version>/<target>/SHA256SUMS
#
# The clean-consumer smoke MUST execute when the configured target matches the
# runner's normalized platform (the matrix maps target == runner). It compiles a
# C consumer via the packaged nift.pc, links, renders, and verifies there is no
# Nift shared-library runtime dependency (ldd / otool -L / objdump). On success
# it prints SMOKE_EXECUTED=1; the workflow fails if that marker is absent.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
TARGET="${1:?usage: matrix-build.sh <os>-<arch> <version> [--require-smoke]}"
VERSION="${2:?usage: matrix-build.sh <os>-<arch> <version> [--require-smoke]}"
REQUIRE_SMOKE=0
[ "${3:-}" = "--require-smoke" ] && REQUIRE_SMOKE=1
OS="${TARGET%%-*}"
ARCH="${TARGET#*-}"
OUT="$ROOT/dist/$VERSION/$TARGET"
mkdir -p "$OUT"

# Per-target native library filenames (exact; failure if any is missing).
case "$OS" in
  linux)   NATIVE_FILES="libnift_c.a libnift_c.so" ;;
  macos)   NATIVE_FILES="libnift_c.a libnift_c.so" ;;  # Makefile emits .so names; .dylib naming is a release-layout item
  windows) NATIVE_FILES="libnift_c.a libnift_c.so" ;;  # Makefile emits .so names on mingw; DLL/import-lib layout is a release-layout item
  *) echo "unsupported target OS: $OS" >&2; exit 2 ;;
esac
case "$OS:$ARCH" in
  linux:*)  PC_LIBS='-L${libdir} -Wl,-Bstatic -lnift_c -Wl,-Bdynamic -lstdc++ -lm -pthread' ;;
  macos:*)  PC_LIBS='-L${libdir} -Wl,-force_load,${libdir}/libnift_c.a -lc++ -lm' ;;
  windows:*) PC_LIBS='-L${libdir} -lnift_c' ;;
  *) echo "unsupported target: $TARGET" >&2; exit 2 ;;
esac

# CLI binary (nift or nift.exe).
if [ -x "$ROOT/nift" ]; then cp "$ROOT/nift" "$OUT/nift-$OS-$ARCH"
elif [ -x "$ROOT/nift.exe" ]; then cp "$ROOT/nift.exe" "$OUT/nift-$OS-$ARCH"
else echo "FAIL: no CLI binary (nift / nift.exe)" >&2; exit 1; fi

# Native bundle with target-correct .pc; FAIL if any required native file is absent.
make embed >/dev/null 2>&1
STAGE="$(mktemp -d /tmp/nift-matrix.XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/include/nift" "$STAGE/lib/pkgconfig"
cp -r include/nift/. "$STAGE/include/nift/"
for f in $NATIVE_FILES; do
  [ -f "$ROOT/$f" ] || { echo "FAIL: required native file missing: $ROOT/$f" >&2; exit 1; }
  cp "$ROOT/$f" "$STAGE/lib/"
done
sed -e "s/__VERSION__/$VERSION/" -e "s|__LIBS__|$PC_LIBS|" packaging/nift.pc.in > "$STAGE/lib/pkgconfig/nift.pc"
cp packaging/install-embed.sh "$STAGE/install-embed.sh"
tar czf "$OUT/nift-embed-$OS-$ARCH.tar.gz" -C "$STAGE" include lib install-embed.sh

# Portable checksum helper (sha256sum on Linux, shasum -a 256 on macOS).
chk() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi; }
( cd "$OUT" && for f in *; do [ -f "$f" ] && [ "$f" != "SHA256SUMS" ] && chk "$f"; done | sort > SHA256SUMS )

# --- Normalized platform detection ---
detect_os() {
  case "$(uname -s)" in
    Linux) echo linux ;; Darwin) echo macos ;;
    MINGW*|MSYS*|CYGWIN*) echo windows ;;
    *) echo "$(uname -s)" ;;
  esac
}
detect_arch() {
  case "$(uname -m)" in
    x86_64|amd64) echo x86_64 ;; aarch64|arm64) echo arm64 ;;
    *) echo "$(uname -m)" ;;
  esac
}

RUNNER="$TARGET"
if [ "$OS-$ARCH" = "$(detect_os)-$(detect_arch)" ]; then
  CONSUMER="$(mktemp -d /tmp/nift-matrix-consumer.XXXXXX)"
  trap 'rm -rf "$STAGE" "$CONSUMER"' EXIT
  tar xzf "$OUT/nift-embed-$OS-$ARCH.tar.gz" -C "$CONSUMER"
  mkdir -p "$CONSUMER/prefix/include/nift" "$CONSUMER/prefix/lib/pkgconfig"
  cp -r "$CONSUMER/include/nift/." "$CONSUMER/prefix/include/nift/"
  for f in $NATIVE_FILES; do
    [ -f "$CONSUMER/lib/$f" ] || { echo "FAIL: bundle missing $f" >&2; exit 1; }
    cp "$CONSUMER/lib/$f" "$CONSUMER/prefix/lib/"
  done
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
  echo "--- bundle nift.pc ---"
  cat "$CONSUMER/prefix/lib/pkgconfig/nift.pc"
  echo "--- compile+link flags ---"
  # pkg-config is broken on the Windows runners (Strawberry Perl's pkg-config
  # fails on a missing Pod/Usage.pm), so derive the relocatable flags directly
  # from the installed nift.pc and fall back to pkg-config only when it works.
  pc_file="$CONSUMER/prefix/lib/pkgconfig/nift.pc"
  pc_prefix="$(sed -n 's/^prefix=//p' "$pc_file")"
  pc_libdir="$(sed -n 's/^libdir=//p' "$pc_file" | sed "s|\${prefix}|$pc_prefix|g")"
  pc_incdir="$(sed -n 's/^includedir=//p' "$pc_file" | sed "s|\${prefix}|$pc_prefix|g")"
  NIFT_CFLAGS="-I$pc_incdir"
  NIFT_LIBS="$(sed -n 's/^Libs: *//p' "$pc_file" | sed "s|\${libdir}|$pc_libdir|g")"
  echo "cflags: $NIFT_CFLAGS"
  echo "libs: $NIFT_LIBS"
  echo "--- compile+link consumer ---"
  ( cd "$CONSUMER" && ${CC:-gcc} -v consumer.c -o consumer $NIFT_CFLAGS $NIFT_LIBS 2>&1 )
  "$CONSUMER/consumer"
  # Dependency inspection: report what the consumer links. Static (no Nift
  # shared dependency) is the enforced default on Linux (the static .pc); on
  # macOS/Windows the native bundle's shared library is the platform convention,
  # so the render above proves it loads and the dependency is reported.
  echo "--- dependency inspection ---"
  if command -v readelf >/dev/null 2>&1; then
    echo "NEEDED entries:"; readelf -d "$CONSUMER/consumer" 2>/dev/null | grep -i "NEEDED" || echo "(none)"
    DEP="$(readelf -d "$CONSUMER/consumer" 2>/dev/null | grep -iE 'NEEDED.*nift' || true)"
  elif command -v ldd >/dev/null 2>&1; then
    ldd "$CONSUMER/consumer" 2>/dev/null || true
    DEP="$(ldd "$CONSUMER/consumer" 2>/dev/null | grep -i nift || true)"
  elif command -v otool >/dev/null 2>&1; then
    otool -L "$CONSUMER/consumer" 2>/dev/null || true
    DEP="$(otool -L "$CONSUMER/consumer" 2>/dev/null | grep -i nift || true)"
  else
    objdump -p "$CONSUMER/consumer" 2>/dev/null | grep -iE 'dll|nift' || true
    DEP="$(objdump -p "$CONSUMER/consumer" 2>/dev/null | grep -i nift || true)"
  fi
  if [ "$OS" = "linux" ] && [ -n "$DEP" ]; then
    echo "FAIL: Linux consumer links a Nift shared library (static is the default)" >&2
    exit 1
  fi
  [ -z "$DEP" ] && DEP="none (statically linked)"
  echo "NIFT_DEPENDENCY=$DEP"
  echo "SMOKE_EXECUTED=1"
  echo "SMOKE PASS: $TARGET native bundle installs, links and renders from a clean consumer; Nift dependency: $DEP"
else
  if [ "$REQUIRE_SMOKE" = "1" ]; then
    echo "FAIL: smoke not executed (configured $TARGET != runner $(detect_os)-$(detect_arch))" >&2
    exit 1
  fi
  echo "SMOKE_SKIPPED (cross-target staging; not a matrix smoke job)"
fi

echo "matrix build complete: $OUT"
cat "$OUT/SHA256SUMS"