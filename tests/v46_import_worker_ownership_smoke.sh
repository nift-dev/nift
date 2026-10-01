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
cat > "$site/.nift/packages/worker/src/child.f" <<'F'
@fn(child_value(x)) { return x }
export(child_value)
F
cat > "$site/.nift/packages/worker/src/main.f" <<'F'
counter := [0]
@fn(worker_job(x)) {
  sleep(20)
  counter[0] += 1
  import("./child.f")
  return child_value(x) + counter[0]
}
@fn(parent_counter()) { return counter[0] }
export(worker_job)
export(parent_counter)
F
cat > "$site/.nift/packages/left/src/main.f" <<'F'
@fn(same()) { return "left" }
left_cb := same
export(left_cb)
F
cat > "$site/.nift/packages/right/src/main.f" <<'F'
@fn(same()) { return "right" }
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
@fn(init_thread_job()) { return seed + 2 }
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
@fn(bump()) { shared[0][0] += 1; return shared[0][0] }
export(shared)
export(bump)
F
cat > "$site/.nift/packages/alias-owner/src/main.f" <<'F'
import("./child.f")
alias := shared
slot := shared[0]
@fn(mutate()) {
  bump()
  alias[0][0] += 10
  slot[0] += 100
  return [shared[0][0], alias[0][0], slot[0]]
}
@fn(state()) { return [shared[0][0], alias[0][0], slot[0]] }
@fn[async](mutate_async()) { return mutate() }
export(mutate)
export(state)
export(mutate_async)
F
cat > "$site/module-init.f" <<'F'
import("init-worker")
print(thread_value)
print(async_value)
F
[ "$(cd "$site" && "$NIFT_ABS" module-init.f)" = "42
43" ] || { echo 'module initialization worker lost its loading module' >&2; exit 1; }
cat > "$site/alias-direct.f" <<'F'
import("alias-owner")
print(mutate() == [111, 111, 111])
print(state() == [111, 111, 111])
F
[ "$(cd "$site" && "$NIFT_ABS" alias-direct.f)" = "true
true" ] || { echo 'direct cross-module alias/reference behavior failed' >&2; exit 1; }
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
[ "$(cd "$site" && "$NIFT_ABS" alias-workers.f)" = "true
true
true
true" ] || { echo 'worker alias/reference cloning or parent isolation failed' >&2; exit 1; }
cat > "$site/direct.f" <<'F'
import("worker")
print(worker_job(1))
F
[ "$(cd "$site" && "$NIFT_ABS" direct.f)" = "2" ] || { echo 'direct nested package import failed' >&2; exit 1; }
cat > "$site/one-worker.f" <<'F'
import("worker")
w := thread(worker_job, 1)
print(w.join())
F
[ "$(cd "$site" && "$NIFT_ABS" one-worker.f)" = "2" ] || { echo 'single worker nested package import failed' >&2; exit 1; }
cat > "$site/exact-async-owner.f" <<'F'
import("async-owner")
future := same_name()
print(await future)
F
if (cd "$site" && "$NIFT_ABS" exact-async-owner.f >/dev/null 2>"$t/exact-async.err"); then
  echo 'async worker mutated a same-name callable in another module' >&2
  exit 1
fi
grep -F 'module-private async callables are unsupported' "$t/exact-async.err" >/dev/null
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
out=$(cd "$site" && "$NIFT_ABS" main.f)
[ "$out" = "11
21
left
right
0
31
1" ] || { printf 'worker module isolation output:\n%s\n' "$out" >&2; exit 1; }

echo 'PASS v4.6 worker module import ownership'
