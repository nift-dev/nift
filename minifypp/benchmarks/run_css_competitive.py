#!/usr/bin/env python3
"""Reproducible process-inclusive CSS comparison with raw evidence."""
from __future__ import annotations

import argparse
import csv
import gzip
import json
import os
import platform
import shutil
import statistics
import subprocess
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def command_for(tool: str, binary: Path, source: Path, output: Path) -> tuple[list[str], Path]:
    if tool == "minifypp":
        work_input = output.with_name(f"{output.stem}-input.css")
        shutil.copy2(source, work_input)
        generated = work_input.with_name(f"{work_input.stem}.min.css")
        if generated.exists():
            generated.unlink()
        return [str(binary), str(work_input)], generated
    if tool == "esbuild":
        return [str(binary), str(source), "--minify", "--legal-comments=none", f"--outfile={output}"], output
    if tool == "lightningcss":
        return [str(binary), "--minify", str(source), "-o", str(output)], output
    raise ValueError(tool)


def run_once(tool: str, binary: Path, source: Path, output: Path) -> tuple[float, list[str]]:
    command, expected = command_for(tool, binary, source, output)
    start = time.perf_counter_ns()
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    elapsed_ms = (time.perf_counter_ns() - start) / 1_000_000
    if completed.returncode != 0:
        raise RuntimeError(f"{tool} failed ({completed.returncode}): " + completed.stderr.decode("utf-8", "replace")[:2000])
    if not expected.is_file():
        raise RuntimeError(f"{tool} did not create expected output {expected}")
    if expected != output:
        shutil.copy2(expected, output)
    return elapsed_ms, command


def percentile(samples: list[float], fraction: float) -> float:
    ordered = sorted(samples)
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    weight = position - lower
    return ordered[lower] * (1 - weight) + ordered[upper] * weight


def version_output(binary: Path, arguments: list[str]) -> str:
    completed = subprocess.run([str(binary), *arguments], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
    return completed.stdout.strip().splitlines()[0]


def cpu_model() -> str:
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or "unknown"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("fixtures", nargs="+", type=Path)
    parser.add_argument("--minifypp", type=Path, default=Path("./minify"))
    parser.add_argument("--esbuild", required=True, type=Path)
    parser.add_argument("--lightningcss", required=True, type=Path)
    parser.add_argument("--iterations", type=int, default=45)
    parser.add_argument("--warmups", type=int, default=5)
    parser.add_argument("--json", required=True, type=Path)
    parser.add_argument("--csv", required=True, type=Path)
    parser.add_argument("--fixture-provenance", action="append", default=[], metavar="NAME=SOURCE")
    parser.add_argument("--minifypp-commit", default="unknown")
    parser.add_argument("--compiler", default="unknown")
    parser.add_argument("--build-flags", default="unknown")
    parser.add_argument("--esbuild-package-version", default="unknown")
    parser.add_argument("--lightningcss-package-version", default="unknown")
    args = parser.parse_args()
    if args.iterations < 1 or args.warmups < 0:
        parser.error("iterations must be positive and warmups non-negative")

    provenance = dict(item.split("=", 1) for item in args.fixture_provenance)
    tools = [("minifypp", args.minifypp.resolve()), ("esbuild", args.esbuild.resolve()), ("lightningcss", args.lightningcss.resolve())]
    for name, binary in tools:
        if not binary.is_file():
            parser.error(f"{name} executable not found: {binary}")
    versions = {
        "minifypp": version_output(tools[0][1], ["--version"]),
        "esbuild": version_output(tools[1][1], ["--version"]),
        "lightningcss": version_output(tools[2][1], ["--version"]),
    }
    package_versions = {
        "minifypp": versions["minifypp"],
        "esbuild": args.esbuild_package_version,
        "lightningcss": args.lightningcss_package_version,
    }
    results: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory(prefix="minifypp-css-bench-") as td:
        temp = Path(td)
        for fixture_arg in args.fixtures:
            fixture = fixture_arg.resolve()
            if not fixture.is_file():
                parser.error(f"fixture not found: {fixture}")
            for tool, binary in tools:
                output = temp / f"{fixture.stem}-{tool}.css"
                samples: list[float] = []
                command: list[str] = []
                for index in range(args.warmups + args.iterations):
                    if output.exists():
                        output.unlink()
                    elapsed, command = run_once(tool, binary, fixture, output)
                    if index >= args.warmups:
                        samples.append(elapsed)
                output_bytes = output.read_bytes()
                median = statistics.median(samples)
                results.append({
                    "fixture": fixture.name, "fixture_source": provenance.get(fixture.name, "not recorded"),
                    "tool": tool, "tool_version": versions[tool],
                    "distribution_version": package_versions[tool], "command": command, "status": "success",
                    "input_bytes": fixture.stat().st_size, "output_bytes": len(output_bytes),
                    "gzip_bytes": len(gzip.compress(output_bytes, compresslevel=9, mtime=0)),
                    "warmups": args.warmups, "iterations": args.iterations, "samples_ms": samples,
                    "median_ms": median, "p25_ms": percentile(samples, 0.25), "p75_ms": percentile(samples, 0.75),
                    "min_ms": min(samples), "max_ms": max(samples),
                    "throughput_mib_s": fixture.stat().st_size / (1024 * 1024) / (median / 1000),
                })

    document = {
        "schema_version": 1,
        "benchmark": "Minify++ same-host process-inclusive CSS CLI comparison",
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "boundary": "one fresh CLI process per sample; filesystem preparation is outside the timer and tool output writing is inside",
        "host": {"hostname": platform.node(), "os": platform.platform(), "kernel": platform.release(), "architecture": platform.machine(), "cpu": cpu_model(), "logical_cpus": os.cpu_count()},
        "minifypp_build": {"commit": args.minifypp_commit, "compiler": args.compiler, "flags": args.build_flags},
        "results": results,
    }
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(document, indent=2) + "\n")
    fields = ["fixture", "tool", "tool_version", "input_bytes", "output_bytes", "gzip_bytes", "median_ms", "p25_ms", "p75_ms", "min_ms", "max_ms", "throughput_mib_s", "warmups", "iterations", "status"]
    args.csv.parent.mkdir(parents=True, exist_ok=True)
    with args.csv.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for result in results:
            writer.writerow({key: result[key] for key in fields})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
