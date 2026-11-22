#!/usr/bin/env python3
"""Measure the existing equality workload and the historical work-list regression."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

from vm_subset import EQUALITY_BUDGET, EQUALITY_MEMORY, env_for

PER_ELEMENT_WORK = '''    if (left.is(Kind::Array)) {
        const auto& a = array_items(left);
        const auto& b = array_items(right);
        std::vector<EqualFrame> work;
        for (std::size_t i = a.size(); i-- > 0;) work.push_back({&a[i], &b[i], 1});
        bool flat = true;
        while (!work.empty()) {
            const auto pair = work.back();
            work.pop_back();
            const Shallow match = shallow_equal(*pair.left, *pair.right, identity);
            if (match == Shallow::Unequal) return Equality::Unequal;
            if (match == Shallow::Descend) { flat = false; break; }
        }
        if (flat) return Equality::Equal;
    }
'''


def build_control(work):
    original = Path("src/rt/compare.cpp").read_text()
    marker = "    std::vector<EqualFrame> open{{&left, &right, 0}};"
    if original.count(marker) != 1:
        raise RuntimeError("cannot locate the current equality work-list implementation")
    source, obj, binary = (work / name for name in ("compare.cpp", "compare.o", "nift-work-list"))
    source.write_text(original.replace(marker, PER_ELEMENT_WORK + marker))
    # Reuse the actual CLI link line and objects with exactly one object changed.
    dry_run = subprocess.check_output(["make", "-n", "-W", "src/rt/compare.o", "nift"], text=True, timeout=60)
    link = next(shlex.split(line) for line in dry_run.splitlines() if line.endswith(" -o nift"))
    subprocess.run([link[0], "-std=c++17", "-O2", "-pthread", "-Isrc", "-iquote", "src/rt",
                    "-c", source, "-o", obj], check=True, timeout=120)
    link[link.index("src/rt/compare.o")] = str(obj)
    link[-1] = str(binary)
    subprocess.run(link, check=True, timeout=120)
    return binary


def measure(work, binary, label, cache_disabled=False):
    _, source, expected = EQUALITY_MEMORY[0]
    (work / "flat.f").write_text(source)
    stats = work / "vm.json"
    native = work / "native.txt"
    env = env_for(str(work))
    env.update(NIFT_VM="require", NIFT_VM_HEAP_BUDGET=str(EQUALITY_BUDGET), NIFT_VM_STATS=str(stats))
    if cache_disabled:
        env["MallocLargeCache"] = "0"
    proc = subprocess.run(["/usr/bin/time", "-l", "-o", native, binary, "flat.f"], cwd=work, env=env,
                          stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120)
    if (proc.returncode, proc.stdout, proc.stderr) != (0, expected, ""):
        raise RuntimeError(f"{label}: exit {proc.returncode}, stdout {proc.stdout!r}, stderr {proc.stderr!r}")
    peak = json.loads(stats.read_text())["heap_peak"]
    print(f"{label} VM heap peak {peak}")
    print(native.read_text(), end="")


def main():
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        mutant = build_control(work)
        for _ in range(2):
            measure(work, Path("nift").resolve(), "production")
            measure(work, mutant, "historical work list")
        measure(work, Path("nift").resolve(), "production with allocator large cache disabled", cache_disabled=True)


if __name__ == "__main__":
    main()
