#!/usr/bin/env python3
"""Output order and standard-stream handles (output-order repair; D-072 #1, D-073 #2 and #3, D-077 #5 item 1).

Every case runs on legacy (NIFT_VM=0) and on the VM (NIFT_VM=require, so a refusal is never hidden by a fallback),
in scratch directories only, with the child's stdout redirected by this harness:

  pipe     stdout and stderr are pipes
  file     stdout is the scratch file o.txt (`> o.txt`)
  append   o.txt already holds `PRE\\n` (`>> o.txt`)
  merged   stderr joins the stdout pipe (`2>&1`)

and compares exact bytes: stdout (or o.txt), stderr (scratch path removed), the exit status, and every other scratch
file. The handle cases open `/dev/fd/1`, `/proc/self/fd/1` (Linux), a scratch symlink to `/dev/fd/1`, or the redirect
target by name, and `/dev/fd/2` under 2>&1, only inside those children, whose descriptors are the scratch file or
pipe. No case calls save() on a standard stream, and nothing here writes a device path itself.

The worker case (`worker-std-stream`) is the approved VM-specific refusal (D-073 #2): its VM bytes are an explicit
oracle; legacy's are recorded only.

  output_order.py --nift NIFT [--only SUBSTR]
  output_order.py --mutants        NIFT_OUTPUT_ORDER_MUTANT=1 (cat bypasses the VM output), =2 (no std-stream
                                   identity) and =3 (no worker refusal), built by sanctioned_mutant.py, must each fail
                                   the cases named in MUTANTS; the mutant directories are deleted afterwards.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MODES = ("pipe", "file", "append", "merged")
BIG = "x" * 1023
DATA = {"a.txt": "hello", "b.txt": "B\n", "bin.dat": b"a\x00b\xff"}
MODULE = 'fn(show(p)) {\n  cat(p)\n  return 0\n}\nexport(show)\n'
ALIASES = {"fd": "/dev/fd/1", "link": "out-link"}
if sys.platform.startswith("linux"):
    ALIASES["proc"] = "/proc/self/fd/1"


def handle_cases():
    cases = {}
    for key, target in list(ALIASES.items()) + [("name", "o.txt")]:
        modes = ("file", "append") if key == "name" else MODES
        def add(name, body):
            cases["%s-%s" % (name, key)] = ('print(1)\ns := ofstream("%s")\n%s' % (target, body), modes)
        add("open-only", 'print(2)\n')
        add("write-close", 's.write("S\\n")\ns.close()\nprint(2)\n')
        add("write-no-close", 's.write("S\\n")\nprint(2)\n')
        add("write-flush", 's.write("S\\n")\ns.flush()\nprint(2)\ns.write("T\\n")\ns.close()\nprint(3)\n')
        add("write-16k", 'big := "%s"\ni := 0\nwhile (i < 16) {\n  s.write(big + "\\n")\n  i += 1\n}\n'
                         's.close()\nprint(2)\n' % BIG)
        add("error-after-write", 's.write("S\\n")\nprint(1 / 0)\n')
    return cases


CASES = {
    # cat in program order (D-072 #1)
    "print-cat-print": ('print(1)\ncat("a.txt")\nprint(2)\n', MODES),
    "two-cats": ('cat("a.txt")\ncat("b.txt")\nprint(3)\n', MODES),
    "three-cats": ('print(0)\ncat("a.txt")\ncat("a.txt")\ncat("b.txt")\nprint(4)\n', MODES),
    "cat-fn-loop": ('fn(f(i)) {\n  print(i)\n  cat("b.txt")\n  return i\n}\ni := 0\nwhile (i < 3) {\n  f(i)\n'
                    '  i += 1\n}\nprint("end")\n', MODES),
    "cat-module": ('import("m.nft")\nprint(1)\nshow("a.txt")\nprint(2)\n', MODES),
    "cat-after-64k": ('big := "%s"\ni := 0\nwhile (i < 80) {\n  print(big)\n  i += 1\n}\ncat("a.txt")\nprint(2)\n'
                      % BIG, MODES),
    "cat-binary": ('print(1)\ncat("bin.dat")\nprint(2)\n', MODES),
    "cat-missing-caught": ('print(1)\ntry {\n  cat("nope.txt")\n} catch (e) {\n  print("caught")\n}\nprint(2)\n',
                           MODES),
    "cat-then-error": ('print(1)\ncat("a.txt")\nprint(1 / 0)\n', MODES),
    "print-then-error": ('print(1)\nprint(2)\nprint(1 / 0)\n', MODES),
    # stderr handle under 2>&1
    "stderr-write-close": ('print(1)\ne := ofstream("/dev/fd/2")\ne.write("E\\n")\ne.close()\nprint(2)\n', MODES),
    # reads of the program's own output (the redirect target by name)
    "read-own-open": ('print(1)\nprint(2)\nprint(open("o.txt").length())\n', ("file", "append")),
    "read-own-cat": ('print("ab")\ncat("o.txt")\nprint(2)\n', ("file", "append")),
    "read-own-file": ('print(1)\nf := file("o.txt")\nf.open("r")\nprint(f.read().length())\nf.close()\n',
                      ("file", "append")),
    # ordinary files are unchanged
    "ordinary-file": ('print(1)\ns := ofstream("x.txt")\ns.write("S\\n")\ns.close()\nprint(2)\ncat("x.txt")\n', MODES),
    # workers: print and cat in order; an ordinary file; a std-stream handle is refused (VM oracle)
    "thread-print-cat": ('fn(w()) {\n  print("w")\n  cat("b.txt")\n  return 1\n}\nprint(1)\nt := thread(w)\n'
                         'print(t.join())\nprint(2)\n', MODES),
    "async-print-cat": ('fn[async](w()) {\n  print("w")\n  cat("b.txt")\n  return 1\n}\nprint(1)\nprint(await w())\n'
                        'print(2)\n', MODES),
    "worker-ordinary-file": ('fn(w()) {\n  s := ofstream("x.txt")\n  s.write("S\\n")\n  s.close()\n  return 1\n}\n'
                             'print(1)\nt := thread(w)\nprint(t.join())\ncat("x.txt")\n', MODES),
}
CASES.update(handle_cases())
# D-073 #2: the approved, VM-specific refusal of a std-stream handle in a worker (exact VM bytes; caught, so the
# program goes on). Legacy writes S through the handle instead.
REFUSED = b"ofstream: a thread() or async() task cannot open the process's standard output or error\n"
WORKER_BODY = ('  try {\n    s := ofstream("/dev/fd/1")\n    s.write("S\\n")\n    s.close()\n  } catch (e) {\n'
               '    print(e.message)\n    print(e.code)\n  }\n  return 1\n}\nprint(1)\n')
VM_ORACLE = {
    "worker-std-stream": ('fn(w()) {\n' + WORKER_BODY + 't := thread(w)\nprint(t.join())\nprint(2)\n',
                          {"pipe": (0, b"1\n" + REFUSED + b"stream.open_failed\n1\n2\n", b"")}),
    "async-std-stream": ('fn[async](w()) {\n' + WORKER_BODY + 'print(await w())\nprint(2)\n',
                         {"pipe": (0, b"1\n" + REFUSED + b"stream.open_failed\n1\n2\n", b"")}),
    # refused before the open truncates: the redirect target (by name) keeps the program's output
    "worker-std-stream-name": ('fn(w()) {\n' + WORKER_BODY.replace("/dev/fd/1", "o.txt") +
                               't := thread(w)\nprint(t.join())\nprint(2)\n',
                               {"file": (0, b"1\n" + REFUSED + b"stream.open_failed\n1\n2\n", b"")}),
    "worker-file-open": ('fn(w()) {\n  try {\n    f := file("/dev/fd/1")\n    f.open("w")\n  } catch (e) {\n'
                         '    print(e.message)\n    print(e.code)\n  }\n  return 1\n}\nprint(1)\nt := thread(w)\n'
                         'print(t.join())\n',
                         {"pipe": (0, b"1\nopen: a thread() or async() task cannot open the process's standard output "
                                      b"or error\nio.open_failed\n1\n", b"")}),
}
MUTANTS = {
    1: ["print-cat-print", "cat-module", "cat-binary"],
    2: ["write-close-fd", "write-flush-fd", "read-own-open", "read-own-cat"],
    3: ["worker-std-stream", "async-std-stream", "worker-std-stream-name", "worker-file-open"],
}


def run(nift, src, mode, vm):
    d = tempfile.mkdtemp(prefix="oo-")
    try:
        with open(os.path.join(d, "main.f"), "w") as f:
            f.write(src)
        with open(os.path.join(d, "m.nft"), "w") as f:
            f.write(MODULE)
        for name, data in DATA.items():
            with open(os.path.join(d, name), "wb") as f:
                f.write(data if isinstance(data, bytes) else data.encode())
        os.symlink("/dev/fd/1", os.path.join(d, "out-link"))
        env = {"PATH": "/usr/bin:/bin", "HOME": d, "LANG": "C", "NIFT_VM": "require" if vm else "0"}
        out_path = os.path.join(d, "o.txt")
        if mode in ("file", "append"):
            with open(out_path, "wb") as f:
                f.write(b"PRE\n" if mode == "append" else b"")
            with open(out_path, "ab" if mode == "append" else "r+b") as out:
                p = subprocess.run(["timeout", "60", nift, "main.f"], cwd=d, env=env, stdout=out,
                                   stderr=subprocess.PIPE, stdin=subprocess.DEVNULL)
            stdout = open(out_path, "rb").read()
        else:
            p = subprocess.run(["timeout", "60", nift, "main.f"], cwd=d, env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT if mode == "merged" else subprocess.PIPE,
                               stdin=subprocess.DEVNULL)
            stdout = p.stdout
        stderr = (p.stderr or b"").replace((d + "/").encode(), b"")
        stdout = stdout.replace((d + "/").encode(), b"")
        files = {}
        for name in sorted(os.listdir(d)):
            if name not in ("o.txt", "out-link") and os.path.isfile(os.path.join(d, name)):
                files[name] = open(os.path.join(d, name), "rb").read()
        return p.returncode, stdout, stderr, files
    finally:
        shutil.rmtree(d, ignore_errors=True)


def check(nift, only=""):
    failing = []
    for name, (src, modes) in CASES.items():
        if only not in name:
            continue
        problems = []
        for mode in modes:
            legacy, vm = run(nift, src, mode, False), run(nift, src, mode, True)
            if legacy != vm:
                problems.append("%s: legacy %r\n      vm     %r" % (mode, legacy[:3], vm[:3]))
            if vm[0] == 70:
                problems.append("%s: exit 70" % mode)
        print(("PASS " if not problems else "FAIL ") + name)
        for p in problems:
            print("    " + p)
        if problems:
            failing.append(name)
    for name, (src, oracle) in VM_ORACLE.items():
        if only not in name:
            continue
        problems = []
        for mode, want in oracle.items():
            got = run(nift, src, mode, True)
            if got[:3] != want:
                problems.append("%s: vm %r, oracle %r" % (mode, got[:3], want))
        print(("PASS " if not problems else "FAIL ") + name)
        for p in problems:
            print("    " + p)
        if problems:
            failing.append(name)
    total = len([n for n in list(CASES) + list(VM_ORACLE) if only in n])
    print("output-order: %d/%d pass" % (total - len(failing), total))
    return failing


def mutants():
    bad = 0
    for n, expected in sorted(MUTANTS.items()):
        out = os.path.join(ROOT, ".build", "mutant-nift", "output-order-%d" % n)
        try:
            b = subprocess.run([sys.executable, "-B", os.path.join(HERE, "sanctioned_mutant.py"), "--out", out,
                                "--define=-DNIFT_OUTPUT_ORDER_MUTANT=%d" % n], capture_output=True, text=True)
            if b.returncode != 0:
                print("FAIL output-order mutant %d: harness: build failed: %s" % (n, b.stderr[-400:] or b.stdout[-400:]))
                bad += 1
                continue
            failing = check(os.path.join(out, "nift"))
            missed = [c for c in expected if c not in failing]
            if missed:
                print("FAIL output-order mutant %d: not caught by %s" % (n, missed))
                bad += 1
            else:
                print("PASS output-order mutant %d: caught by %s" % (n, ", ".join(expected)))
        finally:
            shutil.rmtree(out, ignore_errors=True)
    print("%s output-order mutants: %d of %d caught" % ("FAIL" if bad else "PASS", len(MUTANTS) - bad, len(MUTANTS)))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift")
    ap.add_argument("--only", default="")
    ap.add_argument("--mutants", action="store_true")
    args = ap.parse_args()
    if args.mutants:
        return mutants()
    return 1 if check(os.path.abspath(args.nift), args.only) else 0


if __name__ == "__main__":
    sys.exit(main())
