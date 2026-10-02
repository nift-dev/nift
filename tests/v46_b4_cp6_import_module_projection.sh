#!/usr/bin/env bash
set -euo pipefail

NIFT=${NIFT_BIN:-${NIFT:-./nift}}
case "$NIFT" in /*) ;; *) NIFT="$(pwd)/$NIFT" ;; esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/wd"
cd "$TMP/wd"

# ---------------------------------------------------------------- direct / nested import frames
printf 'value := 1 / 0\n' > bad.f
printf 'import("./bad.f")\n' > mid.f
printf 'import("./mid.f")\n' > top.f
# one import boundary -> one "import: " frame at the importer (fatal projection pinned)
[[ "$("$NIFT" top.f 2>&1)" == *'import: import: division by zero'* ]]
# direct script failure projects the defining source
[[ "$("$NIFT" bad.f 2>&1)" == *"$(pwd)/bad.f:1:1: division by zero"* ]]

# ---------------------------------------------------------------- recoverable crossing imports: defining origin
printf 'throw error("deep", "user.deep")\n' > leaf.f
printf 'import("./leaf.f")\n' > mid2.f
printf 'try { import("./mid2.f") } catch(e) { print(e.code); print(e.source) }\n' > top2.f
[[ "$("$NIFT" top2.f 2>&1)" == $'user.deep\n'"$(pwd)/leaf.f" ]]
printf 'import("./leaf.f")\n' > mid3.f
printf 'import("./mid3.f")\n' > top3.f
[[ "$("$NIFT" top3.f 2>&1; echo X)" == *'import: import: deep'*'X' ]]

# ---------------------------------------------------------------- imported callable: recoverable origin = defining source
printf 'fn(trow()) { throw error("inner", "user.inner") }\nexport(trow)\n' > mod_trow.f
printf 'import("./mod_trow.f")\ntry { trow() } catch(e) { print(e.code); print(e.source) }\n' > imp_trow.f
[[ "$("$NIFT" imp_trow.f 2>&1)" == $'user.inner\n'"$(pwd)/mod_trow.f" ]]

# ---------------------------------------------------------------- relative import ownership
mkdir -p sub
cat > sub/child.f <<'NIFT'
fn(val()) { return 10 }
export(val)
NIFT
cat > sub/parent.f <<'NIFT'
import("./child.f")
fn(pick()) { return val() }
export(pick)
NIFT
printf 'import("./sub/parent.f")\nprint(pick())\n' > rel_main.f
[[ "$("$NIFT" rel_main.f 2>&1)" == '10' ]]

# ---------------------------------------------------------------- module_path / package_path provenance
printf 'print(module_path())\n' > modp.f
[[ "$("$NIFT" modp.f 2>&1)" == "$(pwd)" ]]

# ---------------------------------------------------------------- rollback-before-catch
cat > mod_rb.f <<'NIFT'
struct(box) { value := 1 }
fn(made()) { return 42 }
box_instance := box()
throw error("fail", "user.fail")
NIFT
cat > imp_rb.f <<'NIFT'
try { import("./mod_rb.f") } catch(e) { print("caught:" + e.code) }
print(exists("box_instance"))
try { made() } catch(e) { print("made:" + e.code) }
NIFT
[[ "$("$NIFT" imp_rb.f 2>&1)" == $'caught:user.fail\nfalse\nerror: *undefined callable: made' ]] || \
  [[ "$("$NIFT" imp_rb.f 2>&1)" == *'caught:user.fail'*'false'*'undefined callable: made'* ]]

# ---------------------------------------------------------------- worker-owning failed import -> fatal
cat > mod_w.f <<'NIFT'
fn(work()) { return 1 }
w := thread(work)
throw error("unsafe", "user.unsafe")
NIFT
cat > imp_w.f <<'NIFT'
try { import("./mod_w.f") } catch(e) { print("caught") }
NIFT
if "$NIFT" imp_w.f >/dev/null 2>"$TMP/w.err"; then
    echo 'worker-owning failed import unexpectedly recovered' >&2; exit 1
fi
grep -q 'failed import created worker resources' "$TMP/w.err" || { echo 'worker-owning fatal message mismatch' >&2; exit 1; }

# ---------------------------------------------------------------- @script recovery + template/import frame
cat > tmpl_script.f <<'NIFT'
@script {
    try { throw error("t", "user.t") } catch(e) { print("caught:" + e.code) }
}
NIFT
[[ "$("$NIFT" tmpl_script.f 2>&1)" == 'caught:user.t' ]]
printf 'fn(helper()) { return 1 / 0 }\nexport(helper)\n' > mod_helper.f
cat > tmpl_call.f <<'NIFT'
import("./mod_helper.f")
@script { helper() }
NIFT
# imported callable fatal projection keeps the call site (legacy compatibility surface)
if "$NIFT" tmpl_call.f >"$TMP/t.out" 2>"$TMP/t.err"; then
    echo 'template imported callable failure unexpectedly succeeded' >&2; exit 1
fi
grep -q 'return: division by zero' "$TMP/t.err" || { echo 'template imported callable message mismatch' >&2; exit 1; }

# ---------------------------------------------------------------- worker/async from imported code
printf 'fn(trow2()) { throw error("from-import", "user.fi") }\nexport(trow2)\n' > mod_wimp.f
printf 'import("./mod_wimp.f")\nw := thread(trow2)\ntry { w.join() } catch(e) { print(e.code); print(e.source != "") }\n' > wimp_main.f
[[ "$("$NIFT" wimp_main.f 2>&1)" == $'user.fi\ntrue' ]]
printf '@fn[async](af()) { throw error("f-imp", "user.f") }\nexport(af)\n' > mod_async.f
printf 'import("./mod_async.f")\nh := af()\ntry { await h } catch(e) { print(e.code); print(e.category) }\n' > async_main.f
[[ "$("$NIFT" async_main.f 2>&1)" == $'user.f\nuser' ]]

# ---------------------------------------------------------------- in-memory source label
[[ "$("$NIFT" -e 'value := 1 / 0' 2>&1)" == 'error: <command-line>:1:1: division by zero' ]]

# ---------------------------------------------------------------- single import execution
rm -f side.txt
cat > mod_se.f <<'NIFT'
import("./mod_se_child.f")
fn(show()) { return 1 }
export(show)
NIFT
cat > mod_se_child.f <<'NIFT'
f := file("side.txt")
f.open("w")
f.write("x")
f.save()
f.close()
NIFT
printf 'import("./mod_se.f")\nprint(show())\n' > se_main.f
[[ "$("$NIFT" se_main.f 2>&1)" == '1' ]]
[[ -f side.txt ]] || { echo 'import did not execute' >&2; exit 1; }

# ---------------------------------------------------------------- REPL: modern import executes, failed import reports
printf 'fn(good()) { return 7 }\nexport(good)\n' > mod_good.f
printf 'import("./mod_good.f")\nprint(good())\nquit\n' | "$NIFT" 2>&1 | grep -q '^7$' || {
    echo 'REPL modern import did not install exports' >&2; exit 1
}
printf 'import("./missing_mod.f")\nimport("./mod_good.f")\nprint(good())\nquit\n' | "$NIFT" 2>&1 | grep -q '^7$' || {
    echo 'REPL import recovery after failed import failed' >&2; exit 1
}
printf 'try { throw error("old","user.old") } catch(e) { print("caught:" + e.code) }\nvalue := 1 / 0\nprint("alive")\nquit\n' | "$NIFT" 2>&1 | grep -q 'alive' || {
    echo 'REPL not usable after recoverable then fatal' >&2; exit 1
}

# ---------------------------------------------------------------- uncaught compatibility
[[ "$("$NIFT" bad.f 2>&1)" == *'division by zero'* ]]
[[ "$("$NIFT" top.f 2>&1)" == *'import: import: division by zero'* ]]

echo 'v4.6 Batch 4 CP6 import/module projection: PASS'