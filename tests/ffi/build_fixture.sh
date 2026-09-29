#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_DIR="${1:-$ROOT/.build}"
BASE_NAME="${2:-libnift_ffi_fixture}"
CC_VALUE="${CC:-cc}"
mkdir -p "$OUT_DIR"

case "$(uname -s)" in
  Darwin)
    OUTPUT="$OUT_DIR/$BASE_NAME.dylib"
    "$CC_VALUE" -std=c99 -Wall -Wextra -Werror -fPIC -dynamiclib \
      "$ROOT/tests/ffi/fixture.c" -o "$OUTPUT"
    ;;
  MINGW*|MSYS*|CYGWIN*)
    OUTPUT="$OUT_DIR/$BASE_NAME.dll"
    "$CC_VALUE" -std=c99 -Wall -Wextra -Werror -shared \
      "$ROOT/tests/ffi/fixture.c" -o "$OUTPUT"
    ;;
  *)
    OUTPUT="$OUT_DIR/$BASE_NAME.so"
    "$CC_VALUE" -std=c99 -Wall -Wextra -Werror -fPIC -shared \
      "$ROOT/tests/ffi/fixture.c" -o "$OUTPUT"
    ;;
esac

printf '%s\n' "$OUTPUT"
