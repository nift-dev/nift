#!/usr/bin/env bash
# Worker parsers own deep module snapshots; imports and mutable module state do
# not race or persist back into parent/sibling workers.
set -euo pipefail
NIFT=${NIFT:-${NIFT_BIN:-./nift}}
case "$NIFT" in /*) NIFT_ABS="$NIFT";; *) NIFT_ABS="$(pwd)/$NIFT";; esac
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
site="$t/site"
mkdir -p "$site/.nift/packages/worker/src" "$site/.nift/packages/left/src" "$site/.nift/packages/right/src" "$site/.nift/packages/async-owner/src" "$site/.nift/packages/init-worker/src" "$site/.nift/packages/alias-owner/src"
printf '{"name":"worker","version":"0.1.0","entry":"src/main.f"}\n' > "$site/.nift/packages/worker/manifest.json"
printf '{"name":"left","version":"0.1.0","entry":"src/main.f"}\n' > "$site/.nift/packages/left/manifest.json"
printf '{"name":"right","version":"0.1.0","entry":"src/main.f"}\n' > "$site/.nift/packages/right/manifest.json"
printf '{"name":"async-owner","version":"0.1.0","entry":"src/main.f"}\n' > "$site/.nift/packages/async-owner/manifest.json"
printf '{"name":"init-worker","version":"0.1.0","entry":"src/main.f"}\n' > "$site/.nift/packages/init-worker/manifest.json"
printf '{"name":"alias-owner","version":"0.1.0","entry":"src/main.f"}\n' > "$site/.nift/packages/alias-owner/manifest.json"
printf '{"dependencies":{"alias-owner":{"source":"./alias-owner","ref":"local"},"async-owner":{"source":"./async-owner","ref":"local"},"init-worker":{"source":"./init-worker","ref":"local"},"left":{"source":"./left","ref":"local"},"right":{"source":"./right","ref":"local"},"worker":{"source":"./worker","ref":"local"}}}\n' > "$site/manifest.json"
printf '{"alias-owner":{"source":"./alias-owner","requested":"local","commit":"local"},"async-owner":{"source":"./async-owner","requested":"local","commit":"local"},"init-worker":{"source":"./init-worker","requested":"local","commit":"local"},"left":{"source":"./left","requested":"local","commit":"local"},"right":{"source":"./right","requested":"local","commit":"local"},"worker":{"source":"./worker","requested":"local","commit":"local"}}\n' > "$site/.nift/packages.lock.json"
cat > "$site/.nift/packages/worker/src/child.f" <<'F'
fn(child_value(x)) { return x }
export(child_value)
F
cat > "$site/.nift/packages/worker/src/main.f" <<'F'
counter := [0]
fn(worker_job(x)) {
  sleep(20)
  counter[0] += 1
  import("./child.f")
  return child_value(x) + counter[0]
}
fn(parent_counter()) { return counter[0] }
export(worker_job)
export(parent_counter)
F
cat > "$site/.nift/packages/left/src/main.f" <<'F'
fn(same()) { return "left" }
left_cb := same
export(left_cb)
F
cat > "$site/.nift/packages/right/src/main.f" <<'F'
fn(same()) { return "right" }
right_cb := same
export(right_cb)
F
cat > "$site/.nift/packages/async-owner/src/child.f" <<'F'
@fn[async](same_name()) { return "child" }
export(same_name)
F
cat > "$site/.nift/packages/async-owner/src/main.f" <<'F'
import("./child.f")
child_async := same_name
@fn[async](same_name()) { return child_async() }
export(same_name)
F
cat > "$site/.nift/packages/init-worker/src/main.f" <<'F'
seed := 40
fn(init_thread_job()) { return seed + 2 }
@fn[async](init_async_job()) { return seed + 3 }
init_thread := thread(init_thread_job)
init_future := init_async_job()
thread_value := init_thread.join()
async_value := await init_future
export(thread_value)
export(async_value)
F
cat > "$site/.nift/packages/alias-owner/src/child.f" <<'F'
shared := [[0]]
fn(bump()) { shared[0][0] += 1; return shared[0][0] }
export(shared)
export(bump)
F
cat > "$site/.nift/packages/alias-owner/src/main.f" <<'F'
import("./child.f")
alias := shared
slot := shared[0]
fn(mutate()) {
  bump()
  alias[0][0] += 10
  slot[0] += 100
  return [shared[0][0], alias[0][0], slot[0]]
}
fn(state()) { return [shared[0][0], alias[0][0], slot[0]] }
@fn[async](mutate_async()) { return mutate() }
export(mutate)
export(state)
export(mutate_async)
F
# Review of C100-C102, P2-1 (combined-b3-b5.md section 92): every check below reports a named FAIL
# line (a VM-default refusal with its reason and inventory status) and the script goes on; it exits
# non-zero at the end. A VM-default refusal is accepted only for a check that pins its reason, through
# approved_refusal (status approved or deferred-named, legacy oracle mandatory).
. "$(cd "$(dirname "$0")" && pwd)/runtime2/approved_refusal.sh"
set -o pipefail  # restated after sourcing the helper (it does not change it), for the integrity check
failed=0
fail() { echo "FAIL $*" >&2; failed=1; }
refusal_of() { sed -n '/not compiled for the VM (/{s/.*not compiled for the VM (\([^)]*\)).*/\1/p;q;}' "$1"; }
status_of() { python3 -B "$(cd "$(dirname "$0")" && pwd)/runtime2/approved_refusal.py" status "$1"; }
vm_ok() {  # vm_ok NAME FILE EXPECTED-STDOUT [PINNED-REASON DECISION] (run in $site)
  local rc=0 out reason
  out=$(cd "$site" && "$NIFT_ABS" "$2" 2>"$t/vm.err") || rc=$?
  reason=$(refusal_of "$t/vm.err")
  if [ -n "$reason" ] && [ $# -ge 5 ] && [ "$reason" = "$4" ]; then
    (cd "$site" && approved_refusal "$NIFT_ABS" "$1" "$4" "$5" "$2" "$3") || failed=1
  elif [ -n "$reason" ]; then fail "$1: refused with $reason (inventory status $(status_of "$reason"))"
  elif [ "$rc" != 0 ] || [ "$out" != "$3" ]; then fail "$1: exit $rc, output [$out], stderr [$(cat "$t/vm.err")]"; fi
}
cat > "$site/module-init.f" <<'F'
import("init-worker")
print(thread_value)
print(async_value)
F
vm_ok 'module initialization worker keeps its loading module' module-init.f "42
43" coro-module-init-task "D-064 #2"
cat > "$site/alias-direct.f" <<'F'
import("alias-owner")
print(mutate() == [111, 111, 111])
print(state() == [111, 111, 111])
F
vm_ok 'direct cross-module alias/reference behaviour' alias-direct.f "true
true"
cat > "$site/alias-workers.f" <<'F'
import("alias-owner")
worker := thread(mutate)
print(worker.join() == [111, 111, 111])
print(state() == [0, 0, 0])
future := mutate_async()
async_result := await future
print(async_result == [111, 111, 111])
print(state() == [0, 0, 0])
F
vm_ok 'worker alias/reference cloning and parent isolation' alias-workers.f "true
true
true
true" coro-module-capture "D-064 #3"
cat > "$site/direct.f" <<'F'
import("worker")
print(worker_job(1))
F
vm_ok 'direct nested package import' direct.f 2 import-nested "D-065 1"
cat > "$site/one-worker.f" <<'F'
import("worker")
w := thread(worker_job, 1)
print(w.join())
F
vm_ok 'single worker nested package import' one-worker.f 2 import-nested "D-065 1"
cat > "$site/exact-async-owner.f" <<'F'
import("async-owner")
future := same_name()
print(await future)
F
if (cd "$site" && "$NIFT_ABS" exact-async-owner.f >/dev/null 2>"$t/exact-async.err"); then
  fail 'async worker mutated a same-name callable in another module'
elif [ -n "$(refusal_of "$t/exact-async.err")" ] && [ "$(refusal_of "$t/exact-async.err")" != coro-module-private-async ]; then
  fail "module-private async callables: refused with $(refusal_of "$t/exact-async.err") (inventory status $(status_of "$(refusal_of "$t/exact-async.err")"))"
elif [ -z "$(refusal_of "$t/exact-async.err")" ] && ! grep -F 'module-private async callables are unsupported' "$t/exact-async.err" >/dev/null; then
  fail "module-private async callables: stderr [$(cat "$t/exact-async.err")]"
fi
cat > "$site/main.f" <<'F'
import("worker")
import("left")
import("right")
a := thread(worker_job, 10)
b := thread(worker_job, 20)
l := thread(left_cb)
r := thread(right_cb)
print(a.join())
print(b.join())
print(l.join())
print(r.join())
print(parent_counter())
print(worker_job(30))
print(parent_counter())
F
vm_ok 'worker module isolation output' main.f "11
21
left
right
0
31
1" import-nested "D-065 1"

[ "$failed" = 0 ] || { echo 'FAIL v4.6 worker module import ownership' >&2; exit 1; }
echo 'PASS v4.6 worker module import ownership'
