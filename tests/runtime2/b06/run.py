#!/usr/bin/env python3
"""B06 (owner 2026-11-09 16:46, item 4; docs/runtime-2/checkpoints/combined-b3-b5.md section 37):
admission regressions of B4b, fixed in the kind inference.

Family 1 (B4b 1, 29d7ef0f): an empty literal's proven "holds nothing" element type was lost when a
mutator's site store dropped its positional fact, so `a := []; return a.pop()` was refused
(type:script-return). Family 2 (B4b 8, 3eb749cc): a collection method's callback that is still
unknown on the fixed point's first walk (a factory's result) counted as an inexact callee and widened
every function value to parameters of anything, so `a.sort(mk())` was refused (type:<).

Every `equal` fixture must run on the VM (NIFT_VM=require, never refused) with stdout, stderr and
exit status equal to legacy (NIFT_VM=0) on the same binary, error positions included. Every `refused`
fixture must stay refused for the reason given: an empty type is "holds nothing" only when nothing
may be stored. Comparator-sort fixtures run on the probe build (--probe: it skips only the
comparator's libstdc++ profile refusal); on the production build they must be equal or refused
exactly `method:sort` (that profile refusal, GCC other than the certified 15.2).

usage: run.py --nift NIFT [--probe NIFT_GUARDOFF]
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "fixtures")

# name -> "equal" | "refused:<reason>"
EXPECT = {
    # family 1: an empty receiver (B4b 1); the result is returned, used, or compared
    "empty_pop_return": "equal",
    "empty_pop_twice_return": "equal",
    "empty_remove_return": "equal",
    "empty_pop_return_expr": "equal",
    "empty_pop_return_compare": "equal",
    "empty_clear_pop_return": "equal",
    "empty_reverse_pop_return": "equal",
    "empty_pop_in_if_return": "equal",
    # family 1 siblings that were admitted before too (controls)
    "empty_pop_bind": "equal",
    "empty_pop_expr": "equal",
    "empty_pop_function": "equal",
    "unknown_pop_param": "equal",
    "pushed_pop_return": "equal",
    "pushed_popped_twice_return": "equal",
    # family 2: a factory's callback (B4b 8)
    "factory_comparator_sort": "equal",      # cp14b comparator_factory
    "factory_comparator_bound": "equal",
    "factory_map": "equal",
    "factory_filter": "equal",
    "factory_reduce": "equal",
    "factory_find": "equal",
    "factory_any": "equal",
    "factory_sort_by": "equal",
    "factory_map_error": "equal",
    # refusals that stay: something (an array, an object) may be stored, through any alias
    "keep_pushed_array_return": "refused:type:script-return",
    "keep_alias_array_return": "refused:type:script-return",
    "keep_callee_store_return": "refused:type:script-return",
    "keep_object_return": "refused:type:script-return",
    "keep_shift": "refused:method:shift",
}
COMPARATOR = {"factory_comparator_sort", "factory_comparator_bound"}
REFUSED = "not compiled for the VM ("


def run(nift, name, vm):
    env = dict(os.environ, NIFT_VM=vm)
    env.pop("NIFT_PREVALIDATE", None)
    p = subprocess.run([nift, name + ".f"], cwd=FIXTURES, capture_output=True, text=True, env=env, timeout=60)
    return p.returncode, p.stdout, p.stderr


def check(nift, name, want, production):
    legacy = run(nift, name, "0")
    vm = run(nift, name, "require")
    if want == "equal":
        if REFUSED in vm[2]:
            reason = vm[2].split(REFUSED, 1)[1].split(")", 1)[0]
            if production and name in COMPARATOR and reason == "method:sort":
                return None  # the comparator's libstdc++ profile refusal (not the certified GCC 15.2)
            return f"refused ({reason}); legacy: rc {legacy[0]} {legacy[1]!r} {legacy[2]!r}"
        if vm != legacy:
            return f"VM rc {vm[0]} out {vm[1]!r} err {vm[2]!r} != legacy rc {legacy[0]} out {legacy[1]!r} err {legacy[2]!r}"
        return None
    reason = want.split(":", 1)[1]
    if REFUSED + reason + ")" not in vm[2]:
        return f"not refused for {reason}: rc {vm[0]} out {vm[1]!r} err {vm[2]!r}"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--probe", help="the probe build (nift-guardoff) for the comparator-sort fixtures")
    a = ap.parse_args()
    a.nift = os.path.abspath(a.nift)
    a.probe = os.path.abspath(a.probe) if a.probe else None
    on_disk = sorted(f[:-2] for f in os.listdir(FIXTURES) if f.endswith(".f"))
    if on_disk != sorted(EXPECT):
        print(f"FAIL b06: fixtures and expectations differ: on disk only {sorted(set(on_disk) - set(EXPECT))}, "
              f"expected only {sorted(set(EXPECT) - set(on_disk))}")
        return 2
    fails = 0
    for name in sorted(EXPECT):
        builds = [(a.nift, True)] + ([(a.probe, False)] if a.probe and name in COMPARATOR else [])
        for nift, production in builds:
            why = check(nift, name, EXPECT[name], production)
            tag = "production" if production else "probe"
            if why:
                print(f"FAIL b06 {name} [{tag}]: {why}")
                fails += 1
            else:
                print(f"PASS b06 {name} [{tag}]: {EXPECT[name]}")
    if not a.probe:
        print("FAIL b06: no --probe build: the comparator-sort fixtures did not run on the VM")
        fails += 1
    print(("FAIL" if fails else "PASS") + f" b06: {len(EXPECT)} fixtures, {fails} failure(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
