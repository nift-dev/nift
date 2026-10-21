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
    finally:
        adv.RETRY_FACTOR = old
    for f in failures:
        print("FAIL " + f)
    print(("PASS" if not failures else "FAIL") + " vm adversarial timeout policy: 4 cases")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
