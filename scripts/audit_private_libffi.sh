#!/usr/bin/env bash
set -euo pipefail

[ "$#" -gt 0 ] || { echo "usage: audit_private_libffi.sh <shared-artifact>..." >&2; exit 2; }
FFI_RE='(^|[[:space:]])_?ffi_'

require_defined_symbols() {
  local symbols="$1"
  local prefix="$2"
  for symbol in ffi_call ffi_prep_cif ffi_type_void; do
    printf '%s\n' "$symbols" | grep -E "[[:space:]]${prefix}${symbol}([@[:space:]]|$)" >/dev/null || {
      echo "missing private libffi definition $symbol in $artifact" >&2
      exit 1
    }
  done
}

for artifact in "$@"; do
  [ -f "$artifact" ] || { echo "missing private-symbol audit artifact: $artifact" >&2; exit 1; }
  command -v file >/dev/null 2>&1 || { echo "file(1) is required to identify $artifact" >&2; exit 1; }
  format="$(file -b "$artifact")"
  case "$format" in
    *ELF*)
      command -v readelf >/dev/null 2>&1 || { echo "readelf is required for ELF artifact $artifact" >&2; exit 1; }
      dynamic="$(readelf --dyn-syms --wide "$artifact")" || { echo "failed to inspect ELF exports: $artifact" >&2; exit 1; }
      if printf '%s\n' "$dynamic" | grep -E "$FFI_RE" >/dev/null; then
        echo "preemptible or dynamically exported libffi symbol in $artifact" >&2
        printf '%s\n' "$dynamic" | grep -E "$FFI_RE" >&2
        exit 1
      fi
      relocations="$(readelf --relocs --wide "$artifact")" || { echo "failed to inspect ELF relocations: $artifact" >&2; exit 1; }
      if printf '%s\n' "$relocations" | grep -E "$FFI_RE" >/dev/null; then
        echo "dynamic libffi relocation remains preemptible in $artifact" >&2
        exit 1
      fi
      symbols="$(readelf --symbols --wide "$artifact")" || { echo "failed to inspect ELF symbols: $artifact" >&2; exit 1; }
      require_defined_symbols "$symbols" ""
      ;;
    *Mach-O*)
      command -v nm >/dev/null 2>&1 || { echo "nm is required for Mach-O artifact $artifact" >&2; exit 1; }
      command -v otool >/dev/null 2>&1 || { echo "otool is required for Mach-O artifact $artifact" >&2; exit 1; }
      exports="$(nm -gU "$artifact")" || { echo "failed to inspect Mach-O exports: $artifact" >&2; exit 1; }
      undefined="$(nm -u "$artifact")" || { echo "failed to inspect Mach-O imports: $artifact" >&2; exit 1; }
      bindings="$(otool -Iv "$artifact")" || { echo "failed to inspect Mach-O bindings: $artifact" >&2; exit 1; }
      if printf '%s\n%s\n%s\n' "$exports" "$undefined" "$bindings" | grep -E "$FFI_RE" >/dev/null; then
        echo "exported, imported, or dynamically bound libffi symbol in $artifact" >&2
        exit 1
      fi
      symbols="$(nm "$artifact")" || { echo "failed to inspect Mach-O symbols: $artifact" >&2; exit 1; }
      require_defined_symbols "$symbols" "_"
      ;;
    *PE32*)
      command -v objdump >/dev/null 2>&1 || { echo "objdump is required for PE artifact $artifact" >&2; exit 1; }
      command -v nm >/dev/null 2>&1 || { echo "nm is required for PE artifact $artifact" >&2; exit 1; }
      tables="$(objdump -p "$artifact")" || { echo "failed to inspect PE exports/imports: $artifact" >&2; exit 1; }
      if printf '%s\n' "$tables" | grep -E "$FFI_RE" >/dev/null; then
        echo "exported or imported libffi symbol in $artifact" >&2
        exit 1
      fi
      symbols="$(nm "$artifact")" || { echo "failed to inspect PE symbols: $artifact" >&2; exit 1; }
      require_defined_symbols "$symbols" "_?"
      ;;
    *)
      echo "unsupported or unrecognized binary format for $artifact: $format" >&2
      exit 1
      ;;
  esac
  echo "private non-preemptible libffi symbols: $artifact"
done
