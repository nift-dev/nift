#!/usr/bin/env bash
# Combined ecosystem dogfood: several packages imported into one script with
# both package-API styles (direct exports + module structs) coexisting without
# namespace leakage. Real workflow: fetch -> sqlite store -> redis cache ->
# magick thumbnail -> magick identify, with vips imported simultaneously
# (client-certified) and postgres/mysql imported for namespace isolation.
set -euo pipefail
NIFT="${NIFT:-./nift}"
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
PKG_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/nift-packages"
SQLITE3_BIN="${SQLITE3_BIN:-sqlite3}"
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
mkdir -p "$t/site/.nift" "$t/site/assets" "$t/site/out"
for p in sqlite curl postgres mysql redis vips imagemagick; do
  (cd "$t/site" && "$NIFT_ABS" add "$PKG_ROOT/$p" >/dev/null 2>&1)
done

PORT=$((19000 + RANDOM % 2000))
python3 - "$PORT" "$t/site/assets/pic.png" <<'PYEOF' &
import http.server, sys
port = int(sys.argv[1]); out = sys.argv[2]
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/data.json":
            body = b'[{"title": "first", "views": 12}, {"title": "second", "views": 7}]'
            self.send_response(200); self.send_header("Content-Type", "application/json")
            self.end_headers(); self.wfile.write(body); return
        if self.path == "/pic.png":
            self.send_response(200)
            with open(out, "rb") as f:
                self.send_header("Content-Type", "image/png")
                self.end_headers(); self.wfile.write(f.read()); return
        self.send_response(404); self.end_headers()
    def log_message(self, *a): pass
http.server.HTTPServer(("127.0.0.1", port), H).serve_forever()
PYEOF
SRV=$!
trap 'kill $SRV 2>/dev/null || true; rm -rf "$t"' EXIT
sleep 0.5
if command -v magick >/dev/null 2>&1; then
  magick -size 300x200 gradient:blue-purple "$t/site/assets/pic.png" 2>/dev/null
fi

cat > "$t/site/combined.f" <<NIFT
@import("curl")
@import("sqlite")
@import("postgres")
@import("mysql")
@import("redis")
@import("vips")
@import("imagemagick")

// fetch structured data
resp := request("http://127.0.0.1:$PORT/data.json")
print("request-ok")
print(resp.ok)
print("request-status")
print(resp.status)

// store in sqlite
db := sqlite.open("site.db")
print("sqlite-exec")
print(sqlite.exec(db, "CREATE TABLE IF NOT EXISTS posts(id INTEGER PRIMARY KEY, title TEXT, views INTEGER)").ok)
print("sqlite-insert")
print(sqlite.exec(db, "INSERT INTO posts(title, views) VALUES(?, ?)", "first", 12).ok)
rows := sqlite.query(db, "SELECT title FROM posts")
print("sqlite-rows")
print(rows.rows.size())

// redis cache (db 15, isolated)
client := redis.open({"host": "127.0.0.1", "port": 6379, "db": 15})
print("redis-cache")
print(redis.set(client, "nift:combined", "cached").ok)
print(redis.get(client, "nift:combined").data)
redis.del(client, "nift:combined")

// image pipeline: vips and imagemagick coexist; magick runs live when present.
// The identify/width section is guarded on the resize result so a runner
// without the magick executable cannot crash the dogfood on a missing field.
res := magick.resize("assets/pic.png", "out/thumb.png", {"width": 120, "height": 120})
print("magick-thumb")
print(res.ok)
if(res.ok) {
    id := magick.identify("out/thumb.png")
    print("magick-size")
    print(id.width + "x" + id.height)
}
print("vips-imported")
print(vips.available())

// relational clients coexist (structured failures without live creds)
pg := postgres.query(postgres.open({"host": "127.0.0.1"}), "SELECT 1")
print("pg-struct")
print(pg.ok)
NIFT
out=$(cd "$t/site" && "$NIFT_ABS" run combined.f)
echo "$out" > /tmp/opencode/combined_out.txt
echo "=== COMBINED DOGFOOD OUTPUT ==="
echo "$out"
rm -f /tmp/opencode/combined_out.txt
kill $SRV 2>/dev/null || true
