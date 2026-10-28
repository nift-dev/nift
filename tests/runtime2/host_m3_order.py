#!/usr/bin/env python3
"""Deterministic M3 witness: require raw directory order != sorted order, then
prove the unmodified oracle sorts and the M3 mutant does not."""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def make_fixture(root: Path, attempt: int):
    sub = root / f"sub{attempt}"
    sub.mkdir(parents=True)
    # Deliberately scrambled creation order; the precondition below, not
    # creation order, is what authorises the witness.
    names = [f"entry-{(i * 37 + attempt * 11) % 97:02d}" for i in range(64)]
    names = list(dict.fromkeys(names))
    for name in names:
        (sub / (name + ".txt")).write_text(name + "\n")
    raw = [entry.name for entry in os.scandir(sub) if entry.name.endswith(".txt")]
    return sub, raw


def run(binary: str, root: Path, sub: Path, vm: bool):
    source = root / "case.f"
    source.write_text('l := ls("' + sub.name + '"); print(l.join(","))\n')
    env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_VM")}
    env["NIFT_VM"] = "require" if vm else "0"
    env["NIFT_PREVALIDATE"] = "off"
    proc = subprocess.run([binary, "case.f"], cwd=root, env=env, capture_output=True, text=True, timeout=60)
    return proc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True, help="M3 mutant binary")
    ap.add_argument("--oracle", required=True, help="unmodified legacy binary")
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    oracle = os.path.abspath(args.oracle)
    roots = [Path(os.environ.get("TMPDIR", "/tmp")), Path.cwd() / ".build"]
    failures = 0
    established = False
    for base in roots:
        try:
            base.mkdir(parents=True, exist_ok=True)
            tmp_context = tempfile.TemporaryDirectory(prefix="cp17-m3-order-", dir=base)
        except OSError as error:
            print(f"SKIP root {base}: {error}")
            continue
        with tmp_context as tmp:
            root = Path(tmp)
            for attempt in range(12):
                sub, raw = make_fixture(root, attempt)
                if os.environ.get("NIFT_M3_TEST_SORTED") is not None:
                    raw = sorted(raw)
                if raw == sorted(raw):
                    shutil.rmtree(sub)
                    continue
                established = True
                expected = ",".join(sorted(raw))
                vm = run(nift, root, sub, True)
                legacy = run(oracle, root, sub, False)
                print(f"root={base} attempt={attempt} raw_sorted={raw == sorted(raw)}")
                print(f"legacy={legacy.returncode} {legacy.stdout!r}")
                print(f"mutant={vm.returncode} {vm.stdout!r}")
                if legacy.returncode != 0 or legacy.stdout.strip() != expected:
                    print("FAIL M3: oracle did not produce the sorted order")
                    failures += 1
                elif vm.returncode != 0 or vm.stdout.strip() == expected:
                    print("FAIL M3: mutant did not preserve the unsorted readdir order")
                    failures += 1
                elif vm.stdout.strip() != ",".join(raw):
                    print("FAIL M3: mutant order is not the observed readdir order")
                    failures += 1
                else:
                    print("PASS M3: sorted oracle and unsorted mutant differ on the verified order")
                break
        if established:
            break
    if not established:
        print("FAIL M3: NOT ESTABLISHED (no available filesystem exposed an unsorted readdir order)")
        return 2
    print("PASS M3 ordering witness" if failures == 0 else f"FAIL M3 ordering witness: {failures} failure(s)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
