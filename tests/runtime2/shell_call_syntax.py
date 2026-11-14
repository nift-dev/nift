#!/usr/bin/env python3
"""C144 (D-101): in the interactive and piped shell, function-call syntax is Nift, never a program launch.

  shell_call_syntax.py --nift NIFT              every form, three front ends, both engines
  shell_call_syntax.py --nift NIFT --mutants    the production binary passes every case (no-mutation control), then
                                                NIFT_C144_MUTANT=1..3, each built with sanctioned_mutant.py, must be
                                                caught (and removed afterwards)

Front ends: `nift -i` with piped stdin, plain `nift` with piped stdin, and the interactive session (plain `nift` on a
pty). Engines: the VM default and NIFT_VM=0. Each case runs in a fresh scratch directory whose PATH starts with a
fake bin (cat cp mv rm ls mkdir touch native foo echo read sleep test len print), each leaving LAUNCHED-<name>
if ever run. Call forms must launch nothing; the real command forms (`cp a.txt c.txt`, `command cp ...`, `ls -l`)
must launch the program. Nothing outside the scratch directories is written, and /dev/stdout is never opened.
Exit status: 0 = every case passes (and every mutant is caught); 1 = a failure.
"""
import argparse
import os
import pty
import resource
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
FAKE = ["cat", "cp", "mv", "rm", "ls", "mkdir", "touch", "native", "foo", "echo", "read", "sleep", "test", "len",
        "print"]
FOO = 'fn(foo(x)) { print("U " + x) }'
# Function-call forms: nothing may launch. (lines, check of (engine, output, directory) or None)
CALLS = {
    "cat": ['cat ("a.txt")'],
    "cp": ['cp ("a.txt", "c.txt")'],
    "cp-tab": ['cp\t("a.txt", "c.txt")'],
    "cp-spaces": ['cp   ("a.txt", "c.txt")'],
    "mv": ['mv ("a.txt", "m.txt")'],
    "rm": ['rm ("a.txt")'],
    "ls": ['ls (".")'],
    "mkdir": ['mkdir ("dd")'],
    "touch": ['touch ("t")'],
    "native-spaced-colons": ['native :: cp("a.txt", "c.txt")'],
    "native-space-before-colons": ['native ::cp("a.txt", "c.txt")'],
    "native-spaced-paren": ['native::cp ("a.txt", "c.txt")'],
    "print": ['print ("x")'],
    "read": ['read ()'],
    "sleep": ['sleep (0)'],
    "test": ['test ("x")'],
    "echo": ['echo ("hi")'],
    "len": ['len ("abc")'],
    "user-function": [FOO, 'foo ("x")'],
    "qualified": ['native::cp("a.txt", "q.txt")'],
    "unqualified": ['cp("a.txt", "r.txt")'],
}
# Command forms: the program on PATH must run.
COMMANDS = {
    "command-line": (['cp a.txt c.txt'], "cp"),
    "explicit-command": (['command cp a.txt c.txt'], "cp"),
    "ls-flags": (['ls -l'], "ls"),
}


def limits():
    resource.setrlimit(resource.RLIMIT_AS, (2 << 30, 2 << 30))
    resource.setrlimit(resource.RLIMIT_FSIZE, (64 << 20, 64 << 20))


def fixture():
    d = tempfile.mkdtemp(prefix="c144-")
    os.makedirs(os.path.join(d, "fakebin"))
    for n in FAKE:
        p = os.path.join(d, "fakebin", n)
        with open(p, "w") as f:
            f.write('#!/bin/sh\n: > "%s/LAUNCHED-%s"\n' % (d, n))
        os.chmod(p, 0o755)
    with open(os.path.join(d, "a.txt"), "w") as f:
        f.write("A")
    return d


def env_for(d, vm):
    env = {"PATH": os.path.join(d, "fakebin") + ":/usr/bin:/bin", "HOME": d, "LANG": "C", "TERM": "dumb",
           "NO_COLOR": "1"}
    if not vm:
        env["NIFT_VM"] = "0"
    return env


def piped(nift, d, vm, args, lines):
    try:
        p = subprocess.run([nift] + args, cwd=d, env=env_for(d, vm), input="\n".join(lines) + "\n",
                           capture_output=True, text=True, timeout=60, preexec_fn=limits)
    except subprocess.TimeoutExpired:
        return "timeout"
    return p.stdout + p.stderr


def tty(nift, d, vm, lines):
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(d)
        limits()
        os.execve(nift, [nift], env_for(d, vm))
    out = b""

    def drain(seconds):
        nonlocal out
        end = time.time() + seconds
        while time.time() < end:
            ready, _, _ = select.select([fd], [], [], 0.05)
            if ready:
                try:
                    data = os.read(fd, 4096)
                except OSError:
                    return
                if not data:
                    return
                out += data
    for line in lines + ["exit"]:
        drain(0.4)
        os.write(fd, (line + "\r").encode())
    drain(1.0)
    deadline = time.time() + 20
    while True:
        done, _ = os.waitpid(pid, os.WNOHANG)
        if done:
            break
        if time.time() > deadline:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
            break
        drain(0.2)
    os.close(fd)
    return out.decode(errors="replace").replace("\r\n", "\n")


