#!/usr/bin/env bash
# CP16 legacy routing (docs/runtime-2/checkpoints/cp16-host-ops-impl.md §9):
# the legacy interpreter's run()/cmd().run() launch through the Host layer's
# ProcessGroup, and its Host reads trace through the Host layer.
#
#   1. result objects unchanged (expected values: runtime-2 e8e4b106, legacy launcher)
#   2-4. legacy run()'s historical process behaviour, kept as a temporary
#      compatibility exception (§9.6; CP25 removes it with the legacy launcher)
#   5. trace v2 from the legacy interpreter: one format per file
#   6. stdin is inherited (FileCapture)
#
# Ownership classes. This script asserts legacy run()'s HISTORICAL behaviour
# only -- it claims no cleanup guarantee for legacy-launched descendants:
# (c1) the stages stay in Nift's process group; a SIGKILL of that group
#      reaches them (rt-diff, supervisors);
# (c2) a SIGKILL of Nift alone, and a SIGTERM to Nift, leave a running child
#      running (no PDEATHSIG, no SIGINT/SIGTERM cleanup for legacy run());
# (c3) background descendants outlive a normal script end and page render end;
# (c4) a capture over NIFT_READ_LIMIT stops the stage and its descendants.
# Host-managed processes (their ownership, cleanup, cancellation, C1/C2/C3
# and resource guarantees) are tested by tests/rt/host_process_unit.cpp
# (make test-rt-host), not here.
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
  # 2. (c1) run() children stay in Nift's process group.
  cat > pgid.f <<'EOF'
print(cmd("sh", "-c", "echo $(cut -d' ' -f5 /proc/$$/stat) $(cut -d' ' -f5 /proc/$PPID/stat)").run().stdout)
EOF
  read -r cpgid npgid < <(N pgid.f 2>&1)
  if [ -n "${cpgid:-}" ] && [ "$cpgid" = "${npgid:-}" ]; then ok "(c1) run() child stays in Nift's process group"; else bad "child pgid [$cpgid], Nift pgid [$npgid]"; fi

  # 3. (c1) a SIGKILL of Nift's group reaches the child; (c2) a SIGKILL or a
  #    SIGTERM of Nift alone leaves it running (historical limitation).
  cat > grp.f <<'EOF'
r := cmd("sh", "-c", "echo $$ > grp.pid; sleep 30").run()
print("not reached")
EOF
  for how in group kill term; do
    rm -f grp.pid
    setsid "$NIFT" grp.f > grp.out 2>&1 & npid=$!
    PIDS+=("$npid")
    i=0; while [ ! -s grp.pid ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
    child=$(cat grp.pid 2>/dev/null); cp grp.pid "grp-$how.pid" 2>/dev/null
    sleep 0.1
    case $how in group) kill -9 -- "-$npid";; kill) kill -9 "$npid";; term) kill -TERM "$npid";; esac
    gone_within "$npid" 100 || kill -9 "$npid" 2>/dev/null
    wait "$npid" 2>/dev/null; rc=$?
    if [ "$how" = group ]; then
      if [ -n "$child" ] && gone_within "$child" 30; then ok "(c1) SIGKILL to Nift's group ends the child"; else bad "child [$child] survived its group's SIGKILL"; fi
    else
      sleep 0.3
      if [ -n "$child" ] && alive "$child"; then ok "(c2) SIG$( [ $how = kill ] && echo KILL || echo TERM ) to Nift alone leaves the child running (historical)"; else bad "child [$child] died with Nift ($how)"; fi
      [ "$how" = term ] && expect "(c2) Nift ends with SIGTERM's default action" "143" "$rc"
      kill -9 "$child" 2>/dev/null
    fi
  done

  # 3b. (c4) historical (feature-host-deps D-028, kill_stage_tree): when a
  #     capture exceeds NIFT_READ_LIMIT, the stage AND its descendants are
  #     stopped. Checked without a file-size limit and right away, so a
  #     surviving producer cannot be ended by SIGXFSZ (r3 host-deps finding).
  cat > lim.f <<'EOF'
r := run("sh", "-c", "echo $$ > lim.pid; yes cp16-legacy-limit-probe; touch lim-marker")
EOF
  rm -f lim.pid lim-marker
  ( ulimit -f unlimited 2>/dev/null; NIFT_READ_LIMIT=1000000 timeout -k 5 60 "$NIFT" lim.f > lim.out 2>&1 ); lrc=$?
  sleep 0.2
  lim_left=""
  for d in /proc/[0-9]*; do
    [ "$(cat "$d/comm" 2>/dev/null)" = yes ] && grep -qa cp16-legacy-limit-probe "$d/cmdline" 2>/dev/null && lim_left="$lim_left ${d#/proc/}"
  done
  grep -q 'exceeds the read limit' lim.out && [ "$lrc" != 0 ] && ok "(c4) capture limit: fatal limit error" || bad "(c4) capture limit: rc $lrc, $(tail -c 200 lim.out)"
  if [ -z "$lim_left" ]; then ok "(c4) capture limit stops the stage's descendants (historical)"; else bad "(c4) producer left running:$lim_left"; fi
  for p in $lim_left; do kill -9 "$p" 2>/dev/null; done
  [ -e lim-marker ] && bad "(c4) stage finished after the limit" || ok "(c4) stage stopped before finishing"

  # 4. (c3) background descendants outlive a normal script end and a page
  #    render end (no cleanup at context end for legacy run()).
  cat > bgend.f <<'EOF'
r := cmd("sh", "-c", "sleep 30 >/dev/null 2>&1 & echo $! > bgend.pid").run()
print("done")
EOF
  rm -f bgend.pid
  N bgend.f > /dev/null 2>&1
  bge=$(cat bgend.pid 2>/dev/null); echo "$bge" > bgend-kill.pid
  sleep 0.3
  if [ -n "$bge" ] && alive "$bge"; then ok "(c3) background descendant outlives a normal script end (historical)"; else bad "background descendant [$bge] did not outlive its script"; fi
  kill -9 "$bge" 2>/dev/null
  if [ -d "$HERE/probes/sites/site_run/site" ]; then
    cp -r "$HERE/probes/sites/site_run/site" csite && mkdir -p csite/data
    cat > csite/content/index.html <<'EOF'
R:@script { return run("sh", "-c", "sleep 30 >/dev/null 2>&1 & echo $! > bg.pid; echo ok").stdout }
EOF
    (cd csite && timeout -k 5 60 "$NIFT" build --all > ../csite.out 2>&1); crc=$?
    cbg=$(cat csite/bg.pid 2>/dev/null); echo "$cbg" > csite-kill.pid
    expect "(c3) page build succeeds" "0" "$crc"
    grep -q 'R:ok' csite/public/index.html 2>/dev/null && ok "(c3) page output unchanged" || bad "(c3) page output"
    sleep 0.3
    if [ -n "$cbg" ] && alive "$cbg"; then ok "(c3) background descendant outlives the page render (historical)"; else bad "page background descendant [$cbg] did not outlive the render"; fi
    kill -9 "$cbg" 2>/dev/null
  fi
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
