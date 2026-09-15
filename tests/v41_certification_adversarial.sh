#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT_BIN:-./nift}
R=$(mktemp -d "${TMPDIR:-/tmp}/nift-v41-cert.XXXXXX")
trap 'rm -rf "$R"' EXIT

newproj() {
  local d="$1"
  mkdir -p "$d"
  (cd "$d" && "$NIFT" init >/dev/null)
  printf '@content\n' > "$d/templates/template.html"
}

expect() {
  local name="$1" expected="$2" actual="$3"
  if [[ "$actual" != "$expected" ]]; then
    echo "FAIL [$name]: expected '$expected', got '$actual'" >&2
    exit 1
  fi
}

# --- Mutation-suppression flag must not leak out of nested evaluations. ---
# A function that uses a local declaration must still render at its call site.
P="$R/fn-local-decl"; newproj "$P"
cat > "$P/content/index.html" <<'E'
@fn(f()){
    $[tmp := 10]
    @return(tmp + 1)
}
call=$[f()]
E
(cd "$P" && "$NIFT" build --all >/dev/null)
expect "fn-local-decl renders" 'call=11' "$(grep -o 'call=.*' "$P/public/index.html")"

# A function that mutates an outer binding must still render at its call site.
P="$R/fn-outer-mutation"; newproj "$P"
cat > "$P/content/index.html" <<'E'
@fn(bump()){
    $[counter = counter + 1]
    @return(counter)
}
$[counter := 0]
b1=$[bump()] b2=$[bump()] final=$[counter]
E
(cd "$P" && "$NIFT" build --all >/dev/null)
expect "fn-outer-mutation renders" 'b1=1 b2=2 final=2' "$(grep -o 'b1=[0-9]* b2=[0-9]* final=[0-9]*' "$P/public/index.html")"

# A fragment whose body declares a binding must still render at its call site.
P="$R/frag-local-decl"; newproj "$P"
cat > "$P/content/index.html" <<'E'
@fragment(card(x)){<b>$[x]</b>$[tmp := 9]}
frag=$[card("ok")]
E
(cd "$P" && "$NIFT" build --all >/dev/null)
expect "frag-local-decl renders" 'frag=<b>ok</b>' "$(grep -o 'frag=.*' "$P/public/index.html")"

# A mutation nested inside a larger expression must not suppress the value.
P="$R/nested-subexpr"; newproj "$P"
printf 'b := 1\n' > "$P/m.expr"
cat > "$P/content/index.html" <<'E'
$[a := 0]$[a + inject("m.expr")]
E
(cd "$P" && "$NIFT" build --all >/dev/null)
expect "inject-in-arithmetic renders" '1' "$(grep -oE '^1$' "$P/public/index.html" || true)"
# parenthesised declaration inside arithmetic renders the computed value
cat > "$P/content/index.html" <<'E'
$[(x := 5) + 1]
E
(cd "$P" && "$NIFT" build --all >/dev/null)
expect "paren-decl-arithmetic renders" '6' "$(grep -oE '^6$' "$P/public/index.html" || true)"

# A top-level declaration performed via bare injection is suppressed (declaration semantics).
P="$R/inject-declaration-suppressed"; newproj "$P"
printf 'b := 1\n' > "$P/decl.expr"
cat > "$P/content/index.html" <<'E'
$[inject("decl.expr")]
E
(cd "$P" && "$NIFT" build --all >/dev/null)
expect "bare inject declaration suppressed" '' "$(tr -d '[:space:]' < "$P/public/index.html")"

# --- @return must honour conditional branches, not always the last @return. ---
P="$R/conditional-returns"; newproj "$P"
cat > "$P/content/index.html" <<'E'
@fn(pick(c)){
    @if(c){@return("yes")}
    @return("no")
}
pick-t=$[pick(true)] pick-f=$[pick(false)]
E
(cd "$P" && "$NIFT" build --all >/dev/null)
expect "conditional returns" 'pick-t=yes pick-f=no' "$(grep -o 'pick-t=[a-z]* pick-f=[a-z]*' "$P/public/index.html")"

# Function with no reachable @return errors.
P="$R/missing-return"; newproj "$P"
cat > "$P/content/index.html" <<'E'
@fn(noop()){ $[x := 1] }
$[noop()]
E
if (cd "$P" && "$NIFT" build --all >/dev/null 2>err); then echo "FAIL [missing-return]: expected build failure" >&2; exit 1; fi
grep -q 'function requires @return' "$P/err"

# --- @for loop variables must shadow outer v4.1 bindings with the same name. ---
P="$R/loop-shadowing"; newproj "$P"
cat > "$P/content/index.html" <<'E'
$[item := 99]
@for(item : [1,2,3]){[$[item]]}
E
(cd "$P" && "$NIFT" build --all >/dev/null)
expect "loop shadows outer binding" '[1][2][3]' "$(tr -d '[:space:]' < "$P/public/index.html")"

# --- Undefined callables must error rather than render literally. ---
P="$R/undefined-callable"; newproj "$P"
cat > "$P/content/index.html" <<'E'
$[not_a_function(1,2)]
E
if (cd "$P" && "$NIFT" build --all >/dev/null 2>err); then echo "FAIL [undefined-callable]: expected build failure" >&2; exit 1; fi
grep -q 'undefined callable' "$P/err"

# --- Structured v4.1 bindings support array-index and mixed path access. ---
P="$R/structured-indexing"; newproj "$P"
cat > "$P/content/index.html" <<'E'
$[x := [10,20,30]]
x1=$[x[1]]
$[o := {"a": {"b": [1,2]}}]
deep=$[o.a.b[0]]
E
(cd "$P" && "$NIFT" build --all >/dev/null)
expect "array index on binding" 'x1=20' "$(grep -o 'x1=[0-9]*' "$P/public/index.html")"
expect "mixed dot/index on binding" 'deep=1' "$(grep -o 'deep=[0-9]*' "$P/public/index.html")"

# --- validate() and callables accept structured-literal arguments. ---
P="$R/structured-args"; newproj "$P"
cat > "$P/content/index.html" <<'E'
$[schema := {"type":"object","properties":{"n":{"type":"integer"}}}]
$[ok := validate(schema, {"n": 5})]
ok=$[ok.n]
@fn(pick(o)){@return(o.name)}
name=$[pick({"name": "zed"})]
@fn(first(a)){@return(a[0])}
first=$[first([7,8])]
E
(cd "$P" && "$NIFT" build --all >/dev/null)
expect "validate inline value" 'ok=5' "$(grep -o 'ok=[0-9]*' "$P/public/index.html")"
expect "callable object literal arg" 'name=zed' "$(grep -o 'name=zed' "$P/public/index.html")"
expect "callable array literal arg" 'first=7' "$(grep -o 'first=[0-9]*' "$P/public/index.html")"

# --- Recursive callables fail cleanly once a depth bound is exceeded. ---
P="$R/recursion-bound"; newproj "$P"
cat > "$P/content/index.html" <<'E'
@fn(down(n)){
    @if(n > 0){ @return(down(n - 1)) }
    @return(0)
}
r=$[down(200000)]
E
if (cd "$P" && "$NIFT" build --all >/dev/null 2>err); then
  echo "FAIL [recursion-bound]: expected build failure for unbounded recursion" >&2
  exit 1
fi
grep -q 'recursion' "$P/err"

echo 'Nift v4.1 certification adversarial passed'