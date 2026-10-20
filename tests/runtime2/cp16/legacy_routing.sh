#!/usr/bin/env bash
# CP16 legacy routing (docs/runtime-2/checkpoints/cp16-host-ops-impl.md §9):
# the legacy interpreter's run()/cmd().run() launch through the Host layer's
# ProcessGroup, and its Host reads trace through the Host layer.
#
#   1. result objects unchanged (expected values: runtime-2 e8e4b106, legacy launcher)
#   2. children stay in Nift's process group (legacy; no C3/C1 for legacy run())
#   3. a SIGKILL to Nift's group reaches them, a SIGKILL to Nift alone does not;
#      background members left by a run() outlive a normal script end (legacy)
#      but are terminated by SIGINT/SIGTERM to Nift (C2) and by a group SIGKILL
#   4. C2: SIGTERM to Nift terminates the children, Nift ends with SIGTERM
#   5. trace v2 from the legacy interpreter: one format per file
#   6. stdin is inherited (FileCapture)
#
# Ownership classes (owner conditions for the legacy run() exception,
# cp16-host-ops-impl.md §9.3): (a) SIGINT/SIGTERM to Nift terminates the
# legacy children (running stages and the background members they left);
# (b) processes the Host layer manages follow their CP16/CP17 contract --
# tests/rt/host_process_unit.cpp (make test-rt-host), not this script;
# (c) legacy background descendants on an ordinary context exit (script end,
# page render end) survive, exactly as the documented exception says; (d) an
# unrelated process in the same session survives Nift's signal handling.
# No assertion here requires that nothing is left when (c) allows survival.
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

  # 3b. Background members a legacy run() leaves behind (the call returned,
  #     the member lives on). Legacy behaviour is kept when the script ends
  #     normally (no C1 for legacy run(): the member outlives the script), but
  #     SIGINT/SIGTERM to Nift (C2) terminates it, and so does a SIGKILL to
  #     Nift's process group (rt-diff, supervisors).
  cat > bgend.f <<'EOF'
r := cmd("sh", "-c", "sleep 30 >/dev/null 2>&1 & echo $! > bgend.pid").run()
print("done")
EOF
  rm -f bgend.pid
  N bgend.f > /dev/null 2>&1
  bge=$(cat bgend.pid 2>/dev/null); echo "$bge" > bgend-kill.pid
  sleep 0.3
  if [ -n "$bge" ] && alive "$bge"; then ok "(c) background member outlives a normal script end (legacy exception)"; else bad "background member [$bge] did not outlive its script"; fi
  kill -9 "$bge" 2>/dev/null
  # (c) while Nift lives an adopted background member's parent is Nift (the
  #     subreaper: a legacy-visible difference, §9.6); after a normal exit it
  #     is reparented to the system's reaper, exactly like an orphan of this
  #     shell (init, or the session's subreaper).
  cat > bgppid.f <<'EOF'
r := cmd("sh", "-c", "sleep 30 >/dev/null 2>&1 & echo $! > bgppid.pid").run()
p := cmd("sh", "-c", "cut -d' ' -f4 /proc/$(cat bgppid.pid)/stat; echo $PPID").run()
print(p.stdout)
EOF
  rm -f bgppid.pid
  read -r during nift_pid < <(N bgppid.f 2>&1 | tr '\n' ' ')
  bgp=$(cat bgppid.pid 2>/dev/null); echo "$bgp" > bgppid-kill.pid
  ref=$(sh -c 'sleep 30 >/dev/null 2>&1 & echo $!'); echo "$ref" > ref-kill.pid
  sleep 0.3
  after=$(cut -d' ' -f4 "/proc/$bgp/stat" 2>/dev/null); ref_parent=$(cut -d' ' -f4 "/proc/$ref/stat" 2>/dev/null)
  if [ -n "$during" ] && [ "$during" = "$nift_pid" ]; then ok "(c) adopted member's parent is Nift while Nift lives"; else bad "(c) parent during [$during], Nift [$nift_pid]"; fi
  if [ -n "$bgp" ] && alive "$bgp" && [ -n "$after" ] && [ "$after" = "$ref_parent" ] && [ "$after" != "$nift_pid" ]; then
    ok "(c) after a normal exit the member lives on, reparented to the system's reaper (pid $after)"
  else bad "(c) after exit: alive? parent [$after], expected [$ref_parent]"; fi
  kill -9 "$bgp" "$ref" 2>/dev/null
  # The interactive shell does not enable the subreaper (nor C2).
  printf '%s\n' 'r := cmd("sh", "-c", "sleep 30 >/dev/null 2>&1 & echo $! > shbg.pid").run()' \
    'p := cmd("sh", "-c", "cut -d\" \" -f4 /proc/$(cat shbg.pid)/stat; echo $PPID").run()' 'print(p.stdout)' > shell.in
  rm -f shbg.pid
  read -r sh_parent sh_nift < <(timeout -k 5 60 "$NIFT" < shell.in 2>/dev/null | grep -E '^[0-9]+$' | tr '\n' ' ')
  shbg=$(cat shbg.pid 2>/dev/null); echo "$shbg" > shbg-kill.pid; kill -9 "$shbg" 2>/dev/null
  if [ -n "$sh_parent" ] && [ -n "$sh_nift" ] && [ "$sh_parent" != "$sh_nift" ]; then ok "interactive shell: no subreaper (member's parent is not Nift)"; else bad "interactive shell: member parent [$sh_parent], Nift [$sh_nift]"; fi
  # (c) the same at a page render end
  if [ -d "$HERE/probes/sites/site_run/site" ]; then
    cp -r "$HERE/probes/sites/site_run/site" csite && mkdir -p csite/data
    cat > csite/content/index.html <<'EOF'
