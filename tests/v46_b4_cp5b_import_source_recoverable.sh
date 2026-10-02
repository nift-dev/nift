#!/usr/bin/env bash
set -euo pipefail

NIFT=${NIFT_BIN:-${NIFT:-./nift}}
case "$NIFT" in /*) ;; *) NIFT="$(pwd)/$NIFT" ;; esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/wd"
cd "$TMP/wd"

# ---------------------------------------------------------------- direct missing source recoverable
[[ "$("$NIFT" -e 'try { import("./missing-file.f") } catch(e) { print(e.code); print(e.category); print(e.source != ""); print(e.line > 0); print(e.message) }')" == $'io.import_source_unreadable\nio\ntrue\ntrue\nscript is not readable: '"$(pwd)/missing-file.f" ]]

# uncaught compatibility: message preserved, exit nonzero
if "$NIFT" -e 'import("./missing-file.f")' >"$TMP/u.out" 2>"$TMP/u.err"; then
    echo 'uncaught missing import unexpectedly succeeded' >&2; exit 1
fi
grep -q "import: script is not readable: $(pwd)/missing-file.f" "$TMP/u.err" || {
    echo 'uncaught missing import message mismatch' >&2; cat "$TMP/u.err" >&2; exit 1
}
if grep -q '"code"\|"cause"\|"category"' "$TMP/u.err"; then
    echo 'uncaught missing import exposed Error serialization' >&2; exit 1
fi

# ---------------------------------------------------------------- nested missing source: frame stack
printf 'import("./missing-leaf.f")\n' > mid.f
[[ "$("$NIFT" -e 'try { import("./mid.f") } catch(e) { print(e.code) }')" == 'io.import_source_unreadable' ]]
if "$NIFT" -e 'import("./mid.f")' >/dev/null 2>"$TMP/n.err"; then
    echo 'nested missing import unexpectedly succeeded' >&2; exit 1
fi
grep -q 'import: import: script is not readable' "$TMP/n.err" || {
    echo 'nested missing import frame stack mismatch' >&2; cat "$TMP/n.err" >&2; exit 1
}

# ---------------------------------------------------------------- package not installed / source unreadable
mkdir -p site/.nift/packages/demo/src
cd site
printf '{"dependencies":{"demo":{"source":"./demo","ref":"local"}}}\n' > manifest.json
printf '{"demo":{"source":"./demo","requested":"local","commit":"local"}}\n' > .nift/packages.lock.json
printf '{"name":"demo","version":"1.0.0","entry":"src/main.f"}\n' > .nift/packages/demo/manifest.json
printf 'fn(demo_hello()) { return 42 }\nexport(demo_hello)\n' > .nift/packages/demo/src/main.f

[[ "$("$NIFT" -e 'import("demo"); print(demo_hello())')" == '42' ]]

rm -rf .nift/packages/demo
[[ "$("$NIFT" -e 'try { import("demo") } catch(e) { print(e.code); print(e.category); print(e.message) }')" == $'package.not_installed\npackage\npackage is not installed: demo' ]]
if "$NIFT" -e 'import("demo")' >/dev/null 2>"$TMP/pn.err"; then
    echo 'uncaught package-not-installed unexpectedly succeeded' >&2; exit 1
fi
grep -q 'import: package is not installed: demo' "$TMP/pn.err" || { echo 'package-not-installed message mismatch' >&2; exit 1; }

mkdir -p .nift/packages/demo
printf '{"name":"demo","version":"1.0.0","entry":"src/main.f"}\n' > .nift/packages/demo/manifest.json
[[ "$("$NIFT" -e 'try { import("demo") } catch(e) { print(e.code); print(e.category) }')" == $'package.import_source_unreadable\npackage' ]]

# malformed package manifest stays fatal
printf '{"entry":"src/main.f"}\n' > .nift/packages/demo/manifest.json
if "$NIFT" -e 'try { import("demo") } catch(e) { print("CAUGHT") }' >"$TMP/pm.out" 2>"$TMP/pm.err"; then
    echo 'malformed package manifest unexpectedly succeeded' >&2; exit 1
fi
grep -q 'CAUGHT' "$TMP/pm.out" && { echo 'malformed package manifest was caught' >&2; exit 1; }
grep -q 'invalid package manifest' "$TMP/pm.err" || { echo 'malformed manifest message mismatch' >&2; exit 1; }
cd "$TMP/wd"

# ---------------------------------------------------------------- fatal stays fatal
printf 'if true { print("x"\n' > syntax_bad.f
for script in \
    'try { import(42) } catch(e) { print("CAUGHT") }' \
    'try { import("./syntax_bad.f") } catch(e) { print("CAUGHT") }'; do
    if "$NIFT" -e "$script" >"$TMP/ff.out" 2>"$TMP/ff.err"; then
        echo "fatal import misuse unexpectedly succeeded: $script" >&2; exit 1
    fi
    grep -q 'CAUGHT' "$TMP/ff.out" && { echo "fatal import misuse was caught: $script" >&2; exit 1; }
done

printf 'import("./cyc_b.f")\n' > cyc_a.f
printf 'import("./cyc_a.f")\n' > cyc_b.f
if "$NIFT" -e 'try { import("./cyc_a.f") } catch(e) { print("CAUGHT") }' >"$TMP/cy.out" 2>"$TMP/cy.err"; then
    echo 'import cycle unexpectedly succeeded' >&2; exit 1
fi
grep -q 'CAUGHT' "$TMP/cy.out" && { echo 'import cycle was caught' >&2; exit 1; }
grep -q 'import cycle' "$TMP/cy.err" || { echo 'import cycle message mismatch' >&2; exit 1; }

# ---------------------------------------------------------------- rollback before catch
cat > outer.f <<'NIFT'
import("./missing-inner.f")
outer_var := 99
export(outer_fn)
fn(outer_fn()) { return 1 }
NIFT
cat > main_rb.f <<'NIFT'
try { import("./outer.f") } catch(e) { print("caught:" + e.code) }
print(exists("outer_var"))
NIFT
[[ "$("$NIFT" main_rb.f 2>&1)" == $'caught:io.import_source_unreadable\nfalse' ]]

# ---------------------------------------------------------------- worker-owning failed import stays fatal
cat > outer_w.f <<'NIFT'
fn(work()) { return 1 }
w := thread(work)
import("./missing-inner.f")
NIFT
cat > main_w.f <<'NIFT'
try { import("./outer_w.f") } catch(e) { print("caught") }
NIFT
if "$NIFT" main_w.f >/dev/null 2>"$TMP/w.err"; then
    echo 'worker-owning failed import unexpectedly recovered' >&2; exit 1
fi
grep -q 'failed import created worker resources' "$TMP/w.err" || { echo 'worker-owning fatal message mismatch' >&2; exit 1; }

# ---------------------------------------------------------------- REPL recovery
printf 'fn(good()) { return 7 }\nexport(good)\n' > mod_good.f
repl=$((printf 'try { import("./missing-repl.f") } catch(e) { print("caught:" + e.code) }\nimport("./mod_good.f")\nprint(good())\nquit\n') | "$NIFT" 2>&1)
grep -q 'caught:io.import_source_unreadable' <<<"$repl" || { echo 'REPL caught missing import failed' >&2; exit 1; }
grep -q '^7$' <<<"$repl" || { echo 'REPL import recovery failed' >&2; exit 1; }

# ---------------------------------------------------------------- relative import ownership unchanged
mkdir -p sub
printf 'fn(val()) { return 10 }\nexport(val)\n' > sub/child.f
printf 'import("./child.f")\nfn(pick()) { return val() }\nexport(pick)\n' > sub/parent.f
printf 'import("./sub/parent.f")\nprint(pick())\n' > rel_main.f
[[ "$("$NIFT" rel_main.f 2>&1)" == '10' ]]

echo 'v4.6 Batch 4 CP5b import-source recoverable: PASS'