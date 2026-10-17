#!/usr/bin/env python3
"""Runtime 2.0 CP11: VM proof-of-concept comparison.

Runs the same workloads (same algorithm, same output) on four engines:
  v4.12   the baseline binary (--baseline, built from ce8cd36)
  legacy  the current nift with the VM off
  vm      the current nift with NIFT_VM=require (never falls back; a unit the
          VM cannot run is an error, so every VM number is genuine VM execution)
  cpython python3 (function-local code, as benchmarks.md prescribes)

Groups:
  ops      callgrind marginal instructions per operation (N=4,000 vs 8,000;
           the run.py `ops` methodology and templates) -- the CP11 gate
  fib      callgrind marginal instructions per recursive call (fib(10)->fib(12))
  kernels  whole-process wall time (median) and peak RSS: fib(22), fib(25),
           1M-iteration loop, 200k calls; process startup (trivial script)
  phases   VM phase split: callgrind instructions inside rt::parse_script,
           rt::compile_unit and rt::Vm::run (and legacy translation for
           reference), wall microseconds from NIFT_VM_STATS; cold (fresh
           process) vs warm (execution only, after compile)

Every run checks its output against the golden value; a mismatch aborts.
Output: JSON lines, host fingerprint first. Needs valgrind for ops/fib/phases.

Usage: vm_compare.py --nift ./nift --baseline ../baseline-src/nift [--groups ...] [--runs N] [--out F]
"""
import argparse, json, os, re, statistics, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run as base  # noqa: E402  (run.py: OPS templates, host fingerprint)

GROUPS = ["ops", "fib", "kernels", "phases"]
FIB = "fn(fib(n)) { if(n < 2) { return n } return fib(n - 1) + fib(n - 2) }\n"
PY_FIB = "def fib(n): return n if n < 2 else fib(n-1) + fib(n-2)\n"
FIB_VALUE = {10: 55, 12: 144, 22: 17711, 25: 75025}
FIB_CALLS = {10: 177, 12: 465}
KERNELS = {  # name: (nift source, python source, golden)
    "fib22": (FIB + "print(fib(22))\n", PY_FIB + "print(fib(22))\n", "17711"),
    "fib25": (FIB + "print(fib(25))\n", PY_FIB + "print(fib(25))\n", "75025"),
    "loop1m": (base.NIFT_SRC["loop1m"], base.PY_SRC["loop1m"], "499999500000"),
    "calls200k": (base.NIFT_SRC["calls200k"], base.PY_SRC["calls200k"], "19999900000"),
    "startup": ("x := 1\nprint(x)\n", "x = 1\nprint(x)\n", "1"),
}
NIFT_ENGINES = ("v4.12", "legacy", "vm")


def engine_cmd(a, engine, path):
    if engine == "cpython":
        return ["python3", "-I", path], {}
    env = {"NIFT_VM": "require"} if engine == "vm" else {}
    return [a.baseline if engine == "v4.12" else a.nift, path], env


def environ(extra):
    env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_")}
    env.update(extra)
    return env


def check(out, golden, what):
    last = out.strip().splitlines()[-1:] if out.strip() else []
    if last != [golden]:
        raise RuntimeError(f"{what}: output {out[-200:]!r} != golden {golden!r}")


def callgrind(cmd, env, golden, what, toggle=None):
    fd, cg = tempfile.mkstemp(suffix=".cg")
    os.close(fd)
    extra = [f"--toggle-collect={toggle}"] if toggle else []
    p = subprocess.run(["valgrind", "--tool=callgrind", f"--callgrind-out-file={cg}", *extra, *cmd],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=environ(env))
    os.unlink(cg)
    m = re.search(r"Collected : (\d+)", p.stderr)
    if p.returncode != 0 or not m:
        raise RuntimeError(f"{what}: {p.stderr[-600:]}")
    check(p.stdout, golden, what)
    return int(m.group(1))


RSS_HELPER = r"""
#include <stdio.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
int main(int argc, char** argv) {
    pid_t pid = fork();
    if (pid == 0) { execvp(argv[1], argv + 1); _exit(127); }
    int status = 0; struct rusage usage;
    if (wait4(pid, &status, 0, &usage) < 0) return 126;
    fprintf(stderr, "\nrss-helper maxrss_kib=%ld\n", usage.ru_maxrss);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 125;
}
"""


