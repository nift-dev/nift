#!/usr/bin/env python3
"""CP22 coroutine measurements (docs/runtime-2/checkpoints/cp22-coroutines.md §8).

  cp22_coro.py --nift ./nift [--callgrind] [--runs 5] [--legacy-sleeps]

Workloads (NIFT_VM=require; every program's output is checked):
  sleeps10k     10,000 tasks each sleep(10), awaited (roadmap gate: < 0.2 s wall)
  spawn N       N tiny tasks spawned then awaited, N = 1k, 10k, 100k: wall, and
                with --callgrind the instructions per spawn ((Ir(N) - Ir(0)) / N)
                and the linearity ratio per-spawn(100k) / per-spawn(1k)
  yield         two CPU-bound tasks of 2M loop iterations each vs one task doing
                both loops: scheduling overhead (wall) and preemption count
  wakeup        100 sequential sleep(10) in one task: mean latency beyond 10 ms
  fairness      two CPU-bound tasks: their completion order and the preemption
                count (a rotation every 1,024 safepoints)
  idle          10 tasks sleeping 1 s: process CPU time (user + sys) while idle
  teardown      a run stopped by its memory budget with thousands of suspended
                tasks: wall time of the stop including the release of every task
  memory        heap_peak / heap_live (NIFT_VM_STATS) and max RSS for sleeps10k

Output: JSON lines (host fingerprint first) and a summary. Numbers from a
shared or non-reference machine are exploratory only (owner rule: gates are
measured on the reference platform); nothing here declares a gate passed.
"""
import argparse
import json
import os
import resource
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run as base  # noqa: E402  (host fingerprint, callgrind helper)

SPAWN = """fn[async](t(i)) {{ return i }}
hs := []
i := 0
while(i < {n}) {{ hs.push(t(i)); i += 1 }}
s := 0
for(h : hs) {{ s += await h }}
print(s)
"""
SLEEPS = """fn[async](f(i)) {{ sleep({ms}); return 1 }}
hs := []
i := 0
while(i < {n}) {{ hs.push(f(i)); i += 1 }}
s := 0
for(h : hs) {{ s += await h }}
print(s)
"""
SPIN = """fn[async](spin(n)) {{ i := 0; x := 0; while(i < n) {{ x += 1; i += 1 }}; return x }}
{body}
"""
WAKEUP = """fn[async](f(n)) {{ i := 0; while(i < n) {{ sleep(10); i += 1 }}; return n }}
print(await f({n}))
"""
CPU4 = """fn[async](work(n)) {{ i := 0; x := 0; while(i < n) {{ x = (x * 31 + i) % 1000003; i += 1 }}; return x }}
a := work({n})
b := work({n})
c := work({n})
d := work({n})
print((await a) + (await b) + (await c) + (await d))
"""
# CPU-bound scaling (owner request, 2026-10-27): k concurrent tasks, the same
# work per task (weak scaling), k = 1, 2, 4, 8; two task bodies: integer
# arithmetic, and string/array work that spends more of its time in natives.
SCALE_BODIES = {
    "arith": "i := 0; x := 0; while(i < n) {{ x = (x * 31 + i) % 1000003; i += 1 }}; return x",
    "string": 'i := 0; s := ""; c := 0; while(i < n) {{ s += "ab"; if (s.length() > 64) {{ c += s.length(); s = "" }};'
              ' i += 1 }}; return c',
    # Time spent in one native (the same C++ code on both engines): the VM's
    # interpretive advantage is small, so legacy's parallelism shows.
    "native": 'big := ""; j := 0; while(j < 4096) {{ big += "0123456789abcdef"; j += 1 }}; i := 0; h := "x";'
              ' while(i < n) {{ h = sha256(big + h); i += 1 }}; return h.length()',
}


def scale_program(body, k, n):
    lines = ["fn[async](work(n)) { " + SCALE_BODIES[body].format() + " }"]
    lines += [f"t{j} := work({n})" for j in range(k)]
    lines.append("s := 0")
    lines += [f"s += await t{j}" for j in range(k)]
    lines.append("print(s)")
    return "\n".join(lines) + "\n"


