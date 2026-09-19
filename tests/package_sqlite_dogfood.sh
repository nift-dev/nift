#!/usr/bin/env bash
set -euo pipefail
NIFT="${NIFT:-./nift}"
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
SQLITE_PACKAGE="${SQLITE_PACKAGE:-../sqlite}"
case "$SQLITE_PACKAGE" in /*) PKG_ABS="$SQLITE_PACKAGE";; *) PKG_ABS="$(pwd)/$SQLITE_PACKAGE";; esac
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/site/.nift"
(cd "$tmp/site" && "$NIFT_ABS" add "$PKG_ABS" >/dev/null)
cat > "$tmp/site/test.f" <<'NIFT'
@import("sqlite")
db := sqlite_open("test.db")
print(db.path)
print(sqlite_available())
NIFT
out=$(cd "$tmp/site" && "$NIFT_ABS" run test.f)
grep -q '^test.db$' <<<"$out"
# Availability may be true or false depending on host; it must be a bool.
tail -1 <<<"$out" | grep -Eq '^(true|false)$'
if command -v sqlite3 >/dev/null 2>&1; then
  cat > "$tmp/site/full.f" <<'NIFT'
@import("sqlite")
db := sqlite_open("full.db")
print(sqlite_exec(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT, n REAL)").ok)
print(sqlite_exec(db, "INSERT INTO t(name, n) VALUES(?, ?)", "hello", 3.14).ok)
print(sqlite_exec(db, "INSERT INTO t(name, n) VALUES(?, ?)", "O'Brien", 2.5).ok)
r := sqlite_query(db, "SELECT * FROM t ORDER BY id")
print(r.ok)
print(r.rows.size())
print(r.rows[0].name)
print(r.rows[1].name)
print(r.rows[0].n)
print(sqlite_query(db, "SELECT * FROM t WHERE id = ?", 999).rows.size())
tx := sqlite_transaction(db, ["INSERT INTO t(name) VALUES('tx')", "INSERT INTO nope VALUES(1)"])
print(tx.ok)
print(sqlite_query(db, "SELECT COUNT(*) AS c FROM t").rows[0].c)
NIFT
  fout=$(cd "$tmp/site" && "$NIFT_ABS" run full.f)
  expected=$'true\ntrue\ntrue\ntrue\n2\nhello\nO\x27Brien\n3.14\n0\nfalse\n2'
  [ "$fout" = "$expected" ] || { printf 'unexpected sqlite output:\n%s\nexpected:\n%s\n' "$fout" "$expected" >&2; exit 1; }
fi
echo 'PASS sqlite package dogfood'