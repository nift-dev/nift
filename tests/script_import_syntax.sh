#!/usr/bin/env bash
set -euo pipefail

NIFT_BIN=${NIFT_BIN:-$(pwd)/nift}
case "$NIFT_BIN" in /*) NIFT=$NIFT_BIN;; *) NIFT="$(pwd)/$NIFT_BIN";; esac
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

mkdir -p "$T/scripts"
cat >"$T/scripts/module.f" <<'EOF'
fn(answer()) { return 42 }
export(answer)
EOF
cat >"$T/scripts/bare.f" <<'EOF'
import("module.f")
print(answer())
EOF
cat >"$T/scripts/spaced.f" <<'EOF'
import ("module.f")
print(answer())
EOF
cat >"$T/scripts/legacy.f" <<'EOF'
@import("module.f")
print(answer())
EOF
[[ "$(cd "$T/scripts" && "$NIFT" bare.f)" == 42 ]]
[[ "$(cd "$T/scripts" && "$NIFT" spaced.f)" == 42 ]]
[[ "$(cd "$T/scripts" && "$NIFT" legacy.f)" == 42 ]]
repl=$(cd "$T/scripts" && printf '%s\n' 'fn(import(x)) { return 99 }' 'import("module.f")' 'answer()' | "$NIFT")
grep -q '42' <<<"$repl"
if grep -q '99' <<<"$repl"; then
    echo 'REPL callable dispatch overrode the import statement' >&2
    exit 1
fi

cat >"$T/scripts/inner.f" <<'EOF'
fn(inner_value()) { return 17 }
export(inner_value)
EOF
cat >"$T/scripts/outer.f" <<'EOF'
import("inner.f")
fn(value()) { return inner_value() }
export(value)
EOF
cat >"$T/scripts/nested.f" <<'EOF'
import("outer.f")
print(value())
EOF
[[ "$(cd "$T/scripts" && "$NIFT" nested.f)" == 17 ]]

mkdir -p "$T/site/.nift/packages/demo/src"
printf '{"dependencies":{"demo":{"source":"./demo","ref":"local"}}}\n' > "$T/site/manifest.json"
printf '{"demo":{"source":"./demo","requested":"local","commit":"local"}}\n' > "$T/site/.nift/packages.lock.json"
cat >"$T/site/.nift/packages/demo/manifest.json" <<'EOF'
{"name":"demo","version":"0.1.0","entry":"src/main.f"}
EOF
cat >"$T/site/.nift/packages/demo/src/main.f" <<'EOF'
fn(package_value()) { return 23 }
export(package_value)
EOF
cat >"$T/site/bare-package.f" <<'EOF'
import("demo")
print(package_value())
EOF
cat >"$T/site/legacy-package.f" <<'EOF'
@import("demo")
print(package_value())
EOF
[[ "$(cd "$T/site" && "$NIFT" bare-package.f)" == 23 ]]
[[ "$(cd "$T/site" && "$NIFT" legacy-package.f)" == 23 ]]

cat >"$T/scripts/boundaries.f" <<'EOF'
fn(important()) { return 1 }
fn(imported()) { return 2 }
fn(foo_import()) { return 3 }
print(important() + imported() + foo_import())
print("import(\"missing-string.f\")")
// import("missing-line.f")
/* import("missing-block.f") */
EOF
actual=$(cd "$T/scripts" && "$NIFT" boundaries.f)
[[ "$actual" == $'6\nimport("missing-string.f")' ]]

cat >"$T/scripts/malformed.f" <<'EOF'
print("first line")
    import(
EOF
if (cd "$T/scripts" && "$NIFT" malformed.f) >"$T/malformed.out" 2>"$T/malformed.err"; then
    echo 'bare malformed import unexpectedly succeeded' >&2
    exit 1
fi
grep -q "import has no matching ')'" "$T/malformed.err"
grep -q 'malformed.f:2:5' "$T/malformed.err"
if grep -q 'undefined callable\|@import' "$T/malformed.err"; then
    echo 'bare import diagnostic exposed translation details' >&2
    exit 1
fi
cat >"$T/scripts/same-line-invalid.f" <<'EOF'
print("before");    import(7)
EOF
if (cd "$T/scripts" && "$NIFT" same-line-invalid.f) >"$T/same-line.out" 2>"$T/same-line.err"; then
    echo 'same-line non-string import unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'same-line-invalid.f:1:21' "$T/same-line.err"
cat >"$T/scripts/comment-line-invalid.f" <<'EOF'
/* prior */    import(7)
EOF
if (cd "$T/scripts" && "$NIFT" comment-line-invalid.f) >"$T/comment-line.out" 2>"$T/comment-line.err"; then
    echo 'same-line comment import unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'comment-line-invalid.f:1:16' "$T/comment-line.err"
cat >"$T/scripts/invalid.f" <<'EOF'
import(7)
EOF
if (cd "$T/scripts" && "$NIFT" invalid.f) >"$T/invalid.out" 2>"$T/invalid.err"; then
    echo 'non-string bare import unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'import path must be a string expression' "$T/invalid.err"
cat >"$T/scripts/legacy-invalid.f" <<'EOF'
@import(7)
EOF
if (cd "$T/scripts" && "$NIFT" legacy-invalid.f) >"$T/legacy-invalid.out" 2>"$T/legacy-invalid.err"; then
    echo 'non-string legacy import unexpectedly succeeded' >&2
    exit 1
fi
grep -q '@import path must be a string expression' "$T/legacy-invalid.err"
cat >"$T/scripts/bad-return.f" <<'EOF'
return 1
EOF
cat >"$T/scripts/import-bad-return.f" <<'EOF'
import("bad-return.f")
EOF
if (cd "$T/scripts" && "$NIFT" import-bad-return.f) >"$T/bad-return.out" 2>"$T/bad-return.err"; then
    echo 'value-returning bare import unexpectedly succeeded' >&2
    exit 1
fi
grep -q 'import: return with a value is not allowed in import' "$T/bad-return.err"
if grep -q '@import' "$T/bad-return.err"; then
    echo 'nested bare import diagnostic exposed translation details' >&2
    exit 1
fi

mkdir "$T/project"
(cd "$T/project" && "$NIFT" init >/dev/null)
cat >"$T/project/content/module.f" <<'EOF'
fn(template_value()) { return 31 }
export(template_value)
EOF
cat >"$T/project/content/script-module.f" <<'EOF'
fn(script_value()) { return 37 }
export(script_value)
EOF
cat >"$T/project/content/index.html" <<'EOF'
@import("content/module.f")
template=$[template_value()]
import("content/missing-template.f")
@script { import("script-module.f"); return script_value() }
EOF
(cd "$T/project" && "$NIFT" build --all >/dev/null)
grep -q 'template=31' "$T/project/public/index.html"
grep -q 'import("content/missing-template.f")' "$T/project/public/index.html"
grep -q '37' "$T/project/public/index.html"

echo 'script import syntax: PASS'