def cpu_scale(nift, d, runs, n_by_body):
    """Legacy (thread pool, max(2, hw) workers) vs VM (one thread, cooperative)."""
    rows = []
    for body, n in n_by_body.items():
        for k in (1, 2, 4, 8):
            p = write(d, f"scale-{body}-{k}.f", scale_program(body, k, n))
            res = {}
            for engine, env in (("legacy", dict(os.environ, NIFT_VM="0")),
                                ("vm", vm_env(os.path.join(d, f"scale-{body}-{k}.jsonl")))):
                walls, cpus, outs = [], [], set()
                for _ in range(runs):
                    wall, cpu, _, pr = timed([nift, p], env)
                    walls.append(wall)
                    cpus.append(cpu)
                    outs.add(pr.stdout.decode().strip() if pr.returncode == 0 else f"exit {pr.returncode}")
                res[engine] = {"wall_median_s": statistics.median(walls), "wall_min_s": min(walls),
                               "cpu_median_s": statistics.median(cpus), "outputs": sorted(outs)}
            st_path = Path(d, f"scale-{body}-{k}.jsonl")
            st = json.loads(st_path.read_text().splitlines()[-1]) if st_path.exists() else {}
            rows.append({"workload": f"scale-{body}", "tasks": k, "n_per_task": n, **res,
                         "vm_outcome": st.get("outcome"), "same_output": res["legacy"]["outputs"] == res["vm"]["outputs"],
                         "legacy_over_vm_wall": res["legacy"]["wall_median_s"] / res["vm"]["wall_median_s"],
                         "legacy_parallelism": res["legacy"]["cpu_median_s"] / res["legacy"]["wall_median_s"]})
    return rows


TEARDOWN = """fn[async](f(i)) { x := [i, i, i, i]; sleep(100000); return x.size() }
hs := []
i := 0
while(i < 100000000) { hs.push(f(i)); i += 1 }
print(hs.size())
"""


def write(d, name, text):
    path = Path(d) / name
    path.write_text(text)
    return str(path)


class Done:
    def __init__(self, returncode, stdout, stderr):
        self.returncode, self.stdout, self.stderr = returncode, stdout, stderr


def timed(cmd, env=None):
    """Wall time, CPU time and peak RSS (KiB) of this one child (wait4)."""
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        t = time.perf_counter()
        p = subprocess.Popen(cmd, stdout=out, stderr=err, env=env)
        _, status, usage = os.wait4(p.pid, 0)
        wall = time.perf_counter() - t
        p.returncode = os.waitstatus_to_exitcode(status)
        out.seek(0)
        err.seek(0)
        done = Done(p.returncode, out.read(), err.read())
    return wall, usage.ru_utime + usage.ru_stime, usage.ru_maxrss, done


def vm_env(stats=None):
    env = dict(os.environ, NIFT_VM="require")
    if stats:
        env["NIFT_VM_STATS"] = stats
    return env


def measure(nift, path, golden, runs, stats_dir=None):
    walls, cpus, rss, stats = [], [], 0, None
    for k in range(runs):
        stats_path = os.path.join(stats_dir, f"stats-{k}.jsonl") if stats_dir else None
        wall, cpu, maxrss, p = timed([nift, path], vm_env(stats_path))
        out = p.stdout.decode().strip()
        if p.returncode != 0 or (golden is not None and out != golden):
            raise RuntimeError(f"{path}: exit {p.returncode} out={out[:200]!r} err={p.stderr[:300]!r}")
        walls.append(wall)
        cpus.append(cpu)
        rss = max(rss, maxrss)
        if stats_path and os.path.exists(stats_path):
            stats = json.loads(Path(stats_path).read_text().splitlines()[-1])
    return {"wall_median_s": statistics.median(walls), "wall_min_s": min(walls), "cpu_median_s": statistics.median(cpus),
            "max_rss_kb": rss, "runs": runs, "stats": stats}


