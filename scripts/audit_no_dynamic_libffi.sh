#!/usr/bin/env bash
set -euo pipefail

[ "$#" -gt 0 ] || { echo "usage: audit_no_dynamic_libffi.sh <binary>..." >&2; exit 2; }
for artifact in "$@"; do
  [ -f "$artifact" ] || { echo "missing audit artifact: $artifact" >&2; exit 1; }
  command -v file >/dev/null 2>&1 || { echo "file(1) is required to identify $artifact" >&2; exit 1; }
  format="$(file -b "$artifact")"
  case "$format" in
    *"current ar archive"*)
      echo "static archive is not a dynamic-dependency audit input: $artifact" >&2
      exit 1
      ;;
    *ELF*)
      command -v readelf >/dev/null 2>&1 || { echo "readelf is required for ELF artifact $artifact" >&2; exit 1; }
      readelf -h "$artifact" >/dev/null 2>&1 || { echo "invalid ELF artifact: $artifact" >&2; exit 1; }
      deps="$(readelf -d "$artifact")" || { echo "failed to inspect ELF dependencies: $artifact" >&2; exit 1; }
      ;;
    *Mach-O*)
      command -v otool >/dev/null 2>&1 || { echo "otool is required for Mach-O artifact $artifact" >&2; exit 1; }
      otool -hv "$artifact" >/dev/null 2>&1 || { echo "invalid Mach-O artifact: $artifact" >&2; exit 1; }
      deps="$(otool -L "$artifact")" || { echo "failed to inspect Mach-O dependencies: $artifact" >&2; exit 1; }
      ;;
    *PE32*)
      command -v objdump >/dev/null 2>&1 || { echo "objdump is required for PE artifact $artifact" >&2; exit 1; }
      objdump -f "$artifact" >/dev/null 2>&1 || { echo "invalid PE artifact: $artifact" >&2; exit 1; }
      deps="$(objdump -p "$artifact")" || { echo "failed to inspect PE dependencies: $artifact" >&2; exit 1; }
      ;;
    *)
      echo "unsupported or unrecognized binary format for $artifact: $format" >&2
      exit 1
      ;;
  esac
  if printf '%s\n' "$deps" | grep -iE '(libffi[^/ ]*\.(so|dylib)|ffi[^/ ]*\.dll)' >/dev/null; then
    echo "dynamic libffi dependency detected in $artifact" >&2
    printf '%s\n' "$deps" >&2
    exit 1
  fi
  echo "no dynamic libffi dependency: $artifact"
done
