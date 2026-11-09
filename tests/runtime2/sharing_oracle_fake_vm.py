#!/usr/bin/env python3
"""A deliberately wrong VM for the sharing-oracle self-test (B07, combined-b3-b5.md section 60).

Usage: sharing_oracle_fake_vm.py REAL_NIFT SCRIPT [args...]

Runs the real binary on the VM (NIFT_VM=1), then applies the mutation named on the script's first
line (`// fake: MODE`) to what rt-diff observes. The marker survives the oracle's rewrite T, so
the mutation hits VM(P) and VM(T(P)) alike, as an unrelated VM bug would:
  none          no change (the positive probes)
  extra-stdout  an unrelated extra stdout line
  error-column  the column of every `:<line>:<col>:` position in stderr plus one
  exit-only     exit status 3 instead of the real one (stdout and stderr unchanged)
  refusal       the run's VM statistics report a refusal (outcome refused, no VM execution)
  abort         the process aborts (SIGABRT) after writing its output
"""
import json
import os
import re
import signal
import subprocess
import sys


def main():
    real, args = sys.argv[1], sys.argv[2:]
    mode = "none"
    try:
        with open(args[0], "rb") as handle:
            first = handle.readline().decode("utf-8", "replace").strip()
        if first.startswith("// fake:"):
            mode = first.split(":", 1)[1].strip()
    except (OSError, IndexError):
        pass
    env = dict(os.environ, NIFT_VM="1")
    stdin_data = sys.stdin.buffer.read() if not sys.stdin.isatty() else b""
    combined = os.fstat(1).st_ino == os.fstat(2).st_ino
    fds = (int(env["NIFT_TRACE_HOST"]),) if "NIFT_TRACE_HOST" in env else ()
    proc = subprocess.run([real] + args, env=env, input=stdin_data, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT if combined else subprocess.PIPE, pass_fds=fds)
    out, err, code = proc.stdout, proc.stderr or b"", proc.returncode
    if mode == "extra-stdout":
        out += b"UNRELATED\n"
    elif mode == "error-column":
        bump = lambda m: b":%s:%d:" % (m.group(1), int(m.group(2)) + 1)  # noqa: E731
        out = re.sub(rb":(\d+):(\d+):", bump, out) if combined else out
        err = re.sub(rb":(\d+):(\d+):", bump, err)
    elif mode == "exit-only":
        code = 3
    elif mode == "refusal" and env.get("NIFT_VM_STATS") and os.path.exists(env["NIFT_VM_STATS"]):
        with open(env["NIFT_VM_STATS"]) as handle:
            stats = [json.loads(line) for line in handle if line.strip()]
        for st in stats:
            st.update(outcome="refused", reason="fake-refusal", vm_executions=0)
        with open(env["NIFT_VM_STATS"], "w") as handle:
            handle.writelines(json.dumps(st) + "\n" for st in stats)
    sys.stdout.buffer.write(out)
    sys.stdout.flush()
    if not combined:
        sys.stderr.buffer.write(err)
        sys.stderr.flush()
    if mode == "abort":
        signal.signal(signal.SIGABRT, signal.SIG_DFL)
        os.kill(os.getpid(), signal.SIGABRT)
    sys.exit(code)


if __name__ == "__main__":
    main()
