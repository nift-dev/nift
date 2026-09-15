#!/usr/bin/env bash
set -euo pipefail
NIFT_BIN="${NIFT_BIN:-./nift}"; T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT; cd "$T"; "$NIFT_BIN" init >/dev/null

# Struct member paths must compose with the expression evaluator exactly like
# JSON member paths do: a valid member followed by an operator is an expression
# (a.v + 1), not a malformed pure member path. The struct-walk in resolve_direct
# previously hard-errored on the trailing operator, so these failed while the
# equivalent JSON form worked in v4.1.

# Read arithmetic: a.v + 1 renders 3 (field 2 + 1).
cat > content/index.html <<'EOT'
@struct(node) { v := 2 }
$[a := node()]
$[a.v + 1]
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q '3' public/index.html

# Mutation arithmetic: a.v = a.v + 1 accumulates on the live field.
cat > content/index.html <<'EOT'
@struct(node) { v := 0 }
$[a := node()]
$[a.v = a.v + 1]
$[a.v = a.v + 1]
$[a.v]
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q '2' public/index.html

# Nested struct member arithmetic.
cat > content/index.html <<'EOT'
@struct(inner) { x := 2 }
@struct(outer) { child := inner() }
$[o := outer()]
$[o.child.x + 3]
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q '5' public/index.html

# Pure member reads still resolve.
cat > content/index.html <<'EOT'
@struct(node) { v := 7 }
$[a := node()]
$[a.v]
EOT
"$NIFT_BIN" build --all >/dev/null
grep -q '7' public/index.html

# Unknown struct fields must still fail rather than render literally.
cat > content/index.html <<'EOT'
@struct(node) { v := 1 }
$[a := node()]
$[a.zzz]
EOT
! "$NIFT_BIN" build --all >/dev/null 2>&1