#!/usr/bin/env python3
"""CP22 adversarial coroutine programs: generated, compared with legacy.

  adversarial.py --nift ./nift [--seed 1] [--count 120] [--switch ./nift-switch]

Each program is a random spawn tree over async functions (nested spawns,
awaits in random orders, repeated awaits, awaits through sync helpers and
async lambdas, futures in arrays, short sleeps, sleep(0), loops with
safepoints, thrown Errors caught at random levels, globals written before and
after spawns and inside tasks, tasks never awaited). Output is printed only by
the main program, in a fixed order, so legacy's output is deterministic
whatever its thread interleaving; the VM must produce it exactly (stdout,
stderr, exit) and must run the unit (NIFT_VM=require, one VM execution).

A timeout, a signal, an abnormal exit or missing statistics is a harness
failure (reported, never a pass). With --switch the same programs also run on
the portable switch-dispatch build.
"""
import argparse
import json
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

TIMEOUT = 120


class Gen:
    def __init__(self, rnd):
        self.r = rnd
        self.lines = []

    def leaf(self, k):
        r = self.r
        body = []
        if r.random() < 0.4:
            body.append(f"sleep({r.choice([0, 0, 1, 2, 3])})")
        if r.random() < 0.25:
            body.append(f'if (a % {r.randint(2, 5)} == 0) {{ throw error("leaf{k} " + a, "user.leaf") }}')
        op = r.choice(["a + b", "a * 2 + b", "a - b", "(a + g0) % 97", "a + g1"])
        body.append(f"return {op}")
        return f"fn[async](leaf{k}(a, b)) {{ " + "; ".join(body) + " }"

    def mid(self, k, leaves):
        r = self.r
        body = []
        names = []
        for j in range(r.randint(1, 3)):
            leaf = r.randrange(leaves)
            body.append(f"p{j} := leaf{leaf}(x + {j}, {r.randint(0, 9)})")
            names.append(f"p{j}")
        if r.random() < 0.5:
            body.append(f"g0 = g0 + x")  # a task's own snapshot only
        if r.random() < 0.3:
            body.append("i := 0; while(i < 3) { i += 1 }")
        r.shuffle(names)
        terms = []
        for n in names:
            if r.random() < 0.3:
                terms.append(f"helper({n})")
            elif r.random() < 0.3:
                body.append(f"t_{n} := 0; try {{ t_{n} = await {n} }} catch(e) {{ t_{n} = 500 }}")
                terms.append(f"t_{n}")
            else:
                terms.append(f"(await {n})")
        if r.random() < 0.3:
            body.append(f"extra := leaf{r.randrange(leaves)}(x, x)")  # never awaited: drained at the end
        body.append("return " + " + ".join(terms) + " + g0")
        return f"fn[async](mid{k}(x)) {{ " + "; ".join(body) + " }"

    def program(self):
        r = self.r
        leaves = r.randint(1, 4)
        mids = r.randint(1, 3)
        out = [f"g0 := {r.randint(0, 50)}", f"g1 := {r.randint(0, 50)}",
               "fn(helper(p)) { return await p }"]
        out += [self.leaf(k) for k in range(leaves)]
        out += [self.mid(k, leaves) for k in range(mids)]
        if r.random() < 0.5:
            out.append(f"lam := async (v) => {{ return v * {r.randint(1, 4)} + g1 }}")
        else:
            out.append("lam := async (v) => v + 1")
        n = r.randint(1, 12)
        out.append("hs := []")
        out.append("i := 0")
        spawn = r.choice([f"mid{r.randrange(mids)}(i)", f"leaf{r.randrange(leaves)}(i, 1)", "lam(i)"])
        out.append(f"while(i < {n}) {{ hs.push({spawn}); i += 1 }}")
        if r.random() < 0.5:
            out.append(f"g0 = {r.randint(100, 200)}")  # after the spawns: invisible to the tasks
        if r.random() < 0.3:
            out.append(f"sleep({r.choice([0, 1, 2])})")
        out.append("s := 0")
        out.append("j := 0")
        if r.random() < 0.5:
            out.append("for(h : hs) { try { s += await h } catch(e) { print(e.message); s += 1 }; j += 1 }")
        else:
            out.append(f"while(j < {n}) {{ k := {n} - 1 - j; try {{ s += await hs[k] }} catch(e) {{ print(e.code); s += 2 }}; j += 1 }}")
        if r.random() < 0.4:
            out.append("try { s += await hs[0] } catch(e) { s += 3 }")  # awaited twice
        out.append("print(s)")
        out.append("print(g0)")
        if r.random() < 0.3:
            out.append(f"late := mid{r.randrange(mids)}(7)")  # still running when main ends
        return "\n".join(out) + "\n"


def run(nift, path, vm):
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C", "TZ": "UTC",
           "NIFT_PREVALIDATE": "off", "HOME": str(path.parent)}
    stats = path.parent / "stats.jsonl"
    if stats.exists():
        stats.unlink()
    if vm:
        env.update({"NIFT_VM": "require", "NIFT_VM_STATS": str(stats)})
    try:
        p = subprocess.run([nift, path.name], cwd=path.parent, env=env, capture_output=True, timeout=TIMEOUT,
                           stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        return ("timeout",), None
    st = None
    if vm and stats.exists():
        try:
            st = json.loads(stats.read_text().splitlines()[-1])
        except (ValueError, IndexError):
            st = None
    return (p.stdout, p.stderr, p.returncode), st


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--switch")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--count", type=int, default=120)
    ap.add_argument("--keep")
    a = ap.parse_args()
    binaries = [os.path.abspath(a.nift)] + ([os.path.abspath(a.switch)] if a.switch else [])
    rnd = random.Random(a.seed)
    failures = harness = 0
    spawned = 0
    with tempfile.TemporaryDirectory(prefix="cp22-adv-") as d:
        for k in range(a.count):
            source = Gen(rnd).program()
            path = Path(d) / f"adv{k}.f"
            path.write_text(source)
            legacy, _ = run(binaries[0], path, False)
            if legacy[0] == "timeout" or legacy[2] < 0 or legacy[2] >= 128:
                harness += 1
                print(f"HARNESS legacy adv{k}: {legacy[:3]}")
                continue
            for nift in binaries:
                vm, st = run(nift, path, True)
                tag = "switch" if nift != binaries[0] else "goto"
                if vm[0] == "timeout" or st is None or vm[2] < 0 or vm[2] >= 128:
                    harness += 1
                    print(f"HARNESS {tag} adv{k}: result={vm[:3]!r} stats={st}")
                    continue
                if st.get("outcome") not in ("vm", "vm-error") or st.get("vm_executions") != 1:
                    failures += 1
                    print(f"FAIL {tag} adv{k}: not run on the VM: {st.get('outcome')} {st.get('reason')}")
                elif vm != legacy:
                    failures += 1
                    print(f"FAIL {tag} adv{k}: legacy {legacy!r} VM {vm!r}")
                else:
                    spawned += int(st.get("coro_spawned", 0))
                if a.keep and (failures or harness):
                    Path(a.keep).mkdir(parents=True, exist_ok=True)
                    (Path(a.keep) / f"adv{k}.f").write_text(source)
    ok = failures == 0 and harness == 0
    print(f"{'PASS' if ok else 'FAIL'} cp22 adversarial: seed {a.seed}, {a.count} programs x {len(binaries)} "
          f"build(s), {failures} difference(s), {harness} harness failure(s), {spawned} tasks spawned on the VM")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
