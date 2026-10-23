#!/usr/bin/env python3
"""CP16 N1: per-call Host error codes for the four read natives.

Legacy is the oracle: a try/catch around open/cat/sha256_file/open_bytes prints
the caught code. The VM has no try/catch before CP13B, so the VM half runs the
call uncaught and reads the code from NIFT_VM_STATS. Deterministic post-open
read failure uses /proc/self/mem (Linux); open failure uses a missing path.

`--expect-mutant` is for the negative control (per-builtin mapping restored):
the run must fail on a read-failure code assertion.
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
FAILURES = [
    ("read-failure", "/proc/self/mem", "io.read_failed", "input failure"),
    ("open-failure", "@MISSING@", "io.open_failed", "cannot open path"),
]


def run(nift, source, root, vm):
    (root / "case.f").write_text(source)
    env = dict(os.environ, HOME=str(root), TMPDIR=str(root), LANG="C", LC_ALL="C",
               NIFT_PREVALIDATE="off")
    env["NIFT_VM"] = "require" if vm else "0"
    stats = root / "stats.jsonl"
    if vm:
        env["NIFT_VM_STATS"] = str(stats)
    else:
        env.pop("NIFT_VM_STATS", None)
    proc = subprocess.run([nift, "case.f"], cwd=root, env=env, stdin=subprocess.DEVNULL,
                          capture_output=True, text=True, timeout=60)
    st = None
    if vm and stats.exists() and stats.read_text().strip():
        st = json.loads(stats.read_text().splitlines()[-1])
    return proc, st


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--expect-mutant", action="store_true")
    args = ap.parse_args()
    nift = str(Path(args.nift).resolve())
    failures = []
    checks = 0
    with tempfile.TemporaryDirectory(prefix="cp16-n1-") as folder:
        root = Path(folder)
        missing = root / "missing-n1-file"
        for name, call, vm_supported in CASES:
            for kind, path, want_code, want_text in FAILURES:
                real_path = str(missing) if path == "@MISSING@" else path
                expr = call.format(path=json.dumps(real_path))
                # Legacy oracle: caught, so the code is observable.
                legacy_src = ("try {\n  x := %s\n  print(\"ok\")\n} catch(e) {\n"
                              "  print(e.code)\n  print(e.message)\n}\n" % expr)
                legacy, _ = run(nift, legacy_src, root, False)
                checks += 1
                if legacy.returncode != 0:
                    failures.append(f"{name}/{kind}: legacy harness failed rc={legacy.returncode}: {legacy.stderr.strip()}")
                    continue
                code, message = legacy.stdout.splitlines()
                if code != want_code or want_text not in message:
                    failures.append(f"{name}/{kind}: legacy code {code!r} message {message!r}, "
                                    f"want {want_code!r}")
                    continue
                # VM: uncaught (no try/catch before CP13B), code from statistics.
                vm_src = "x := %s\nprint(\"ok\")\n" % expr
                proc, st = run(nift, vm_src, root, True)
                checks += 1
                if not vm_supported:
                    if st is None or st.get("outcome") != "require-error" or \
                            st.get("reason") != "builtin-bytes:open_bytes":
                        failures.append(f"{name}/{kind}: expected the CP12 bytes refusal, got {st}")
                    else:
                        print(f"ok   {name}/{kind}: legacy {want_code}; VM refused builtin-bytes "
                              "(native-level code covered by host-native-unit)")
                    continue
                if st is None:
                    failures.append(f"{name}/{kind}: no NIFT_VM_STATS")
                    continue
                got_code = st.get("code", "")
                if want_code == "io.read_failed" and got_code != want_code:
                    print(f"N1 read-failure code mismatch: {name} got {got_code!r}, want {want_code!r}")
                if st.get("outcome") != "vm-error" or got_code != want_code or \
                        st.get("catchable") is not True or st.get("vm_executions") != 1 or \
                        st.get("legacy_executions") != 0:
                    failures.append(f"{name}/{kind}: VM outcome={st.get('outcome')} code={got_code!r} "
                                    f"catchable={st.get('catchable')} "
                                    f"vm_executions={st.get('vm_executions')} "
                                    f"legacy_executions={st.get('legacy_executions')}")
                    continue
                if st.get("reason") != message or proc.stdout != "" or proc.returncode != 1:
                    failures.append(f"{name}/{kind}: VM reason/stdout/exit differ from legacy: "
                                    f"{st.get('reason')!r} {proc.stdout!r} {proc.returncode}")
                    continue
                if want_text not in proc.stderr:
                    failures.append(f"{name}/{kind}: VM stderr does not carry legacy's message: {proc.stderr!r}")
                    continue
                print(f"ok   {name}/{kind}: {want_code}")
    for failure in failures:
        print("FAIL", failure)
    if args.expect_mutant:
        if failures:
            print(f"PASS N1 negative control: per-builtin mapping rejected "
                  f"({checks} checks, {len(failures)} failure(s))")
            return 0
        print("FAIL N1 negative control: the per-builtin mapping passed", file=sys.stderr)
        return 1
    print(f"{'PASS' if not failures else 'FAIL'} N1 per-call codes: {checks} checks, "
          f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
