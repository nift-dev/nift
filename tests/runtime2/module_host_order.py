#!/usr/bin/env python3
"""R1 (owner, 2026-10-21): imports and host operations keep legacy's order.

A linked unit (CP12b modules) reads every module before it runs, so a host
operation that legacy runs before or between imports would see different
files and trace in a different order. Until imports run at their position,
such units are refused before they run (`module-host-op:<built-in>`).

Each case runs in two fresh copies of its files: legacy (NIFT_VM=0) and the
VM in auto mode (NIFT_VM=1, which falls back to legacy when refused). stdout,
stderr, the exit status, the Host trace and the resulting tree must be
identical, and the VM's stats must show the expected outcome. A third run
with NIFT_VM=require must refuse refused cases before any effect (no trace
line, unchanged tree).

Negative control: a build with -DNIFT_R1_MUTANT (the refusal scan disabled)
must fail this test; see the Makefile target test-runtime2-module-host-order.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

M = 'x := 1\nexport(x)\n'
CASES = {
    # name: (files, expected outcome under NIFT_VM=1)
    "host-before-import-top": ({"m.nft": M, "main.f": 'print(exists("a.txt"))\nimport("m.nft")\nprint(x)\n'},
                               "module-host-op:exists"),
    "host-before-import-fn": ({"m.nft": M, "main.f": 'fn(probe()) {\n  return exists("a.txt")\n}\n'
                               'print(probe())\nimport("m.nft")\nprint(x)\n'}, "module-host-op:exists"),
    "host-before-import-loop": ({"m.nft": M, "main.f": 'i0 := 0\nwhile (i0 < 2) {\n  i0 += 1\n'
                                 '  print(exists("a.txt"))\n}\nimport("m.nft")\nprint(x)\n'},
                                "module-host-op:exists"),
    "copy-over-module": ({"m.nft": M, "m2.nft": 'x := 2\nexport(x)\n',
                          "main.f": 'cp("m2.nft", "m.nft")\nimport("m.nft")\nprint(x)\n'}, "module-host-op:cp"),
    "remove-module": ({"m.nft": M, "main.f": 'print("go")\nremove("m.nft")\nimport("m.nft")\nprint(x)\n'},
                      "module-host-op:remove"),
    "host-in-module-init": ({"m.nft": 'print(exists("a.txt"))\nk := 1\nexport(k)\n',
                             "n.nft": 'j := 2\nexport(j)\n',
                             "main.f": 'import("m.nft")\nimport("n.nft")\nprint(k + j)\n'},
                            "module-host-op:exists"),
    "host-in-nested-module-fn": ({"sub/a.nft": 'import("../c.nft")\nfn(af()) {\n  return cf()\n}\nexport(af)\n',
                                  "c.nft": 'fn(cf()) {\n  return is_file("a.txt")\n}\nexport(cf)\n',
                                  "main.f": 'import("sub/a.nft")\nprint(af())\n'},
                                 "module-host-op:is_file"),
    "host-after-import": ({"m.nft": M, "main.f": 'import("m.nft")\nprint(exists("a.txt"))\nprint(x)\n'},
                          "module-host-op:exists"),
    # Controls: the same host op without an import, and an import without a
    # host op, still run on the VM.
    "host-no-import": ({"main.f": 'print(exists("a.txt"))\n'}, "vm"),
    "import-no-host": ({"m.nft": M, "main.f": 'print("a")\nimport("m.nft")\nprint(x)\n'}, "vm"),
}


def materialise(files):
    d = tempfile.mkdtemp(prefix="r1-")
    for rel, text in files.items():
        path = os.path.join(d, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(text)
    return d


def tree(d):
    out = {}
    for base, _, names in os.walk(d):
        for n in names:
            if n in ("trace.jsonl", "stats.jsonl"):
                continue
            p = os.path.join(base, n)
            out[os.path.relpath(p, d)] = open(p, "rb").read()
    return out


def run(nift, files, mode):
    d = materialise(files)
    try:
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "LANG": "C", "NIFT_VM": mode,
               "NIFT_TRACE_HOST": "3", "NIFT_VM_STATS": os.path.join(d, "stats.jsonl")}
        fd = os.open(os.path.join(d, "trace.jsonl"), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        try:
            p = subprocess.run(["timeout", "30", nift, "main.f"], cwd=d, env=env, capture_output=True,
                               preexec_fn=lambda: os.dup2(fd, 3), pass_fds=(fd,))
        finally:
            os.close(fd)
        trace = open(os.path.join(d, "trace.jsonl")).read().replace(d, "<root>")
        stats_path = os.path.join(d, "stats.jsonl")
        stats = json.loads(open(stats_path).read().splitlines()[-1]) if os.path.exists(stats_path) else {}
        return {"rc": p.returncode, "out": p.stdout, "err": p.stderr.replace(d.encode(), b"<root>"),
                "trace": trace, "tree": tree(d), "stats": stats}
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    args.nift = os.path.abspath(args.nift)
    failures = []
    for name, (files, want) in CASES.items():
        legacy = run(args.nift, files, "0")
        auto = run(args.nift, files, "1")
        st = auto["stats"]
        got = "vm" if st.get("outcome") in ("vm", "vm-error") else st.get("reason", st.get("outcome"))
        problems = []
        if got != want:
            problems.append(f"outcome {got!r}, expected {want!r}")
        for key in ("rc", "out", "err", "trace", "tree"):
            if legacy[key] != auto[key]:
                problems.append(f"{key} differs: legacy {legacy[key]!r} vm {auto[key]!r}")
        if want != "vm":
            req = run(args.nift, files, "require")
            if req["rc"] == 0 or req["trace"] or req["tree"] != {k: v.encode() for k, v in files.items()}:
                problems.append(f"NIFT_VM=require not refused before effects: rc {req['rc']} trace {req['trace']!r}")
        print(("PASS " if not problems else "FAIL ") + name + (f" ({got})" if not problems else ""))
        for p in problems:
            print("   ", p)
        if problems:
            failures.append(name)
    print(f"module-host-order: {len(CASES) - len(failures)}/{len(CASES)} pass")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
