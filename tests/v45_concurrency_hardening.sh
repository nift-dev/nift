#!/usr/bin/env bash
set -euo pipefail
NIFT=${NIFT:-$(pwd)/nift}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cat > "$TMP/work.f" <<'NIFT'
fn(add(x, y)) { return x + y }
fn(task(x)) { a := async(add, x, 1); t := thread(add, x, 2); return await(a) + t.join() }
print(task(10));
NIFT
# Independent runtimes must not share parser state or handles.
pids=()
for i in $(seq 1 12); do "$NIFT" "$TMP/work.f" > "$TMP/out.$i" & pids+=("$!"); done
for pid in "${pids[@]}"; do wait "$pid"; done
for i in $(seq 1 12); do test "$(cat "$TMP/out.$i")" = "23"; done
# Concurrency unused must remain deterministic.
cat > "$TMP/plain.f" <<'NIFT'
print(1 + 2);
NIFT
for i in $(seq 1 20); do "$NIFT" "$TMP/plain.f"; done | sort -u > "$TMP/plain.out"
test "$(cat "$TMP/plain.out")" = "3"
echo "v4.5 concurrency hardening smoke: PASS"
