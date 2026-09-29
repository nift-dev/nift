#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
TARGET=.build/pic/src/Parser.o
DEPFILE=.build/pic/src/Parser.d
[ -n "${LIBFFI_INCLUDE:-}" ] || { echo "LIBFFI_INCLUDE is required" >&2; exit 1; }
[ -f "$TARGET" ] || { echo "missing PIC Parser object: $TARGET" >&2; exit 1; }
[ -f "$DEPFILE" ] || { echo "missing PIC Parser depfile: $DEPFILE" >&2; exit 1; }

TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-pic-deps.XXXXXX")"
current_header=
current_stamp=
cleanup() {
  if [ -n "$current_header" ] && [ -n "$current_stamp" ]; then
    touch -r "$current_stamp" "$current_header"
  fi
  rm -rf "$TMP"
}
trap cleanup EXIT
headers=(src/FfiAbi.h "$LIBFFI_INCLUDE/ffi.h" "$LIBFFI_INCLUDE/ffitarget.h")
for header in "${headers[@]}"; do
  [ -f "$header" ] || { echo "missing PIC dependency probe header: $header" >&2; exit 1; }
  stamp="$TMP/$(basename "$header").stamp"
  touch -r "$header" "$stamp"
  current_header="$header"
  current_stamp="$stamp"
  touch "$header"
  dry_run="$(make -n "$TARGET")"
  touch -r "$stamp" "$header"
  current_header=
  current_stamp=
  printf '%s\n' "$dry_run" | grep -F -- '-c src/Parser.cpp' >/dev/null || {
    echo "PIC Parser did not rebuild after touching $header" >&2
    exit 1
  }
done

echo "PIC depfile header invalidation: PASS"