def callgrind(nift, path):
    old = os.environ.get("NIFT_VM")
    os.environ["NIFT_VM"] = "require"
    try:
        ir, _ = base.callgrind_ir([nift, path])
    finally:
        if old is None:
            os.environ.pop("NIFT_VM", None)
        else:
            os.environ["NIFT_VM"] = old
    return ir


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--callgrind", action="store_true")
    ap.add_argument("--legacy-sleeps", type=int, default=0, help="also time N legacy sleeps (slow)")
    ap.add_argument("--cpu-n", type=int, default=0, help="CPU-bound parallelism (legacy vs VM): loop size per task")
    ap.add_argument("--scale-only", action="store_true", help="only the CPU-bound scaling matrix (1/2/4/8 tasks)")
    ap.add_argument("--scale-n", default="arith=600000,string=300000", help="iterations per task, per body")
    a = ap.parse_args()
    nift = os.path.abspath(a.nift)
    rows = [{"host": base.host(), "loadavg": open("/proc/loadavg").read().split()[:3],
             "cores": {"nproc": os.cpu_count(), "sched_affinity": len(os.sched_getaffinity(0))},
             "label": "exploratory: not the reference platform unless stated by the operator"}]
    if a.scale_only:
        n_by_body = {kv.split("=")[0]: int(kv.split("=")[1]) for kv in a.scale_n.split(",")}
        with tempfile.TemporaryDirectory(prefix="cp22-scale-") as d:
            rows += cpu_scale(nift, d, a.runs, n_by_body)
        for row in rows:
            print(json.dumps(row, sort_keys=True))
        return 0
    with tempfile.TemporaryDirectory(prefix="cp22-bench-") as d:
        p = write(d, "sleeps10k.f", SLEEPS.format(n=10000, ms=10))
        rows.append({"workload": "sleeps10k", **measure(nift, p, "10000", a.runs, d)})
        if a.legacy_sleeps:
            lp = write(d, "legacy_sleeps.f", SLEEPS.format(n=a.legacy_sleeps, ms=10))
            wall, cpu, _, pr = timed([nift, lp], dict(os.environ, NIFT_VM="0"))
            rows.append({"workload": f"legacy-sleeps{a.legacy_sleeps}", "wall_s": wall, "cpu_s": cpu,
                         "ok": pr.stdout.decode().strip() == str(a.legacy_sleeps)})
        spawn = {}
        for n in (0, 1000, 10000, 100000):
            p = write(d, f"spawn{n}.f", SPAWN.format(n=n))
            golden = str(n * (n - 1) // 2)
            spawn[n] = measure(nift, p, golden, a.runs if n else 1, d)
            if a.callgrind:
                spawn[n]["instructions"] = callgrind(nift, p)
            rows.append({"workload": f"spawn{n}", **spawn[n]})
        if a.callgrind:
            per = {n: (spawn[n]["instructions"] - spawn[0]["instructions"]) / n for n in (1000, 10000, 100000)}
            rows.append({"workload": "spawn-linearity", "instructions_per_spawn": per,
                         "ratio_100k_over_1k": per[100000] / per[1000]})
        n = 2000000
        one = write(d, "spin1.f", SPIN.format(body=f"a := spin({n})\nb := spin({n})\nprint((await a) + (await b))"))
        seq = write(d, "spin0.f", SPIN.format(body=f"a := await spin({n})\nb := await spin({n})\nprint(a + b)"))
        both = measure(nift, one, str(2 * n), a.runs, d)
        alone = measure(nift, seq, str(2 * n), a.runs, d)
        rows.append({"workload": "yield", "interleaved": both, "sequential": alone,
                     "overhead": both["wall_median_s"] / alone["wall_median_s"] - 1.0})
        p = write(d, "wakeup.f", WAKEUP.format(n=100))
        w = measure(nift, p, "100", max(1, a.runs // 2), d)
        rows.append({"workload": "wakeup", **w, "mean_latency_ms": (w["wall_median_s"] - 1.0) * 10.0})
        p = write(d, "fair.f", SPIN.format(body=f"a := spin({n})\nb := spin({n // 2})\nprint(await b)\nprint(await a)"))
        rows.append({"workload": "fairness", **measure(nift, p, f"{n // 2}\n{n}", a.runs, d)})
        p = write(d, "idle.f", SLEEPS.format(n=10, ms=1000))
        rows.append({"workload": "idle", **measure(nift, p, "10", 3, d)})
        if a.cpu_n:
            # Owner decision input (architect, 2026-10-27): legacy runs the four
            # tasks on its thread pool in parallel, the VM interleaves them on
            # one thread. Same binary, same program, both engines.
            p = write(d, "cpu4.f", CPU4.format(n=a.cpu_n))
            res = {}
            for engine, env in (("legacy", dict(os.environ, NIFT_VM="0")), ("vm", vm_env())):
                walls, cpus, outs = [], [], set()
                for _ in range(a.runs):
                    wall, cpu, _, pr = timed([nift, p], env)
                    walls.append(wall)
                    cpus.append(cpu)
                    outs.add(pr.stdout.decode().strip() if pr.returncode == 0 else f"exit {pr.returncode}")
                res[engine] = {"wall_median_s": statistics.median(walls), "cpu_median_s": statistics.median(cpus),
                               "outputs": sorted(outs)}
            rows.append({"workload": f"cpu4-n{a.cpu_n}", **res,
                         "vm_over_legacy_wall": res["vm"]["wall_median_s"] / res["legacy"]["wall_median_s"]})
        p = write(d, "teardown.f", TEARDOWN)
        env = dict(vm_env(os.path.join(d, "td.jsonl")), NIFT_VM_HEAP_BUDGET=str(64 << 20))
        wall, cpu, rss, pr = timed([nift, p], env)
        st = json.loads(Path(d, "td.jsonl").read_text().splitlines()[-1]) if Path(d, "td.jsonl").exists() else {}
        rows.append({"workload": "teardown", "wall_s": wall, "cpu_s": cpu, "max_rss_kb": rss, "exit": pr.returncode,
                     "outcome": st.get("outcome"), "reason": st.get("reason"), "tasks": st.get("coro_spawned"),
                     "heap_live": st.get("heap_live")})
    for row in rows:
        print(json.dumps(row, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
