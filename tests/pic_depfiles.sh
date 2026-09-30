#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
TARGETS=(.build/pic/src/Parser*.o)
[ -n "${LIBFFI_INCLUDE:-}" ] || { echo "LIBFFI_INCLUDE is required" >&2; exit 1; }
for target in "${TARGETS[@]}"; do
  [ -f "$target" ] || { echo "missing PIC Parser object: $target" >&2; exit 1; }
  [ -f "${target%.o}.d" ] || { echo "missing PIC Parser depfile: ${target%.o}.d" >&2; exit 1; }
done

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
headers=(src/Parser.h src/FfiAbi.h "$LIBFFI_INCLUDE/ffi.h" "$LIBFFI_INCLUDE/ffitarget.h")
sources=(src/ParserTemplate.cpp src/ParserExpression.cpp src/ParserExpression.cpp src/ParserExpression.cpp)
for index in "${!headers[@]}"; do
  header="${headers[$index]}"
  source="${sources[$index]}"
  [ -f "$header" ] || { echo "missing PIC dependency probe header: $header" >&2; exit 1; }
  stamp="$TMP/$(basename "$header").stamp"
  touch -r "$header" "$stamp"
  current_header="$header"
  current_stamp="$stamp"
  touch "$header"
  dry_run="$(make -n "${TARGETS[@]}")"
  touch -r "$stamp" "$header"
  current_header=
  current_stamp=
  printf '%s\n' "$dry_run" | grep -F -- "-c $source" >/dev/null || {
    echo "PIC ${source##*/} did not rebuild after touching $header" >&2
    exit 1
  }
done

echo "PIC depfile header invalidation: PASS"
