#!/usr/bin/env bash
# CP16 legacy routing (docs/runtime-2/checkpoints/cp16-host-ops-impl.md §9):
# the legacy interpreter's run()/cmd().run() launch through the Host layer's
# ProcessGroup, and its Host reads trace through the Host layer.
#
#   1. result objects unchanged (expected values: runtime-2 e8e4b106, legacy launcher)
#   2. children stay in Nift's process group (legacy; no C3/C1 for legacy run())
#   3. a SIGKILL to Nift's group reaches them, a SIGKILL to Nift alone does not
#   4. C2: SIGTERM to Nift terminates the children, Nift ends with SIGTERM
#   5. trace v2 from the legacy interpreter: one format per file
#   6. stdin is inherited (FileCapture)
#
# D-031: file-size and address-space limits, a timeout on every nift call,
# and every process the tests start is killed by its recorded PID at exit.
set -u -o pipefail
NIFT="${NIFT_BIN:-$(pwd)/nift}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
W="$(mktemp -d)"
PIDS=()
cleanup() {
  for p in "${PIDS[@]}"; do kill -9 "$p" 2>/dev/null; done
  for f in "$W"/*.pid; do [ -f "$f" ] && kill -9 "$(cat "$f")" 2>/dev/null; done
  rm -rf "$W"
}
trap cleanup EXIT
ulimit -f 1048576
[ "${NIFT_SANITIZED:-0}" = 1 ] || ulimit -v 4194304   # sanitizer builds reserve large shadow mappings
cd "$W" || exit 2
fail=0
ok() { echo "PASS $1"; }
bad() { echo "FAIL $1"; fail=1; }
expect() { # name expected actual
  if [ "$2" = "$3" ]; then ok "$1"; else bad "$1: expected [$2] got [$3]"; fi
}
N() { timeout -k 5 60 "$NIFT" "$@"; }
alive() { kill -0 "$1" 2>/dev/null && [ "$(cut -d' ' -f3 "/proc/$1/stat" 2>/dev/null)" != Z ]; }
# Waits up to $2 tenths of a second for pid $1 to be gone; true when gone.
gone_within() { local i=0; while alive "$1"; do i=$((i + 1)); [ "$i" -gt "$2" ] && return 1; sleep 0.1; done; return 0; }

# 1. Result objects: byte-identical to the legacy launcher.
cat > res.f <<'EOF'
r := run("sh", "-c", "echo out; echo err >&2; exit 3")
print(r.exit_code)
print(r.stdout)
print(r.stderr)
print(r.launched)
m := run("cp16-no-such-program-xyz")
print(m.launched)
print(m.exit_code)
print(m.error)
p := cmd("sh", "-c", "echo a").pipe(cmd("cp16-no-such-program-xyz")).pipe(cmd("cat")).run()
print(p.launched)
print(p.exit_code)
print(p.stages[0].launched)
print(p.stages[1].launched)
print(p.stages[1].error)
print(p.stages[2].exit_code)
k := run("sh", "-c", "kill -9 $$")
print(k.exit_code)
e := cmd("sh", "-c", "echo $V; pwd").env("V", "vv").cwd("/").run()
print(e.stdout)
x := cmd("sh", "-c", "cat; echo more").stdin("in.txt").stdout("o.txt").run()
print(x.stdout)
print(open("o.txt"))
y := cmd("sh", "-c", "echo again").stdout("o.txt", "append").run()
print(open("o.txt"))
EOF
printf 'from-file\n' > in.txt
expect "result objects unchanged" \
  "3|out||err||true|false|127|executable not found: cp16-no-such-program-xyz|true|0|true|false|executable not found: cp16-no-such-program-xyz|0|137|vv|/|||from-file|more||from-file|more|again||rc=0" \
  "$(N res.f 2>&1 | tr '\n' '|'; echo "rc=${PIPESTATUS[0]}")"

if [ -d /proc/self ]; then
  # 2. Legacy process group: run() children stay in Nift's process group
  #    (no C3 for legacy run(); cp16-host-ops-impl.md §9.3).
  cat > pgid.f <<'EOF'
print(cmd("sh", "-c", "echo $(cut -d' ' -f5 /proc/$$/stat) $(cut -d' ' -f5 /proc/$PPID/stat)").run().stdout)
EOF
  read -r cpgid npgid < <(N pgid.f 2>&1)
  if [ -n "${cpgid:-}" ] && [ "$cpgid" = "${npgid:-}" ]; then ok "run() child stays in Nift's process group"; else bad "child pgid [$cpgid], Nift pgid [$npgid]"; fi

  # 3. A signal to Nift's process group reaches the child (rt-diff and other
  #    supervisors kill the group); SIGKILL to Nift alone does not (a hook's
  #    child keeps the build lock: test-consumer-snapshots interrupt barrier).
  cat > grp.f <<'EOF'
r := cmd("sh", "-c", "echo $$ > grp.pid; sleep 30").run()
EOF
  for how in group pid; do
    rm -f grp.pid
    setsid "$NIFT" grp.f > /dev/null 2>&1 & npid=$!
    PIDS+=("$npid")
    i=0; while [ ! -s grp.pid ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
    child=$(cat grp.pid 2>/dev/null); cp grp.pid "grp-$how.pid" 2>/dev/null
    sleep 0.1
    if [ "$how" = group ]; then kill -9 -- "-$npid"; else kill -9 "$npid"; fi
    wait "$npid" 2>/dev/null
    if [ "$how" = group ]; then
      if [ -n "$child" ] && gone_within "$child" 30; then ok "SIGKILL to Nift's group ends the child"; else bad "child [$child] survived its group's SIGKILL"; fi
    else
      sleep 0.3
      if [ -n "$child" ] && alive "$child"; then ok "SIGKILL to Nift alone leaves the child running (legacy)"; else bad "child [$child] died with Nift"; fi
      kill -9 "$child" 2>/dev/null
    fi
  done

  # 4. C2: SIGTERM to Nift blocked in run(): the child is terminated (after
  #    the grace period when it ignores SIGTERM) and Nift ends with SIGTERM.
  for variant in plain ignoring; do
    # The pid file is written after the trap is set (no race with the signal).
    if [ "$variant" = plain ]; then pre=''; body='sleep 30'; else pre="trap '' TERM; "; body="sleep 30 & wait \$!; sleep 30"; fi
    cat > c2.f <<EOF
r := cmd("sh", "-c", "$pre echo \$\$ > c2-$variant.pid; $body").run()
print("not reached")
EOF
    rm -f "c2-$variant.pid"
    # Started directly (not under `timeout`, which would receive the signal);
    # bounded below: SIGKILL by PID after 15 s.
    "$NIFT" c2.f > c2.out 2>&1 & npid=$!
    PIDS+=("$npid")
    i=0; while [ ! -s "c2-$variant.pid" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
    child=$(cat "c2-$variant.pid" 2>/dev/null)
    t0=$(date +%s%N)
    kill -TERM "$npid"
    gone_within "$npid" 150 || kill -9 "$npid" 2>/dev/null
    wait "$npid"; rc=$?
    ms=$(( ($(date +%s%N) - t0) / 1000000 ))
    expect "C2 ($variant): Nift ends with SIGTERM (status 143)" "143" "$rc"
    if [ -n "$child" ] && gone_within "$child" 80; then ok "C2 ($variant): child terminated (${ms} ms)"; else bad "C2 ($variant): child [$child] alive"; fi
    if [ "$variant" = ignoring ]; then
      # SIGTERM ignored: SIGKILL after the 2,000 ms grace period, not before.
      if [ "$ms" -ge 1500 ] && [ "$ms" -lt 9000 ]; then ok "C2 (ignoring): SIGKILL after the grace period (${ms} ms)"; else bad "C2 (ignoring): ended after ${ms} ms"; fi
    fi
    if grep -q 'not reached' c2.out; then bad "C2 ($variant): script continued after the signal"; else ok "C2 ($variant): script did not continue"; fi
  done
fi

# 5. Trace v2 from the legacy interpreter: every line is v2 (routed ops and
#    the ops legacy still traces itself), failures carry their error code.
printf 'hello' > a.txt
cat > tr.f <<'EOF'
print(open("a.txt"))
print(exists("nope.txt"))
print(ls(".").length())
touch("w.txt")
print(run("true").exit_code)
print(getenv("CP16_UNSET_VAR"))
EOF
if NIFT_TRACE_HOST=3 NIFT_TRACE_HOST_VERSION=2 timeout -k 5 60 "$NIFT" tr.f 3> trace.jsonl > tr.out 2>&1; then
  total=$(wc -l < trace.jsonl)
  v2=$(grep -c '^{"v":2,' trace.jsonl)
  if [ "$total" -gt 0 ] && [ "$total" = "$v2" ]; then ok "trace v2: $total lines, all v2"; else bad "trace v2: $v2 of $total lines are v2"; cat trace.jsonl; fi
  grep -q '"op":"read","arg":"[^"]*/a.txt","res":"5"' trace.jsonl && ok "trace v2: read" || bad "trace v2: no read line"
  grep -q '"op":"stat","arg":"[^"]*/nope.txt","res":"missing"' trace.jsonl && ok "trace v2: stat" || bad "trace v2: no stat line"
  grep -q '"op":"spawn","arg":"true","res":"launched","argv":\["true"\]' trace.jsonl && ok "trace v2: spawn" || bad "trace v2: no spawn line"
  grep -q '"op":"touch"' trace.jsonl && ok "trace v2: legacy-traced write in v2 framing" || bad "trace v2: no touch line"
  grep -q '"op":"getenv","arg":"CP16_UNSET_VAR","res":"unset"' trace.jsonl && ok "trace v2: getenv" || bad "trace v2: no getenv line"
else
  bad "trace v2 run failed: $(cat tr.out)"
fi
# A missing file: v2 reports the failure with its code (v1 writes nothing).
cat > tr2.f <<'EOF'
print(open("missing.txt"))
EOF
NIFT_TRACE_HOST=3 NIFT_TRACE_HOST_VERSION=2 timeout -k 5 60 "$NIFT" tr2.f 3> trace2.jsonl > /dev/null 2>&1
grep -q '"op":"read","arg":"[^"]*/missing.txt","res":"error","err":"io.open_failed"' trace2.jsonl && ok "trace v2: failed read with code" || { bad "trace v2: failed read"; cat trace2.jsonl; }
NIFT_TRACE_HOST=3 timeout -k 5 60 "$NIFT" tr2.f 3> trace1.jsonl > /dev/null 2>&1
expect "trace v1: failed read writes nothing" "0" "$(wc -l < trace1.jsonl | tr -d ' ')"

# 6. stdin is inherited by run() (FileCapture).
cat > stdin.f <<'EOF'
print(run("cat").stdout)
EOF
expect "stdin inherited" "piped|" "$(printf 'piped' | N stdin.f 2>&1 | tr '\n' '|')"

exit $fail
