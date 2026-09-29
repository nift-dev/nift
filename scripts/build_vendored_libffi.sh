#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:?usage: build_vendored_libffi.sh <build-directory>}"
SOURCE_DIGEST="$(python3 "$ROOT/scripts/check_vendored_libffi.py" --print-digest "$ROOT/third_party/libffi")"
BUILD_LOGIC_DIGEST="$(python3 - "$ROOT/scripts/build_vendored_libffi.sh" "$ROOT/scripts/check_vendored_libffi.py" <<'PY'
import hashlib
import pathlib
import sys

digest = hashlib.sha256()
for name in sys.argv[1:]:
    digest.update(pathlib.Path(name).read_bytes())
    digest.update(b"\0")
print(digest.hexdigest())
PY
)"
CC_VALUE="${CC:-cc}"
CXX_VALUE="${CXX:-c++}"
BASE_CFLAGS="${CFLAGS:-}"
# Shared Nift artifacts consume this static archive. Hidden definitions keep
# libffi private while preserving ordinary static archive resolution.
CFLAGS="$BASE_CFLAGS -fvisibility=hidden"
export CFLAGS
CONFIGURE_ARGS="--disable-shared --enable-static --with-pic --disable-docs --disable-multi-os-directory"
FINGERPRINT="$(python3 - "$SOURCE_DIGEST" "$BUILD_LOGIC_DIGEST" "$CC_VALUE" "$CXX_VALUE" "$CFLAGS" "${CPPFLAGS:-}" "${LDFLAGS:-}" "$CONFIGURE_ARGS" <<'PY'
import hashlib
import os
import subprocess
import sys

values = sys.argv[1:]
for compiler in values[2:4]:
    path = subprocess.check_output(["sh", "-c", "command -v \"$1\"", "sh", compiler], text=True).strip()
    version = subprocess.check_output([compiler, "--version"], text=True, stderr=subprocess.STDOUT)
    values.extend((path, version))
digest = hashlib.sha256()
for value in values:
    digest.update(value.encode())
    digest.update(b"\0")
print(digest.hexdigest())
PY
)"

if [ -f "$BUILD/.nift-fingerprint" ] && [ "$(cat "$BUILD/.nift-fingerprint")" = "$FINGERPRINT" ] && \
   [ -f "$BUILD/install/include/ffi.h" ] && [ -f "$BUILD/install/include/ffitarget.h" ] && \
   [ -f "$BUILD/install/lib/libffi.a" ]; then
  exit 0
fi

RECONFIGURED=0
if [ -f "$BUILD/Makefile" ] && { [ ! -f "$BUILD/.nift-fingerprint" ] || [ "$(cat "$BUILD/.nift-fingerprint")" != "$FINGERPRINT" ]; }; then
  rm -rf "$BUILD"
  RECONFIGURED=1
fi
mkdir -p "$BUILD"
BUILD="$(cd "$BUILD" && pwd)"
PREFIX="$BUILD/install"
if [ ! -f "$BUILD/Makefile" ]; then
  RECONFIGURED=1
  rm -rf "$BUILD/source"
  cp -R "$ROOT/third_party/libffi" "$BUILD/source"
  (
    cd "$BUILD"
    ./source/configure $CONFIGURE_ARGS \
      --prefix="$PREFIX"
  )
fi
make -C "$BUILD" -j"${NIFT_BUILD_JOBS:-2}"
make -C "$BUILD" install
printf '%s\n' "$FINGERPRINT" > "$BUILD/.nift-fingerprint"
if [ "$RECONFIGURED" -eq 1 ] || [ ! -f "$BUILD/.nift-built" ]; then
  touch "$BUILD/.nift-built"
fi
