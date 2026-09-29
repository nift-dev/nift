#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/nift-private-ffi-audit.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
CC_VALUE="${CC:-cc}"

cat > "$TMP/exported.c" <<'C'
#if defined(_WIN32)
# define EXPORT __declspec(dllexport)
#else
# define EXPORT __attribute__((visibility("default")))
#endif
EXPORT void ffi_call(void) {}
EXPORT void ffi_prep_cif(void) {}
EXPORT int ffi_type_void;
C

case "$(uname -s)" in
  Darwin)
    bad="$TMP/exported.dylib"
    "$CC_VALUE" -dynamiclib "$TMP/exported.c" -o "$bad"
    ;;
  MINGW*|MSYS*|CYGWIN*)
    bad="$TMP/exported.dll"
    "$CC_VALUE" -shared "$TMP/exported.c" -o "$bad"
    ;;
  *)
    bad="$TMP/exported.so"
    "$CC_VALUE" -fPIC -shared "$TMP/exported.c" -o "$bad"
    ;;
esac

if bash "$ROOT/scripts/audit_private_libffi.sh" "$bad" >"$TMP/out" 2>"$TMP/err"; then
  echo "private libffi audit accepted exported symbols" >&2
  exit 1
fi
grep -Eq 'exported|preemptible' "$TMP/err"
echo "private libffi audit fail-closed self-test: PASS"
