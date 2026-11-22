#!/usr/bin/env python3
"""Script invocation bindings `cmd` and `args` on the VM
(docs/runtime-2/checkpoints/weekend-args-cmd.md).

Legacy (Parser::install_script_invocation_bindings) gives every script and
session two immutable, deep-readonly bindings: `cmd`, the script as invoked
(the path argument, `<command-line>` for -e/-c, `<stdin>` for `-`, `<repl>`
for a session), and `args`, the script arguments (strings). The VM installs
the same bindings, described once (rt/session_invocation.h) for scripts and
sessions.

  1  parity with NIFT_VM=0 on every entry point (stdout, stderr, exit status),
     the default run proven to execute on the VM (NIFT_VM_STATS)
  2  runtime-error parity (mutation of `args`, out-of-range index, `args(...)`)
  3  refused before any effect: the deferred `cmd(...)` builder, `run()`,
     writes, shadowing, modules naming the bindings; NIFT_VM=0 shows the effect
     is real (the side file exists there)
  4  sessions: `cmd(...)` is refused before the statement runs (the VM-session
     runtime bug: it was compiled as a call of the string), piped and on a tty
  5  intentional difference (D-040 value semantics): a copy of `args` is
     independent of it
  6  eval binds neither name, on either engine
  7  the six real-run fallback units of the G03 proposal study
     (release-plan/proposals/import-package-args.md B.2), verbatim: each runs
     entirely on the VM with legacy's output
  8  top-level shadowing (`args := …`, `cmd := …`), legacy's
     `is_script_invocation` exception: supported where it is an ordinary
     top-level binding; any use before it, or a shadow in a block or function,
     stays refused before any effect; `cmd(...)` stays the built-in
  9  heap accounting: a script that never names the bindings ends with zero
     live heap at stop; one that uses `args` ends with the same live heap as
     the equivalent array literal (the closures-s1 stress regression on
     b30bc637: unused invocation values were kept alive, 128 bytes)

Mutation controls: tests/runtime2/args_cmd/mutants.sh builds binaries without
the binding, with a wrong value, without the `cmd(...)` precedence and without
the module rule; each must fail this suite.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "vm_default"))
from vm_default import run_tty  # noqa: E402  (the shared pty helper)

FAILS = []

PROG = r'''print("cmd=" + cmd)
print("argc=" + args.size().to_string())
for(a : args) { print("[" + a + "]") }
print(type(cmd) + "/" + type(args))
print(args.join("|"))
fn(count()) { return args.size() }
fn(script_name()) { return cmd }
print("fn=" + count().to_string() + " " + script_name())
f := () => cmd + ":" + args.size().to_string()
print("lambda=" + f())
t := thread(count)
print("thread=" + t.join().to_string())
fn[async](later()) { return args.join(",") }
print("async=" + (await later()))
print(args.stringify())
'''
# A session unit: no coroutine/thread code (persistent-session coroutines are deferred).
SESSION_PROG = 'print("cmd=" + cmd)\nprint("argc=" + args.size().to_string())\nfn(count()) { return args.size() }\n' \
    'print("fn=" + count().to_string())\n'
INLINE = 'print(cmd); print(args.size()); for(a : args) { print("<" + a + ">") }'
SESSION_INPUT = 'print(cmd)\nprint(args.size())\nprint(args.join(","))\n'
ARGS = ["x", "y z", "", "ü", "--flag"]


def check(ok, label, detail=""):
    print(("PASS " if ok else "FAIL ") + label + ((": " + detail) if detail and not ok else ""))
    if not ok:
        FAILS.append(label)


def env_for(mode, home, extra=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_")}
    env.update(HOME=str(home), LANG="C.UTF-8", NO_COLOR="1")
    if mode is not None:
        env["NIFT_VM"] = mode
    env.update(extra or {})
    return env


def run(argv, cwd, mode, home, stdin="", extra=None):
    stats = Path(cwd) / ".stats.jsonl"
    if stats.exists():
        stats.unlink()
    env = env_for(mode, home, extra)
    env["NIFT_VM_STATS"] = str(stats)
    p = subprocess.run(argv, cwd=cwd, env=env, input=stdin, capture_output=True, text=True, timeout=120)
    rows = [json.loads(l) for l in stats.read_text().splitlines() if l.strip()] if stats.exists() else []
    stats.unlink(missing_ok=True)
    return p, rows


def on_vm(rows):
    return bool(rows) and all(r.get("vm_executions") == 1 and r.get("legacy_executions") == 0 for r in rows)


def parity(label, argv, cwd, home, stdin="", expect=None, extra=None, session=False):
    legacy, _ = run(argv, cwd, "0", home, stdin, extra)
    vm, rows = run(argv, cwd, None, home, stdin, extra)
    same = (legacy.returncode, legacy.stdout, legacy.stderr) == (vm.returncode, vm.stdout, vm.stderr)
    check(same, label + ": same stdout/stderr/exit as NIFT_VM=0",
          "legacy=%r vm=%r" % ((legacy.returncode, legacy.stdout, legacy.stderr), (vm.returncode, vm.stdout, vm.stderr)))
    # A session records no per-script row; its engine is the VM by construction (D-054).
    if not session:
        check(on_vm(rows), label + ": ran on the VM", repr(rows))
    if expect is not None:
        check(all(e in vm.stdout for e in expect), label + ": expected values", repr(vm.stdout))
    return vm


PROGRAM_ERRORS = {"import-scope:invocation-init"}


def refused(label, src, cwd, home, reason, argv_tail=(), effect="side.txt", files=None):
    work = Path(tempfile.mkdtemp(dir=cwd))
    for name, text in (files or {}).items():
        (work / name).write_text(text)
    (work / "main.f").write_text(src)
    vm, rows = run([NIFT, "main.f", *argv_tail], work, None, home)
    check(vm.returncode == 1, label + ": refused with exit 1", repr((vm.returncode, vm.stderr)))
    if reason.startswith("invocation-assign:"):
        # D-062: a program error, not missing VM functionality.
        name = reason.split(":", 1)[1]
        check(": " + reason + ": cannot assign to `" + name + "`: the script invocation bindings `args` and `cmd` "
              "are read-only\n" in vm.stderr and "no part of it ran" in vm.stderr, label + ": D-062 diagnostic",
              vm.stderr)
        check("not supported" not in vm.stderr and "not compiled for the VM" not in vm.stderr and
              "report unsupported" not in vm.stderr, label + ": not worded as a missing VM feature", vm.stderr)
    elif reason in PROGRAM_ERRORS:
        # D-NEXT-packages: a program error (legacy fails on it too), not a missing VM feature.
        check(any(r.get("reason") == reason for r in rows) and "no part of it ran" in vm.stderr and
              "not supported" not in vm.stderr and "not compiled for the VM" not in vm.stderr,
              label + ": program error " + reason, repr((vm.stderr, rows)))
    else:
        check("(" + reason + ")" in vm.stderr and "no part of it ran" in vm.stderr,
              label + ": diagnostic names " + reason, vm.stderr)
    check(not (work / effect).exists() and "before" not in vm.stdout,
          label + ": refused before any effect", repr((vm.stdout, sorted(p.name for p in work.iterdir()))))
    check(not any(r.get("vm_executions") for r in rows), label + ": nothing executed", repr(rows))
    shutil.rmtree(work)
    work = Path(tempfile.mkdtemp(dir=cwd))
    for name, text in (files or {}).items():
        (work / name).write_text(text)
    (work / "main.f").write_text(src)
    legacy, _ = run([NIFT, "main.f", *argv_tail], work, "0", home)
    check((work / effect).exists(), label + ": control: NIFT_VM=0 performs the effect", repr(legacy.stderr))
    shutil.rmtree(work)


def main():
    global NIFT
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    NIFT = os.path.abspath(ap.parse_args().nift)
    root = Path(tempfile.mkdtemp(prefix="nift-args-cmd-"))
    try:
        return suite(root)
    finally:
        shutil.rmtree(root, ignore_errors=True)


def suite(root):
    home = root / "home"
    home.mkdir()
    work = root / "w"
    work.mkdir()
    (work / "prog.f").write_text(PROG)
    (work / "sess.f").write_text(SESSION_PROG)
    bindir = root / "bin"
    bindir.mkdir()
    if os.name == "nt":
        shutil.copy2(NIFT, bindir / "nift.exe")
    else:
        os.symlink(NIFT, bindir / "nift")
    (work / "ex.f").write_text("#!/usr/bin/env nift\n" + PROG)
    os.chmod(work / "ex.f", 0o755)
    path_env = {"PATH": str(bindir) + os.pathsep + os.environ.get("PATH", "")}

    # 1. Entry points.
    full = ["cmd=prog.f", "argc=5", "[x]", "[y z]", "[]", "[ü]", "[--flag]", "string/array",
            "x|y z||ü|--flag", "fn=5 prog.f", "lambda=prog.f:5", "thread=5", "async=x,y z,,ü,--flag",
            '["x","y z","","ü","--flag"]']
    parity("file (relative path)", [NIFT, "prog.f", *ARGS], work, home, expect=full)
    parity("file (./ path)", [NIFT, "./prog.f", "a"], work, home, expect=["cmd=./prog.f", "argc=1", "[a]"])
    parity("file (absolute path)", [NIFT, str(work / "prog.f")], work, home,
           expect=["cmd=" + str(work / "prog.f"), "argc=0"])
    parity("file (no arguments)", [NIFT, "prog.f"], work, home, expect=["argc=0", "[]\n"])
    parity("file: `--` keeps option-like arguments", [NIFT, "prog.f", "--", "--no-process", "-i"], work, home,
           expect=["argc=2", "[--no-process]", "[-i]"])
    parity("file: --no-process is an option, not an argument", [NIFT, "prog.f", "--no-process", "z"], work, home,
           expect=["argc=1", "[z]"])
    parity("file with --platform", [NIFT, "--platform=windows", "prog.f", "q"], work, home,
           expect=["cmd=prog.f", "argc=1", "[q]"])
    shebang_cases = [
        ("shebang script executed directly", [str(work / "ex.f"), "a", "b c"],
         ["cmd=" + str(work / "ex.f"), "argc=2", "[b c]"]),
        ("shebang script as ./ex.f", ["./ex.f", "a"], ["cmd=./ex.f", "[a]"]),
    ]
    for label, argv, expected in shebang_cases:
        try:
            parity(label, argv, work, home, extra=path_env, expect=expected)
        except OSError as error:
            check(False, label + ": direct invocation failed", str(error))
    parity("nift ex.f (shebang stripped)", [NIFT, "ex.f", "a"], work, home, expect=["cmd=ex.f", "[a]"])
    for flag in ("-e", "-c"):
        parity(flag + " program", [NIFT, flag, INLINE, "a", "b c"], work, home,
               expect=["<command-line>\n2\n<a>\n<b c>\n"])
    parity("-e program: `--` and -i after it", [NIFT, "-e", INLINE, "--", "-i", "x"], work, home,
           expect=["<command-line>\n2\n<-i>\n<x>\n"])
    parity("- (program on stdin)", [NIFT, "-", "p", "q"], work, home, stdin=INLINE,
           expect=["<stdin>\n2\n<p>\n<q>\n"])
    parity("-i file, piped session", [NIFT, "-i", "sess.f", "a", "b"], work, home, stdin=SESSION_INPUT,
           expect=["cmd=sess.f\nargc=2\nfn=2\nsess.f\n2\na,b\n"], session=True)
    # Owner ruling "args/cmd x reference guard, Option 2, 2026-11-01": the Phase A guard refuses a loop over the session
    # array `args` (reference-guard:shared-part, a temporary over-refusal Phase B
    # must admit). The original and legacy's output are preserved as
    # tests/runtime2/refsem/pending/args_session_loop.*; the loop over `args`
    # in a script stays covered by the -e/-c cases above.
    legacy, _ = run([NIFT, "-i", "-e", INLINE, "a"], work, "0", home, SESSION_INPUT)
    vm, _ = run([NIFT, "-i", "-e", INLINE, "a"], work, None, home, SESSION_INPUT)
    check(legacy.returncode == 0 and legacy.stdout == "<command-line>\n1\n<a>\n<command-line>\n1\na\n",
          "-i -e, piped session: legacy output kept", repr((legacy.returncode, legacy.stdout)))
    # B5 (D-NEXT-refsem-b, b5-refsem.md section 6, case 1): the guard is gone; the loop runs, legacy-equal.
    check(vm.returncode == 0 and vm.stdout == legacy.stdout and "refused" not in vm.stderr,
          "-i -e, piped session: runs as legacy (B5)", repr((vm.returncode, vm.stdout, vm.stderr)))
    parity("-i -e, piped session without a loop over args", [NIFT, "-i", "-e", 'print(cmd); print(args.size()); print(args[0])', "a"],
           work, home, stdin=SESSION_INPUT, expect=["<command-line>\n1\na\n<command-line>\n1\na\n"], session=True)
    parity("bare piped session", [NIFT], work, home, stdin=SESSION_INPUT, expect=["<repl>\n0\n\n"], session=True)
    (home / ".niftrc").write_text('print("rc " + cmd + " " + args.size().to_string())\n')
    parity("~/.niftrc, piped session", [NIFT], work, home, stdin=SESSION_INPUT,
           expect=["rc <repl> 0\n<repl>\n0\n"], session=True)
    (home / ".niftrc").unlink()

    status, out = run_tty(NIFT, work, [], {}, ["print(cmd)", "print(args.size())", 'print("x"); c := cmd("echo")',
                                                'print("after " + cmd)', "exit"])
    check(status == 0 and "<repl>\n" in out and "\n0\n" in out, "tty REPL: cmd and args", repr((status, out)))
    check("(callable:cmd)" in out or "callable:cmd:" in out, "tty REPL: cmd(...) refused", out)
    check("\nx\n" not in out and "after <repl>" in out,
          "tty REPL: the refused statement left no effect; the session continues", out)
    (home / ".niftrc").unlink(missing_ok=True)
    status, out = run_tty(NIFT, work, ["-i", "sess.f", "k"], {}, ["print(args.join(\"+\") + \"!\")", "exit"])
    check(status == 0 and "cmd=sess.f" in out and "fn=1" in out and "k!" in out, "tty -i file: cmd and args",
          repr((status, out)))

    # 2. Runtime errors: legacy's text, status and order.
    # Owner ruling "args/cmd x reference guard, Option 2, 2026-11-01": a mutation of `args` is refused before any effect by
    # the Phase A guard (reference-guard:*), not raised as legacy's const error
    # after earlier output. A temporary diagnostic-classification difference;
    # correcting the diagnostic is a pre-release obligation. The originals and
    # legacy's behaviour are preserved as tests/runtime2/refsem/pending/
    # args_push_after_output.*, args_sort.*.
    for label, src in (("args.push is an error after earlier output", 'print("e")\nargs.push("z")\n'),
                       ("args.sort is an error", 'args.sort()\n')):
        (work / "rt.f").write_text(src)
        legacy, _ = run([NIFT, "rt.f", "a"], work, "0", home)
        vm, _ = run([NIFT, "rt.f", "a"], work, None, home)
        check(legacy.returncode == 1 and "cannot mutate const array: args" in legacy.stderr,
              "runtime error: " + label + ": legacy's const error kept", repr((legacy.returncode, legacy.stderr)))
        # B5 (D-NEXT-refsem-b, case 2): the immutable object's error (kImmutable), legacy's text and order.
        check((vm.returncode, vm.stdout) == (legacy.returncode, legacy.stdout) and
              "cannot mutate const array: args\n" in vm.stderr and "not compiled" not in vm.stderr,
              "runtime error: " + label + ": legacy's const error, after earlier output (B5)",
              repr((vm.returncode, vm.stdout, vm.stderr)))
    # The const binding is still classified (review of 2a1c228a, F1): pushing
    # an array or object onto `args` takes the const-array path and is refused
    # by the guard, never as `aliasing` (the path of a writable binding). Phrased
    # as "refused by the guard, not aliasing" so it stays valid once the
    # diagnostic becomes the const error (a pre-release obligation).
    for label, src in (("args.push(args)", 'print("e")\nargs.push(args)\n'),
                       ("args.push(x) with x := [1]", 'print("e")\nx := [1]\nargs.push(x)\n')):
        (work / "rt.f").write_text(src)
        vm, _ = run([NIFT, "rt.f", "a"], work, None, home)
        # B5 (D-NEXT-refsem-b, case 3): the const error after earlier output, never aliasing or a cycle
        # (the immutability check precedes the cycle check, B1 stage order).
        check(vm.returncode == 1 and vm.stdout == "e\n" and "cannot mutate const array: args\n" in vm.stderr,
              "const args: " + label + ": the const error after earlier output (B5)",
              repr((vm.returncode, vm.stdout, vm.stderr)))
        check("(aliasing)" not in vm.stderr and "cycle" not in vm.stderr,
              "const args: " + label + ": not aliasing, not a cycle", repr(vm.stderr))
    for label, src in (("out-of-range index", 'print(args[5])\n'),
                       ("calling args", 'print("e")\nargs("a")\n')):
        (work / "rt.f").write_text(src)
        vm = parity("runtime error: " + label, [NIFT, "rt.f", "a"], work, home)
        check(vm.returncode == 1 and vm.stderr.startswith("error: ") and "rt.f:" in vm.stderr, "runtime error: " + label + ": exit 1",
              repr((vm.returncode, vm.stderr)))

    # 3. Refused before any effect (NIFT_VM=0 shows the effect is real).
    touch = 'native::touch("side.txt")\nprint("before")\n'
    refused("cmd(...) builder", touch + 'c := cmd("echo", "hi")\n', work, home, "callable:cmd")
    refused("cmd(...).run()", touch + 'r := cmd("ls").run()\n', work, home, "callable:cmd")
    refused("run() neighbour", touch + 'r := run("echo", "hi")\n', work, home, "callable:run")
    # D-062 (approved break): writes to the bindings are rejected before execution, so even the statements
    # before them have no effect (the `touch` and the print); legacy fails at the statement, after them.
    refused("assign args", touch + 'args = []\n', work, home, "invocation-assign:args")
    refused("assign an element of args", touch + 'args[0] = "z"\n', work, home, "invocation-assign:args", ["a"])
    refused("assign cmd", touch + 'cmd = "x"\n', work, home, "invocation-assign:cmd")
    refused("compound-assign cmd", touch + 'cmd += "x"\n', work, home, "invocation-assign:cmd")
    refused("assign args in a function", touch + 'fn(f()) { args = [] }\nf()\n', work, home, "invocation-assign:args")
    refused("compound-assign an element of args", touch + 'args[0] += "z"\n', work, home, "invocation-assign:args",
            ["a"])
    refused("assign a nested element of args", touch + 'args[0][0] = "q"\n', work, home, "invocation-assign:args",
            ["a"])
    refused("assign a member of an element of args", touch + 'args[0].x = 1\n', work, home, "invocation-assign:args",
            ["a"])
    refused("assign args after a loop of effects", 'for(i : [1, 2]) { native::touch("side.txt") }\nprint("before")\n'
            'args = ["x"]\n', work, home, "invocation-assign:args")
    refused("read args before a top-level shadow", touch + 'print(args.size())\nargs := ["k"]\n', work, home,
            "unresolved-name", ["a"])
    refused("shadow initialised from args", touch + 'args := args.slice(1)\nprint(args.size())\n', work, home,
            "unresolved-name", ["a", "b"])
    refused("shadow args in a block", touch + 'if(true) { args := ["k"]\nprint(args[0]) }\n', work, home,
            "binding-name-reserved")
    refused("shadow args in a function", touch + 'fn(f()) { args := ["k"]\nreturn args[0] }\nprint(f())\n', work,
            home, "binding-name-reserved")
    refused("function reading a shadowed args before its declaration",
            'fn(f()) { return args.size() }\n' + touch + 'print(f())\nargs := ["k"]\n', work, home, "free-name")
    refused("cmd(...) with cmd shadowed", touch + 'cmd := "q"\nc := cmd("echo")\n', work, home, "callable:cmd")
    refused("function named args", touch + 'fn(args(x)) { return x }\nprint(args(1))\n', work, home,
            "fn-name-reserved")
    refused("struct method reading args", 'struct(P) { fn(m()) { return args.size() } }\n' + touch +
            'p := P()\nprint(p.m())\n', work, home, "free-name")
    # Modules (D-NEXT-packages #4, superseding D-062 #1): the full contract is
    # tests/runtime2/packages/packages.py; here the args/cmd side of it.
    refused("module top level naming args", touch + 'import("lib.f")\nprint(w)\n', work, home,
            "import-scope:invocation-init",
            files={"lib.f": 'w := args.size()\nexport(w)\n'})
    d = Path(tempfile.mkdtemp(dir=work))
    (d / "lib.f").write_text('fn(g()) { return cmd + ":" + args.join(",") }\nexport(g)\n')
    (d / "main.f").write_text('import("lib.f")\nprint(g())\n')
    parity("module function reading the entry's cmd and args", [NIFT, "main.f", "a", "b"], d, home,
           expect=["main.f:a,b\n"])
    shutil.rmtree(d)
    # A module that declares `args`/`cmd` at its top level has its own private
    # binding (supported, as legacy); exporting it is not proved and is refused.
    for name, value in (("args", '["m1", "m2"]'), ("cmd", '"modcmd"')):
        for exported in (True, False):
            for entry_names_it in (True, False):
                label = "module declares %s (%s, entry %s)" % (name, "exported" if exported else "private",
                                                               "names it" if entry_names_it else "does not")
                d = Path(tempfile.mkdtemp(dir=work))
                (d / "lib.f").write_text(name + " := " + value + "\n" + ("export(" + name + ")\n" if exported else
                                         'print("lib " + ' + (name + '.join(",")' if name == "args" else name) +
                                         ')\n'))
                (d / "main.f").write_text('print("before")\nimport("lib.f")\n' +
                                          ('print(%s)\n' % (name + '.join(",")' if name == "args" else name)
                                           if entry_names_it else 'print("main")\n'))
                if exported:
                    # Into the entry: legacy's own error at the import, a program error (D-NEXT-packages).
                    vm, rows = run([NIFT, "main.f", "a"], d, None, home)
                    check(vm.returncode == 1 and vm.stdout == "" and
                          "main.f:2:1: import: export collides with existing binding: " + name + "\n" in vm.stderr
                          and "not supported" not in vm.stderr and not any(r.get("vm_executions") for r in rows)
                          and any(r.get("reason") == "import-scope:invocation-export" for r in rows),
                          label + ": rejected before any effect with legacy's error at the import",
                          repr((vm.returncode, vm.stdout, vm.stderr[:240])))
                else:
                    parity(label + ": the module's own binding, as legacy", [NIFT, "main.f", "a"], d, home)
                shutil.rmtree(d)
    # A module whose only mention of `args`/`cmd` is a struct or enum *name*
    # (review of de6e6dde, MEDIUM): refused at that name's real position, never
    # an invented one (the end of the file) and never "no exact position".
    for label, lib, at in (
            ("module struct named args",
             'fn(h()) { return 1 }\nexport(h)\nstruct(args) {\n  v := 1\n}\n// trailing comment line', "lib.f:3:8"),
            ("module enum named args", 'enum args {\n  A,\n  B\n}\nfn(h()) { return 1 }\nexport(h)\n', "lib.f:1:6"),
            ("module struct named cmd", 'struct(cmd) {\n  v := 1\n}\nfn(h()) { return 1 }\nexport(h)\n', "lib.f:1:8")):
        d = Path(tempfile.mkdtemp(dir=work))
        (d / "lib.f").write_text(lib)
        (d / "main.f").write_text('print("before")\nimport("lib.f")\nprint(h())\n')
        vm, rows = run([NIFT, "main.f", "a", "b"], d, None, home)
        check(vm.returncode == 1 and vm.stdout == "" and "(import-scope:invocation-ambiguous)" in vm.stderr
              and at + ":" in vm.stderr and "no exact position" not in vm.stderr
              and not any(r.get("vm_executions") for r in rows),
              label + ": refused before any effect at the name's own position " + at,
              repr((vm.returncode, vm.stdout, vm.stderr[:240])))
        shutil.rmtree(d)
    # The module's own code is the control here: legacy cannot see `args` at a
    # module's top level (its error), a module function sees the caller's.
    (work / "lib.f").write_text('w := args.size()\nexport(w)\n')
    (work / "m.f").write_text('import("lib.f")\nprint(w)\n')
    legacy, _ = run([NIFT, "m.f", "a"], work, "0", home)
    check(legacy.returncode == 1 and "unknown value or malformed expression: args" in legacy.stderr,
          "control: legacy modules do not see args at their top level", repr(legacy.stderr))
    (work / "m.f").write_text('import("lib2.f")\nprint(args.size())\n')
    (work / "lib2.f").write_text('v := 1\n')
    parity("an import does not hide the entry's bindings", [NIFT, "m.f", "a", "b"], work, home, expect=["2\n"])

    # 4. Sessions: cmd(...) is the built-in's, refused before the statement runs.
    line = 'print("before"); c := cmd("echo", "hi"); print("after")\nprint("next")\n'
    vm, _ = run([NIFT], work, None, home, line)
    check(vm.returncode == 1 and "callable:cmd" in vm.stderr and "undefined callable" not in vm.stderr,
          "piped session: cmd(...) refused (not a call of the string)", repr((vm.returncode, vm.stderr)))
    check("before" not in vm.stdout and "next" not in vm.stdout,
          "piped session: nothing of the refused line ran; the session stopped", repr(vm.stdout))
    legacy, _ = run([NIFT], work, "0", home, line)
    check("before\nafter\n" in legacy.stdout, "control: NIFT_VM=0 session runs the line", repr(legacy.stdout))
    vm, _ = run([NIFT, "-i", "-e", 'x := 1', "a"], work, None, home, 'print(args[0]); r := cmd("ls").run()\n')
    # The session reports the first refusal its compiler meets (here the outer `.run()`).
    check(vm.returncode == 1 and ("callable:cmd" in vm.stderr or "method:run" in vm.stderr) and
          "undefined callable" not in vm.stderr and "a\n" not in vm.stdout,
          "-i -e session: cmd(...).run() refused before effect", repr((vm.returncode, vm.stdout, vm.stderr)))
    vm, _ = run([NIFT, "-i", "-e", 'x := 1', "a"], work, None, home, 'print(args[0]); c := cmd("ls")\n')
    check(vm.returncode == 1 and "callable:cmd" in vm.stderr and "a\n" not in vm.stdout,
          "-i -e session: cmd(...) refused before effect", repr((vm.returncode, vm.stdout, vm.stderr)))

    vm, _ = run([NIFT], work, None, home, 'native::touch("s.txt"); cmd = "x"\nprint("next")\n')
    check(vm.returncode == 1 and "NIFT_VM session refused: invocation-assign:cmd: cannot assign to `cmd`" in vm.stderr
          and "not supported" not in vm.stderr and not (work / "s.txt").exists() and "next" not in vm.stdout,
          "piped session: D-062 assignment rejected before the line runs", repr((vm.returncode, vm.stderr)))

    # 5. A copy of args then mutated. Owner ruling "args/cmd x reference guard, Option 2, 2026-11-01": refused before any
    # effect by the Phase A guard (reference-guard:inflow; formerly D-040's
    # independent copy). Phase B must establish the reference-and-immutability
    # contract (aliasing must not bypass a genuinely immutable binding; a
    # successful mutation is not assumed right). Preserved as
    # tests/runtime2/refsem/pending/args_copy_push.*.
    (work / "copy.f").write_text('b := args\nb.push("q")\nprint(b.size())\nprint(args.size())\n')
    vm, rows = run([NIFT, "copy.f", "a"], work, None, home)
    legacy, _ = run([NIFT, "copy.f", "a"], work, "0", home)
    # B5 (D-NEXT-refsem-b, owner Q1, case 4): `b` is the immutable args object: the error names `b`.
    check(vm.returncode == 1 and vm.stdout == "" and "copy.f:2:1: cannot mutate const array: b\n" in vm.stderr,
          "a copy of args then mutated: the const error through the alias (B5, Q1)",
          repr((vm.returncode, vm.stdout, vm.stderr)))
    check(legacy.stdout == "2\n2\n", "D-040 control: legacy aliases the array", repr(legacy.stdout))

    # 6. eval binds neither name (legacy: unknown value; VM: refused, nothing run).
    for name in ("cmd", "args"):
        legacy, _ = run([NIFT, "eval", name], work, "0", home)
        vm, _ = run([NIFT, "eval", name], work, None, home)
        check(legacy.returncode == 2 and "unknown value" in legacy.stderr, "eval " + name + ": legacy has no binding",
              repr((legacy.returncode, legacy.stderr)))
        check(vm.returncode == 1 and "(unresolved-name)" in vm.stderr, "eval " + name + ": the VM has no binding",
              repr((vm.returncode, vm.stderr)))

    # 7. The six real-run fallback units (proposal study B.2), verbatim.
    six = root / "six"
    six.mkdir()
    units = [
        ("vm-corpus/cp11/fallback_args.f", "fallback_args.f", "print(args.length())\n", [], "0\n"),
        ("rt-diff-corpus/scripts/args/main.f", "main.f", "print(args.stringify())\n",
         ["one", "two words", "--flag"], '["one","two words","--flag"]\n'),
        ("v44_executable_script_smoke hello.f", "hello.f",
         '#!/usr/bin/env nift\n\n/*\n    Executable script fixture.\n*/\nprint("argc=" + args.size().to_string())\n'
         'for(a : args) { print("arg:" + a) }\nprint("env=" + getenv("NIFT_EXEC_TEST"))\nprint("hello")\n',
         ["staging", "--force"], "argc=2\narg:staging\narg:--force\nenv=certified\nhello\n"),
        ("v44_executable_script_smoke certify.f", "certify.f",
         '#!/usr/bin/env nift\nprint(cmd)\nprint(args.join("|"))\nprint(pwd())\nprint(getenv("NIFT_EXEC_TEST"))\n',
         ["one", "two"], "./certify.f\none|two\n" + str(six.resolve()) + "\ncertified\n"),
        ("v45_cli_invocation_smoke invocation.f", "invocation.f", 'print(cmd)\nprint(args.join("|"))\n',
         ["hello world", "--", "--no-process", "-x"], "invocation.f\nhello world|--no-process|-x\n"),
        ("v44_package_language_smoke a.f", "a.f",
         'args := []\nargs.push("hello")\nargs.push("-w")\nargs.push("%{http_code}")\nprint(args.size())\n'
         'print(args[1])\n', [], "3\n-w\n"),
    ]
    env = dict(path_env, NIFT_EXEC_TEST="certified")
    for label, name, src, argv, out in units:
        (six / name).write_text(src)
        os.chmod(six / name, 0o755)
        start = ["./" + name] if name == "certify.f" else [NIFT, name]
        parity("six: " + label, start + argv, six, home, extra=env, expect=[out])

    # 8. Top-level shadowing: an ordinary binding after it.
    for label, src in (("args shadowed then mutated", 'args := ["k"]\nargs.push("l")\nprint(args.join(","))\n'),
                       ("cmd shadowed", 'cmd := 5\nprint(cmd + 1)\n'),
                       ("a function reads the shadow after it",
                        'fn(f()) { return args.size() }\nargs := [1, 2, 3]\nprint(f())\n'),
                       ("args shadowed, cmd still bound", 'args := []\nprint(cmd + ":" + args.size().to_string())\n')):
        (work / "sh.f").write_text(src)
        parity("shadow: " + label, [NIFT, "sh.f", "a", "b"], work, home)

    # 10. Argument-count limits: positionless refusals (no invented location); legacy runs them.
    (work / "many.f").write_text('native::touch("side.txt")\nprint(args.size())\n')
    for count, reason in ((70000, "limit:invocation-arguments"), (40000, "limit:too many registers")):
        if os.name == "nt":
            # Windows imposes a 32,767 UTF-16 unit command-line limit. Use the
            # same CLI with the full argument vector past that OS boundary.
            launcher = HERE.parents[2] / ".build" / "rt-args-count-launcher.exe"
            argv = [str(launcher), str(count), NIFT, "many.f", "a"]
        else:
            argv = [NIFT, "many.f", *(["a"] * count)]
        vm, _ = run(argv, work, None, home)
        check(vm.returncode == 1 and "(" + reason + ")" in vm.stderr and
              "many.f (the VM gave no exact position)" in vm.stderr and "many.f:1:1" not in vm.stderr and
              not (work / "side.txt").exists(), "%d arguments: positionless %s refusal" % (count, reason),
              vm.stderr[:300])
        legacy, _ = run(argv, work, "0", home)
        check(legacy.returncode == 0 and legacy.stdout == "%d\n" % count,
              "%d arguments: legacy runs it (known VM limit)" % count, legacy.stderr[:200])
        (work / "side.txt").unlink(missing_ok=True)

    # 9. Live heap at stop (NIFT_VM_STATS heap_live).
    def live(src, argv):
        (work / "heap.f").write_text(src)
        p, rows = run([NIFT, "heap.f", *argv], work, None, home)
        return p.returncode, [r.get("heap_live") for r in rows]
    for label, src in (("no bindings named", 'x := 1 + 2\nprint(x)\n'),
                       ("only scalars", 'n := 3\nfn(f()) { return n }\nprint(f())\n')):
        rc, heap = live(src, ["a", "b c"])
        check(rc == 0 and heap == [0], "heap: " + label + ": zero live heap at stop with arguments", repr((rc, heap)))
    rc_args, heap_args = live('print(args.size())\nprint(args[1])\n', ["a", "b c"])
    rc_lit, heap_lit = live('x := ["a", "b c"]\nprint(x.size())\nprint(x[1])\n', [])
    check(rc_args == rc_lit == 0 and heap_args == heap_lit and len(heap_args) == 1,
          "heap: args ends with the live heap of the equivalent array literal", repr((heap_args, heap_lit)))
    rc_cmd, heap_cmd = live('print(cmd)\n', ["a"])
    rc_s, heap_s = live('print("heap.f")\n', [])
    check(rc_cmd == rc_s == 0 and heap_cmd == heap_s, "heap: cmd ends with the live heap of the string literal",
          repr((heap_cmd, heap_s)))

    print("args/cmd: %d failure(s)" % len(FAILS))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
