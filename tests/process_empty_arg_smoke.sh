#!/usr/bin/env bash
# Regression: Nift run() must pass empty-string arguments to the child intact.
# On Windows this exercises quote_win_arg, which previously returned an
# unquoted empty string that Windows command-line parsing dropped entirely
# (so `--status-path ""` reached argparse with no value). On POSIX it is a
# no-op check that the child actually receives the empty argument.
set -euo pipefail
NIFT="${NIFT:-./nift}"
PY="${PYTHON:-}"
if [ -z "$PY" ]; then PY="$(command -v python3 || command -v python || echo python)"; fi
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