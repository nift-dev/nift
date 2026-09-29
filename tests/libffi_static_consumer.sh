#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-libffi-static.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
CC_VALUE="${CC:-cc}"

case "$(uname -s)" in
  Darwin) LIBS=(-lc++ -lm -pthread) ;;
  MINGW*|MSYS*|CYGWIN*) LIBS=(-lstdc++ -lm -pthread) ;;
  *) LIBS=(-lstdc++ -lm -pthread -ldl) ;;
esac

"$CC_VALUE" -I"$ROOT/include" "$ROOT/tests/c_abi_smoke.c" \
  "$ROOT/libnift_c.a" "${LIBS[@]}" -o "$TMP/consumer"
"$TMP/consumer"
echo "static archive clean consumer: PASS"
