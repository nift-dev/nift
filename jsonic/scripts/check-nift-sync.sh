#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 1 ]]; then echo "usage: $0 /path/to/nift" >&2; exit 2; fi
root=$(cd "$(dirname "$0")/.." && pwd)
nift=$(cd "$1" && pwd)
files=(
  .gitignore HANDOVER.md LICENSE Makefile README.md ReleaseNotes.md
  include/json.h
  tests/json_smoke.cpp tests/json_adversarial.cpp
  scripts/check-nift-sync.sh scripts/check-minify-sync.sh
  docs/handover/ARCHITECTURE.md docs/handover/DEVELOPMENT.md docs/handover/TESTING.md
  docs/handover/DECISIONS.md docs/handover/ROADMAP.md docs/handover/PROJECT-HISTORY.md
)
for file in "${files[@]}"; do
  cmp -s "$root/$file" "$nift/jsonic/$file" || {
    echo "Jsonic++ sync mismatch: $file" >&2
    exit 1
  }
done
grep -q '#include "../jsonic/include/json.h"' "$nift/src/Json.h" || {
  echo "Nift src/Json.h is not the Jsonic++ compatibility wrapper" >&2
  exit 1
}
echo "Jsonic++ standalone/Nift synchronization passed (${#files[@]} files)"
