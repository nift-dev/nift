#!/usr/bin/env bash
set -euo pipefail

NIFT=${NIFT:-./nift}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

out=$($NIFT -e 't := timer(); print(type(t)); print(t.elapsed()); print(t.running()); print(t.paused()); print(type(t.start())); sleep(2); t.stop(); print(t.elapsed() >= 0); print(type(t.reset()))')
[[ "$out" == $'timer\n0\nfalse\nfalse\nnull\ntrue\nnull' ]]
[[ "$(printf 'type(timer())\n' | "$NIFT")" == '"timer"' ]]
for expression in 'timer(1)' 'timer().start(1)' 'timer().elapsed(1)' 'timer().pause(1)' 'timer().resume(1)' 'timer().stop(1)' 'timer().reset(1)' 'timer().running(1)' 'timer().paused(1)'; do
    if "$NIFT" -e "$expression" >/dev/null 2>&1; then
        echo "accepted invalid timer expression: $expression" >&2
        exit 1
    fi
done

if "$NIFT" eval 'timer()' >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'timer values are not serializable' "$TMP/err"
if "$NIFT" eval '[timer()]' >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'timer values are not serializable' "$TMP/err"

for program in \
  '@join([timer()], ",")' \
  '@map(x : [1] => timer())'; do
    if "$NIFT" -e "$program" >"$TMP/out" 2>"$TMP/err"; then
        echo "legacy timer rendering unexpectedly succeeded: $program" >&2
        exit 1
    fi
    grep -q 'timer' "$TMP/err"
done

cat >"$TMP/timer.f" <<'NIFT'
t := timer()
alias := t
copied := copy(t)
deep := deepcopy(t)
other := timer()
print(alias == t && copied == t && deep == t)
print(other != t)
t.start()
sleep(2)
t.pause()
frozen := t.elapsed()
sleep(2)
print(t.elapsed() == frozen && t.paused())
t.resume()
t.stop()
print(!t.running() && !t.paused())
NIFT
[[ "$($NIFT "$TMP/timer.f")" == $'true\ntrue\ntrue\ntrue' ]]

