#!/usr/bin/env python3
"""A deliberately wrong `nift` for the CP16 fixture comparison (self-check).

Usage: mutant.py MODE REAL_NIFT [nift args...]

Runs the real binary and applies exactly one mutation to what capture.py
observes, so `capture.py --check --mutant MODE` must report DIFF for every
probe the mutation touches. This shows that the Host-op trace / dependency
comparison catches the defect classes the CP16 VM layer could introduce:

  trace-path      one traced path names a different file (wrong path)
  trace-res       one trace result class changes (e.g. file -> missing)
  trace-drop:OP   every trace line of operation OP is lost (missing effect)
  trace-swap      two adjacent trace lines in one context swap (wrong order)
  code            io.open_failed reported as io.read_failed (wrong error code)
  dep-drop        the first page-info dependency is dropped (missing dependency)
  absent-drop     page-info "absent" observations are dropped
"""
import glob
import json
import os
import subprocess
import sys


def main():
    mode, real, args = sys.argv[1], sys.argv[2], sys.argv[3:]
    env = dict(os.environ)
    trace_fd = env.get("NIFT_TRACE_HOST")
    tmp = os.path.join(env.get("TMPDIR", "."), ".cp16-mutant-trace-%d" % os.getpid())
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    env["NIFT_TRACE_HOST"] = str(fd)
    stdin = sys.stdin.buffer.read()
    proc = subprocess.run([real] + args, env=env, input=stdin, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, pass_fds=(fd,))
    os.close(fd)
    with open(tmp, "rb") as handle:
        lines = handle.read().decode("utf-8").splitlines(True)
    os.remove(tmp)
    out = proc.stdout.decode("utf-8", "surrogateescape")

    if mode == "trace-path" and lines:
        rec = json.loads(lines[0]); rec["arg"] = rec["arg"] + ".x"
        lines[0] = json.dumps(rec, separators=(",", ":")) + "\n"
    elif mode == "trace-res" and lines:
        rec = json.loads(lines[0]); rec["res"] = "missing" if rec["res"] != "missing" else "file"
        lines[0] = json.dumps(rec, separators=(",", ":")) + "\n"
    elif mode.startswith("trace-drop:"):
        op = mode.split(":", 1)[1]
        lines = [l for l in lines if json.loads(l).get("op") != op]
    elif mode == "trace-swap" and len(lines) > 1:
        for i in range(len(lines) - 1):
            if lines[i] != lines[i + 1]:
                lines[i], lines[i + 1] = lines[i + 1], lines[i]
                break
    elif mode == "code":
        out = out.replace("io.open_failed", "io.read_failed")
    elif mode in ("dep-drop", "absent-drop"):
        for path in glob.glob(os.path.join(os.getcwd(), ".nift", "**", "*.info.json"), recursive=True):
            os.chmod(path, 0o644)
            with open(path, encoding="utf-8") as handle:
                info = json.load(handle)
            if mode == "dep-drop" and info.get("dependencies"):
                info["dependencies"] = info["dependencies"][1:]
            if mode == "absent-drop":
                info.pop("absent", None)
            with open(path, "w", encoding="utf-8") as handle:
                json.dump(info, handle, indent=2)

    if trace_fd is not None:
        data = "".join(lines).encode("utf-8")
        while data:
            n = os.write(int(trace_fd), data)
            data = data[n:]
    sys.stdout.buffer.write(out.encode("utf-8", "surrogateescape"))
    sys.stderr.buffer.write(proc.stderr)
    sys.exit(proc.returncode)


if __name__ == "__main__":
    main()
