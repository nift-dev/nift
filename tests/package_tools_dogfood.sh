#!/usr/bin/env bash
# Dogfood for the v0.1.0 tool packages: postgres/mysql/redis (databases) and
# vips/imagemagick (images). Redis and ImageMagick run live where the tool is
# present and reachable; PostgreSQL/MySQL certify client discovery, argv
# construction and structured failures without touching local servers; vips
# certifies the missing-executable path on this machine. Privacy is checked.
set -euo pipefail
NIFT="${NIFT:-./nift}"
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
PKG_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/nift-packages"
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
mkdir -p "$t/site/.nift" "$t/site/assets" "$t/site/out"
for p in postgres mysql redis vips imagemagick; do
  (cd "$t/site" && "$NIFT_ABS" add "$PKG_ROOT/$p" >/dev/null 2>&1)
done
if command -v magick >/dev/null 2>&1; then
  magick -size 400x300 gradient:red-blue "$t/site/assets/base.png" 2>/dev/null
fi

cat > "$t/site/db.f" <<NIFT
@import("postgres")
@import("mysql")
@import("redis")

print("pg-avail")
print(postgres.available())
db := postgres.open({"host": "127.0.0.1", "port": 5432, "database": "dogfood", "user": "nobody"})
print("pg-conn")
print(db.conn != "")
r := postgres.query(db, "SELECT * FROM nope")
print("pg-fail")
print(r.ok)
print("pg-code")
print(r.exit_code)
tx := postgres.transaction(db, ["BEGIN", "ROLLBACK"])
print("pg-tx")
print(tx.ok)

print("mysql-avail")
print(mysql.available())
m := mysql.open({"host": "127.0.0.1", "port": 3306, "user": "root", "database": "dogfood"})
print("mysql-conn")
print(m.conn.size() > 0)
mr := mysql.query(m, "SELECT * FROM nope")
print("mysql-fail")
print(mr.ok)
print("mysql-code")
print(mr.exit_code)

print("redis-avail")
print(redis.available())
client := redis.open({"host": "127.0.0.1", "port": 6379, "db": 15})
print("redis-set")
print(redis.set(client, "nift:tools", "value").ok)
print("redis-get")
print(redis.get(client, "nift:tools").data)
print("redis-exists")
print(redis.exists(client, "nift:tools").data)
print("redis-del")
print(redis.del(client, "nift:tools").data)
print("redis-null")
print(type(redis.get(client, "nift:gone").data))
NIFT
out=$(cd "$t/site" && "$NIFT_ABS" run db.f)
echo "=== DATABASE TOOLS DOGFOOD ==="; echo "$out"

cat > "$t/site/img.f" <<NIFT
@import("vips")
@import("imagemagick")

print("vips-avail")
print(vips.available())
print("vips-meta")
vm := vips.metadata("assets/base.png")
print(vm.ok)
vr := vips.resize("assets/base.png", "out/vr.png", 0.5)
print("vips-resize")
print(vr.ok)

print("magick-avail")
print(magick.available())
print("magick-ident")
id := magick.identify("assets/base.png")
print(id.ok)
print(id.width + "x" + id.height + " " + id.format)
print("magick-thumb")
print(magick.resize("assets/base.png", "out/t.png", {"width": 100, "height": 100}).ok)
print("magick-crop")
print(magick.crop("assets/base.png", "out/c.png", {"width": 50, "height": 40, "left": 5, "top": 5}).ok)
print("magick-rotate")
print(magick.rotate("assets/base.png", "out/r.png", 90).ok)
print("magick-convert")
print(magick.convert("assets/base.png", "out/b.jpg").ok)
print("magick-verify")
v := magick.identify("out/b.jpg")
print(v.format)
print("magick-invalid")
print(magick.identify("out/missing.png").ok)
NIFT
out=$(cd "$t/site" && "$NIFT_ABS" run img.f)
echo "=== IMAGE TOOLS DOGFOOD ==="; echo "$out"

cat > "$t/site/priv.f" <<NIFT
@import("postgres")
@import("mysql")
@import("redis")
@import("vips")
@import("imagemagick")
print(postgres_literal)
print(mysql_bind)
print(redis_copy)
print(vips_guard)
print(magick_run)
NIFT
if (cd "$t/site" && "$NIFT_ABS" run priv.f >/dev/null 2>&1); then echo "private helper leaked" >&2; exit 1; fi
echo "privacy: PASS"
