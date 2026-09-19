#!/usr/bin/env bash
set -euo pipefail
NIFT="${NIFT:-./nift}"
SQLITE_PACKAGE="${SQLITE_PACKAGE:-../sqlite}"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/site/.nift"
(cd "$tmp/site" && "$OLDPWD/$NIFT" add "$OLDPWD/$SQLITE_PACKAGE" >/dev/null)
cat > "$tmp/site/test.f" <<'NIFT'
@import("sqlite")
db := sqlite_open("test.db")
print(db.path)
print(sqlite_available())
NIFT
out=$(cd "$tmp/site" && "$OLDPWD/$NIFT" run test.f)
grep -q '^test.db$' <<<"$out"
# Availability may be true or false depending on host; it must be a bool.
tail -1 <<<"$out" | grep -Eq '^(true|false)$'
echo 'PASS sqlite package dogfood'
