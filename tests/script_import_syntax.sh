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
# Review of C100-C102, P2-1 (combined-b3-b5.md section 92): every check reports a named FAIL line
# with its reason and status, and the script goes on; it exits non-zero at the end. A VM-default
# refusal is accepted only for a check that pins its reason, through approved_refusal (status
# approved or deferred-named, legacy oracle mandatory).
. "$(dirname "$0")/runtime2/approved_refusal.sh"
set -o pipefail  # restated after sourcing the helper (it does not change it), for the integrity check
failed=0
fail() { echo "FAIL $*" >&2; failed=1; }
refusal_of() { sed -n '/not compiled for the VM (/{s/.*not compiled for the VM (\([^)]*\)).*/\1/p;q;}' "$1"; }
status_of() { python3 -B "$APPROVED_REFUSAL_PY" status "$1"; }
# ok NAME DIR FILE EXPECTED-STDOUT
ok() {
  local rc=0 out reason
  out=$(cd "$2" && "$NIFT" "$3" 2>"$T/run.err") || rc=$?
  reason=$(refusal_of "$T/run.err")
  if [ -n "$reason" ]; then fail "$1: refused with $reason (inventory status $(status_of "$reason")), not pinned"
  elif [ "$rc" != 0 ] || [ "$out" != "$4" ]; then fail "$1: exit $rc, output [$out], stderr [$(cat "$T/run.err")]"; fi
}
# err NAME DIR FILE LEGACY-STDOUT PINNED-REASON|- TEXT...: the run must fail and its stderr contain every
# TEXT. Under the VM default a refusal is accepted only when it is PINNED-REASON (approved_refusal with
# the legacy oracle exit 1, LEGACY-STDOUT and the first TEXT); otherwise it is named with its status.
err() {
  local name=$1 dir=$2 file=$3 lout=$4 pin=$5 reason text; shift 5
  if (cd "$dir" && "$NIFT" "$file") >"$T/run.out" 2>"$T/run.err"; then fail "$name: unexpectedly succeeded"; return; fi
  reason=$(refusal_of "$T/run.err")
  if [ -n "$reason" ]; then
    if [ "$reason" = "$pin" ]; then
      (cd "$dir" && approved_refusal "$NIFT" "$name" "$pin" "pinned" "$file" "$lout" 1 "$1") || failed=1
    else
      fail "$name: refused with $reason (inventory status $(status_of "$reason")) instead of the expected error"
    fi
    return
  fi
  for text in "$@"; do grep -qF -- "$text" "$T/run.err" || fail "$name: stderr lacks [$text]: $(cat "$T/run.err")"; done
}
ok "bare import" "$T/scripts" bare.f 42
ok "spaced import" "$T/scripts" spaced.f 42
ok "@import" "$T/scripts" legacy.f 42
# Owner 2026-11-10 13:26, item 5 (combined-b3-b5.md section 81): a function named like a built-in
# (`import`) is refused under the VM default (fn-name-reserved). The session refuses that
# first statement before running it and stops: nothing ran. Legacy (NIFT_VM=0) keeps the original
# check (the import statement is not overridden by the user callable).
# Review of C83-C99, P1-1/P1-2 (combined-b3-b5.md section 89): the refusal counts only while its
# inventory status (classification.tsv) is approved or deferred-named; fn-name-reserved is PENDING-OWNER.
status=$(python3 -B "$(dirname "$0")/runtime2/approved_refusal.py" status fn-name-reserved)
if [ "$status" != approved ] && [ "$status" != deferred-named ]; then
    fail "REPL fn named import: refused with fn-name-reserved, whose inventory status is $status (only approved or deferred-named may be accepted)"
fi
rc=0
printf '%s\n' 'fn(import(x)) { return 99 }' 'import("module.f")' 'answer()' > "$T/repl.in"
repl=$(cd "$T/scripts" && "$NIFT" < "$T/repl.in" 2>&1) || rc=$?
if [[ $rc -ne 1 ]] || ! grep -qF 'NIFT_VM session refused: fn-name-reserved:' <<<"$repl" \
   || ! grep -qF 'note: this statement was not executed' <<<"$repl" || grep -q '42\|99' <<<"$repl"; then
    fail "REPL fn named import: not the session refusal fn-name-reserved: exit $rc: $repl"
