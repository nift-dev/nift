#!/usr/bin/env python3
"""records20k acceptance (integration stage 4): the benchmark suite's
`records` workload (scoreboard e2e) entirely on the VM, with the suite's work
witness.

  * n = 5,000, 10,000 and 20,000: NIFT_VM=require runs it entirely on the VM
    (statistics outcome `vm`), byte-identical to legacy (stdout, stderr, exit
    status), and the witness line equals the suite's expectation; wall-clock
    time of each engine is printed (evidence, not a gate: the suite's
    callgrind/wall runs are the performance measurement);
  * json_transform (reported, not lifted): the reason NIFT_VM=classify gives.

  vm_records20k.py --nift ./nift --suite <nift-bench>/benchmarks/runtime2/suite
"""
import argparse
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

WITNESS = re.compile(r"^W n=(\d+) ops=(\d+) chk=(-?\d+)$", re.M)


def load_workloads(suite):
    spec = importlib.util.spec_from_file_location("suite_workloads", os.path.join(suite, "workloads.py"))
    module = importlib.util.module_from_spec(spec)
    sys.path.insert(0, suite)
    try:
        spec.loader.exec_module(module)
    finally:
        sys.path.pop(0)
    return module


def run(nift, d, mode, timeout):
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C", "TZ": "UTC", "HOME": d,
           "TMPDIR": d, **{k: os.environ[k] for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS") if k in os.environ}}
    stats = os.path.join(d, ".stats")
    if os.path.exists(stats):
        os.unlink(stats)
    if mode:
        env.update({"NIFT_VM": mode, "NIFT_VM_STATS": stats})
    p = subprocess.run([nift, "p.f"], cwd=d, env=env, capture_output=True, stdin=subprocess.DEVNULL, timeout=timeout)
    st = None
    if mode and os.path.exists(stats):
        lines = open(stats).read().splitlines()
        st = json.loads(lines[-1]) if lines else None
    return (p.stdout, p.stderr, p.returncode), st


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--suite", required=True, help="nift-bench benchmarks/runtime2/suite directory")
    ap.add_argument("--timeout", type=float, default=600)
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    wl = load_workloads(os.path.abspath(a.suite))
    records = wl.W["records"]
    failures = []
    work = tempfile.mkdtemp(prefix="nift-records20k-")
    try:
        for n in (5000, 10000, 20000):
            d = tempfile.mkdtemp(dir=work)
            with open(os.path.join(d, "p.f"), "w") as f:
                f.write(records["nift"].format(N=n))
            t0 = time.perf_counter()
            vm, st = run(nift, d, "require", a.timeout)
            t_vm = time.perf_counter() - t0
            outcome = st and st.get("outcome")
            m = WITNESS.search(vm[0].decode("utf-8", "replace"))
            got = (int(m.group(1)), int(m.group(3))) if m else None
            want = tuple(records["expect"](n))
            line = f"records n={n}: outcome {outcome}, exit {vm[2]}, witness {m.group(0) if m else None}"
            if outcome != "vm" or vm[2] != 0:
                failures.append(f"{line}: not entirely on the VM ({st and st.get('reason')})")
            elif got != want:
                failures.append(f"{line}: witness (n, chk) {got} != expected {want}")
            t0 = time.perf_counter()
            legacy, _ = run(nift, d, None, a.timeout)
            t_legacy = time.perf_counter() - t0
            same = legacy == vm
            line += (f"; byte-identical to legacy: {same}; wall legacy {t_legacy:.2f} s, VM {t_vm:.2f} s"
                     + (f" ({t_legacy / t_vm:.1f}x)" if outcome == "vm" and t_vm > 0 else ""))
            if not same:
                failures.append(f"records n={n}: VM output differs from legacy")
            print(line)
        d = tempfile.mkdtemp(dir=work)
        with open(os.path.join(d, "p.f"), "w") as f:
            f.write(wl.W["json_transform"]["nift"].format(N=1000))
        _, cst = run(nift, d, "classify", a.timeout)
        print(f"json_transform (reported, not lifted): {cst and (cst.get('outcome'), cst.get('reason'), cst.get('line'))}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for f in failures:
        print("FAIL " + f)
    print(("PASS" if not failures else "FAIL") + f" vm records20k: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