R:@script { return run("sh", "-c", "sleep 30 >/dev/null 2>&1 & echo $! > bg.pid; echo ok").stdout }
EOF
    (cd csite && timeout -k 5 60 "$NIFT" build --all > ../csite.out 2>&1); crc=$?
    cbg=$(cat csite/bg.pid 2>/dev/null); echo "$cbg" > csite-kill.pid
    expect "(c) page build succeeds" "0" "$crc"
    sleep 0.3
    if [ -n "$cbg" ] && alive "$cbg"; then ok "(c) background member outlives the page render (legacy exception)"; else bad "page background member [$cbg] did not outlive the render"; fi
    kill -9 "$cbg" 2>/dev/null
  fi
  # Adopted members that exit are reaped (no zombie children of Nift).
  cat > zomb.f <<'EOF'
a := cmd("sh", "-c", "sleep 0.1 >/dev/null 2>&1 &").run()
b := cmd("sh", "-c", "sleep 0.1 >/dev/null 2>&1 &").run()
c := cmd("sh", "-c", "sleep 0.4").run()
d := cmd("sh", "-c", "true").run()
print(cmd("sh", "-c", "ps -o stat= --ppid $PPID | grep -c Z").run().stdout)
EOF
  if command -v ps >/dev/null; then expect "adopted background members reaped (zombie children of Nift)" "0||" "$(N zomb.f 2>&1 | tr '\n' '|')"; fi
  cat > bgsig.f <<'EOF'
r := cmd("sh", "-c", "sleep 30 >/dev/null 2>&1 & echo $! > bgsig.pid").run()
w := cmd("sh", "-c", "echo $$ > hold.pid; sleep 30").run()
print("not reached")
EOF
  for how in TERM INT group; do
    rm -f bgsig.pid hold.pid
    # SIGINT at its default even if this shell runs us as a background job.
    if [ "$how" = group ]; then
      setsid "$NIFT" bgsig.f > bgsig.out 2>&1 & npid=$!
    else
      python3 -c 'import os, signal, sys; signal.signal(signal.SIGINT, signal.SIG_DFL); os.execv(sys.argv[1], sys.argv[1:])' "$NIFT" bgsig.f > bgsig.out 2>&1 & npid=$!
    fi
    PIDS+=("$npid")
    i=0; while [ ! -s hold.pid ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
    bgm=$(cat bgsig.pid 2>/dev/null); hold=$(cat hold.pid 2>/dev/null)
    echo "$bgm" > "bgsig-$how.pid"; echo "$hold" > "hold-$how.pid"
    if [ -z "$bgm" ] || ! alive "$bgm"; then bad "bg member not running before the signal ($how)"; continue; fi
    # (d) an unrelated process: started by this shell, not by Nift, same session
    sleep 30 & sib=$!; PIDS+=("$sib")
    case $how in TERM) kill -TERM "$npid";; INT) kill -INT "$npid";; group) kill -9 -- "-$npid";; esac
    gone_within "$npid" 150 || kill -9 "$npid" 2>/dev/null
    wait "$npid"; rc=$?
    case $how in TERM) want=143;; INT) want=130;; group) want=137;; esac
    expect "(a) Nift ends with the signal ($how)" "$want" "$rc"
    if gone_within "$bgm" 80; then ok "(a) legacy background member terminated ($how)"; else bad "background member [$bgm] survived ($how)"; fi
    if gone_within "$hold" 80; then ok "(a) running legacy stage terminated ($how)"; else bad "stage [$hold] survived ($how)"; fi
    grep -q 'not reached' bgsig.out && bad "script continued after $how" || true
    if [ "$how" != group ]; then
      if alive "$sib"; then ok "(d) unrelated process in the same session survives Nift's $how handling"; else bad "unrelated process [$sib] killed by Nift's $how handling"; fi
    fi
    kill -9 "$sib" 2>/dev/null; wait "$sib" 2>/dev/null
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
