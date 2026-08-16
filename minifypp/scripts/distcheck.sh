#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOP="$(git -C "$ROOT" rev-parse --show-toplevel)"
if [[ "$TOP" != "$ROOT" ]]; then
  echo "Minify++ distcheck must run from the standalone repository" >&2
  exit 2
fi
if ! git -C "$ROOT" diff --quiet || ! git -C "$ROOT" diff --cached --quiet ||
   [[ -n "$(git -C "$ROOT" ls-files --others --exclude-standard)" ]]; then
  echo "Minify++ distcheck requires a clean standalone working tree" >&2
  exit 2
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/minifypp-distcheck.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
git -C "$ROOT" archive HEAD -o "$TMP/source.tar"
mkdir "$TMP/source"
tar -xf "$TMP/source.tar" -C "$TMP/source"

if find "$TMP/source" -type f \( -name '*.o' -o -name '*.obj' -o -name '*.d' -o -name '*.exe' -o -name '*.pdb' \) -print -quit | grep -q . ||
   [[ -e "$TMP/source/minify" || -e "$TMP/source/.build" ]]; then
  echo "Minify++ distcheck found generated build products in the source archive" >&2
  exit 1
fi

make -C "$TMP/source" -j2
make -C "$TMP/source" test
echo "Minify++ clean source-package check passed"
