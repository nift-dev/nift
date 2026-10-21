#!/usr/bin/env python3
"""Self-test of vm_adversarial.py's timeout policy (run_pair), with fake
engines instead of nift. The policy: both engines get the same bound; if
exactly one times out, both are rerun once with RETRY_FACTOR times the bound,
the retry is reported, and a pair that is still asymmetric is a difference.

Cases (fake `nift`, a shell script that sleeps per engine and per attempt):
  same        both fast                         -> no retry
  endless     both always slow                  -> no retry, both time out
  slow-once   legacy slow only on attempt 1     -> retry reported, both completed
  divergent   legacy always slow, VM fast       -> retry reported, still asymmetric
  heavy-lock  two processes holding the heavy lane never overlap, also when
              they run with different TMPDIRs (as `make certify` targets do)
  lock-path   the default lane path does not depend on TMPDIR
  lock-death  a lane holder killed with SIGKILL releases the lane
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vm_adversarial as adv  # noqa: E402

FAKE = """#!/bin/sh
# $NIFT_VM unset: legacy; set: the VM. Attempt counters live in the cwd.
engine=${NIFT_VM:+vm}; engine=${engine:-legacy}
n=$(cat .n.$engine 2>/dev/null || echo 0); n=$((n + 1)); echo $n > .n.$engine
case "$MODE:$engine:$n" in
  divergent:legacy:*) sleep 6 ;;
  endless:*|slow-once:legacy:1) sleep 3 ;;
esac
[ -n "$NIFT_VM_STATS" ] && echo '{"outcome":"vm","reason":"","vm_executions":1,"legacy_executions":0}' > "$NIFT_VM_STATS"
echo out
"""


def case(mode):
    d = tempfile.mkdtemp(prefix="adv-timeout-")
    nift = os.path.join(d, "fake-nift")
    with open(nift, "w") as f:
        f.write(FAKE)
    os.chmod(nift, 0o755)
    path = os.path.join(d, "p.f")
    open(path, "w").close()
    return adv.run_pair(nift, path, d, {"MODE": mode}, 0.5)


LOCKER = """import sys, time
sys.path.insert(0, sys.argv[1])
import vm_adversarial as adv
with adv.heavy_lock(True):
    start = time.monotonic()
    time.sleep(1.0)
    print(start, time.monotonic())
"""


def heavy_lock_case():
    import subprocess
    d = tempfile.mkdtemp(prefix="adv-lock-")
    lock = os.path.join(d, "heavy.lock")
    procs = []
    for k in range(2):
        tmp = os.path.join(d, "tmp%d" % k)
        os.makedirs(tmp)
        env = dict(os.environ, NIFT_ADV_HEAVY_LOCK=lock, TMPDIR=tmp)
        procs.append(subprocess.Popen([sys.executable, "-c", LOCKER, HERE], env=env, stdout=subprocess.PIPE,
                                      text=True))
    outputs = [p.communicate()[0] for p in procs]
    if any(p.returncode != 0 for p in procs):
        return False
    spans = sorted(tuple(map(float, out.split())) for out in outputs)
    return len(spans) == 2 and spans[0][1] <= spans[1][0]


HOLDER = """import sys, time
sys.path.insert(0, sys.argv[1])
import vm_adversarial as adv
with adv.heavy_lock(True):
    print("held", flush=True)
    time.sleep(60)
"""


def lock_death_case():
    import signal, subprocess, time
    d = tempfile.mkdtemp(prefix="adv-lockdeath-")
    env = dict(os.environ, NIFT_ADV_HEAVY_LOCK=os.path.join(d, "heavy.lock"))
    holder = subprocess.Popen([sys.executable, "-c", HOLDER, HERE], env=env, stdout=subprocess.PIPE, text=True)
    if holder.stdout.readline().strip() != "held":
        return False
    holder.send_signal(signal.SIGKILL)
    holder.wait()
    t0 = time.monotonic()
    waiter = subprocess.run([sys.executable, "-c", LOCKER, HERE], env=env, capture_output=True, text=True,
                            timeout=20)
    return waiter.returncode == 0 and time.monotonic() - t0 < 10


def default_lock_path_case():
    import subprocess
    paths = set()
    for tmp in (tempfile.mkdtemp(prefix="adv-t1-"), tempfile.mkdtemp(prefix="adv-t2-")):
        env = {k: v for k, v in os.environ.items() if k != "NIFT_ADV_HEAVY_LOCK"}
        env["TMPDIR"] = tmp
        probe = subprocess.run([sys.executable, "-c", "import sys; sys.path.insert(0, sys.argv[1]); "
                                "import vm_adversarial as a; print(a.HEAVY_LOCK)", HERE],
                               env=env, capture_output=True, text=True)
        if probe.returncode != 0:
            return False
        paths.add(probe.stdout.strip())
    return len(paths) == 1


def main():
    old = adv.RETRY_FACTOR
    adv.RETRY_FACTOR = 8  # 0.5 s bound, 4 s on retry: the 3 s sleeps finish on retry
    failures = []
    try:
        legacy, vm, _, retry = case("same")
        if retry or legacy[2] == "timeout" or vm[2] == "timeout":
            failures.append(f"same: {retry}")
        legacy, vm, _, retry = case("endless")
        if retry or legacy[2] != "timeout" or vm[2] != "timeout":
            failures.append(f"endless: {retry}")
        legacy, vm, _, retry = case("slow-once")
        if not retry or "both completed" not in retry or legacy[2] == "timeout":
            failures.append(f"slow-once: {retry}")
        legacy, vm, _, retry = case("divergent")
        if not retry or "still asymmetric" not in retry:
            failures.append(f"divergent: {retry}")
        if not heavy_lock_case():
            failures.append("heavy-lock: two holders overlapped")
        if not default_lock_path_case():
            failures.append("heavy-lock: the default lane path depends on TMPDIR")
        if not lock_death_case():
            failures.append("lock-death: the lane stayed held after its holder was killed")
    finally:
        adv.RETRY_FACTOR = old
    for f in failures:
        print("FAIL " + f)
    print(("PASS" if not failures else "FAIL") + " vm adversarial timeout policy: 7 cases")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
