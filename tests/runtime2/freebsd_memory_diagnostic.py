#!/usr/bin/env python3
"""FreeBSD diagnostic measurements. The certification ceilings are unchanged."""
import json
from pathlib import Path
import subprocess
import tempfile

from darwin_equality_rss_probe import build_control
from vm_subset import EQUALITY_BUDGET, EQUALITY_MEMORY, env_for, equality_process

ROOT = Path.cwd()
NIFT = ROOT / "nift"


def run_equality(work):
    mutant = build_control(work)
    _, flat, flat_output = EQUALITY_MEMORY[0]
    valid = True
    sources = (
        ("startup", "print(0)\n"),
        ("startup-cpu-tail", "print(0)\ni := 0\nwhile(i < 2000000) { i += 1 }\n"),
        ("flat", flat),
    )
    for run in range(3):
        for label, source in sources:
            for variant, binary, malloc in (
                ("production", NIFT, None),
                ("prompt-decay", NIFT, "dirty_decay_ms:0,muzzy_decay_ms:0"),
                ("historical-work-list", mutant, None),
            ):
                if label != "flat" and variant != "production":
                    continue
                script = work / "case.f"
                script.write_text(source)
                stats = work / "stats.json"
                stats.unlink(missing_ok=True)
                env = dict(env_for(str(work)), NIFT_VM="require",
                           NIFT_VM_HEAP_BUDGET=str(EQUALITY_BUDGET), NIFT_VM_STATS=str(stats))
                if malloc:
                    env["MALLOC_CONF"] = malloc
                rc, stdout, stderr, rss = equality_process(str(binary), str(work), env, script.name)
                stats_text = stats.read_text() if stats.exists() else ""
                print(label, variant, run, rc, rss, stdout, stderr, stats_text, flush=True)
                expected = flat_output.encode() if label == "flat" else b"0\n"
                try:
                    heap = json.loads(stats_text).get("heap_peak")
                except (ValueError, AttributeError):
                    heap = None
                valid &= (rc == 0 and stdout == expected and isinstance(heap, int) and
                          (rss is not None or label == "startup"))
    return valid


def run_streams(work):
    tracker = work / "peak.so"
    subprocess.run(["cc", "-O2", "-shared", "-fPIC", "-o", str(tracker),
                    str(ROOT / "tests/runtime2/cp16_peak.c")], check=True)
    (work / "case.f").write_text('s := open("/dev/stdin"); print(s.length())\n')
    env = dict(env_for(str(work)), NIFT_VM="require", NIFT_VM_HEAP_BUDGET="60000000",
               NIFT_HOST_TRIM_PROBE="1", LD_PRELOAD=str(tracker))
    valid = True
    for mib in (20, 23, 24, 25, 26, 27, 30, 31):
        result = subprocess.run([str(NIFT), "case.f"], cwd=work, env=env,
                                input=b"x" * (mib << 20), capture_output=True, timeout=60)
        print("stream", mib, result.returncode, result.stdout, result.stderr, flush=True)
        if mib < 31:
            valid &= result.returncode == 0 and result.stdout == f"{mib << 20}\n".encode()
        else:
            valid &= result.returncode == 1 and not result.stdout and b"memory budget" in result.stderr
        valid &= b"PEAK_LARGE_BYTES " in result.stderr and b"PEAK_LAST_REQUESTED " in result.stderr
    return valid


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="nift-freebsd-memory-") as directory:
        equality_valid = run_equality(Path(directory))
        streams_valid = run_streams(Path(directory))
    if not (equality_valid and streams_valid):
        raise SystemExit("incomplete FreeBSD memory diagnostic evidence")