fi
rc=0
repl=$(cd "$T/scripts" && NIFT_VM=0 "$NIFT" < "$T/repl.in" 2>&1) || rc=$?
if [[ $rc -ne 0 ]] || ! grep -q '42' <<<"$repl"; then
    fail "REPL import statement (legacy channel): exit $rc: $repl"
elif grep -q '99' <<<"$repl"; then
    fail 'REPL callable dispatch overrode the import statement (legacy channel)'
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
ok "nested import" "$T/scripts" nested.f 17

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
ok "bare package import" "$T/site" bare-package.f 23
ok "@import package" "$T/site" legacy-package.f 23

cat >"$T/scripts/boundaries.f" <<'EOF'
fn(important()) { return 1 }
fn(imported()) { return 2 }
fn(foo_import()) { return 3 }
print(important() + imported() + foo_import())
print("import(\"missing-string.f\")")
// import("missing-line.f")
/* import("missing-block.f") */
EOF
ok "import word boundaries" "$T/scripts" boundaries.f $'6\nimport("missing-string.f")'

cat >"$T/scripts/malformed.f" <<'EOF'
print("first line")
    import(
EOF
# The VM refuses the malformed import as opaque:template-text (PENDING-OWNER): pinned, still failing.
err "bare malformed import" "$T/scripts" malformed.f "first line" opaque:template-text "import has no matching ')'" 'malformed.f:2:5'
if grep -q 'undefined callable\|@import' "$T/run.err"; then fail 'bare malformed import: the diagnostic exposed translation details'; fi
cat >"$T/scripts/same-line-invalid.f" <<'EOF'
print("before");    import(7)
EOF
err "same-line non-string import" "$T/scripts" same-line-invalid.f before import-dynamic 'import path must be a string expression' 'same-line-invalid.f:1:21'
cat >"$T/scripts/comment-line-invalid.f" <<'EOF'
/* prior */    import(7)
EOF
err "same-line comment import" "$T/scripts" comment-line-invalid.f "" import-dynamic 'import path must be a string expression' 'comment-line-invalid.f:1:16'
cat >"$T/scripts/invalid.f" <<'EOF'
import(7)
EOF
err "non-string bare import" "$T/scripts" invalid.f "" import-dynamic 'import path must be a string expression'
cat >"$T/scripts/legacy-invalid.f" <<'EOF'
@import(7)
EOF
err "non-string @import" "$T/scripts" legacy-invalid.f "" import-dynamic '@import path must be a string expression'
cat >"$T/scripts/bad-return.f" <<'EOF'
return 1
EOF
cat >"$T/scripts/import-bad-return.f" <<'EOF'
import("bad-return.f")
EOF
err "value-returning bare import" "$T/scripts" import-bad-return.f "" - 'import: return with a value is not allowed in import'
if grep -q '@import' "$T/run.err"; then fail 'value-returning bare import: the diagnostic exposed translation details'; fi

mkdir "$T/project"
(cd "$T/project" && "$NIFT" init >/dev/null) || fail "project init"
cat >"$T/project/content/module.f" <<'EOF'
fn(template_value()) { return 31 }
export(template_value)
EOF
cat >"$T/project/content/script-module.f" <<'EOF'
fn(script_value()) { return 37 }
export(script_value)
EOF
cat >"$T/project/content/index.html" <<'EOF'
@import("module.f")
template=$[template_value()]
import("content/missing-template.f")
@script { import("script-module.f"); return script_value() }
EOF
if (cd "$T/project" && "$NIFT" build --all >/dev/null 2>"$T/build.err"); then
    for text in 'template=31' 'import("content/missing-template.f")' '37'; do
        grep -qF -- "$text" "$T/project/public/index.html" || fail "template and @script imports: page lacks [$text]"
    done
else
    fail "template and @script imports: build failed: $(cat "$T/build.err")"
fi

[ "$failed" = 0 ] || { echo 'script import syntax: FAIL' >&2; exit 1; }
echo 'script import syntax: PASS'
