#!/usr/bin/env bash
# v4.4 native script-land Minify++: minify(...) delegates to the embedded
# Minify++ (never a subprocess), so it must also work under --no-process.
# String->string, file->.min.ext, {output}, {in_place}, structured failures,
# fs-root restriction and CLI parity.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
mkdir -p "$t/assets" "$t/custom"
printf 'const a = 1;\nconst b = 2;\n' > "$t/assets/app.js"
# string -> string across all supported formats
cat > "$t/s.f" <<'F'
r := minify("const x = 1;   const y = 2;", "js")
print(r.ok)
print(r.output)
print(minify("<div>  <p>x</p> </div>", "html").output)
print(minify("body { color: red;  background: blue; }", "css").output)
print(minify("{\"a\": 1, \"b\": 2}", "json").output)
F
out=$("$NIFT_ABS" run "$t/s.f")
[ "$(echo "$out" | sed -n '1p')" = "true" ] || { echo "$out" >&2; exit 1; }
[ "$(echo "$out" | sed -n '2p')" = "const x=1;const y=2" ] || exit 1
[ "$(echo "$out" | sed -n '3p')" = "<div> <p>x</p> </div>" ] || exit 1
[ "$(echo "$out" | sed -n '4p')" = "body{color:red;background:blue;}" ] || exit 1
[ "$(echo "$out" | sed -n '5p')" = '{"a":1,"b":2}' ] || exit 1
# file -> conventional .min.ext, explicit output, in-place
cat > "$t/f.f" <<'F'
print(minify("assets/app.js").ok)
print(exists("assets/app.min.js"))
print(minify("assets/app.js", {"output": "custom/out.js"}).ok)
print(exists("custom/out.js"))
print(minify("assets/app.js", {"in_place": true}).ok)
print(open("assets/app.js"))
F
out=$(cd "$t" && "$NIFT_ABS" run f.f)
[ "$(echo "$out" | sed -n '1p')" = "true" ] || { echo "$out" >&2; exit 1; }
[ "$(echo "$out" | sed -n '2p')" = "true" ] || exit 1
[ "$(echo "$out" | sed -n '3p')" = "true" ] || exit 1
[ "$(echo "$out" | sed -n '4p')" = "true" ] || exit 1
[ "$(echo "$out" | sed -n '5p')" = "true" ] || exit 1
[ "$(echo "$out" | sed -n '6p')" = "const a=1;const b=2" ] || exit 1
# structured failures: missing file, unsupported format
cat > "$t/e.f" <<'F'
m := minify("assets/nope.js")
print(m.ok)
print(m.error != "")
u := minify("assets/app.js", "yaml")
print(u.ok)
print(u.error != "")
F
out=$(cd "$t" && "$NIFT_ABS" run e.f)
[ "$(echo "$out" | sed -n '1p')" = "false" ] || exit 1
[ "$(echo "$out" | sed -n '2p')" = "true" ] || exit 1
[ "$(echo "$out" | sed -n '3p')" = "false" ] || exit 1
[ "$(echo "$out" | sed -n '4p')" = "true" ] || exit 1
# native: works under --no-process
out=$(NIFT_NO_PROCESS=1 "$NIFT_ABS" run "$t/s.f")
[ "$(echo "$out" | sed -n '2p')" = "const x=1;const y=2" ] || exit 1
# fs-root restriction on output
printf 'const z = 1;\n' > "$t/assets/z.js"
cat > "$t/fr.f" <<'F'
print(minify("assets/z.js", {"output": "../escape.js"}).ok)
F
if (cd "$t" && "$NIFT_ABS" run --fs-root="$t" fr.f >/dev/null 2>&1); then
  echo "minify fs-root escape was not blocked" >&2; exit 1
fi
# CLI parity: same embedded Minify++ output
printf 'const q = 1;   const r = 2;\n' > "$t/p.js"
"$NIFT_ABS" minify "$t/p.js" >/dev/null 2>&1
cat > "$t/pf.f" <<'F'
print(open("p.min.js"))
F
script_out=$(cd "$t" && "$NIFT_ABS" run pf.f)
cli_out=$(cat "$t/p.min.js")
[ "$script_out" = "$cli_out" ] || { echo "mismatch: script=$script_out cli=$cli_out" >&2; exit 1; }
echo 'PASS v4.4 native script-land Minify++'
