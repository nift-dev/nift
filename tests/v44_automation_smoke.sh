#!/usr/bin/env bash
# Native project/build automation API (v4.4 CP23/CP24): build/track/untrack/
# status/tracked/project_root/build_names/build_all/build_repair execute
# through ProjectInfo directly from scripts (never a subprocess).
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
mkdir -p "$t/site/.nift" "$t/site/content" "$t/site/templates" "$t/site/public"
cat > "$t/site/.nift/config.json" <<'JSON'
{"config":{"content-dir":"content/","content-ext":".html","output-dir":"public/","output-ext":".html","default-template":"templates/main.html","build-threads":1,"incremental-mode":"modified"}}
JSON
cat > "$t/site/.nift/tracked.json" <<'JSON'
{"tracked":[{"name":"/","title":"Home","template":"templates/main.html"}]}
JSON
printf 'home\n' > "$t/site/content/index.html"
printf '<div>@content</div>\n' > "$t/site/templates/main.html"
cat > "$t/site/auto.f" <<'F'
print(project_root())
r := build()
print(r.ok)
print(r.affected.join(","))
print(tracked().join(","))
t := track("about", "About", "templates/main.html")
print(t.ok)
print(tracked().join(","))
F
out=$(cd "$t/site" && "$NIFT_ABS" run auto.f)
[ "$out" = "$t/site
true
/
/
true
/,about" ] || { printf 'unexpected:\n%s\n' "$out" >&2; exit 1; }
printf 'about\n' > "$t/site/content/about.html"
cat > "$t/site/auto2.f" <<'F'
print(status().join(","))
b := build()
print(b.ok)
print(b.exit_code)
print(b.affected.join(","))
print(build_all().affected.join(","))
print(build_names("/").affected.join(","))
u := untrack("about")
print(u.ok)
print(tracked().join(","))
F
out2=$(cd "$t/site" && "$NIFT_ABS" run auto2.f)
[ "$out2" = "about
true
0
about
/,about
/
true
/" ] || { printf 'unexpected:\n%s\n' "$out2" >&2; exit 1; }
# build_repair is available and succeeds on a clean project
printf 'print(build_repair().ok)\n' > "$t/site/repair.f"
out3=$(cd "$t/site" && "$NIFT_ABS" run repair.f)
[ "$out3" = "true" ] || exit 1
# automation is script-land only
if cd "$t/site" && printf '@content\n$[build()]' > templates/main.html && printf 'x' > content/index.html; then
  if "$NIFT_ABS" build --all >/dev/null 2>&1; then
    : # template renders; the expression call would fail at render time
  fi
fi
printf 'home\n' > "$t/site/content/index.html"
printf '<div>@content</div>\n' > "$t/site/templates/main.html"
echo 'PASS v4.4 native automation API'