def rss_helper(d):
    """A tiny C launcher: the child is forked from a ~1 MB process, so its
    ru_maxrss is the measured program's own peak (a child forked from Python
    inherits Python's ~15 MB high-water mark until exec)."""
    path = os.path.join(d, "rss-helper")
    if not os.path.exists(path):
        src = write(d, "rss-helper.c", RSS_HELPER)
        subprocess.run(["cc", "-O2", src, "-o", path], check=True)
    return path


def timed(cmd, env, golden, what, d):
    """Wall seconds (includes the launcher's fork, ~0.3 ms) and peak RSS (KiB)."""
    t = time.perf_counter()
    p = subprocess.run([rss_helper(d), *cmd], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environ(env))
    dt = time.perf_counter() - t
    err = p.stderr.decode(errors="replace")
    m = re.search(r"rss-helper maxrss_kib=(\d+)", err)
    if p.returncode != 0 or not m:
        raise RuntimeError(f"{what}: exit {p.returncode}: {err[-400:]}")
    check(p.stdout.decode(errors="replace"), golden, what)
    return dt, int(m.group(1))


def write(d, name, text):
    path = os.path.join(d, name)
    with open(path, "w") as f:
        f.write(text)
    return path


def g_ops(a, d, emit):
    n1, n2 = 4000, 8000
    for name, (nt, pt) in base.OPS.items():
        row = {}
        for engine in NIFT_ENGINES + ("cpython",):
            irs = []
            for n in (n1, n2):
                if engine == "cpython":
                    src, path = base.py_fn(pt.format(N=n), name), f"op_{name}_{n}.py"
                else:
                    src, path = nt.format(N=n), f"op_{name}_{n}.f"
                p = write(d, path, src)
                golden = subprocess.run(["python3", "-I", write(d, "g.py", base.py_fn(pt.format(N=n), name))],
                                        stdout=subprocess.PIPE, text=True).stdout.strip()
                cmd, env = engine_cmd(a, engine, p)
                irs.append(callgrind(cmd, env, golden, f"ops {name} {engine} N={n}"))
            row[engine] = round((irs[1] - irs[0]) / (n2 - n1))
        emit("ops", name, {"instr": row, "vm_vs_v4.12": round(row["v4.12"] / row["vm"], 2),
                           "vm_vs_cpython": round(row["vm"] / row["cpython"], 2)})


def g_fib(a, d, emit):
    for label, wrap in (("fib_call_toplevel", False), ("fib_call_in_loop", True)):
        row = {}
        for engine in NIFT_ENGINES + ("cpython",):
            vals = []
            for n in (10, 12):
                if engine == "cpython":
                    p = write(d, "fib.py", PY_FIB + f"print(fib({n}))\n")
                else:
                    body = (f"i := 0\nwhile(i < 1) {{ print(fib({n})); i += 1 }}\n" if wrap else f"print(fib({n}))\n")
                    p = write(d, "fib.f", FIB + body)
                cmd, env = engine_cmd(a, engine, p)
                vals.append(callgrind(cmd, env, str(FIB_VALUE[n]), f"{label} {engine} fib({n})"))
            row[engine] = round((vals[1] - vals[0]) / (FIB_CALLS[12] - FIB_CALLS[10]))
        emit("fib", label, {"instr_per_call": row, "vm_vs_v4.12": round(row["v4.12"] / row["vm"], 2),
                            "vm_vs_cpython": round(row["vm"] / row["cpython"], 2)})


def g_kernels(a, d, emit):
    for name, (nsrc, psrc, golden) in KERNELS.items():
        row = {}
        for engine in NIFT_ENGINES + ("cpython",):
            if engine in ("v4.12", "legacy") and name == "fib25" and not a.slow:
                continue  # ~7 s per legacy run; enable with --slow
            p = write(d, f"{name}.py" if engine == "cpython" else f"{name}.f", psrc if engine == "cpython" else nsrc)
            cmd, env = engine_cmd(a, engine, p)
            walls, rss = [], []
            for _ in range(a.runs):
                dt, kib = timed(cmd, env, golden, f"kernel {name} {engine}", d)
                walls.append(dt)
                rss.append(kib)
            row[engine] = {"median_s": round(statistics.median(walls), 5), "min_s": round(min(walls), 5),
                           "peak_rss_kib": max(rss), "n": a.runs}
        emit("kernels", name, row)


