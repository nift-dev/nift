#!/usr/bin/env python3
"""Interpolation scaling probe (CP15B): fragments and output size.

Not a pass/fail threshold: reports median wall time for the corrected legacy
engine (NIFT_VM=0) and the VM (NIFT_VM=require) on the same script, plus the
output equality. A super-linear VM result is reported with numbers.
Usage: scaling.py --nift PATH [--runs N]
"""
import argparse, os, statistics, subprocess, sys, tempfile, time


def script_for_fragments(n):
    return 'print("' + ("x" * 100 + "$[1]") * n + '")\n'


def script_for_size(size):
    text = "x" * max(0, size - 100)
    return 'print("' + text + "$[1]" + '")\n'


def run(nift, text, vm):
    with tempfile.TemporaryDirectory(prefix="nift-interp-scale-") as folder:
        path = os.path.join(folder, "case.f")
        with open(path, "w", encoding="utf-8", newline="") as handle:
            handle.write(text)
        env = dict(os.environ)
        env["NIFT_VM"] = "require" if vm else "0"
        start = time.perf_counter()
        result = subprocess.run([nift, path], cwd=folder, capture_output=True, text=True,
                                timeout=120, env=env)
        return time.perf_counter() - start, result.stdout, result.returncode, result.stderr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nift", required=True)
    parser.add_argument("--runs", type=int, default=3)
    args = parser.parse_args()
    nift = os.path.abspath(args.nift)
    groups = [
        ("fragments", [("1", script_for_fragments(1)), ("10", script_for_fragments(10)),
                       ("100", script_for_fragments(100)), ("1000", script_for_fragments(1000))]),
        ("output", [("1KB", script_for_size(1024)), ("10KB", script_for_size(10 * 1024)),
                    ("100KB", script_for_size(100 * 1024)), ("1MB", script_for_size(1024 * 1024))]),
    ]
    for group, cases in groups:
        print(f"== {group}")
        for name, text in cases:
            legacy_times, vm_times = [], []
            legacy_out = vm_out = None
            for _ in range(args.runs):
                t, out, rc, err = run(nift, text, vm=False)
                if rc != 0:
                    print(f"  {name}: legacy FAILED rc={rc} {err.strip()}")
                    break
                legacy_times.append(t)
                legacy_out = out
                t, out, rc, err = run(nift, text, vm=True)
                if rc != 0:
                    print(f"  {name}: VM FAILED rc={rc} {err.strip()}")
                    break
                vm_times.append(t)
                vm_out = out
            else:
                same = legacy_out == vm_out
                l = statistics.median(legacy_times) * 1000
                v = statistics.median(vm_times) * 1000
                print(f"  {name:5s} legacy={l:8.1f} ms  vm={v:8.1f} ms  ratio={v/l:5.2f}  output_equal={same}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
