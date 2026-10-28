#!/usr/bin/env python3
"""CP22 evidence for the A5 admission rule (record §12.1): why task output and
scheduling observations are refused by default. Not a gate: legacy is
nondeterministic here, so results are reported, and the script fails only on
a happens-before violation by the VM or a harness problem.

  print_order.py --nift ./nift [--runs 30] [--programs 40] [--seed 1]

1. nondeterminism: tasks that print without sleeping, run N times on legacy:
   the distinct output orders legacy produces on this machine.
2. pool bound: legacy runs tasks on max(2, hardware_concurrency) workers, a
   task sleeping holds its worker; with three tasks on a 2-worker pool legacy
   orders the output "a c b" while the VM (and legacy on >= 3 workers) gives
   "c a b": an order legacy cannot produce on this machine.
3. clock: three tasks sleep 100 ms; the elapsed time main observes is
   >= 200 ms on a 2-worker legacy pool and ~100 ms on the VM.
4. happens-before: generated programs whose tasks print (VM with the
   non-contractual NIFT_VM_COOPERATIVE_OBSERVE=1 switch); every VM output must
   be a linearisation of the program's happens-before order (program order in
   main and in each task, a task's lines after its spawn, before the line main
   prints after awaiting it) and contain the same lines as legacy's output.
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

NONDET = """fn[async](f(k)) { print("t" + k); return k }
a := f(1)
b := f(2)
c := f(3)
d := f(4)
print("m")
s := (await a) + (await b) + (await c) + (await d)
print(s)
"""
POOL = """fn[async](a()) { sleep(100); print("a"); return 1 }
fn[async](b()) { sleep(150); print("b"); return 2 }
fn[async](c()) { print("c"); return 3 }
x := a()
y := b()
z := c()
print((await x) + (await y) + (await z))
"""
CLOCK = """fn[async](f()) { sleep(100); return 1 }
t0 := epoch()
a := f()
b := f()
c := f()
s := (await a) + (await b) + (await c)
print(epoch() - t0)
"""


def run(nift, d, text, vm):
    path = Path(d) / "p.f"
    path.write_text(text)
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C", "HOME": d}
    env["NIFT_VM"] = "require" if vm else "0"
    if vm:
        env["NIFT_VM_COOPERATIVE_OBSERVE"] = "1"
    p = subprocess.run([nift, "p.f"], cwd=d, env=env, capture_output=True, timeout=120)
    return p.returncode, p.stdout.decode(), p.stderr.decode()


def generate(rnd):
    """Main spawns n tasks (print before each spawn), then awaits them in a
    random order, printing after each await. Task k prints m_k lines with
    optional short sleeps between them."""
    n = rnd.randint(2, 6)
    lines = []
    tasks = []
    for k in range(n):
        body = []
        count = rnd.randint(1, 3)
        for i in range(count):
            if rnd.random() < 0.5:
                body.append(f"sleep({rnd.choice([1, 2, 3, 5])})")
            body.append(f'print("t{k}.{i}")')
        tasks.append(count)
        lines.append(f"fn[async](f{k}()) {{ " + "; ".join(body) + f"; return {k} }}")
    main = []
    for k in range(n):
        main.append(f'print("m.s{k}")')
        main.append(f"h{k} := f{k}()")
    order = list(range(n))
    rnd.shuffle(order)
    for k in order:
        main.append(f"v{k} := await h{k}")
        main.append(f'print("m.a{k}")')
    return "\n".join(lines + main) + "\n", n, tasks, order


def happens_before_ok(out, n, tasks, order):
    lines = out.split()
    pos = {line: i for i, line in enumerate(lines)}
    expected = {f"m.s{k}" for k in range(n)} | {f"m.a{k}" for k in range(n)}
    expected |= {f"t{k}.{i}" for k in range(n) for i in range(tasks[k])}
    if set(lines) != expected or len(lines) != len(expected):
        return f"lines differ: {sorted(set(lines) ^ expected)}"
    main_seq = [f"m.s{k}" for k in range(n)] + [f"m.a{k}" for k in order]
    if [pos[x] for x in main_seq] != sorted(pos[x] for x in main_seq):
        return "main's lines out of program order"
    for k in range(n):
        seq = [pos[f"t{k}.{i}"] for i in range(tasks[k])]
        if seq != sorted(seq):
            return f"task {k}'s lines out of order"
        if min(seq) < pos[f"m.s{k}"]:
            return f"task {k} printed before its spawn"
        if max(seq) > pos[f"m.a{k}"]:
            return f"task {k} printed after main returned from awaiting it"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--runs", type=int, default=30)
    ap.add_argument("--programs", type=int, default=40)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    failures = 0
    print(f"host: os.cpu_count()={os.cpu_count()}, legacy pool = max(2, hardware_concurrency)")
    with tempfile.TemporaryDirectory(prefix="cp22-order-") as d:
        orders = {}
        for _ in range(a.runs):
            code, out, _ = run(nift, d, NONDET, False)
            orders[out] = orders.get(out, 0) + 1
        code, vm_out, err = run(nift, d, NONDET, True)
        print(f"1 nondeterminism: legacy produced {len(orders)} distinct orders in {a.runs} runs: "
              + "; ".join(f"{o.split()!r} x{c}" for o, c in sorted(orders.items(), key=lambda x: -x[1])))
        print(f"  VM order: {vm_out.split()!r} (exit {code}{', ' + err.strip() if err.strip() else ''})")
        pool = {}
        for _ in range(max(5, a.runs // 3)):
            _, out, _ = run(nift, d, POOL, False)
            pool[out] = pool.get(out, 0) + 1
        _, vm_pool, _ = run(nift, d, POOL, True)
        print("2 pool bound: legacy " + "; ".join(f"{o.split()!r} x{c}" for o, c in pool.items())
              + f"; VM {vm_pool.split()!r}; VM order produced by legacy here: {vm_pool in pool}")
        legacy_clock = [int(run(nift, d, CLOCK, False)[1].split()[0]) for _ in range(3)]
        vm_clock = [int(run(nift, d, CLOCK, True)[1].split()[0]) for _ in range(3)]
        print(f"3 clock: elapsed ms legacy {legacy_clock}, VM {vm_clock}")
        rnd = random.Random(a.seed)
        checked = 0
        for k in range(a.programs):
            text, n, tasks, order = generate(rnd)
            code, out, err = run(nift, d, text, True)
            lcode, lout, _ = run(nift, d, text, False)
            bad = happens_before_ok(out, n, tasks, order) if code == 0 else f"VM exit {code}: {err[:200]}"
            lbad = happens_before_ok(lout, n, tasks, order) if lcode == 0 else f"legacy exit {lcode}"
            if bad:
                failures += 1
                print(f"FAIL program {k}: VM {bad}\n{text}")
            if lbad:
                print(f"note program {k}: legacy output not checked ({lbad})")
            checked += 1
        print(f"4 happens-before: {checked} generated programs, {failures} VM violation(s)")
    print(f"{'PASS' if failures == 0 else 'FAIL'} cp22 print-order evidence")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
