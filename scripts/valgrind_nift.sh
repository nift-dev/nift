#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
command -v valgrind >/dev/null 2>&1 || { echo "error: valgrind not found" >&2; exit 2; }
exec valgrind --leak-check=full --show-leak-kinds=all \
  --errors-for-leak-kinds=definite,indirect,possible --track-origins=yes \
  --error-exitcode=99 "$ROOT/nift" "$@"
