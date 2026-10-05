#!/usr/bin/env python3
"""Runtime-architecture campaign benchmark harness.

Single source of truth for the POST-V46-RUNTIME-ARCHITECTURE campaign workloads.
Every checkpoint is judged against the same shapes and measurement rules.

  python3 benchmarks/runtime_arch_review.py --nift /path/to/nift
  python3 benchmarks/runtime_arch_review.py --nift ./nift --callgrind --json out.json
  python3 benchmarks/runtime_arch_review.py --list

Callgrind instruction counts are the primary, load-independent evidence for the
interpreter workloads. Native wall-clock (median of --reps) is corroborating
only. Shapes intentionally mirror the review's re-derived workloads; iteration
counts keep a callgrind run tractable.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

# (name, source, iterations, native_reps)
WORKLOADS: list[tuple[str, str, int, int]] = [
    ("numeric_loop",
     "fn(main(n)) { s := 0; i := 0; while(i < n) { s += i; i += 1 } print(s) }\n"
     "main(@N@)\n", 500000, 3),
    ("array_push_index",
     "fn(main(n)) { a := []; i := 0; while(i < 10000) { a.push(i); i += 1 }\n"
     "  t := 0; i = 0; while(i < n) { t += a[i % 10000]; i += 1 } print(t) }\n"
     "main(@N@)\n", 500000, 3),
    ("fn_empty",
     "fn(noop()) { return 0 }\n"
     "fn(main(n)) { i := 0; while(i < n) { noop(); i += 1 } print(i) }\n"
     "main(@N@)\n", 20000, 3),
    ("fn_args",
     "fn(add1(x)) { return x }\n"
     "fn(main(n)) { i := 0; while(i < n) { add1(i); i += 1 } print(i) }\n"
     "main(@N@)\n", 20000, 3),
    ("lambda",
     "fn(main(n)) { f := (x) => x; i := 0; while(i < n) { f(i); i += 1 } print(i) }\n"
     "main(@N@)\n", 20000, 3),
    ("map_set_new",
     "fn(main(n)) { m := map(); i := 0; while(i < n) { m.set(i, i); i += 1 } print(m.size()) }\n"
     "main(@N@)\n", 2000, 3),
    ("map_get",
     "fn(main(n)) { m := map(); i := 0; while(i < 500) { m.set(i, i); i += 1 }\n"
     "  t := 0; i = 0; while(i < n) { t += m.get(i % 500); i += 1 } print(t) }\n"
     "main(@N@)\n", 20000, 3),
    ("map_contains",
     "fn(main(n)) { m := map(); i := 0; while(i < 500) { m.set(i, i); i += 1 }\n"
     "  c := 0; i = 0; while(i < n) { if(m.contains(i % 1000)) { c += 1 }; i += 1 } print(c) }\n"
     "main(@N@)\n", 20000, 3),
    ("map_iterate",
     "fn(main(n)) { m := map(); i := 0; while(i < 1000) { m.set(i, i); i += 1 }\n"
     "  total := 0; k := 0; while(k < n) { for((key,val) : m) { total += val }; k += 1 } print(total) }\n"
     "main(@N@)\n", 100, 3),
]


def gen(dir_: str) -> dict[str, str]:
    paths = {}
    for name, src, n, _ in WORKLOADS:
        p = os.path.join(dir_, name + ".f")
        with open(p, "w") as fh:
            fh.write(src.replace("@N@", str(n)))
        paths[name] = p
    return paths


def native_ms(nift: str, path: str, reps: int) -> float:
    samples = []
    for _ in range(reps):
        t0 = time.perf_counter()
        r = subprocess.run([nift, path], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
        samples.append((time.perf_counter() - t0) * 1000.0)
        if r.returncode != 0:
            return float("nan")
    return statistics.median(samples)


def callgrind_ir(nift: str, path: str, out_dir: str, timeout: int) -> int | None:
    cg = os.path.join(out_dir, os.path.basename(path) + ".cg")
    try:
        subprocess.run(["valgrind", "--tool=callgrind",
                        "--callgrind-out-file=" + cg, nift, path],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=timeout, check=True)
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError):
        return None
    with open(cg) as fh:
        for line in fh:
            if line.startswith("summary:"):
                return int(line.split()[1])
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", default="./nift")
    ap.add_argument("--callgrind", action="store_true")
    ap.add_argument("--reps", type=int, default=None)
    ap.add_argument("--json", default=None)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--cg-timeout", type=int, default=900)
    args = ap.parse_args()

    if args.list:
        for name, _, n, _ in WORKLOADS:
            print(f"{name}\t{n}")
        return 0

    if not (os.path.isfile(args.nift) and os.access(args.nift, os.X_OK)):
        print(f"nift binary not executable: {args.nift}", file=sys.stderr)
        return 2

    tmp = tempfile.mkdtemp(prefix="nift-runtime-arch-")
    try:
        paths = gen(tmp)
        rows = []
        for name, _, _, reps in WORKLOADS:
            ms = native_ms(args.nift, paths[name], args.reps or reps)
            ir = callgrind_ir(args.nift, paths[name], tmp, args.cg_timeout) if args.callgrind else None
            rows.append({"workload": name, "native_ms": ms, "ir": ir})
            irs = f"{ir:,}" if ir is not None else "-"
            print(f"{name:20s} native={ms:9.2f} ms   Ir={irs}")
        if args.json:
            with open(args.json, "w") as fh:
                json.dump({"nift": os.path.abspath(args.nift), "rows": rows}, fh, indent=2)
            print(f"wrote {args.json}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
