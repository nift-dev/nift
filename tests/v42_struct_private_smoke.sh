#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT_BIN:-${NIFT:-./nift}}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$PWD/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT

run() { "$NIFT_ABS" "$1"; }
must_fail() {
  local name=$1 pattern=$2 source=$3
  if run "$source" >"$t/$name.out" 2>"$t/$name.err"; then
    echo "$name unexpectedly succeeded" >&2
    exit 1
  fi
  grep -q "$pattern" "$t/$name.err" || { cat "$t/$name.err" >&2; exit 1; }
}

# Bare struct script syntax, internal private access, constructors and all public
# member kinds remain usable. This also exercises a callable field whose body
# reads private receiver state.
cat >"$t/internal.f" <<'NIFT'
struct(vault) {
  private secret := 0
  public_value := 2
  private fn(step()) { secret += 1 }
  private helper := (x) => secret + x
  public_helper := (x) => x + 20
  fn(vault(start)) { secret = start }
  fn(read()) { return secret }
  fn(call_step()) { step(); return secret }
  fn(call_helper(x)) { return helper(x) }
  fn(read_other(other)) { return other.secret }
}
v := vault(10)
print(v.read())
print(v.call_step())
print(v.call_helper(2))
print(v.public_value)
print(v.public_helper(3))
NIFT
[ "$(run "$t/internal.f")" = $'10\n11\n13\n2\n23' ]

# @struct remains valid in script land.
cat >"$t/legacy-struct.f" <<'NIFT'
@struct(box) { private value := 4; fn(read()) { return value } }
b := box()
print(b.read())
NIFT
[ "$(run "$t/legacy-struct.f")" = '4' ]

cat >"$t/read.f" <<'NIFT'
struct(vault) { private secret := 7 }
v := vault()
print(v.secret)
NIFT
must_fail private-read 'private struct field: secret' "$t/read.f"

cat >"$t/write.f" <<'NIFT'
struct(vault) { private secret := 7 }
v := vault()
v.secret = 8
NIFT
must_fail private-write 'private struct field: secret' "$t/write.f"

cat >"$t/compound.f" <<'NIFT'
struct(vault) { private secret := 7 }
v := vault()
v.secret += 1
NIFT
must_fail private-compound 'private struct field: secret' "$t/compound.f"

cat >"$t/method.f" <<'NIFT'
struct(vault) { private fn(secret()) { return 7 } }
v := vault()
print(v.secret())
NIFT
must_fail private-method 'private struct method: secret' "$t/method.f"

cat >"$t/callable.f" <<'NIFT'
struct(vault) { private helper := (x) => x + 1 }
v := vault()
print(v.helper(5))
NIFT
must_fail private-callable 'private struct field: helper' "$t/callable.f"

# A prepared named callable reaches struct dispatch through the legacy fallback;
# it must report the same privacy failure as a direct legacy expression.
cat >"$t/prepared.f" <<'NIFT'
struct(vault) { private helper := (x) => x + 1 }
fn(invoke(v)) { return v.helper(5) }
v := vault()
print(invoke(v))
NIFT
must_fail prepared-private-callable 'private struct field: helper' "$t/prepared.f"

cat >"$t/reference.f" <<'NIFT'
struct(vault) { private secret := [7] }
v := vault()
alias := v.secret
alias.push(8)
NIFT
must_fail private-reference 'private struct field: secret' "$t/reference.f"

cat >"$t/nested.f" <<'NIFT'
struct(vault) { private secret := 7 }
struct(holder) { child := vault() }
h := holder()
print(h.child.secret)
NIFT
must_fail private-nested 'private struct field: secret' "$t/nested.f"

cat >"$t/private-intermediate-write.f" <<'NIFT'
struct(child) { value := 1 }
struct(vault) {
  private nested := child()
  fn(read()) { return nested.value }
}
v := vault()
v.nested.value = 8
NIFT
must_fail private-intermediate-write 'private struct field: nested' "$t/private-intermediate-write.f"

# Privacy remains same-instance: a method cannot inspect another instance.
cat >"$t/cross-instance.f" <<'NIFT'
struct(vault) {
  private secret := 0
  fn(vault(start)) { secret = start }
  fn(read_other(other)) { return other.secret }
}
a := vault(1)
b := vault(2)
print(a.read_other(b))
NIFT
must_fail cross-instance 'private struct field: secret' "$t/cross-instance.f"

# Copy/deepcopy continue to copy private state without making it public.
cat >"$t/copy.f" <<'NIFT'
struct(vault) {
  private secret := 1
  fn(increment()) { secret += 1 }
  fn(read()) { return secret }
}
a := vault()
b := copy(a)
b.increment()
c := deepcopy(a)
c.increment()
print(a.read())
print(b.read())
print(c.read())
NIFT
[ "$(run "$t/copy.f")" = $'1\n2\n2' ]

# Serialization still omits private fields.
cat >"$t/serialize.f" <<'NIFT'
struct(vault) { private hidden := 7; shown := 3 }
v := vault()
print(v.stringify())
NIFT
[ "$(run "$t/serialize.f")" = 'vault{shown:3}' ]

# Preserve template/build coverage for @struct and template expression access.
mkdir "$t/project"
(
  cd "$t/project"
  "$NIFT_ABS" init >/dev/null
  cat >content/index.html <<'NIFT'
@struct(vault) { private secret := 7; fn(read()) { return secret } }
$[v := vault()]$[v.read()]
NIFT
  "$NIFT_ABS" build >/dev/null
  grep -q 7 public/index.html
  cat >content/index.html <<'NIFT'
@struct(vault) { private secret := 7 }
$[v := vault()]$[v.secret]
NIFT
  if "$NIFT_ABS" build >/dev/null 2>&1; then
    echo 'template private read unexpectedly succeeded' >&2
    exit 1
  fi
)

echo 'v4.2 struct privacy smoke passed'
