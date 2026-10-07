#!/usr/bin/env bash
# First-class warn(...) primitive (v4.8): non-fatal warning semantics. warn
# emits exactly one canonical "warning: <value>" line to stderr, continues
# execution, leaves stdout (and template output) untouched, and returns null
# like print/err. Ordinary, prepared (loop/while/function) and template-driven
# execution must agree; invalid calls fail like the other output builtins.
set -euo pipefail
NIFT=${NIFT:-./nift}
case "$NIFT" in /*) B="$NIFT";; *) B="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
rk(){ # <name> <expected-stdout> <expected-stderr> <program>
  local name="$1" exp_out="$2" exp_err="$3" prog="$4"
  "$B" -e "$prog" >"$t/o" 2>"$t/e" || { echo "FAIL $name: rc" >&2; cat "$t/e" >&2; exit 1; }
  [ "$(cat "$t/o")" = "$exp_out" ] || { echo "FAIL $name stdout: [$(cat "$t/o")]" >&2; exit 1; }
  [ "$(cat "$t/e")" = "$exp_err" ] || { echo "FAIL $name stderr: [$(cat "$t/e")]" >&2; exit 1; }
}
rk "basic stdout/stderr separated, execution continues" $'A\nB' "warning: note" 'print("A"); warn("note"); print("B")'
rk "value types" "" $'warning: text\nwarning: \nwarning: 123\nwarning: true\nwarning: null' 'warn("text"); warn(""); warn(123); warn(true); warn(null)'
rk "multiple warnings" "" $'warning: one\nwarning: two' 'warn("one"); warn("two")'
rk "return null" "true" "warning: y" 'x := warn("y"); print(x == null)'
rk "function" "" "warning: in fn" 'fn(f()) { warn("in fn") }
f()'
rk "conditional" "" "warning: cond" 'if(true) { warn("cond") }'
rk "prepared for" "after" $'warning: 1\nwarning: 2' 'for(i : [1,2]) { warn(i) }
print("after")'
rk "while" "" $'warning: w1\nwarning: w2' 'i := 1
while(i <= 2) { warn("w" + i); i = i + 1 }'
rk "identifier: warn remains a plain variable" "5" "" 'warn := 5
print(warn)'
rk "unicode" "" "warning: héllo 中 😀" 'warn("héllo 中 😀")'
rk "embedded newline" "" $'warning: a\nb' 'warn("a\nb")'

reject(){ # <name> <stderr-substring> <program>
  local name="$1" needle="$2" prog="$3"
  if "$B" -e "$prog" >"$t/o" 2>"$t/e"; then echo "FAIL $name: unexpectedly succeeded" >&2; exit 1; fi
  grep -q "$needle" "$t/e" || { echo "FAIL $name missing [$needle]: $(cat "$t/e")" >&2; exit 1; }
}
reject "wrong argument count" "warn: expected one value" 'warn()'
reject "wrong argument count (two)" "warn: expected one value" 'warn(1, 2)'
reject "non-renderable" "warn: value is not directly renderable" 'warn([1,2])'
reject "bytes rejected" "warn: bytes values cannot be rendered as text" 'warn("x".encode("utf-8"))'

# Template-driven warning: a template function body warns during page render;
# the rendered output is unaffected and the build continues successfully.
mkdir -p "$t/site/.nift" "$t/site/content" "$t/site/templates"
printf '%s' '{"config": {"content-dir": "content/", "output-dir": "public/", "default-template": "templates/template.html", "build-threads": -1}}' > "$t/site/.nift/config.json"
printf '%s' '{"tracked": [{"name": "/", "title": "t", "template": "templates/template.html"}]}' > "$t/site/.nift/tracked.json"
printf '%s\n' '<body>@content</body>' > "$t/site/templates/template.html"
cat > "$t/site/content/index.html" <<'E'
@fn(dep()){ warn("old_field is deprecated; use new_field"); return "" }
$[dep()]Hello
E
(cd "$t/site" && "$B" build --all >"$t/o" 2>"$t/e")
[ "$(cat "$t/e")" = "warning: old_field is deprecated; use new_field" ] || { echo "FAIL template stderr: [$(cat "$t/e")]" >&2; exit 1; }
grep -q "Hello" "$t/site/public/index.html" || { echo "FAIL template output changed" >&2; exit 1; }

echo "PASS v4.8 warn primitive"