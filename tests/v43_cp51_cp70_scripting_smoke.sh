#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-$(pwd)/nift}"; T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cd "$T"; "$NIFT_BIN" init >/dev/null
body(){ tr -d '\n\r\t ' < public/index.html | sed 's/.*<body>//;s#</body>.*##'; }

# CP51-60: one native statement engine, inline @script current scope and returns.
cat > content/index.html <<'EOT'
$[outer := 2]
@script {
  outer += 3
  local := 4
  fn(double(x)) { return x * 2 }
  struct(box) { value := 1; fn(box(v)) { value = v } fn(get()) { return value } }
  b := box(7)
  f := x => x + outer
  vals := [1,2,3]
  total := vals.reduce((a,x) => a + x, 0)
  if(total == 6) { return "S:$[double(local)]/$[b.get()]/$[f(1)]" }
}
$[outer]
EOT
"$NIFT_BIN" build --all >/dev/null
[[ "$(body)" == *"S:8/7/65"* ]]
# bare return is no output; return null is a value and renders null.
cat > content/index.html <<'EOT'
A@script { return }B@script { return null }C
EOT
"$NIFT_BIN" build --all >/dev/null
[[ "$(body)" == *"ABnullC"* ]]
# Returned source-looking strings are not reparsed.
cat > content/index.html <<'EOT'
@script { return "literal @if(x){bad}" }
EOT
"$NIFT_BIN" build --all >/dev/null
[[ "$(body)" == *'literal@if(x){bad}'* ]]
# Internal reference values cannot leak through script return rendering.
cat > content/index.html <<'EOT'
@script { f := () => 1; return f }
EOT
if "$NIFT_BIN" build --all >/dev/null 2>&1; then echo 'script callable return leaked' >&2; exit 1; fi
# break outside a loop in script land is rejected.
cat > content/index.html <<'EOT'
@script { break }
EOT
if "$NIFT_BIN" build --all >/dev/null 2>&1; then echo 'script break outside loop unexpectedly succeeded' >&2; exit 1; fi

# CP61-68: isolated imports, binding exports, live closures, atomic validation.
cat > content/lib.nift <<'EOT'
secret := 100
n := 3
inc := () => ++n
fn(double(x)) { return x * 2 }
export(n)
export(inc)
export(double)
EOT
cat > content/index.html <<'EOT'
$[secret := 7]
@import("content/lib.nift")
$[secret],$[n],$[inc()],$[n],$[double(5)]
EOT
"$NIFT_BIN" build --all >/dev/null
[[ "$(body)" == *"7,3,4,4,10"* ]]
# Caller bindings are invisible to the imported script.
cat > content/isolated.nift <<'EOT'
x := caller_only
export(x)
EOT
cat > content/index.html <<'EOT'
$[caller_only := 9]@import("content/isolated.nift")
EOT
if "$NIFT_BIN" build --all >/dev/null 2>&1; then echo 'import saw caller scope' >&2; exit 1; fi
# Duplicate/missing/colliding exports and value returns reject.
cat > content/baddup.nift <<'EOT'
x := 1
export(x)
export(x)
EOT
cat > content/index.html <<'EOT'
@import("content/baddup.nift")
EOT
if "$NIFT_BIN" build --all >/dev/null 2>&1; then echo 'duplicate export succeeded' >&2; exit 1; fi
cat > content/badreturn.nift <<'EOT'
return 5
EOT
cat > content/index.html <<'EOT'
@import("content/badreturn.nift")
EOT
if "$NIFT_BIN" build --all >/dev/null 2>&1; then echo 'value return from import succeeded' >&2; exit 1; fi
cat > content/early.nift <<'EOT'
x := 8
export(x)
return
x = 9
EOT
cat > content/index.html <<'EOT'
@import("content/early.nift")$[x]
EOT
"$NIFT_BIN" build --all >/dev/null; [[ "$(body)" == *"8"* ]]
# Nested import/dependency path resolution.
mkdir -p content/lib
cat > content/lib/base.nift <<'EOT'
y := 11
export(y)
EOT
cat > content/lib/top.nift <<'EOT'
@import("base.nift")
export(y)
EOT
cat > content/index.html <<'EOT'
@import("content/lib/top.nift")$[y]
EOT
"$NIFT_BIN" build --all >/dev/null; [[ "$(body)" == *"11"* ]]

# Imported scripts are tracked dependencies for incremental builds.
cat > content/dep.nift <<'EOT'
z := 1
export(z)
EOT
cat > content/index.html <<'EOT'
@import("content/dep.nift")$[z]
EOT
"$NIFT_BIN" build --all >/dev/null; [[ "$(body)" == *"1"* ]]
sleep 1
cat > content/dep.nift <<'EOT'
z := 2
export(z)
EOT
"$NIFT_BIN" build >/dev/null; [[ "$(body)" == *"2"* ]]

# CP69: array ergonomics.
cat > content/index.html <<'EOT'
$[a := [1,2,3,4]]$[a.join("-")]
$[s := a.slice(1,3)]@for(x : s){$[x]}
$[r := a.splice(1,2,[8,9])]@for(x : r){$[x]}|@for(x : a){$[x]}
$[a.reverse()]@for(x : a){$[x]}
EOT
"$NIFT_BIN" build --all >/dev/null
[[ "$(body)" == *"1-2-3-42323|18944981"* ]]
# Out-of-range slice/splice start clamps; negative indices reject.
cat > content/index.html <<'EOT'
$[a := [1,2]]$[s := a.slice(99)]$[s.size()]$[r := a.splice(99,5,[3])]$[r.size()],$[a.last()]
EOT
"$NIFT_BIN" build --all >/dev/null; [[ "$(body)" == *"00,3"* ]]
cat > content/index.html <<'EOT'
$[a := [1]]$[a.slice(-1)]
EOT
if "$NIFT_BIN" build --all >/dev/null 2>&1; then echo 'negative slice unexpectedly succeeded' >&2; exit 1; fi

# CP70: substr, quoted interpolation, deliberate string concatenation.
cat > content/index.html <<'EOT'
$[text := "abcdef"]$[text.substr(2)],$[text.substr(1,3)]
$[person := "Nick"]$[msg := "hello, $[person]"]$[msg]|$["hello, " + person]|$["n=" + 3]
EOT
"$NIFT_BIN" build --all >/dev/null
[[ "$(body)" == *"cdef,bcdhello,Nick|hello,Nick|n=3"* ]]
# No JS-style equality coercion.
cat > content/index.html <<'EOT'
$[1 == "1"]
EOT
"$NIFT_BIN" build --all >/dev/null; [[ "$(body)" == *"false"* ]]