prepared=$($NIFT -e '
fn(check_timer()) { t := timer(); t.start(); t.pause(); return type(t) == "timer" && t.paused() }
i := 0
result := false
while(i < 1) { result = check_timer(); i += 1 }
print(result)
')
[[ "$prepared" == true ]]

for program in \
  't := timer(); print(t)' \
  't := timer(); print("timer=" + t)' \
  't := timer(); print(t.stringify())' \
  't := timer(); print(t.prettify())' \
  't := timer(); print([t].stringify())' \
  't := timer(); print([t].prettify())' \
  't := timer(); print("$[t]")' \
  't := timer(); $[t]' \
  't := timer(); return t' \
  't := timer(); f := file("timer.txt"); f.open("w"); f.write(t)' \
  't := timer(); s := ofstream("timer.txt"); s.write(t)'; do
    if (cd "$TMP" && "$NIFT" -e "$program") >"$TMP/out" 2>"$TMP/err"; then
        echo "timer marker exposure unexpectedly succeeded: $program" >&2
        exit 1
    fi
done

for program in \
  'm := mutex(timer())' \
  'm := mutex(); m.lock(); m.set({"nested": [timer()]})' \
  'fn(id(x)) { return x }; worker := thread(id, mutex(timer()))' \
  '@fn[async](id(x)) { return x }; future := id(mutex(timer()))'; do
    if "$NIFT" -e "$program" >"$TMP/out" 2>"$TMP/err"; then
        echo "mutex timer smuggling unexpectedly succeeded: $program" >&2
        exit 1
    fi
    grep -q 'non-transferable timer' "$TMP/err"
done

for program in \
  'm := map(); m.set(timer(), 1)' \
  'print([1].group_by(x => timer()))'; do
    if "$NIFT" -e "$program" >"$TMP/out" 2>"$TMP/err"; then
        echo "timer key unexpectedly succeeded: $program" >&2
        exit 1
    fi
done

cat >"$TMP/transfer.f" <<'NIFT'
fn(id(x)) { return x }
t := timer()
worker := thread(id, {"nested": [t]})
NIFT
if "$NIFT" "$TMP/transfer.f" >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'non-transferable' "$TMP/err"

cat >"$TMP/collection-transfer.f" <<'NIFT'
fn(id(x)) { return x }
m := map()
m.set("timer", timer())
worker := thread(id, m)
NIFT
if "$NIFT" "$TMP/collection-transfer.f" >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'non-transferable timer' "$TMP/err"

cat >"$TMP/worker-local.f" <<'NIFT'
fn(measure()) { t := timer(); t.start(); return t.elapsed() }
worker := thread(measure)
print(type(worker.join()))
NIFT
[[ "$("$NIFT" "$TMP/worker-local.f")" == int ]]

cat >"$TMP/worker-result.f" <<'NIFT'
fn(make_timer()) { return timer() }
worker := thread(make_timer)
print(worker.join())
NIFT
if "$NIFT" "$TMP/worker-result.f" >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'non-transferable timer' "$TMP/err"

cat >"$TMP/future-transfer.f" <<'NIFT'
@fn[async](id(x)) { return x }
t := timer()
future := id({"nested": [t]})
NIFT
if "$NIFT" "$TMP/future-transfer.f" >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'non-transferable' "$TMP/err"

cat >"$TMP/future-local.f" <<'NIFT'
@fn[async](measure()) { t := timer(); t.start(); return t.elapsed() }
future := measure()
print(type(await future))
NIFT
[[ "$("$NIFT" "$TMP/future-local.f")" == int ]]

cat >"$TMP/future-result.f" <<'NIFT'
@fn[async](make_timer()) { return timer() }
future := make_timer()
print(await future)
NIFT
if "$NIFT" "$TMP/future-result.f" >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'non-transferable timer' "$TMP/err"

cat >"$TMP/timer-module.f" <<'NIFT'
hidden_timer := timer()
fn(exported_thread_timer()) { return hidden_timer.elapsed() }
@fn[async](exported_future_timer()) { return hidden_timer.elapsed() }
export(exported_thread_timer)
export(exported_future_timer)
NIFT

cat >"$TMP/module-thread.f" <<'NIFT'
import("timer-module.f")
worker := thread(exported_thread_timer)
NIFT
if (cd "$TMP" && "$NIFT" module-thread.f) >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'thread: capture contains a non-transferable timer' "$TMP/err"

cat >"$TMP/module-future.f" <<'NIFT'
import("timer-module.f")
future := exported_future_timer()
NIFT
if (cd "$TMP" && "$NIFT" module-future.f) >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'async function capture contains a non-transferable timer' "$TMP/err"

cat >"$TMP/timer-reexport.f" <<'NIFT'
import("timer-module.f")
export(exported_thread_timer)
NIFT

cat >"$TMP/module-reexport-thread.f" <<'NIFT'
import("timer-reexport.f")
worker := thread(exported_thread_timer)
NIFT
if (cd "$TMP" && "$NIFT" module-reexport-thread.f) >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'thread: capture contains a non-transferable timer' "$TMP/err"

cat >"$TMP/loading-module-thread.f" <<'NIFT'
hidden_timer := timer()
fn(loading_timer()) { return hidden_timer.elapsed() }
worker := thread(loading_timer)
NIFT

cat >"$TMP/import-loading-module.f" <<'NIFT'
import("loading-module-thread.f")
NIFT
if (cd "$TMP" && "$NIFT" import-loading-module.f) >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'thread: capture contains a non-transferable timer' "$TMP/err"

cat >"$TMP/loading-imported-wrapper-thread.f" <<'NIFT'
import("timer-module.f")
fn(loading_imported_timer_wrapper()) { return exported_thread_timer() }
worker := thread(loading_imported_timer_wrapper)
NIFT

cat >"$TMP/import-loading-imported-wrapper-thread.f" <<'NIFT'
import("loading-imported-wrapper-thread.f")
NIFT
if (cd "$TMP" && "$NIFT" import-loading-imported-wrapper-thread.f) >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'thread: capture contains a non-transferable timer' "$TMP/err"

cat >"$TMP/loading-imported-wrapper-async.f" <<'NIFT'
import("timer-module.f")
@fn[async](loading_imported_timer_async_wrapper()) { return exported_thread_timer() }
future := loading_imported_timer_async_wrapper()
NIFT

cat >"$TMP/import-loading-imported-wrapper-async.f" <<'NIFT'
import("loading-imported-wrapper-async.f")
NIFT
if (cd "$TMP" && "$NIFT" import-loading-imported-wrapper-async.f) >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'async function capture contains a non-transferable timer' "$TMP/err"

cat >"$TMP/callable-arg-thread.f" <<'NIFT'
import("timer-module.f")
fn(ignore(x)) { return 1 }
worker := thread(ignore, exported_thread_timer)
NIFT
if (cd "$TMP" && "$NIFT" callable-arg-thread.f) >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'thread: argument contains a non-transferable timer' "$TMP/err"

cat >"$TMP/nested-callable-arg-thread.f" <<'NIFT'
import("timer-module.f")
fn(ignore(x)) { return 1 }
worker := thread(ignore, {"nested": [exported_thread_timer]})
NIFT
if (cd "$TMP" && "$NIFT" nested-callable-arg-thread.f) >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'thread: argument contains a non-transferable timer' "$TMP/err"

cat >"$TMP/container-callable-arg-thread.f" <<'NIFT'
import("timer-module.f")
fn(ignore(x)) { return 1 }
callbacks := map()
callbacks.set("dangerous", exported_thread_timer)
worker := thread(ignore, callbacks)
NIFT
if (cd "$TMP" && "$NIFT" container-callable-arg-thread.f) >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'thread: argument contains a non-transferable timer' "$TMP/err"

cat >"$TMP/callable-arg-future.f" <<'NIFT'
import("timer-module.f")
@fn[async](ignore_async(x)) { return 1 }
future := ignore_async(exported_thread_timer)
NIFT
if (cd "$TMP" && "$NIFT" callable-arg-future.f) >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'async function argument contains a non-transferable timer' "$TMP/err"

cat >"$TMP/nested-callable-arg-future.f" <<'NIFT'
import("timer-module.f")
@fn[async](ignore_async(x)) { return 1 }
future := ignore_async({"nested": [exported_thread_timer]})
NIFT
if (cd "$TMP" && "$NIFT" nested-callable-arg-future.f) >"$TMP/out" 2>"$TMP/err"; then exit 1; fi
grep -q 'async function argument contains a non-transferable timer' "$TMP/err"

cat >"$TMP/clean-callable-args.f" <<'NIFT'
fn(clean_target()) { return 3 }
fn(ignore(x)) { return 1 }
@fn[async](ignore_async(x)) { return 2 }
worker := thread(ignore, {"nested": [clean_target]})
future := ignore_async(clean_target)
print(worker.join())
print(await future)
NIFT
[[ "$(cd "$TMP" && "$NIFT" clean-callable-args.f)" == $'1\n2' ]]

cat >"$TMP/clean-module.f" <<'NIFT'
fn(clean_module_target()) { return 4 }
export(clean_module_target)
NIFT

cat >"$TMP/clean-module-worker.f" <<'NIFT'
import("clean-module.f")
worker := thread(clean_module_target)
print(worker.join())
NIFT
[[ "$(cd "$TMP" && "$NIFT" clean-module-worker.f)" == 4 ]]

cat >"$TMP/clean-loading-imported-wrappers.f" <<'NIFT'
import("clean-module.f")
fn(clean_loading_wrapper()) { return clean_module_target() }
@fn[async](clean_loading_async_wrapper()) { return clean_module_target() }
worker := thread(clean_loading_wrapper)
future := clean_loading_async_wrapper()
print(worker.join())
print(await future)
NIFT

cat >"$TMP/import-clean-loading-imported-wrappers.f" <<'NIFT'
import("clean-loading-imported-wrappers.f")
NIFT
[[ "$(cd "$TMP" && "$NIFT" import-clean-loading-imported-wrappers.f)" == $'4\n4' ]]

cat >"$TMP/unrelated-timer.f" <<'NIFT'
unrelated := timer()
fn(clean()) { return 7 }
worker := thread(clean)
print(worker.join())
NIFT
[[ "$(cd "$TMP" && "$NIFT" unrelated-timer.f)" == 7 ]]

echo 'PASS v4.6 timer runtime'
