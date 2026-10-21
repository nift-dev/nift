#!/usr/bin/env bash
# Regression: Nift run() must pass empty-string arguments to the child intact.
# On Windows this exercises quote_win_arg, which previously returned an
# unquoted empty string that Windows command-line parsing dropped entirely
# (so `--status-path ""` reached argparse with no value). On POSIX it is a
# no-op check that the child actually receives the empty argument.
set -euo pipefail
if [ -n "${PYTHON:-}" ]; then
  read -r -a PYTHON_CMD <<< "$PYTHON"
elif command -v python3 >/dev/null 2>&1; then
  PYTHON_CMD=(python3)
elif command -v python >/dev/null 2>&1; then
  PYTHON_CMD=(python)
elif command -v py >/dev/null 2>&1; then
  PYTHON_CMD=(py -3)
else
  PYTHON_CMD=()
  for candidate in /usr/local/bin/python3.[0-9]*; do
    if [ -x "$candidate" ] && [ -f "$candidate" ]; then
      PYTHON_CMD=("$candidate")
      break
    fi
  done
  if [ "${#PYTHON_CMD[@]}" -eq 0 ]; then
    printf '%s\n' "error: Python 3 is required to run this script" >&2
    exit 1
  fi
fi
NIFT="${NIFT:-./nift}"
PY="${PYTHON:-}"
if [ -z "$PY" ]; then PY="${PYTHON_CMD[0]}"; fi
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cat > "$t/empty.f" <<EOF
r := run("$PY", "-c", "import sys; open('args_seen.txt','w').write('yes' if '' in sys.argv else 'no')", "")
print("ran")
EOF
( cd "$t" && "$NIFT" empty.f >/dev/null )
if [ ! -f "$t/args_seen.txt" ] || [ "$(cat "$t/args_seen.txt")" != "yes" ]; then
  echo "FAIL empty-string argument was not passed to the child"
  exit 1
fi
echo "PASS empty-string argument passed to child"
