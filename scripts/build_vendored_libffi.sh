#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:?usage: build_vendored_libffi.sh <build-directory>}"
if [ -n "${PYTHON:-}" ]; then
  read -r -a PYTHON_CMD <<< "$PYTHON"
elif command -v python3 >/dev/null 2>&1; then
  PYTHON_CMD=(python3)
elif command -v python >/dev/null 2>&1; then
  PYTHON_CMD=(python)
elif command -v py >/dev/null 2>&1; then
  PYTHON_CMD=(py -3)
else
  printf '%s\n' "error: Python 3 is required to build vendored libffi" >&2
  exit 1
fi
SOURCE_DIGEST="$("${PYTHON_CMD[@]}" "$ROOT/scripts/check_vendored_libffi.py" --print-digest "$ROOT/third_party/libffi")"
BUILD_LOGIC_DIGEST="$("${PYTHON_CMD[@]}" - "$ROOT/scripts/build_vendored_libffi.sh" "$ROOT/scripts/check_vendored_libffi.py" <<'PY'
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
FINGERPRINT="$("${PYTHON_CMD[@]}" - "$SOURCE_DIGEST" "$BUILD_LOGIC_DIGEST" "$CC_VALUE" "$CXX_VALUE" "$CFLAGS" "${CPPFLAGS:-}" "${LDFLAGS:-}" "$CONFIGURE_ARGS" <<'PY'
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

# The cache is unusable (first build, fingerprint/toolchain change, or an
# interrupted previous build that left partial autoconf artifacts without a
# usable Makefile). Always start from a clean slate so stale configure state
# can never poison the rebuild; libffi itself only takes a few seconds cold.
rm -rf "$BUILD"
mkdir -p "$BUILD"
BUILD="$(cd "$BUILD" && pwd)"
PREFIX="$BUILD/install"
LOG="$BUILD/.build.log"
START_TIME="$({ date +%s%N 2>/dev/null || date +%s; } || echo 0)"
echo "BUILD vendored libffi ($(basename "$BUILD"))"
fail_build() {
  tail -n 40 "$LOG" >&2
  echo "error: vendored libffi build failed; full log: $LOG" >&2
  exit 1
}
cp -R "$ROOT/third_party/libffi" "$BUILD/source"
if ! ( cd "$BUILD" && CONFIG_SHELL=sh SHELL=sh ./source/configure $CONFIGURE_ARGS \
        --prefix="$PREFIX" ) >"$LOG" 2>&1; then
  fail_build
fi

# libffi clears MAKEOVERRIDES, so carry the relative libtool command through both recursive levels.
LIBFFI_MAKE_FLAGS="LIBTOOL='sh ./libtool' AM_MAKEFLAGS=\"LIBTOOL='sh ./libtool'\""
if ! make -C "$BUILD" SHELL=sh "AM_MAKEFLAGS=$LIBFFI_MAKE_FLAGS" -j"${NIFT_BUILD_JOBS:-2}" >"$LOG" 2>&1; then
  fail_build
fi
if ! make -C "$BUILD" SHELL=sh "AM_MAKEFLAGS=$LIBFFI_MAKE_FLAGS" install >>"$LOG" 2>&1; then
  fail_build
fi
printf '%s\n' "$FINGERPRINT" > "$BUILD/.nift-fingerprint"
if [ ! -f "$BUILD/.nift-built" ]; then
  touch "$BUILD/.nift-built"
fi
if [ "$START_TIME" != "0" ]; then
  END_TIME="$({ date +%s%N 2>/dev/null || date +%s; } || echo 0)"
  printf 'BUILD vendored libffi: done (%ss)\n' "$(( (END_TIME - START_TIME) / 1000000000 ))"
else
  echo "BUILD vendored libffi: done"
fi