FRONTS = {"-i": lambda n, d, vm, lines: piped(n, d, vm, ["-i"], lines),
          "pipe": lambda n, d, vm, lines: piped(n, d, vm, [], lines),
          "tty": tty}


def launched(d):
    return sorted(f[len("LAUNCHED-"):] for f in os.listdir(d) if f.startswith("LAUNCHED-"))


def exists(d, n):
    return os.path.exists(os.path.join(d, n))


def check(nift, quiet=False):
    failing = []
    for front, runner in FRONTS.items():
        for vm in (True, False):
            engine = "vm" if vm else "legacy"
            for name, lines in CALLS.items():
                d = fixture()
                try:
                    out = runner(nift, d, vm, lines)
                    problems = ["launched %s" % launched(d)] if launched(d) else []
                    if name == "qualified" and not exists(d, "q.txt"):
                        problems.append("native::cp did not copy")
                    if name == "unqualified":
                        if vm and ("native-unqualified" not in out or exists(d, "r.txt")):
                            problems.append("no native-unqualified refusal")
                        if not vm and not exists(d, "r.txt"):
                            problems.append("legacy cp did not copy")
                    if name == "user-function" and not vm and "U x" not in out:
                        problems.append("legacy did not call the user function")
                    label = "%s %s call %s" % (front, engine, name)
                    if problems:
                        failing.append(label)
                    if problems or not quiet:
                        print("%s %s%s" % ("FAIL" if problems else "PASS", label,
                                           (": %s; out %r" % ("; ".join(problems), out[-200:])) if problems else ""))
                finally:
                    shutil.rmtree(d, ignore_errors=True)
            for name, (lines, program) in COMMANDS.items():
                d = fixture()
                try:
                    out = runner(nift, d, vm, lines)
                    ok = launched(d) == [program]
                    label = "%s %s command %s" % (front, engine, name)
                    if not ok:
                        failing.append(label)
                    if not ok or not quiet:
                        print("%s %s%s" % ("PASS" if ok else "FAIL", label,
                                           "" if ok else ": launched %s; out %r" % (launched(d), out[-200:])))
                finally:
                    shutil.rmtree(d, ignore_errors=True)
    total = len(FRONTS) * 2 * (len(CALLS) + len(COMMANDS))
    print("shell-call-syntax: %d/%d pass" % (total - len(failing), total))
    return failing


# mutant -> labels that must fail (a sample of each defect's consequences)
MUTANTS = {
    1: ["%s %s call cp" % (f, e) for f in FRONTS for e in ("vm", "legacy")],  # the first-word-only rule
    2: ["%s %s call native-spaced-colons" % (f, e) for f in FRONTS for e in ("vm", "legacy")],  # native rule dropped
    # captures `cp a.txt c.txt`: the VM session refuses it (nothing launches). On legacy the line still runs PATH cp,
    # because legacy's own statement splitter treats a command-style line without `(` as a command (CP10).
    3: ["%s vm command %s" % (f, c) for f in FRONTS for c in ("command-line", "ls-flags")],
}


def mutants(nift):
    control = check(nift, quiet=True)
    if control:
        print("FAIL shell-call-syntax mutants: no-mutation control: the production binary fails %s" % control)
        return 1
    print("PASS shell-call-syntax no-mutation control: the production binary passes every case")
    bad = 0
    for n, expected in sorted(MUTANTS.items()):
        out = os.path.join(ROOT, ".build", "mutant-nift", "shell-call-syntax-%d" % n)
        try:
            b = subprocess.run([sys.executable, "-B", os.path.join(HERE, "sanctioned_mutant.py"), "--out", out,
                                "--define=-DNIFT_C144_MUTANT=%d" % n], capture_output=True, text=True)
            if b.returncode != 0:
                print("FAIL shell-call-syntax mutant %d: harness: build failed: %s" % (n, (b.stderr or b.stdout)[-400:]))
                bad += 1
                continue
            failing = check(os.path.join(out, "nift"), quiet=True)
            missed = [c for c in expected if c not in failing]
            if missed:
                print("FAIL shell-call-syntax mutant %d: not caught by %s" % (n, missed))
                bad += 1
            else:
                print("PASS shell-call-syntax mutant %d: caught by %s" % (n, ", ".join(expected)))
        finally:
            shutil.rmtree(out, ignore_errors=True)
    print("%s shell-call-syntax mutants: %d of %d caught" % ("FAIL" if bad else "PASS", len(MUTANTS) - bad,
                                                              len(MUTANTS)))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--mutants", action="store_true")
    args = ap.parse_args()
    if os.name != "posix":
        print("shell-call-syntax: skipped (POSIX pty and fake PATH)")
        return 77
    nift = os.path.abspath(args.nift)
    if args.mutants:
        return mutants(nift)
    return 1 if check(nift) else 0


if __name__ == "__main__":
    sys.exit(main())