def vm_stats(a, path, runs):
    """Median NIFT_VM_STATS phase times (microseconds) over `runs` processes."""
    fd, sp = tempfile.mkstemp(suffix=".jsonl")
    os.close(fd)
    for _ in range(runs):
        subprocess.run([a.nift, path], stdout=subprocess.DEVNULL, env=environ({"NIFT_VM": "require",
                       "NIFT_VM_STATS": sp}), check=True)
    rows = [json.loads(l) for l in open(sp)]
    os.unlink(sp)
    assert all(r["outcome"] == "vm" for r in rows), rows[:1]
    return {k: round(statistics.median(r[k] for r in rows), 1) for k in ("parse_us", "compile_us", "execute_us")} | {
        "instructions": rows[0]["instructions"], "source_bytes": rows[0]["source_bytes"]}


def large_program(functions):
    parts = []
    for k in range(functions):
        parts.append(f"fn(f{k}(a, b)) {{\n  x := a + {k}\n  if(x > b) {{ x = x - b }} else {{ x = x * 2 }}\n"
                     f"  i := 0\n  while(i < 3) {{ x += i; i += 1 }}\n  return x\n}}\n")
    parts.append("print(f0(1, 2))\n")
    return "".join(parts), "5"


def g_phases(a, d, emit):
    progs = {name: (src, golden) for name, (src, _, golden) in KERNELS.items()}
    progs["large_500_functions"] = large_program(500)
    for name, (src, golden) in progs.items():
        p = write(d, name + ".f", src)
        rec = {"source_bytes": len(src.encode())}
        for phase, fn in (("parse", "nift::rt::parse_script*"), ("compile", "nift::rt::compile_unit*"),
                          ("execute", "nift::rt::Vm::run*"), ("vm_total", "nift::rt::run_script_on_vm*")):
            if name in ("fib25",) and phase in ("execute", "vm_total"):
                continue  # slow under valgrind; fib22 covers execution
            rec[f"{phase}_instr"] = callgrind([a.nift, p], {"NIFT_VM": "require"}, golden, f"phase {phase} {name}", fn)
        rec["process_instr_vm"] = (callgrind([a.nift, p], {"NIFT_VM": "require"}, golden, f"process {name}")
                                   if name not in ("fib25",) else None)
        rec["legacy_translate_instr"] = callgrind([a.nift, p], {}, golden, f"translate {name}",
                                                  "Parser::translate_function_program*") if name != "fib25" else None
        rec["wall_us_median"] = vm_stats(a, p, a.runs)
        cold = [timed([a.nift, p], {"NIFT_VM": "require"}, golden, f"cold {name}", d)[0] for _ in range(a.runs)]
        rec["cold_process_s_median"] = round(statistics.median(cold), 5)
        if a.vm_run and name != "fib25":
            # In-process: compile once, execute 20 times (first = cold caches, rest = warm).
            r = subprocess.run([a.vm_run, "--repeat", "20", p], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, check=True)
            check(r.stdout, golden, f"vm-run {name}")
            rec["in_process"] = json.loads(r.stderr.strip().splitlines()[-1])
        emit("phases", name, rec)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--groups", default=",".join(GROUPS))
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--slow", action="store_true", help="also time fib(25) on v4.12 and legacy")
    ap.add_argument("--vm-run", help="tests/rt/vm_run.cpp binary (.build/rt/vm-run) for in-process warm runs")
    ap.add_argument("--out")
    a = ap.parse_args()
    a.nift, a.baseline = os.path.abspath(a.nift), os.path.abspath(a.baseline)
    a.vm_run = os.path.abspath(a.vm_run) if a.vm_run else None
    out = open(a.out, "w") if a.out else sys.stdout
    head = subprocess.run(["git", "-C", HERE, "rev-parse", "HEAD"], stdout=subprocess.PIPE, text=True).stdout.strip()
    meta = {"kind": "meta", "checkpoint": "CP11", "commit": head, "nift": a.nift, "baseline": a.baseline,
            "baseline_version": subprocess.run([a.baseline, "version"], stdout=subprocess.PIPE, text=True).stdout.strip(),
            "python": subprocess.run(["python3", "--version"], stdout=subprocess.PIPE, text=True).stdout.strip(),
            "host": base.host(), "loadavg": open("/proc/loadavg").read().split()[:3]}
    out.write(json.dumps(meta) + "\n")

    def emit(group, name, data):
        rec = {"group": group, "name": name, **data}
        out.write(json.dumps(rec) + "\n")
        out.flush()
        if out is not sys.stdout:
            print(json.dumps(rec), flush=True)

    with tempfile.TemporaryDirectory(prefix="nift-cp11-") as d:
        for g in a.groups.split(","):
            globals()["g_" + g](a, d, emit)


if __name__ == "__main__":
    main()
