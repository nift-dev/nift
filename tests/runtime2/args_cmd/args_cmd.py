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
    parity("shebang script executed directly", [str(work / "ex.f"), "a", "b c"], work, home, extra=path_env,
           expect=["cmd=" + str(work / "ex.f"), "argc=2", "[b c]"])
    parity("shebang script as ./ex.f", ["./ex.f", "a"], work, home, extra=path_env, expect=["cmd=./ex.f", "[a]"])
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
    parity("-i -e, piped session", [NIFT, "-i", "-e", INLINE, "a"], work, home, stdin=SESSION_INPUT,
           expect=["<command-line>\n1\n<a>\n<command-line>\n1\na\n"], session=True)
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
    for label, src in (("args.push is an error after earlier output", 'print("e")\nargs.push("z")\n'),
                       ("args.sort is an error", 'args.sort()\n'),
                       ("out-of-range index", 'print(args[5])\n'),
                       ("calling args", 'print("e")\nargs("a")\n')):
        (work / "rt.f").write_text(src)
        vm = parity("runtime error: " + label, [NIFT, "rt.f", "a"], work, home)
        check(vm.returncode == 1 and vm.stderr.startswith("error: ") and "rt.f:" in vm.stderr, "runtime error: " + label + ": exit 1",
              repr((vm.returncode, vm.stderr)))

    # 3. Refused before any effect (NIFT_VM=0 shows the effect is real).
    touch = 'touch("side.txt")\nprint("before")\n'
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
    refused("assign args after a loop of effects", 'for(i : [1, 2]) { touch("side.txt") }\nprint("before")\n'
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
    refused("module top level naming args", touch + 'import("lib.f")\nprint(w)\n', work, home,
            "import-scope:invocation-binding",
            files={"lib.f": 'w := args.size()\nexport(w)\n'})
    refused("module function naming cmd", touch + 'import("lib.f")\nprint(g())\n', work, home,
            "import-scope:invocation-binding",
            files={"lib.f": 'fn(g()) { return cmd }\nexport(g)\n'})
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

    vm, _ = run([NIFT], work, None, home, 'touch("s.txt"); cmd = "x"\nprint("next")\n')
    check(vm.returncode == 1 and "NIFT_VM session refused: invocation-assign:cmd: cannot assign to `cmd`" in vm.stderr
          and "not supported" not in vm.stderr and not (work / "s.txt").exists() and "next" not in vm.stdout,
          "piped session: D-062 assignment rejected before the line runs", repr((vm.returncode, vm.stderr)))

    # 5. D-040: value semantics (intentional, owner-approved): a copy is independent.
    (work / "copy.f").write_text('b := args\nb.push("q")\nprint(b.size())\nprint(args.size())\n')
    vm, rows = run([NIFT, "copy.f", "a"], work, None, home)
    legacy, _ = run([NIFT, "copy.f", "a"], work, "0", home)
    check(vm.returncode == 0 and vm.stdout == "2\n1\n" and on_vm(rows), "D-040: a copy of args is independent",
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

    print("args/cmd: %d failure(s)" % len(FAILS))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
