#!/usr/bin/env bash
set -euo pipefail

NIFT=${NIFT_BIN:-${NIFT:-./nift}}
case "$NIFT" in /*) ;; *) NIFT="$(pwd)/$NIFT" ;; esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/wd"
cd "$TMP/wd"

# ---------------------------------------------------------------- repeated await replay
repl=$("$NIFT" -e '
fn[async](af()) { throw error("future-fail", "user.f") }
h := af()
try { await h } catch(e) { print(e.code + "|" + e.message + "|" + (e.source != "")) }
try { await h } catch(e) { print(e.code + "|" + e.message) }
')
[[ "$repl" == $'user.f|future-fail|true\nuser.f|future-fail' ]] || { echo "await replay mismatch: $repl" >&2; exit 1; }

# ---------------------------------------------------------------- repeated join replay
repl=$("$NIFT" -e '
fn(tf()) { throw error("thread-fail", "user.t") }
t := thread(tf)
try { t.join() } catch(e) { print(e.code + "|" + e.message) }
try { t.join() } catch(e) { print(e.code + "|" + e.message) }
')
[[ "$repl" == $'user.t|thread-fail\nuser.t|thread-fail' ]] || { echo "join replay mismatch: $repl" >&2; exit 1; }

# ---------------------------------------------------------------- fatal worker failure bypasses catch
if "$NIFT" -e '
fn[async](af()) { return 1 / 0 }
h := af()
try { await h } catch(e) { print("CAUGHT") }
' >"$TMP/fatal.out" 2>"$TMP/fatal.err"; then
    echo 'fatal async failure unexpectedly recovered' >&2; exit 1
fi
grep -q 'CAUGHT' "$TMP/fatal.out" && { echo 'fatal async failure was caught' >&2; exit 1; }
grep -q 'division by zero' "$TMP/fatal.err" || { echo 'fatal async message mismatch' >&2; exit 1; }

if "$NIFT" -e '
fn(tf()) { value := 1 / 0 }
t := thread(tf)
try { t.join() } catch(e) { print("CAUGHT") }
' >"$TMP/fatal2.out" 2>"$TMP/fatal2.err"; then
    echo 'fatal thread failure unexpectedly recovered' >&2; exit 1
fi
grep -q 'CAUGHT' "$TMP/fatal2.out" && { echo 'fatal thread failure was caught' >&2; exit 1; }

# ---------------------------------------------------------------- nested pool progress / no starvation
[[ "$("$NIFT" -e 'fn[async](child()) { return 42 }; fn[async](parent()) { c := child(); return await c }; print(await parent())')" == '42' ]]
[[ "$("$NIFT" -e 'fn[async](af()) { return 7 }; fn(tf()) { h := af(); return await h }; t := thread(tf); print(t.join())')" == '7' ]]
[[ "$("$NIFT" -e 'fn(wf()) { return 9 }; fn[async](af()) { t := thread(wf); return t.join() }; print(await af())')" == '9' ]]
# several parents each awaiting children
[[ "$("$NIFT" -e 'fn[async](child(v)) { return v * 2 }; fn[async](parent(v)) { return await child(v) }; a := parent(1); b := parent(2); c := parent(3); print(await a); print(await b); print(await c)')" == $'2\n4\n6' ]]

# ---------------------------------------------------------------- unobserved teardown (clean, no terminate/leak)
[[ "$("$NIFT" -e 'fn[async](af()) { throw error("unobserved","user.u") }; h := af(); print("alive")')" == 'alive' ]]
[[ "$("$NIFT" -e 'fn(tf()) { value := 1 / 0 }; t := thread(tf); print("alive")')" == 'alive' ]]

# ---------------------------------------------------------------- worker provenance (imported + package)
printf 'fn(timp()) { throw error("from-import","user.fi") }\nexport(timp)\n' > mod_wimp.f
cat > wimp.f <<NIFT
import("./mod_wimp.f")
t := thread(timp)
try { t.join() } catch(e) { print(e.code); print(e.source == "$TMP/wd/mod_wimp.f") }
NIFT
[[ "$("$NIFT" wimp.f 2>&1)" == $'user.fi\ntrue' ]]

mkdir -p pkg/.nift/packages/demo/src
printf '{"dependencies":{"demo":{"source":"./demo","ref":"local"}}}\n' > pkg/manifest.json
printf '{"demo":{"source":"./demo","requested":"local","commit":"local"}}\n' > pkg/.nift/packages.lock.json
printf '{"name":"demo","version":"1.0.0","entry":"src/main.f"}\n' > pkg/.nift/packages/demo/manifest.json
printf 'fn(pkg_work()) { throw error("pkg-worker","user.pk") }\nexport(pkg_work)\n' > pkg/.nift/packages/demo/src/main.f
(cd pkg && printf 'import("demo")\nt := thread(pkg_work)\ntry { t.join() } catch(e) { print(e.code); print(e.category) }\n' > pkgw.f && "$NIFT" pkgw.f 2>&1) | { IFS= read -r c; IFS= read -r cat; [[ "$c" == 'user.pk' && "$cat" == 'user' ]] || { echo "package worker provenance mismatch" >&2; exit 1; }; }

# ---------------------------------------------------------------- failed-import worker lifecycle fatal rule
cat > mod_w.f <<'NIFT'
fn(work()) { return 1 }
w := thread(work)
import("./missing-inner.f")
NIFT
cat > main_w.f <<'NIFT'
try { import("./mod_w.f") } catch(e) { print("caught") }
NIFT
if "$NIFT" main_w.f >/dev/null 2>"$TMP/w.err"; then
    echo 'worker-owning failed import unexpectedly recovered' >&2; exit 1
fi
grep -q 'failed import created worker resources' "$TMP/w.err" || { echo 'worker-owning fatal message mismatch' >&2; exit 1; }

# ---------------------------------------------------------------- worker-owned resource lifetime
[[ "$("$NIFT" -e 'fn(wf()) { o := ofstream("w.txt"); o.write("made"); o.close(); return 1 }; t := thread(wf); print(t.join()); print(open("w.txt"))')" == $'1\nmade' ]]

# ---------------------------------------------------------------- transfer restrictions
if "$NIFT" -e 'fn(wt()) { return timer() }; t := thread(wt); t.join(); print("after")' >"$TMP/tr.out" 2>"$TMP/tr.err"; then
    echo 'timer transfer unexpectedly succeeded' >&2; exit 1
fi
grep -q 'non-transferable timer' "$TMP/tr.err" || { echo 'timer transfer message mismatch' >&2; exit 1; }

echo 'v4.6 Batch 4 CP7 worker hardening: PASS'