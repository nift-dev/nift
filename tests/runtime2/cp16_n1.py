#!/usr/bin/env python3
"""CP16 N1: per-call Host error codes for the four read natives.

Legacy is the oracle: a try/catch around open/cat/sha256_file/open_bytes prints
the caught code. The VM has no try/catch before CP13B, so the VM half runs the
call uncaught and reads the code from fresh NIFT_VM_STATS.

Portable: missing-path open failures for all four natives, the open_bytes VM
refusal, and the N1 HostCall sink/reset/classification unit. Linux-only:
post-open read failure (/proc/self/mem). Unavailable Linux cases are reported
as SKIP, never PASS.

`--expect-mutant` is the structured negative-control predicate: only exactly
open/cat/sha256_file read-code mismatches, with every other assertion correct
and no harness failure, may pass. It is not a generic "some test failed" mode.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

CASES = [
    ("open", 'open({path})', True),
    ("cat", 'cat({path})', True),
    ("sha256_file", 'sha256_file({path})', True),
    ("open_bytes", 'open_bytes({path})', False),  # VM refuses builtin-bytes on this baseline
]
READ = ("read-failure", "/proc/self/mem", "io.read_failed", "input failure")
OPEN = ("open-failure", "@MISSING@", "io.open_failed", "cannot open path")
LINUX_READ = sys.platform == "linux" and os.path.exists("/proc/self/mem")


class Failures:
    def __init__(self):
        self.harness = []
        self.legacy = []
        self.open_codes = []
        self.read_mismatches = []
        self.other = []

    def any(self):
        return bool(self.harness or self.legacy or self.open_codes or self.other)

    def add(self, bucket, text):
        getattr(self, bucket).append(text)


def run(nift, source, root, vm, stats_id, timeout):
    (root / "case.f").write_text(source)
    env = dict(os.environ, HOME=str(root), TMPDIR=str(root), LANG="C", LC_ALL="C",
               NIFT_PREVALIDATE="off")
    env["NIFT_VM"] = "require" if vm else "0"
    stats = root / f"stats-{stats_id}.jsonl"
    try:
        stats.unlink()
    except FileNotFoundError:
        pass
    if vm:
        env["NIFT_VM_STATS"] = str(stats)
    else:
        env.pop("NIFT_VM_STATS", None)
    try:
        proc = subprocess.run([nift, "case.f"], cwd=root, env=env, stdin=subprocess.DEVNULL,
                              capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, None, "timeout"
    if not vm:
        return proc, None, None
    if not stats.exists():
        return proc, None, "missing NIFT_VM_STATS"
    lines = [line for line in stats.read_text(errors="replace").splitlines() if line.strip()]
    if len(lines) != 1:
        return proc, None, f"expected one fresh statistics record, found {len(lines)}"
    try:
        return proc, json.loads(lines[0]), None
    except ValueError:
        return proc, None, "malformed NIFT_VM_STATS"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--expect-mutant", action="store_true")
    ap.add_argument("--timeout", type=int, default=60)
    args = ap.parse_args()
    nift = str(Path(args.nift).resolve())
    f = Failures()
    checks = 0
    skipped = 0
    stats_id = 0
    with tempfile.TemporaryDirectory(prefix="cp16-n1-") as folder:
        root = Path(folder)
        missing = root / "missing-n1-file"
        for name, call, vm_supported in CASES:
            kinds = [OPEN] + ([READ] if LINUX_READ else [])
            for kind, path, want_code, want_text in kinds:
                real_path = str(missing) if path == "@MISSING@" else path
                expr = call.format(path=json.dumps(real_path))
                # Legacy oracle: caught, so the code is observable.
                legacy_src = ("try {\n  x := %s\n  print(\"ok\")\n} catch(e) {\n"
                              "  print(e.code)\n  print(e.message)\n}\n" % expr)
                stats_id += 1
                legacy, _, harness = run(nift, legacy_src, root, False, stats_id, args.timeout)
                checks += 1
                if harness:
                    f.add("harness", f"{name}/{kind}: legacy {harness}")
                    continue
                if legacy is None or legacy.returncode != 0:
                    f.add("legacy", f"{name}/{kind}: legacy rc={getattr(legacy, 'returncode', '?')}")
                    continue
                lines = legacy.stdout.splitlines()
                if len(lines) < 2 or lines[0] != want_code or want_text not in " ".join(lines[1:]):
                    f.add("legacy", f"{name}/{kind}: legacy {legacy.stdout!r}, want {want_code!r}")
                    continue
                message = "\n".join(lines[1:])
                # VM: uncaught (no try/catch before CP13B), code from fresh stats.
                vm_src = "x := %s\nprint(\"ok\")\n" % expr
                stats_id += 1
                proc, st, harness = run(nift, vm_src, root, True, stats_id, args.timeout)
                checks += 1
                if harness:
                    f.add("harness", f"{name}/{kind}: VM {harness}")
                    continue
                if proc is None:
                    f.add("harness", f"{name}/{kind}: VM no process")
                    continue
                if proc.returncode < 0 or proc.returncode > 128:
                    f.add("harness", f"{name}/{kind}: VM signal/unexpected exit {proc.returncode}")
                    continue
                if not vm_supported:
                    if (proc.returncode != 1 or st is None or st.get("outcome") != "require-error"
                            or st.get("reason") != "builtin-bytes:open_bytes"
                            or st.get("vm_executions") != 0 or st.get("legacy_executions") != 0
                            or proc.stdout != "" or "not compiled for the VM" not in proc.stderr):
                        f.add("other", f"{name}/{kind}: VM refusal not exact: {st}")
                    else:
                        print(f"ok   {name}/{kind}: legacy {want_code}; VM refused builtin-bytes "
                              "(native-level code covered by host-native-unit)")
                    continue
                if proc.returncode != 1 or proc.stdout != "" or "error:" not in proc.stderr:
                    f.add("harness", f"{name}/{kind}: VM exit/stdout/stderr {proc.returncode} "
                                     f"{proc.stdout!r} {proc.stderr!r}")
                    continue
                if (st is None or st.get("outcome") != "vm-error" or st.get("catchable") is not True
                        or st.get("vm_executions") != 1 or st.get("legacy_executions") != 0):
                    f.add("other", f"{name}/{kind}: VM statistics {st}")
                    continue
                got_code = st.get("code", "")
                if got_code != want_code:
                    if kind == "read-failure":
                        f.read_mismatches.append(f"{name}: {got_code!r} != {want_code!r}")
                    else:
                        f.open_codes.append(f"{name}: {got_code!r} != {want_code!r}")
                    continue
                if st.get("reason") != message or want_text not in proc.stderr:
                    f.add("other", f"{name}/{kind}: VM message/exit differ: {st.get('reason')!r} "
                                   f"{proc.stderr!r}")
                    continue
                print(f"ok   {name}/{kind}: {want_code}")
        if not LINUX_READ:
            skipped = len(CASES)
            print(f"SKIP {skipped} post-open read-failure cases: /proc/self/mem is unavailable "
                  "on this platform (Linux-only coverage)")
    if args.expect_mutant:
        exact = {"open", "cat", "sha256_file"}
        got = {m.split(":", 1)[0] for m in f.read_mismatches}
        result = {
            "read_code_mismatches": sorted(got),
            "open_failure_codes": f.open_codes,
            "harness_failures": f.harness,
            "legacy_failures": f.legacy,
            "other_failures": f.other,
            "checks": checks,
        }
        print("MUTANT-RESULT " + json.dumps(result, sort_keys=True))
        if not LINUX_READ:
            print("SKIP mutant predicate: Linux read-failure cases are unavailable")
            return 2
        if f.any():
            print(f"FAIL N1 mutant predicate: {len(f.harness)} harness, {len(f.legacy)} legacy, "
                  f"{len(f.open_codes)} open-code, {len(f.other)} other failure(s)")
            return 1
        if got != exact or len(f.read_mismatches) != 3:
            print(f"FAIL N1 mutant predicate: read-code mismatches {sorted(got)}, want {sorted(exact)}")
            return 1
        print("PASS N1 mutant predicate: exactly open/cat/sha256_file read-code mismatches")
        return 0
    for bucket, items in (("harness", f.harness), ("legacy", f.legacy),
                          ("open-code", f.open_codes), ("read-code", f.read_mismatches),
                          ("other", f.other)):
        for item in items:
            print(f"FAIL {bucket}: {item}")
    failures = len(f.harness) + len(f.legacy) + len(f.open_codes) + len(f.read_mismatches) + len(f.other)
    print(f"{'PASS' if failures == 0 else 'FAIL'} N1 per-call codes: {checks} checks, "
          f"{failures} failure(s), {skipped} skipped")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
