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
print(sqlite.available())
db := sqlite.open("test.db")
print(db.path)
NIFT
out=$(cd "$tmp/site" && "$NIFT_ABS" run test.f)
grep -q '^true\|^false$' <<<"$(head -1 <<<"$out")"
grep -q '^test.db$' <<<"$out"
# Private helpers must not be visible to the importer.
cat > "$tmp/site/priv.f" <<'NIFT'
@import("sqlite")
print(sqlite_cli_query)
NIFT
if (cd "$tmp/site" && "$NIFT_ABS" run priv.f >/dev/null 2>&1); then echo "private helper leaked" >&2; exit 1; fi
if command -v sqlite3 >/dev/null 2>&1; then
  cat > "$tmp/site/full.f" <<'NIFT'
@import("sqlite")
db := sqlite.open("full.db")
print(sqlite.exec(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT, n REAL)").ok)
print(sqlite.exec(db, "INSERT INTO t(name, n) VALUES(?, ?)", "hello", 3.14).ok)
print(sqlite.exec(db, "INSERT INTO t(name, n) VALUES(?, ?)", "O'Brien", 2.5).ok)
r := sqlite.query(db, "SELECT * FROM t ORDER BY id")
print(r.ok)
print(r.rows.size())
print(r.rows[0].name)
print(r.rows[1].name)
print(r.rows[0].n)
print(sqlite.query(db, "SELECT * FROM t WHERE id = ?", 999).rows.size())
tx := sqlite.transaction(db, ["INSERT INTO t(name) VALUES('tx')", "INSERT INTO nope VALUES(1)"])
print(tx.ok)
print(sqlite.query(db, "SELECT COUNT(*) AS c FROM t").rows[0].c)
NIFT
  fout=$(cd "$tmp/site" && "$NIFT_ABS" run full.f)
  expected=$'true\ntrue\ntrue\ntrue\n2\nhello\nO\x27Brien\n3.14\n0\nfalse\n2'
  [ "$fout" = "$expected" ] || { printf 'unexpected sqlite output:\n%s\nexpected:\n%s\n' "$fout" "$expected" >&2; exit 1; }
  # Unicode database path and data, plus no leaked temp files.
  cat > "$tmp/site/uni.f" <<'NIFT'
@import("sqlite")
db := sqlite.open("wéird ü.db")
print(sqlite.exec(db, "CREATE TABLE t(v TEXT)").ok)
print(sqlite.exec(db, "INSERT INTO t VALUES(?)", "héllo").ok)
print(sqlite.exec(db, "INSERT INTO t VALUES(?)", null).ok)
r := sqlite.query(db, "SELECT * FROM t")
print(r.rows.size())
print(r.rows[0].v)
print(r.rows[1].v)
NIFT
  uout=$(cd "$tmp/site" && "$NIFT_ABS" run uni.f)
  [ "$uout" = $'true\ntrue\ntrue\n2\nh\xc3\xa9llo\nnull' ] || { printf 'unexpected unicode sqlite output:\n%s\n' "$uout" >&2; exit 1; }
  rm -f "$tmp/site/wéird ü.db"
  # Query fallback without mktemp on PATH (deterministic temp-name path).
  sqlite3_bin="$(command -v sqlite3)"
  rm -f "$tmp/site/full.db"
  nopq=$(cd "$tmp/site" && PATH="$(dirname "$sqlite3_bin")" "$NIFT_ABS" run full.f)
  [ "$nopq" = "$expected" ] || { printf 'sqlite fallback (no mktemp) mismatch:\n%s\n' "$nopq" >&2; exit 1; }
  leaks=$(cd "$tmp/site" && ls .nift-sqlite-*.json 2>/dev/null || true)
  [ -z "$leaks" ] || { printf 'leaked sqlite temp files: %s\n' "$leaks" >&2; exit 1; }
fi
echo 'PASS sqlite package dogfood'