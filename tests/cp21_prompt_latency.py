#!/usr/bin/env python3
"""CP21 prompt-return latency observations.

Interval: from writing one complete command line (including '\r') to the next
prompt being fully emitted on the PTY. Each observation is individual and
includes process launch/command execution; it is not a pure prompt-paint
timer. The threshold stays at 3 ms and the classification is UNVERIFIED when
the local tail exceeds it or the reference platform has not measured it.
"""
import argparse
import importlib.util
import json
import os
import statistics
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("cp21_contract", HERE / "cp21_interactive_host.py")
contract = importlib.util.module_from_spec(spec)
spec.loader.exec_module(contract)


def observe(binary, command, samples):
    shell = contract.Pty(binary)
    try:
        shell.prompt(initial=True)
        values = []
        for _ in range(samples):
            shell.transcript += shell.buf
            shell.buf = b""
            start = time.perf_counter()
            os.write(shell.fd, (command + "\r").encode())
            shell.prompt(timeout=5.0)
            values.append((time.perf_counter() - start) * 1000.0)
        return values
    finally:
        shell.close()


def summarise(values, threshold=3.0):
    ordered = sorted(values)
    index = min(len(ordered) - 1, int(round(0.95 * (len(ordered) - 1))))
    return {
        "samples": values,
        "count": len(values),
        "median_ms": statistics.median(values),
        "p95_ms": ordered[index],
        "max_ms": max(values),
        "over_threshold": sum(value > threshold for value in values),
        "threshold_ms": threshold,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("nift")
    parser.add_argument("--output")
    parser.add_argument("--samples", type=int, default=100)
    args = parser.parse_args()
    binary = os.path.abspath(args.nift)
    if not os.access(binary, os.X_OK):
        print(f"FAIL missing binary {binary}")
        return 1
    results = {}
    results["true"] = summarise(observe(binary, "/bin/true", args.samples))
    results["true_pipeline"] = summarise(observe(binary, "/bin/true | /bin/cat", args.samples))

    # Transition distributions requested by the review: background launch,
    # wait on completed job, and fg of a stopped job that exits at once.
    for name, initial, command in (
        ("background_launch", None, "/bin/true &"),
        ("completed_wait", "/bin/true &", "wait 1"),
        ("stopped_fg", "/bin/sh -c 'kill -STOP $$; exit 0' &", "fg 1"),
    ):
        values = []
        for _ in range(30):
            shell = contract.Pty(binary)
            try:
                shell.prompt(initial=True)
                if initial:
                    shell.line(initial)
                    shell.prompt()
                    if name == "stopped_fg":
                        contract.wait_jobs_state(shell, 1, "Stopped")
                    else:
                        time.sleep(0.02)
                shell.transcript += shell.buf
                shell.buf = b""
                start = time.perf_counter()
                os.write(shell.fd, (command + "\r").encode())
                shell.prompt(timeout=5.0)
                values.append((time.perf_counter() - start) * 1000.0)
            finally:
                shell.close()
        results[name] = summarise(values)

    over = any(item["over_threshold"] for item in results.values())
    results["classification"] = "UNVERIFIED" if over else "LOCAL-UNDER-THRESHOLD"
    results["note"] = ("individual PTY command-to-prompt observations; includes launch/execution, "
                       "not a pure prompt-paint interval; reference platform not measured")
    print(json.dumps({key: (value if key in ("classification", "note") else
                            {k: v for k, v in value.items() if k != "samples"})
                      for key, value in results.items()}, indent=2, sort_keys=True))
    if args.output:
        Path(args.output).write_text(json.dumps(results, indent=2, sort_keys=True) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
