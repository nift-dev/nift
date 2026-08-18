#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ $# -ne 1 ]]; then
  echo "usage: $0 /path/to/nift/minifypp" >&2
  exit 2
fi
EMBEDDED="$(cd "$1" 2>/dev/null && pwd)" || {
  echo "Minify++ sync check: embedded directory does not exist: $1" >&2
  exit 2
}

FILES=(
  benchmarks/minify_benchmark.cpp
  LICENSE
  Makefile
  README.md
  ReleaseNotes.md
  cli/main.cpp
  docs/MEMORY-SAFETY.md
  include/minify/Minify.h
  scripts/check-nift-sync.sh
  scripts/distcheck.sh
  scripts/memory_safety.py
  src/Json.h
  src/Minify.cpp
  tests/cli_smoke.sh
  tests/cross_format_adversarial.sh
  tests/fuzz_smoke.cpp
  tests/memory_lifetime.cpp
  tests/memory_cli_stress.sh
  tests/minify_format_idempotence.sh
  tests/minify_css_postcss_semantics.sh
  tests/minify_generated_semantics.sh
  tests/minify_jsx_generated.sh
  tests/minify_node_semantics.sh
  tests/minify_smoke.cpp
)

failed=0
for file in "${FILES[@]}"; do
  if [[ ! -f "$ROOT/$file" ]]; then
    echo "Minify++ sync check: standalone file missing: $file" >&2
    failed=1
  elif [[ ! -f "$EMBEDDED/$file" ]]; then
    echo "Minify++ sync check: embedded file missing: $file" >&2
    failed=1
  elif ! cmp -s "$ROOT/$file" "$EMBEDDED/$file"; then
    echo "Minify++ sync check: files differ: $file" >&2
    failed=1
  fi
done

if [[ $failed -ne 0 ]]; then
  exit 1
fi
printf 'Minify++ standalone/Nift synchronization passed (%d files)\n' "${#FILES[@]}"
