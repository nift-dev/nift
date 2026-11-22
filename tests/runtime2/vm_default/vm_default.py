#!/usr/bin/env python3
"""VM-default policy: the VM is the default scripting engine, with no silent
legacy fallback.

  NIFT_VM unset        VM required: an unsupported program is refused before
                       any effect, with the full diagnostic and exit status 1
  NIFT_VM=require      identical to the default
  NIFT_VM=0            explicit legacy interpreter (escape hatch), unchanged
  NIFT_VM=1            VM first with legacy fallback, unchanged (no hint)
  classify[-all]       test tooling, unchanged
  anything else        (empty, off, false, banana, ...) a configuration error on
                       every scripting entry point: exit 1, the value and the
                       accepted values named, nothing run (owner ruling);
                       website builds never consult NIFT_VM

Execution engine is proven by NIFT_VM_STATS records (vm_executions /
legacy_executions), never by output alone. Website builds keep the legacy
renderer: byte-identical output under the default and NIFT_VM=0, and no VM
statistics record. Interactive sessions are VM-owned by default (D-054) and a
refused session unit gets the same full diagnostic, located in the user's terms
(`<stdin>:line:column` over the whole input stream, or the -i unit's path).

--platform=<target> is honoured by the VM (platform() returns the selection,
as legacy's script host does), so every script entry (file, -e/-c, stdin, -i,
the REPL) with any platform runs on the VM under the default policy, never on
legacy; an unsupported program with a platform selection is refused like any
other, and its legacy instruction keeps the selector. NIFT_VM=0 is legacy.
A refusal inside an imported module names the module position and nothing in
the main program or the module runs.

Negative controls: the same predicates must reject the old default
(NIFT_VM=0 standing in for "unset = legacy") and a fallback environment
(NIFT_VM=1), and the diagnostic predicate must reject a diagnostic missing
any required part.
"""
import argparse
import errno
import json
import os
import subprocess
import sys
import pty
import select
import signal
import tempfile
import time
from pathlib import Path

FAILS = []
ISSUES = "https://github.com/nift-dev/nift/issues"

UNSUPPORTED = ('f := file("side.txt")\nf.open("w")\nf.write("x")\nf.save()\nf.close()\n'
               'print("before")\nr := run("echo", "hi")\nprint(type(r))\n')
REFUSED_AT = (7, 6)  # `run(` on line 7
REASON = "callable:run"
SUPPORTED = 'f := file("made.txt")\nf.open("w")\nf.write("y")\nf.save()\nf.close()\nx := 2 + 3\nprint(x)\n'


def check(ok, label, detail=""):
    print(("PASS " if ok else "FAIL ") + label + ((": " + detail) if detail and not ok else ""))
    if not ok:
        FAILS.append(label)


# The program's own output lines: every captured line except the
# diagnostics ("error: ..." / "note: ..."), which name paths under the
# temporary work directory (its random name, e.g. vm-default-pugdqe2f, can
# contain any short token a check looks for: "e2", "b2", "s5", ...). Checks
# that a short program output is absent, or printed once, compare whole lines
# of this list - never a substring of the captured text (zero-flakiness
# policy). --work-prefix runs the suite in a directory whose name contains
# the tokens (the Makefile target does both).
DIAGNOSTIC_PREFIXES = ("error: ", "note: ")
PATH_TOKENS = ("e2", "b2", "s5", "p2", "s1", "inside", "middle", "block", "before")


def program_lines(out):
    return [line for line in out.split("\n") if not line.startswith(DIAGNOSTIC_PREFIXES)]


PATH_CONTROLLED = set()


def path_control(work, out, token, label):
    """With a work directory named after `token`, the captured output has
    more occurrences of it (in a diagnostic's path: the old substring check
    failed there) than the program's lines: the check addresses the cause."""
    if token in work.name and work.name in out:  # (an output naming no path cannot confuse the check)
        PATH_CONTROLLED.add(token)
        check(out.count(token) > program_lines(out).count(token),
              f"regression ({label}): {token!r} in the temp path is in the captured output, not in the program's "
              "lines", f"out={out!r}")


def base_env(extra):
    env = {k: v for k, v in os.environ.items() if not k.startswith("NIFT_")}
    env.update(extra)
    return env


def run(nift, cwd, args, extra, stdin=None, stats=True):
    cwd = Path(cwd)
    stats_path = cwd / "stats.jsonl"
    if stats_path.exists():
        stats_path.unlink()
    env = base_env(extra)
    if stats:
        env["NIFT_VM_STATS"] = str(stats_path)
    proc = subprocess.run([nift, *args], cwd=cwd, env=env, input=stdin, capture_output=True, text=True,
                          timeout=120)
    rows = [json.loads(line) for line in stats_path.read_text().splitlines() if line.strip()] \
        if stats_path.exists() else []
    return proc, rows


RUN_FEATURE = "the call `run()`"
PLATFORM_SCRIPT = 'native::touch("side.txt")\nprint(platform())\n'


PROGRAM_NOTE = "note: the program was not executed: no part of it ran\n"
# A session refuses one statement: earlier statements have run and later ones
# still run, so its note must never claim that nothing ran.
STATEMENT_NOTE = "note: this statement was not executed; earlier statements in this session may already have executed\n"
# Owner ruling (Option A): a non-interactive session stops at the refusal; a
# typed REPL on a terminal continues at the prompt.
SESSION_NOTE = STATEMENT_NOTE + "note: the session stopped here: no later input was run (exit status 1)\n"
TTY_SESSION_NOTE = STATEMENT_NOTE + "note: the session continues; the statement left no definitions or effects\n"


def notes(hint, executed=PROGRAM_NOTE):
    return (executed +
            f"note: to run it on the legacy interpreter: NIFT_VM=0 nift{(' ' + hint) if hint else ''}\n"
            f"note: please report unsupported programs at {ISSUES}\n")


def run_tty(nift, cwd, args, extra, lines, settle=0.4):
    """Type `lines` into nift on a real terminal (pty); returns (status, output
    with the line editor's redraws removed)."""
    env = base_env(extra)
    env.update(HOME=str(cwd), TERM="dumb", NO_COLOR="1")
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(cwd)
        os.execve(nift, [nift, *args], env)
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
    for line in lines:
        drain(settle)
        try:
            os.write(fd, (line + "\r").encode())
        except OSError as error:
            if error.errno != errno.EIO:
                raise
            # The slave can close before the test attempts to send input,
            # notably when an initial unit refuses and terminates the child.
            break
    drain(2.0)
    deadline = time.time() + 20
    while True:
        done, status = os.waitpid(pid, os.WNOHANG)
        if done:
            break
        if time.time() > deadline:
            os.kill(pid, signal.SIGKILL)
            done, status = os.waitpid(pid, 0)
            break
        drain(0.2)
    os.close(fd)
    text = out.decode(errors="replace").replace("\r\n", "\n")
    # Keep only completed lines (the editor redraws `\r\x1b[2K<prompt><partial>`).
    kept = [chunk.split("\r")[-1] for chunk in text.split("\n")]
    return os.waitstatus_to_exitcode(status), "\n".join(k for k in kept if not k.startswith("\x1b[2K"))


def expected_diagnostic(where, hint, reason=REASON, feature=RUN_FEATURE):
    return (f"error: {where}: not compiled for the VM ({reason}): {feature} is not supported by the "
            "Runtime 2.0 VM yet\n" + notes(hint))


def expected_session_diagnostic(where, hint, reason=REASON, feature=RUN_FEATURE, note=SESSION_NOTE):
    return (f"error: {where}: NIFT_VM session refused: {reason}: {feature} is not supported by the "
            "Runtime 2.0 VM yet\n" + notes(hint, note))


def diagnostic_problems(stderr, where, hint, reason=REASON, feature=RUN_FEATURE, session=False):
    """Every required part, and the exact deterministic text."""
    problems = []
    parts = [where + ":", "NIFT_VM session refused" if session else "not compiled for the VM", reason, feature,
             "not executed", f"NIFT_VM=0 nift{(' ' + hint) if hint else ''}\n", ISSUES]
    for part in parts:
        if part not in stderr:
            problems.append(f"missing {part!r}")
    if session and "no part of it ran" in stderr:
        problems.append("session note claims that no part of it ran")
    expected = (expected_session_diagnostic if session else expected_diagnostic)(where, hint, reason, feature)
    if stderr != expected:
        problems.append(f"stderr {stderr!r}")
    return problems


def refusal_problems(nift, root, extra):
    """The default/require policy on an unsupported script: refused before any
    side effect, full diagnostic, exit 1, no engine executed it."""
    root = Path(root)
    (root / "case.f").write_text(UNSUPPORTED)
    side = root / "side.txt"
    if side.exists():
        side.unlink()
    proc, rows = run(nift, root, ["case.f"], extra)
    problems = []
    if proc.returncode != 1:
        problems.append(f"rc={proc.returncode}")
    if proc.stdout != "":
        problems.append(f"stdout {proc.stdout!r}")
    if side.exists():
        problems.append("side effect: side.txt exists")
    where = f"{(root / 'case.f').resolve().as_posix()}:{REFUSED_AT[0]}:{REFUSED_AT[1]}"
    problems += diagnostic_problems(proc.stderr, where, "case.f")
    if len(rows) != 1 or rows[0].get("outcome") != "require-error" or rows[0].get("reason") != REASON or \
            rows[0].get("vm_executions") != 0 or rows[0].get("legacy_executions") != 0:
        problems.append(f"stats {rows}")
    return problems, proc


def supported_problems(nift, root, extra):
    root = Path(root)
    (root / "ok.f").write_text(SUPPORTED)
    made = root / "made.txt"
    if made.exists():
        made.unlink()
    proc, rows = run(nift, root, ["ok.f"], extra)
    problems = []
    if proc.returncode != 0 or proc.stdout != "5\n" or proc.stderr != "":
        problems.append(f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r}")
    if not made.exists():
        problems.append("made.txt missing")
    if len(rows) != 1 or rows[0].get("outcome") != "vm" or rows[0].get("vm_executions") != 1 or \
            rows[0].get("legacy_executions") != 0:
        problems.append(f"stats {rows}")
    return problems


def site(root: Path):
    for d in (".nift", "content", "templates", "public", "scripts", "data"):
        (root / d).mkdir(parents=True, exist_ok=True)
    (root / ".nift/config.json").write_text(json.dumps({"config": {
        "content-dir": "content/", "content-ext": ".html", "output-dir": "public/", "output-ext": ".html",
        "default-template": "templates/t.html", "build-threads": 1, "incremental-mode": "modified",
        "minify-exts": [], "pre build": "scripts/pre.f"}}))
    (root / ".nift/tracked.json").write_text(json.dumps({"tracked": [
        {"name": "index", "title": "Index", "template": "templates/t.html", "pre-build": "scripts/pre.f"},
        {"name": "about", "title": "About"}]}))
    (root / "templates/t.html").write_text('<html>TPL:$[open("data/inside.txt")]\n@content\n</html>\n')
    (root / "data/inside.txt").write_text("PAGE-OK")
    # `try` and run() are refused by the VM compiler: a VM-routed page would fail.
    (root / "content/index.html").write_text(
        '<p>@script { x := ""; try { x = open("data/inside.txt") } catch(e) { x = "ERR" }; return x }</p>\n')
    (root / "content/about.html").write_text('<p>@script { r := run("echo", "about"); return r.stdout }</p>\n')
    (root / "scripts/pre.f").write_text('try { touch("hook-marker.txt") } catch(e) { }\n')


def tree(path: Path):
    return {p.relative_to(path).as_posix(): p.read_bytes() for p in sorted(path.rglob("*")) if p.is_file()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--work-prefix", default="vm-default-",
                    help="temporary directory prefix (the regression run uses one containing e2, b2, ...)")
    opts = ap.parse_args()
    nift = str(Path(opts.nift).resolve())
    with tempfile.TemporaryDirectory(prefix=opts.work_prefix) as work:
        work = Path(work)

        # 0. The line checks: a token inside a diagnostic's path is never
        # program output; a real leaked line always is.
        in_path = "e1\nerror: /tmp/vm-default-pugdqe2f/s5-b2-s1/init.f:3:1: refused\nnote: in /tmp/x-inside-p2\nPROMPT\n"
        check(not any(t in program_lines(in_path) for t in PATH_TOKENS) and program_lines(in_path).count("e1") == 1,
              "control: a token inside a diagnostic's path is not a program line")
        for token in PATH_TOKENS:
            leaked = (in_path.replace("PROMPT\n", token + "\nPROMPT\n"), in_path + token, token + "\n" + in_path)
            check(all(token in program_lines(text) for text in leaked) and
                  all(program_lines(text).count(token) == 1 for text in leaked),
                  f"control: a real leaked {token!r} line is still caught next to the path")
        if opts.work_prefix != "vm-default-":
            check(all(t in work.name for t in PATH_TOKENS),
                  f"regression: the work directory {work.name!r} contains every checked token")

        # 1. Default runs a supported script on the VM (stats prove the engine).
        for label, extra in (("unset", {}), ("require", {"NIFT_VM": "require"})):
            d = work / f"supported-{label}"
            d.mkdir()
            problems = supported_problems(nift, d, extra)
            check(not problems, f"supported script runs on the VM (NIFT_VM {label})", "; ".join(problems))

        # 2. Default (and empty, require) refuses an unsupported script before any effect.
        results = {}
        for label, extra in (("unset", {}), ("require", {"NIFT_VM": "require"})):
            d = work / f"refused-{label}"
            d.mkdir()
            problems, proc = refusal_problems(nift, d, extra)
            results[label] = (proc.returncode, proc.stdout, proc.stderr.replace(str(d.resolve()), "$D"))
            check(not problems, f"unsupported script refused with the full diagnostic (NIFT_VM {label})",
                  "; ".join(problems))
        check(results["unset"] == results["require"],
              "NIFT_VM=require is identical to the default", repr(results))

        # 3. NIFT_VM=0 runs the same unsupported script on legacy successfully.
        d = work / "legacy"
        d.mkdir()
        (d / "case.f").write_text(UNSUPPORTED)
        proc, rows = run(nift, d, ["case.f"], {"NIFT_VM": "0"})
        check(proc.returncode == 0 and proc.stdout == "before\nobject\n" and (d / "side.txt").exists() and
              rows == [], "NIFT_VM=0 runs the unsupported script on legacy",
              f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} rows={rows}")

        # 4. NIFT_VM=1 still falls back (legacy executes once; no legacy hint).
        d = work / "fallback"
        d.mkdir()
        (d / "case.f").write_text(UNSUPPORTED)
        proc, rows = run(nift, d, ["case.f"], {"NIFT_VM": "1"})
        check(proc.returncode == 0 and proc.stdout == "before\nobject\n" and (d / "side.txt").exists() and
              len(rows) == 1 and rows[0].get("outcome") == "fallback" and rows[0].get("legacy_executions") == 1
              and rows[0].get("vm_executions") == 0 and "NIFT_VM=0" not in proc.stderr,
              "NIFT_VM=1 still falls back to legacy, without the hint",
              f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} rows={rows}")

        # 5. -e program and eval: refused by default; the hint repeats the invocation.
        d = work / "inline"
        d.mkdir()
        proc, rows = run(nift, d, ["-e", 'native::touch("t.txt")\nr := run("echo", "hi")'], {})
        hint = "-e 'native::touch(\"t.txt\")\nr := run(\"echo\", \"hi\")'"
        problems = diagnostic_problems(proc.stderr, "<command-line>:2:6", hint)
        check(proc.returncode == 1 and proc.stdout == "" and not (d / "t.txt").exists() and not problems and
              [r.get("outcome") for r in rows] == ["require-error"],
              "-e program refused by default before effects", f"rc={proc.returncode} {problems} rows={rows}")
        proc, rows = run(nift, d, ["-e", 'native::touch("t.txt")\nr := run("echo", "hi")\nprint(type(r))'],
                         {"NIFT_VM": "0"})
        check(proc.returncode == 0 and proc.stdout == "object\n" and (d / "t.txt").exists() and rows == [],
              "-e program runs on legacy under NIFT_VM=0", f"rc={proc.returncode} err={proc.stderr!r}")
        proc, rows = run(nift, d, ["eval", "1 + 2"], {})
        check(proc.returncode == 0 and proc.stdout == "3\n" and [r.get("outcome") for r in rows] == ["vm"],
              "eval runs on the VM by default", f"rc={proc.returncode} out={proc.stdout!r} rows={rows}")
        proc, rows = run(nift, d, ["eval", 'run("echo", "x")'], {})
        problems = diagnostic_problems(proc.stderr, "<eval>:1:1", "eval 'run(\"echo\", \"x\")'")
        check(proc.returncode == 1 and proc.stdout == "" and not problems and
              [r.get("outcome") for r in rows] == ["require-error"], "eval refused by default",
              f"rc={proc.returncode} {problems} rows={rows}")
        proc, rows = run(nift, d, ["eval", 'type(run("echo", "x"))'], {"NIFT_VM": "0"})
        check(proc.returncode == 0 and proc.stdout == "object\n" and rows == [], "eval on legacy under NIFT_VM=0",
              f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r}")

        # 6. Website build: legacy renderer, byte-identical, no VM statistics.
        outputs = {}
        for label, extra in (("default", {}), ("legacy", {"NIFT_VM": "0"})):
            root = work / f"site-{label}"
            site(root)
            proc, rows = run(nift, root, ["build", "--all"], extra)
            outputs[label] = tree(root / "public")
            check(proc.returncode == 0 and "TPL:PAGE-OK" in outputs[label].get("index.html", b"").decode() and
                  b"about" in outputs[label].get("about.html", b"") and (root / "hook-marker.txt").exists() and
                  rows == [], f"website build ({label}) renders on legacy, no VM record",
                  f"rc={proc.returncode} err={proc.stderr!r} rows={rows} files={list(outputs[label])}")
        check(outputs["default"] == outputs["legacy"] and outputs["default"],
              "website build output is byte-identical under the default and NIFT_VM=0")

        # 7. Sessions are VM-owned by default (D-054). Owner ruling (Option A):
        # a non-interactive session (stdin not a terminal) stops at the first
        # refusal - full positioned diagnostic, the refused statement and every
        # later one not run, earlier effects kept, cleanup, exit status 1; a
        # typed REPL on a terminal reports it and returns to the prompt.
        d = work / "session"
        d.mkdir()
        lines = 'x := 4\nprint(x)\nnative::touch("early.txt")\nr := run("echo", "hi");native::touch("refused.txt")\n' \
                'print("B")\nnative::touch("late.txt")\n'
        sessions = {}
        for label, extra in (("unset", {}), ("require", {"NIFT_VM": "require"})):
            for name in ("early.txt", "refused.txt", "late.txt"):
                if (d / name).exists():
                    (d / name).unlink()
            proc, rows = run(nift, d, [], extra, stdin=lines)
            sessions[label] = (proc.returncode, proc.stdout, proc.stderr, [r.get("outcome") for r in rows])
            problems = diagnostic_problems(proc.stderr, "<stdin>:4:6", "", session=True)
            check(proc.returncode == 1, f"piped session (NIFT_VM {label}): exit status 1 at the refusal",
                  f"rc={proc.returncode}")
            check(proc.stdout == "4\n" and (d / "early.txt").exists(),
                  f"piped session (NIFT_VM {label}): earlier statements ran and their effects are kept",
                  f"out={proc.stdout!r}")
            check(not (d / "refused.txt").exists(),
                  f"piped session (NIFT_VM {label}): the refused statement did not run")
            check("B" not in proc.stdout and not (d / "late.txt").exists(),
                  f"piped session (NIFT_VM {label}): no later statement ran", f"out={proc.stdout!r}")
            check(not problems and "no part of it ran" not in proc.stderr,
                  f"piped session (NIFT_VM {label}): accurate positioned diagnostic (statement not executed, "
                  "earlier statements may have, session stopped)", "; ".join(problems))
            check([r.get("outcome") for r in rows] == ["vm", "vm", "vm", "require-error"] and
                  rows[3].get("reason") == REASON and (rows[3].get("line"), rows[3].get("column")) == (1, 6) and
                  all(r.get("legacy_executions", 0) == 0 for r in rows),
                  f"piped session (NIFT_VM {label}): VM-owned, never falls back", f"rows={rows}")
        check(sessions["unset"] == sessions["require"], "REPL session: NIFT_VM=require is identical to the default",
              repr(sessions))
        # A multi-line unit: the position is the stream line of the refused call.
        proc, rows = run(nift, d, ["-i"], {}, stdin='x := 4\nif(x > 1) {\n  r := run("echo", "hi")\n}\nprint(x + 1)\n')
        problems = diagnostic_problems(proc.stderr, "<stdin>:3:8", "-i", session=True)
        check(proc.returncode == 1 and proc.stdout == "" and not problems,
              "nift -i on piped stdin: a multi-line refused unit is located at its stream line (<stdin>:3:8)",
              f"rc={proc.returncode} out={proc.stdout!r} {problems}")
        for name in ("early.txt", "refused.txt", "late.txt"):
            if (d / name).exists():
                (d / name).unlink()
        proc, rows = run(nift, d, [], {"NIFT_VM": "0"}, stdin=lines + "print(type(r))\n")
        check(proc.returncode == 0 and proc.stdout == "4\nnull\nB\nobject\n" and rows == [] and
              all((d / n).exists() for n in ("early.txt", "refused.txt", "late.txt")),
              "NIFT_VM=0 piped session is legacy and unchanged (runs every statement, exit 0)",
              f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r}")
        # Cleanup at the stop: a background job and an unsaved managed file of
        # earlier statements are finalized (the job is gone, nothing is saved).
        proc, rows = run(nift, d, [], {}, stdin='command /bin/sleep 30 &\nf := file("held.txt")\nf.open("w")\n'
                                                'f.write("unsaved")\nr := run("echo", "hi")\nf.save()\n')
        job = proc.stdout.split()[1] if proc.stdout.startswith("[1] ") else ""
        time.sleep(0.3)
        alive = False
        if job.isdigit():
            try:
                os.kill(int(job), 0)
                alive = True
            except OSError:
                pass
        check(proc.returncode == 1 and job.isdigit() and not alive and
              (not (d / "held.txt").exists() or (d / "held.txt").read_text() == ""),
              "piped session stop: earlier background job and managed file are cleaned up, nothing later ran",
              f"rc={proc.returncode} out={proc.stdout!r} alive={alive} err={proc.stderr!r}")
        if alive:
            os.kill(int(job), signal.SIGKILL)
        # A typed REPL on a terminal: reported, back at the prompt, usable; the
        # refused statement published nothing (no definition, no effect).
        for name in ("early.txt", "refused.txt"):
            if (d / name).exists():
                (d / name).unlink()
        status, out = run_tty(nift, d, [], {}, ['print("A")', 'native::touch("early.txt")',
                                                'fn(gdef()){return 1};native::touch("refused.txt");r := run("echo")',
                                                "print(5)", "gdef()", "exit"])
        tty_diag = expected_session_diagnostic("<stdin>:3:56", "", note=TTY_SESSION_NOTE)
        check("A\n" in out and (d / "early.txt").exists() and tty_diag in out and
              not (d / "refused.txt").exists() and "\n5\n" in out and "callable:gdef" in out and status == 0 and
              "no part of it ran" not in out,
              "tty REPL: refusal reported, statement not run and left no definition, later statement works",
              f"status={status} out={out!r}")
        (d / "case.f").write_text(UNSUPPORTED)
        proc, rows = run(nift, d, ["-i", "case.f"], {}, stdin="")
        where = f"{(d / 'case.f').resolve().as_posix()}:{REFUSED_AT[0]}:{REFUSED_AT[1]}"
        problems = diagnostic_problems(proc.stderr, where, "-i case.f", session=True)
        check(proc.returncode == 1 and proc.stdout == "" and not (d / "side.txt").exists() and not problems and
              all(r.get("legacy_executions", 0) == 0 and r.get("vm_executions", 0) == 0 for r in rows),
              "-i script: VM-owned session refuses the initial unit before effects, full diagnostic at the file "
              "position", f"rc={proc.returncode} out={proc.stdout!r} {problems} rows={rows}")
        proc, rows = run(nift, d, ["-i", "-e", 'native::touch("t.txt");r := run("echo", "hi")'], {}, stdin="")
        problems = diagnostic_problems(proc.stderr, "<command-line>:1:29",
                                       "-i -e 'native::touch(\"t.txt\");r := run(\"echo\", \"hi\")'", session=True)
        check(proc.returncode == 1 and proc.stdout == "" and not (d / "t.txt").exists() and not problems,
              "-i -e: the initial unit is refused before effects with the full, shell-quoted diagnostic",
              f"rc={proc.returncode} out={proc.stdout!r} {problems}")

        # 8. --platform=<target>: the VM honours the selection (platform()), so
        # every script entry runs on the VM by default with legacy's output, and
        # an unsupported program is refused (never run on legacy) with a legacy
        # instruction that keeps the selector. NIFT_VM=0 is legacy.
        d = work / "platform"
        d.mkdir()
        (d / "p.f").write_text(PLATFORM_SCRIPT)
        (d / "case.f").write_text(UNSUPPORTED)

        def clean():
            for name in ("side.txt", "t.txt", "r.txt"):
                if (d / name).exists():
                    (d / name).unlink()

        def on_vm(args, stdin, extra, out, effect, label, units=1):
            clean()
            proc, rows = run(nift, d, args, extra, stdin=stdin)
            check(proc.returncode == 0 and proc.stdout == out and proc.stderr == "" and (d / effect).exists() and
                  len(rows) == units and all(r.get("outcome") == "vm" and r.get("vm_executions") == 1 and
                                             r.get("legacy_executions") == 0 for r in rows),
                  f"--platform: {label} runs on the VM and reports the selection",
                  f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} rows={rows}")

        for label, extra in (("unset", {}), ("require", {"NIFT_VM": "require"}),
                             ("1", {"NIFT_VM": "1"})):
            on_vm(["--platform=linux", "p.f"], None, extra, "linux\n", "side.txt", f"script file (NIFT_VM {label})")
        on_vm(["p.f", "--platform=windows"], None, {}, "windows\n", "side.txt", "selector after the script path")
        on_vm(["--platform=my.custom", "-e", 'native::touch("t.txt");print(platform())'], None, {}, "my.custom\n", "t.txt",
              "-e program")
        on_vm(["--platform=linux", "-c", 'native::touch("t.txt");print(platform())'], None, {}, "linux\n", "t.txt",
              "-c program")
        on_vm(["--platform=linux", "-"], 'native::touch("t.txt")\nprint(platform())\n', {}, "linux\n", "t.txt",
              "stdin program")
        on_vm(["--platform=linux", "-i", "p.f"], "print(platform())\n", {}, "linux\nlinux\n", "side.txt",
              "-i script and the session after it", units=2)
        on_vm(["--platform=linux", "-i", "-e", 'native::touch("t.txt")'], "print(platform())\n", {}, "linux\n", "t.txt",
              "-i -e program and the session after it", units=2)
        on_vm(["--platform=linux"], 'native::touch("r.txt")\nprint(platform())\n', {}, "linux\n", "r.txt",
              "REPL session", units=2)
        for label, args in (("--platform=native", ["--platform=native", "p.f"]), ("no selector", ["p.f"])):
            on_vm(args, None, {}, "native\n", "side.txt", f"{label} (as before)")
        clean()
        proc, rows = run(nift, d, ["--platform=linux", "p.f"], {"NIFT_VM": "0"})
        check(proc.returncode == 0 and proc.stdout == "linux\n" and proc.stderr == "" and (d / "side.txt").exists()
              and rows == [], "--platform=linux under NIFT_VM=0 runs on legacy with the same output",
              f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} rows={rows}")
        clean()
        proc, rows = run(nift, d, ["--platform=linux"], {"NIFT_VM": "0"}, stdin='native::touch("r.txt")\nprint(platform())\n')
        check(proc.returncode == 0 and proc.stdout == "linux\n" and (d / "r.txt").exists() and rows == [],
              "--platform=linux REPL under NIFT_VM=0 is legacy with the same output",
              f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} rows={rows}")
        # An unsupported program with a platform selection: refused before any
        # effect (never run on legacy), full diagnostic, selector in the hint.
        for args, where, hint, label in (
                (["--platform=linux", "case.f"], f"{(d / 'case.f').resolve().as_posix()}:7:6",
                 "--platform=linux case.f", "script file"),
                (["--platform=windows", "-e", 'native::touch("t.txt")\nr := run("echo", "hi")'], "<command-line>:2:6",
                 "--platform=windows -e 'native::touch(\"t.txt\")\nr := run(\"echo\", \"hi\")'", "-e program"),
                (["--platform=linux", "-"], "<stdin>:2:6", "--platform=linux -", "stdin program")):
            for mode, extra in (("unset", {}), ("require", {"NIFT_VM": "require"})):
                clean()
                proc, rows = run(nift, d, args, extra,
                                 stdin='native::touch("t.txt")\nr := run("echo", "hi")\n' if args[-1] == "-" else None)
                effects = [n for n in ("side.txt", "t.txt") if (d / n).exists()]
                problems = diagnostic_problems(proc.stderr, where, hint)
                check(proc.returncode == 1 and proc.stdout == "" and not effects and not problems and
                      [r.get("outcome") for r in rows] == ["require-error"] and
                      rows[0].get("legacy_executions") == 0 and rows[0].get("vm_executions") == 0,
                      f"--platform: unsupported {label} refused before effects, never legacy (NIFT_VM {mode})",
                      f"rc={proc.returncode} out={proc.stdout!r} effects={effects} {problems} rows={rows}")
        clean()
        proc, rows = run(nift, d, ["--platform=linux"], {}, stdin='x := 1\nr := run("echo", "hi")\nprint(x)\n')
        problems = diagnostic_problems(proc.stderr, "<stdin>:2:6", "--platform=linux", session=True)
        check(proc.returncode == 1 and proc.stdout == "" and not problems and
              [r.get("outcome") for r in rows] == ["vm", "require-error"],
              "--platform REPL: a refused unit gets the full session diagnostic, selector in the hint",
              f"rc={proc.returncode} out={proc.stdout!r} {problems} rows={rows}")
        clean()
        proc, rows = run(nift, d, ["--platform=linux", "case.f"], {"NIFT_VM": "1"})
        check(proc.returncode == 0 and proc.stdout == "before\nobject\n" and (d / "side.txt").exists() and
              len(rows) == 1 and rows[0].get("outcome") == "fallback" and rows[0].get("legacy_executions") == 1,
              "--platform under NIFT_VM=1: an unsupported program still falls back to legacy",
              f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} rows={rows}")

        # 9. A refusal inside an imported module: located in the module, and
        # nothing in the main program or the module ran.
        d = work / "module"
        d.mkdir()
        (d / "m.nft").write_text('native::touch("mod-side.txt")\nfn(go()) {\n  r := run("echo", "hi")\n  return r\n}\n'
                                 'export(go)\n')
        (d / "main.f").write_text('native::touch("main-side.txt")\nprint("before")\nimport("m.nft")\nprint(type(go()))\n')
        for label, extra in (("unset", {}), ("require", {"NIFT_VM": "require"})):
            for name in ("main-side.txt", "mod-side.txt"):
                if (d / name).exists():
                    (d / name).unlink()
            proc, rows = run(nift, d, ["main.f"], extra)
            effects = [n for n in ("main-side.txt", "mod-side.txt") if (d / n).exists()]
            problems = diagnostic_problems(proc.stderr, f"{(d / 'm.nft').resolve().as_posix()}:3:8", "main.f")
            check(proc.returncode == 1 and proc.stdout == "" and not effects and not problems and
                  len(rows) == 1 and rows[0].get("outcome") == "require-error" and rows[0].get("reason") == REASON
                  and rows[0].get("vm_executions") == 0 and rows[0].get("legacy_executions") == 0,
                  f"refusal inside an imported module (NIFT_VM {label}): module position, no effect from main "
                  "or module", f"rc={proc.returncode} out={proc.stdout!r} effects={effects} {problems} rows={rows}")
        proc, rows = run(nift, d, ["main.f"], {"NIFT_VM": "0"})
        check(proc.returncode == 0 and proc.stdout == "before\nobject\n" and (d / "main-side.txt").exists() and
              (d / "mod-side.txt").exists() and rows == [], "the module program runs on legacy under NIFT_VM=0",
              f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} rows={rows}")

        # 10. Owner ruling: only unset, require, 0, 1, classify and classify-all
        # are accepted. Any other value, the empty one included, is rejected on
        # every scripting entry point before anything (imports, ~/.niftrc) runs.
        d = work / "invalid"
        d.mkdir()
        (d / "ok.f").write_text('native::touch("side.txt")\nprint("ran")\n')
        (d / "m.nft").write_text('native::touch("mod-side.txt")\nfn(go()) {\n  return 1\n}\nexport(go)\n')
        (d / "main.f").write_text('import("m.nft")\nnative::touch("side.txt")\nprint(go())\n')
        (d / "home").mkdir()
        (d / "home/.niftrc").write_text('native::touch("rc-side.txt")\n')
        entries = (("script file", ["ok.f"], None), ("module program", ["main.f"], None),
                   ("-e", ["-e", 'native::touch("side.txt")'], None), ("-c", ["-c", 'native::touch("side.txt")'], None),
                   ("stdin", ["-"], 'native::touch("side.txt")\n'), ("eval", ["eval", 'native::touch("side.txt")'], None),
                   ("REPL", [], 'native::touch("side.txt")\n'), ("-i", ["-i"], 'native::touch("side.txt")\n'),
                   ("-i script", ["-i", "ok.f"], ""), ("--platform", ["--platform=linux", "ok.f"], None),
                   ("--platform REPL", ["--platform=linux"], 'native::touch("side.txt")\n'))
        accepted = ("unset", "require", "0 (", "1 (", "classify", "classify-all")
        for value in ("", "off", "false", "banana"):
            shown = f"'{value}'" + (" (empty)" if value == "" else "")
            message = (f"error: invalid NIFT_VM value {shown}: accepted values are unset (the VM is required; the "
                       "default), require, 0 (the legacy interpreter), 1 (the VM with legacy fallback), classify, "
                       "classify-all; nothing was run\n")
            for label, args, stdin in entries:
                for name in ("side.txt", "mod-side.txt", "rc-side.txt"):
                    if (d / name).exists():
                        (d / name).unlink()
                proc, rows = run(nift, d, args, {"NIFT_VM": value, "HOME": str(d / "home")}, stdin=stdin)
                effects = [n for n in ("side.txt", "mod-side.txt", "rc-side.txt") if (d / n).exists()]
                check(proc.returncode == 1 and proc.stdout == "" and proc.stderr == message and
                      all(a in proc.stderr for a in accepted) and not effects and rows == [],
                      f"NIFT_VM={value!r} rejected before anything runs: {label}",
                      f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} effects={effects} rows={rows}")
        # Website builds never consult NIFT_VM: an invalid value changes nothing.
        root = work / "site-invalid"
        site(root)
        proc, rows = run(nift, root, ["build", "--all"], {"NIFT_VM": "banana"})
        check(proc.returncode == 0 and tree(root / "public") == outputs["default"] and
              (root / "hook-marker.txt").exists() and rows == [],
              "website build with NIFT_VM=banana is unchanged (builds never read NIFT_VM)",
              f"rc={proc.returncode} err={proc.stderr!r}")

        # 11. Build hooks: a child `nift child.f` spawned by a hook inherits the
        # VM default and is refused (nonzero, before effects); the website and
        # template rendering of the same build are unchanged.
        root = work / "site-hook"
        site(root)
        (root / "child.f").write_text(UNSUPPORTED)
        (root / "scripts/pre.f").write_text(
            f'try {{ touch("hook-marker.txt") }} catch(e) {{ }}\nr := run({json.dumps(nift)}, "child.f")\n'
            'f := file("child-result.json")\nf.open("w")\nf.write(r.stringify())\nf.save()\nf.close()\n')
        proc, rows = run(nift, root, ["build", "--all"], {})
        result = json.loads((root / "child-result.json").read_text()) if (root / "child-result.json").exists() \
            else {}
        check(proc.returncode == 0 and tree(root / "public") == outputs["default"] and
              result.get("exit_code") == 1 and "not compiled for the VM (callable:run)" in result.get("stderr", "")
              and result.get("stdout") == "" and not (root / "side.txt").exists(),
              "build hook: a child `nift child.f` is refused by the VM default; the site renders unchanged",
              f"rc={proc.returncode} err={proc.stderr!r} child={result}")

        # 12. A refusal inside `$[...]` shell command interpolation: the full
        # session diagnostic (location in the expression, hint, issues URL), the
        # containing command does not run; a non-interactive session stops with
        # exit 1, a tty REPL returns to the prompt.
        d = work / "interpolation"
        d.mkdir()
        proc, rows = run(nift, d, [], {}, stdin='print("A")\ncommand /usr/bin/touch made.txt $[run("echo","x")]\n'
                                                'print("B")\n')
        # Located at the stdin line and the column of `run`, quoted as typed.
        expected = ('error: command interpolation failed: $[run("echo","x")]: <stdin>:2:35: NIFT_VM session '
                    f"refused: {REASON}: {RUN_FEATURE} is not supported by the Runtime 2.0 VM yet\n" +
                    notes("", SESSION_NOTE))
        check(proc.returncode == 1 and proc.stdout == "A\n" and proc.stderr == expected and
              not (d / "made.txt").exists() and "no part of it ran" not in proc.stderr,
              "$[...] refusal (piped): full diagnostic, the command did not run, session stopped with exit 1",
              f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r}")
        status, out = run_tty(nift, d, [], {}, ['print("A")', 'command /usr/bin/touch made.txt $[run("echo","x")]',
                                                'print(7)', "exit"])
        expected = ('error: command interpolation failed: $[run("echo","x")]: <stdin>:2:35: NIFT_VM session '
                    f"refused: {REASON}: {RUN_FEATURE} is not supported by the Runtime 2.0 VM yet\n" +
                    notes("", TTY_SESSION_NOTE))
        check(expected in out and not (d / "made.txt").exists() and "\n7\n" in out and status == 0,
              "$[...] refusal (tty REPL): full diagnostic, the command did not run, back at a usable prompt",
              f"status={status} out={out!r}")

        # 13. A refused `$[...]` abandons the whole command line: no later `;`
        # segment, `&&`/`||` branch or pipeline stage runs (piped: then the
        # session stops with exit 1; tty: back at a usable prompt).
        lines = (("`;`", 'touch s1.txt; echo $[run("x")]; touch s2.txt', ["s1.txt"], ["s2.txt"]),
                 ("`||`", 'echo A $[run("x")] || touch fallback.txt', [], ["fallback.txt"]),
                 ("`&&`", 'touch a1.txt && echo $[run("x")] && touch a2.txt', ["a1.txt"], ["a2.txt"]),
                 ("`;` then `||`", 'echo A $[run("x")] ; touch b.txt || touch c.txt', [], ["b.txt", "c.txt"]),
                 ("pipeline", 'touch q0.txt; /bin/echo hi | /usr/bin/tee p1.txt $[run("x")] | /usr/bin/tee p2.txt',
                  ["q0.txt"], ["p1.txt", "p2.txt"]),
                 ("pipeline first stage", 'echo $[run("x")] | /usr/bin/tee p3.txt', [], ["p3.txt"]))
        names = ("s1.txt", "s2.txt", "fallback.txt", "a1.txt", "a2.txt", "b.txt", "c.txt", "q0.txt", "p1.txt",
                 "p2.txt", "p3.txt", "later.txt", "after.txt")
        for label, line, made, never in lines:
            for name in names:
                if (d / name).exists():
                    (d / name).unlink()
            proc, rows = run(nift, d, [], {}, stdin=line + '\ntouch later.txt\n')
            check(proc.returncode == 1 and all((d / n).exists() for n in made) and
                  not any((d / n).exists() for n in never + ["later.txt"]) and "A" not in proc.stdout and
                  proc.stderr.count("NIFT_VM session refused: callable:run") == 1 and
                  "the session stopped here" in proc.stderr,
                  f"$[...] refusal abandons the whole line ({label}, piped): earlier segments ran, nothing after",
                  f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} "
                  f"files={[n for n in names if (d / n).exists()]}")
            for name in names:
                if (d / name).exists():
                    (d / name).unlink()
            status, out = run_tty(nift, d, [], {}, [line, 'native::touch("after.txt")', "exit"])
            check(status == 0 and all((d / n).exists() for n in made) and (d / "after.txt").exists() and
                  not any((d / n).exists() for n in never) and "FALLBACK" not in out and
                  "the session continues" in out,
                  f"$[...] refusal abandons the whole line ({label}, tty): back at a usable prompt",
                  f"status={status} out={out!r} files={[n for n in names if (d / n).exists()]}")

        # 14. A refusal stop does not silently discard unsaved managed-file
        # changes: they are not committed (the file on disk is unchanged) but
        # reported like at end of input; saved writes are kept; exit 1.
        d = work / "refusal-files"
        d.mkdir()
        (d / "keep.txt").write_text("original")
        proc, rows = run(nift, d, [], {}, stdin='f := file("keep.txt")\nf.open("rw")\nf.write("dirty")\n'
                                                'r := run("echo", "hi")\nprint("LATER")\n')
        left_open = f"error: managed file left open: {(d / 'keep.txt').resolve().as_posix()} (unsaved changes discarded)\n"
        check(proc.returncode == 1 and (d / "keep.txt").read_text() == "original" and left_open in proc.stderr and
              "NIFT_VM session refused: callable:run" in proc.stderr and "LATER" not in proc.stdout and
              proc.stderr.index("NIFT_VM session refused") < proc.stderr.index("managed file left open"),
              "refusal stop: unsaved changes not committed but reported (left open), refusal shown, LATER not "
              "run, exit 1", f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} "
                            f"file={(d / 'keep.txt').read_text()!r}")
        (d / "saved.txt").write_text("")
        proc, rows = run(nift, d, [], {}, stdin='g := file("saved.txt")\ng.open("w")\ng.write("one")\ng.save()\n'
                                                'g.write("two")\nr := run("echo", "hi")\nprint("LATER")\n')
        left_open = f"error: managed file left open: {(d / 'saved.txt').resolve().as_posix()} (unsaved changes discarded)\n"
        check(proc.returncode == 1 and (d / "saved.txt").read_text() == "one" and left_open in proc.stderr and
              "NIFT_VM session refused: callable:run" in proc.stderr and "LATER" not in proc.stdout,
              "refusal stop after save(): the saved write is kept, the later unsaved write is reported, exit 1",
              f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} file={(d / 'saved.txt').read_text()!r}")

        # 15. Owner ruling (startup files on a terminal): ~/.niftrc or the file
        # of `nift -i file` refused while stdin is a tty stops at the refused
        # statement (earlier statements ran once, later ones not), prints the
        # diagnostic with the "interrupted" note and continues at a usable
        # prompt. Piped, the session still stops with exit 1 before running it.
        startup = ('print("{tag}-START")\nnative::touch("{tag}-early.txt")\nfn(greet()){{return "hello"}}\nearly := 5\n'
                   'r := run("echo", "x")\nnative::touch("{tag}-late.txt")\nlate := 6\n')
        typed = ["print(early)", "print(greet())", 'native::touch("typed.txt")', "print(late)", "exit"]
        for label, args, name, tag in (("~/.niftrc", [], ".niftrc", "RC"), ("nift -i file", ["-i", "init.f"],
                                                                            "init.f", "INIT")):
            d = work / f"startup-{tag.lower()}"
            d.mkdir()
            (d / name).write_text(startup.format(tag=tag))
            if tag == "INIT":
                (d / ".niftrc").write_text('print("RC-START")\n')  # -i file never loads ~/.niftrc (order kept)
            path = (d / name).resolve().as_posix()
            status, out = run_tty(nift, d, args, {}, typed)
            diag = (f"error: {path}:5:6: NIFT_VM session refused: {REASON}: {RUN_FEATURE} is not supported by the "
                    "Runtime 2.0 VM yet\n" + STATEMENT_NOTE +
                    f"note: initialisation from {path} was interrupted at line 5 (`r := run(\"echo\", \"x\")`); later "
                    "statements in it were not run\n"
                    "note: the session continues at the prompt\n"
                    f"note: to run it on the legacy interpreter: NIFT_VM=0 nift{' -i init.f' if args else ''}\n"
                    f"note: please report unsupported programs at {ISSUES}\n")
            check(status == 0 and (d / f"{tag}-early.txt").exists() and out.count(f"{tag}-START") == 1 and
                  (tag == "RC" or "RC-START" not in out),
                  f"{label} on a tty: earlier statements ran exactly once, initialisation order kept (no rerun)",
                  f"status={status} out={out!r}")
            after = out.split(diag, 1)[-1]
            check(not (d / f"{tag}-late.txt").exists() and "NIFT_VM session refused: unresolved-name" in after and
                  "\n6\n" not in after,
                  f"{label} on a tty: later statements of the file did not run (`late` is not defined)",
                  f"out={out!r}")
            check(diag in out and "no part of it ran" not in out,
                  f"{label} on a tty: full diagnostic with the interrupted note", f"out={out!r}")
            check(diag in out and out.split(diag, 1)[1].startswith("5\nhello\n") and (d / "typed.txt").exists(),
                  f"{label} on a tty: usable prompt - a typed statement works and uses a definition made before "
                  "the refusal", f"out={out!r}")
            for f in (f"{tag}-early.txt", f"{tag}-late.txt", "typed.txt"):
                if (d / f).exists():
                    (d / f).unlink()
            proc, rows = run(nift, d, args, {"HOME": str(d)}, stdin="\n".join(typed) + "\n")
            check(proc.returncode == 1 and "the session stopped here" in proc.stderr and
                  "initialisation from" not in proc.stderr and not (d / "typed.txt").exists() and
                  not (d / f"{tag}-late.txt").exists() and "5" not in proc.stdout.split(),
                  f"{label} piped: the session still stops with exit 1 (unchanged)",
                  f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r}")

        # 16. A refusal while running a shell-form segment (a user function or
        # builtin called in command form) abandons the whole logical line, as a
        # refused `$[...]` does: no later `||`/`;`/`&&` segment runs.
        d = work / "shell-form"
        d.mkdir()
        cases = (("user function, `||`", ['fn(f(a)) { return a.size() }', 'f 3 || touch FALLBACK.txt'],
                  ["FALLBACK.txt"]),
                 ("user function, `;`", ['fn(f(a)) { return a.size() }', 'f 3; touch AFTER.txt'], ["AFTER.txt"]),
                 ("user function, `&&` after success", ['fn(f(a)) { return a.size() }',
                                                         'touch OK.txt && f 3 && touch AND.txt; touch SEMI.txt'],
                  ["AND.txt", "SEMI.txt"]),
                 ("builtin, `||`", ['pwd a b || touch FALLBACK2.txt'], ["FALLBACK2.txt"]))
        witnesses = ("FALLBACK.txt", "AFTER.txt", "OK.txt", "AND.txt", "SEMI.txt", "FALLBACK2.txt", "later.txt",
                     "typed.txt")
        for label, lines, never in cases:
            for name in witnesses:
                if (d / name).exists():
                    (d / name).unlink()
            proc, rows = run(nift, d, [], {}, stdin="\n".join(lines + ['native::touch("later.txt")']) + "\n")
            present = [n for n in witnesses if (d / n).exists()]
            check(proc.returncode == 1 and not any(n in present for n in never + ["later.txt"]) and
                  "NIFT_VM session refused" in proc.stderr and "the session stopped here" in proc.stderr and
                  ("OK.txt" in present) == ("OK.txt" in lines[-1]),
                  f"shell-form refusal abandons the line (piped, {label}): no later segment or input ran",
                  f"rc={proc.returncode} files={present} err={proc.stderr!r}")
            for name in witnesses:
                if (d / name).exists():
                    (d / name).unlink()
            status, out = run_tty(nift, d, [], {}, lines + ['native::touch("typed.txt")', "exit"])
            present = [n for n in witnesses if (d / n).exists()]
            check(status == 0 and not any(n in present for n in never) and "typed.txt" in present and
                  "the session continues" in out,
                  f"shell-form refusal abandons the line (tty, {label}): nothing later on the line, usable prompt",
                  f"status={status} files={present} out={out!r}")

        # 17. A refusal inside a function defined by earlier input is not given
        # the current unit's stdin line (the session keeps no line for the
        # defining unit): a truthful location instead.
        d = work / "earlier-unit"
        d.mkdir()
        source = 'fn(f(a)) {\n  print("in f")\n  return a.size()\n}\nprint("x")\nprint(f("3"))\n'
        where = "<stdin>: in earlier input, at line 3, column 10 of the unit that defined it"
        proc, rows = run(nift, d, [], {}, stdin=source)
        check(proc.returncode == 1 and proc.stdout == "x\n" and
              proc.stderr.startswith(f"error: {where}: NIFT_VM session refused: type:length: ") and
              "<stdin>:8:" not in proc.stderr,
              "refusal inside an earlier unit's function: truthful location, no current-unit offset (piped)",
              f"rc={proc.returncode} err={proc.stderr!r}")
        status, out = run_tty(nift, d, [], {}, source.rstrip("\n").split("\n") + ["exit"])
        check(status == 0 and f"error: {where}: NIFT_VM session refused: type:length: " in out and
              "<stdin>:8:" not in out,
              "refusal inside an earlier unit's function: truthful location (tty)", f"out={out!r}")
        proc, rows = run(nift, d, [], {}, stdin='fn(f(a)) { return a.size() }\nf 3\n')
        check(proc.returncode == 1 and "<stdin>: in earlier input, at line 1, column 19 " in proc.stderr and
              "<stdin>:2:19" not in proc.stderr,
              "shell-form call into an earlier unit: truthful location", f"err={proc.stderr!r}")

        # 17b. `$[...]` uses the same location logic: a refusal inside a function
        # from earlier stdin input gets the labelled earlier-input location (not
        # an offset into the shell line); one from ~/.niftrc keeps its path.
        d = work / "interp-earlier"
        d.mkdir()
        line = 'echo $[f(3)+0+0+0+0+0+0+0+0+0+0+0+0]'
        earlier = "<stdin>: in earlier input, at line 1, column 19 of the unit that defined it"
        proc, rows = run(nift, d, [], {}, stdin='fn(f(a)) { return a.size() }\n' + line + '\n')
        check(proc.returncode == 1 and f": {earlier}: NIFT_VM session refused: type:length" in proc.stderr and
              "<stdin>:2:" not in proc.stderr,
              "$[...] calling an earlier-stdin function: labelled earlier-input location (piped)",
              f"err={proc.stderr!r}")
        status, out = run_tty(nift, d, [], {}, ['fn(f(a)) { return a.size() }', line, "exit"])
        check(f": {earlier}: NIFT_VM session refused: type:length" in out and "<stdin>:2:" not in out,
              "$[...] calling an earlier-stdin function: labelled earlier-input location (tty)", f"out={out!r}")
        d = work / "interp-rc"
        d.mkdir()
        (d / ".niftrc").write_text('x := 1\n\n\nfn(f(a)) {\n  return a.size()\n}\n')
        rc = (d / ".niftrc").resolve().as_posix()
        proc, rows = run(nift, d, [], {"HOME": str(d)}, stdin='echo $[f(3)]\n')
        check(proc.returncode == 1 and f": {rc}:5:10: NIFT_VM session refused: type:length" in proc.stderr and
              "<stdin>:1:" not in proc.stderr,
              "$[...] calling a ~/.niftrc function: the .niftrc position (piped)", f"err={proc.stderr!r}")
        status, out = run_tty(nift, d, [], {}, ['echo $[f(3)]', "exit"])
        check(f": {rc}:5:10: NIFT_VM session refused: type:length" in out and "<stdin>:1:" not in out,
              "$[...] calling a ~/.niftrc function: the .niftrc position (tty)", f"out={out!r}")

        # 17c. `-i -e` on a tty: the initial unit is not a startup file; a refusal
        # stops the session with exit 1 (documented in REFUSAL_MATRIX.md).
        d = work / "ie-tty"
        d.mkdir()
        status, out = run_tty(nift, d, ["-i", "-e", 'native::touch("e.txt");r := run("echo", "hi")'], {},
                              ['native::touch("typed.txt")', "exit"])
        check(status == 1 and "the session stopped here" in out and not (d / "e.txt").exists() and
              not (d / "typed.txt").exists(),
              "-i -e on a tty: refused initial unit stops the session with exit 1, nothing typed runs",
              f"status={status} out={out!r}")

        # 18. Startup files on a terminal run statement by statement through the
        # session, like typed input (owner ruling): the first statement the VM
        # refuses and everything after it never run; each earlier statement
        # runs exactly once; the refusal names the refused position and the
        # interrupted statement. Piped: the whole file is refused, nothing runs.
        init = ('print("s1")\nn := 0\nn += 1\nfn(g(a)) { return a.size() }\nprint("s3")\nnative::touch("s3.txt")\n'
                'g(3)\nprint("s5")\nnative::touch("s5.txt")\n')
        for label, args, name in (("~/.niftrc", [], ".niftrc"), ("nift -i file", ["-i", "init.f"], "init.f")):
            d = work / f"incremental-{name.strip('.').replace('.', '-')}"
            d.mkdir()
            (d / name).write_text(init)
            path = (d / name).resolve().as_posix()
            status, out = run_tty(nift, d, args, {}, ["print(n)", "print(7)", "exit"])
            diag = (f"error: {path}:4:19: NIFT_VM session refused: type:length: ")
            note = (f"note: initialisation from {path} was interrupted at line 7 (`g(3)`); later statements in it "
                    "were not run\nnote: the session continues at the prompt\n")
            path_control(work, out, "s1", "s1")
            check(status == 0 and out.startswith("s1\ns3\n") and program_lines(out).count("s1") == 1 and
                  out.count("s3\n") == 1 and (d / "s3.txt").exists(),
                  f"{label} on a tty: statements before the refused one ran, each exactly once", f"out={out!r}")
            path_control(work, out, "s5", "s5")
            check("s5" not in program_lines(out) and not (d / "s5.txt").exists(),
                  f"{label} on a tty: the refused statement and everything after it never ran", f"out={out!r}")
            check(diag in out and note in out and out.count("NIFT_VM session refused") == 1,
                  f"{label} on a tty: the refused position (in g) and the interrupted statement (line 7) reported",
                  f"out={out!r}")
            check(out.split(note, 1)[-1].split("\n", 4)[2:4] == ["1", "7"],
                  f"{label} on a tty: usable prompt, definitions from before the refusal usable (n == 1: once)",
                  f"out={out!r}")
            for f in ("s3.txt", "s5.txt"):
                if (d / f).exists():
                    (d / f).unlink()
            proc, rows = run(nift, d, args, {"HOME": str(d)}, stdin="print(n)\n")
            check(proc.returncode == 1 and proc.stdout == "" and not (d / "s3.txt").exists() and
                  f"error: {path}:4:19: NIFT_VM session refused" in proc.stderr and
                  "the session stopped here" in proc.stderr,
                  f"{label} piped: the whole file is refused before anything runs, exit 1",
                  f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r}")

        # 18b. A startup file on a terminal is split by the Nift parser's own
        # top-level statement boundaries: multi-line definitions and blocks are
        # one statement each (a physical line is not a statement).
        multi = ('fn(greet(name)) {\n  print("hi " + name)\n  return 1\n}\nif(1 < 2) {\n  print("block")\n}\n'
                 'greet("a")\nr := run("echo", "x")\nprint("later")\n')
        for label, args, name in (("~/.niftrc", [], ".niftrc"), ("nift -i file", ["-i", "init.f"], "init.f")):
            d = work / f"multiline-{name.strip('.').replace('.', '-')}"
            d.mkdir()
            (d / name).write_text(multi)
            path = (d / name).resolve().as_posix()
            status, out = run_tty(nift, d, args, {}, ['greet("b")', "exit"])
            head = f"error: {path}:9:6: NIFT_VM session refused: callable:run"
            # No value echo: only what the statements print.
            path_control(work, out, "block", "block")
            check(status == 0 and out.startswith("block\nhi a\nerror: ") and program_lines(out).count("block") == 1 and
                  out.count("hi a") == 1 and head in out and "\nlater\n" not in out and
                  f"initialisation from {path} was interrupted at line 9" in out and
                  "hi b\n" in out.split(head, 1)[-1],
                  f"{label} on a tty: multi-line fn and if-block run once in order, refusal at 9:6, no later "
                  "print, the multi-line function callable from the prompt", f"status={status} out={out!r}")

        # 18c. Startup files on a terminal are Nift only (owner ruling): no
        # shell-line dispatch, no value echo, comments / if-else chains / loops /
        # `;` / bare expressions as in legacy, a syntax error stops the file
        # before the statement that contains it (earlier statements ran once),
        # and `exit` is a Nift statement. Piped, the whole file is refused.
        startup_cases = (
            ("comments with shell commands",
             'x := 5\n/*\ntouch("COMMENTED.txt")\nls\nrm -rf nothing\n*/\n// touch("SLASH.txt")\nprint("y=" + x)\n',
             "y=5\n", [], ["COMMENTED.txt", "SLASH.txt"], None),
            ("if/else split across lines", 'v := 3\nif(v > 2) {\n  print("big")\n}\nelse {\n  print("small")\n}\n',
             "big\n", [], [], None),
            ("else-if chain and a loop", 'v := 0\nn := 0\nwhile(n < 3) {\n  n += 1\n}\nif(n == 1) { print("one") }\n'
             'else if(n == 3) { print("three") }\nelse { print("other") }\n', "three\n", [], [], None),
            ("semicolon statements on one line", 'a := 1; b := 2; print(a + b)\nprint("end")\n', "3\nend\n", [], [],
             None),
            ("no value echo", 'fn(two()) { return 2 }\ntwo()\nq := 7\nq += 1\nif(q > 1) { print("in") }\n'
             'print("only")\n', "in\nonly\n", [], [], None),
            ("malformed statement after valid ones",
             'print("v1")\nnative::touch("v1.txt")\nz := (1 +\nprint("v2")\nnative::touch("v2.txt")\n', "v1\n", ["v1.txt"],
             ["v2.txt"], ":3:"),
            ("unterminated final block", 'print("u1")\nfn(f()) {\n  print("x")\n', "u1\n", [], [], ":2:"),
        )
        for label, body, printed, made, never, syntax_at in startup_cases:
            for where_label, args, name in (("~/.niftrc", [], ".niftrc"), ("nift -i file", ["-i", "init.f"],
                                                                         "init.f")):
                d = work / f"sc-{len(label)}-{name.strip('.').replace('.', '-')}-{abs(hash(label)) % 10000}"
                d.mkdir()
                (d / name).write_text(body)
                status, out = run_tty(nift, d, args, {}, ['print("PROMPT")', "exit"])
                problems = []
                if status != 0: problems.append(f"status {status}")
                if not out.startswith(printed): problems.append("printed output")
                if "PROMPT\n" not in out: problems.append("prompt not usable")
                if "command not found" in out or "... " in out: problems.append("shell dispatch / continuation")
                if any(not (d / n).exists() for n in made): problems.append("earlier effect missing")
                if any((d / n).exists() for n in never): problems.append("a later or commented effect happened")
                if syntax_at:
                    path = (d / name).resolve().as_posix()
                    if f"error: {path}{syntax_at}" not in out or "was interrupted at line" not in out or \
                            "the session continues at the prompt" not in out:
                        problems.append("syntax error not reported at its position with the interruption notes")
                elif "error:" in out:
                    problems.append("unexpected error")
                check(not problems, f"{where_label} on a tty, {label}: " + ("legacy-shaped run" if not syntax_at else
                                                                               "earlier statements ran, then the "
                                                                               "syntax error stopped the file"),
                      f"{problems} out={out!r}")
                for n in made + never:
                    if (d / n).exists():
                        (d / n).unlink()
                if syntax_at:
                    proc, rows = run(nift, d, args, {"HOME": str(d)}, stdin='print("PROMPT")\n')
                    check(proc.returncode == 1 and "PROMPT" not in proc.stdout and
                          not any((d / n).exists() for n in made + never) and proc.stdout == "",
                          f"{where_label} piped, {label}: the whole file is refused before anything runs, exit 1",
                          f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r}")
        # A bare expression statement is a Nift statement (never a shell command):
        # the VM refuses it as it does in a script file, so the file is
        # interrupted there; nothing is reported as "command not found".
        for where_label, args, name in (("~/.niftrc", [], ".niftrc"), ("nift -i file", ["-i", "init.f"], "init.f")):
            d = work / f"bare-{name.strip('.').replace('.', '-')}"
            d.mkdir()
            (d / name).write_text('print("b1")\n1+1\nprint("b2")\n')
            path = (d / name).resolve().as_posix()
            status, out = run_tty(nift, d, args, {}, ['print("PROMPT")', "exit"])
            path_control(work, out, "b2", "b2")
            check(status == 0 and out.startswith("b1\nerror: ") and
                  f"error: {path}:2:1: NIFT_VM session refused: expression-statement" in out and "b2" not in program_lines(out) and
                  "command not found" not in out and "was interrupted at line 2" in out and "PROMPT\n" in out,
                  f"{where_label} on a tty: a bare expression is a refused Nift statement (as in a script file)",
                  f"status={status} out={out!r}")
        # `exit` in a startup file is a Nift statement, never the shell's exit:
        # legacy fails on it at startup (unknown value, exit 1); the VM refuses
        # it before it runs, so on a tty the file is interrupted there and the
        # session continues (approved by the owner, 2026-10-31, D-061); piped,
        # the file is refused (exit 1) as in legacy.
        for where_label, args, name in (("~/.niftrc", [], ".niftrc"), ("nift -i file", ["-i", "init.f"], "init.f")):
            d = work / f"exit-{name.strip('.').replace('.', '-')}"
            d.mkdir()
            (d / name).write_text('print("e1")\nnative::touch("e1.txt")\nexit\nprint("e2")\nnative::touch("e2.txt")\n')
            path = (d / name).resolve().as_posix()
            status, out = run_tty(nift, d, args, {}, ['print("PROMPT")', 'native::touch("typed.txt")', "exit"])
            path_control(work, out, "e2", "e2")
            check(status == 0 and out.startswith("e1\nerror: ") and f"error: {path}:3:1: NIFT_VM session refused" in out
                  and f"initialisation from {path} was interrupted at line 3 (`exit`); later statements in it were "
                  "not run" in out and "e2" not in program_lines(out) and not (d / "e2.txt").exists() and (d / "e1.txt").exists() and
                  "PROMPT\n" in out and (d / "typed.txt").exists(),
                  f"{where_label} on a tty: `exit` refused (named in the note), the earlier effect kept, nothing later "
                  "ran, prompt usable (D-061)", f"status={status} out={out!r}")
            for f in ("e1.txt", "e2.txt", "typed.txt"):
                if (d / f).exists():
                    (d / f).unlink()
            status, out = run_tty(nift, d, args, {"NIFT_VM": "0"}, ['print("PROMPT")', "exit"])
            path_control(work, out, "e2", "e2")
            check(status == 1 and out.startswith("e1\n") and "unknown value or malformed expression: exit" in out and
                  "e2" not in program_lines(out), f"{where_label} on a tty under NIFT_VM=0: legacy treats `exit` as an error (exit 1)",
                  f"status={status} out={out!r}")
            for f in ("e1.txt", "e2.txt"):
                if (d / f).exists():
                    (d / f).unlink()
            proc, rows = run(nift, d, args, {"HOME": str(d)}, stdin='print("PROMPT")\n')
            check(proc.returncode == 1 and proc.stdout == "", f"{where_label} piped: `exit` file refused, exit 1",
                  f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r}")
        # Owner witness: a definition is admitted on its own, never judged by a
        # later call; the call is refused with a truthful location.
        for where_label, args, name in (("~/.niftrc", [], ".niftrc"), ("nift -i file", ["-i", "init.f"], "init.f")):
            d = work / f"defcall-{name.strip('.').replace('.', '-')}"
            d.mkdir()
            (d / name).write_text('print("before")\nfn(f(x)) {\n  return x.size()\n}\nprint("middle")\n'
                                  'print(f(3))\nprint("after")\n')
            path = (d / name).resolve().as_posix()
            status, out = run_tty(nift, d, args, {}, ['print("PROMPT")', "exit"])
            path_control(work, out, "before", "before")
            path_control(work, out, "middle", "middle")
            check(status == 0 and out.startswith("before\nmiddle\nerror: ") and program_lines(out).count("before") == 1 and
                  program_lines(out).count("middle") == 1 and f"error: {path}:3:10: NIFT_VM session refused: type:length" in out and
                  "interrupted at line 6" in out and "\nafter" not in out and "PROMPT\n" in out,
                  f"{where_label} on a tty: definition admitted on its own; the call refused at f's size() "
                  "(3:10), interrupted at line 6; `after` never runs", f"status={status} out={out!r}")

        # 18d. A refusal without a source position is never given an invented
        # 1:1: the refused input's start line, labelled as such.
        d = work / "nopos"
        d.mkdir()
        proc, rows = run(nift, d, [], {}, stdin='print(1)\nprint(2)\necho $[if(1){2}]\n')
        # The Opaque region's recorded position: `if` at 3:8 (never 1:1).
        check(proc.returncode == 1 and "$[if(1){2}]: <stdin>:3:8: NIFT_VM session refused: session:opaque" in
              proc.stderr and "<stdin>:1:1" not in proc.stderr,
              "Opaque `$[...]` refusal: the Opaque region's own position (3:8), never 1:1", f"err={proc.stderr!r}")
        proc, rows = run(nift, d, [], {}, stdin='print(1)\n  x := `a`\n')
        check(proc.returncode == 1 and "<stdin>:2:3: NIFT_VM session refused: session:opaque" in proc.stderr,
              "an Opaque statement is refused at the Opaque region's own position (not 1:1)", f"err={proc.stderr!r}")
        for where_label, args, name in (("~/.niftrc", [], ".niftrc"), ("nift -i file", ["-i", "init.f"], "init.f")):
            dd = work / f"nopos-{name.strip('.').replace('.', '-')}"
            dd.mkdir()
            (dd / name).write_text('print("p1")\n\n\n\n  x := `a`\nprint("p2")\n')
            path = (dd / name).resolve().as_posix()
            status, out = run_tty(nift, dd, args, {}, ['print("PROMPT")', "exit"])
            path_control(work, out, "p2", "p2")
            check(f"error: {path}:5:3: NIFT_VM session refused: session:opaque" in out and f"{path}:1:1" not in out
                  and "interrupted at line 5" in out and "p2" not in program_lines(out),
                  f"{where_label} on a tty: an Opaque statement at 5:3 is located there (not 1:1)", f"out={out!r}")
            proc, rows = run(nift, dd, args, {"HOME": str(dd)}, stdin="")
            check(proc.returncode == 1 and f"{path}:1:1" not in proc.stderr,
                  f"{where_label} piped: no invented 1:1 for the whole-file refusal", f"err={proc.stderr!r}")

        # 18e. A refusal whose position the compiler did not record (a limit:
        # `limit:too many registers` from a 33000-element literal) is never
        # printed as an exact position: the flag, not the geometry, decides.
        d = work / "unpositioned"
        d.mkdir()
        big = "x := [" + ",".join(["1"] * 33000) + "]\n"
        (d / "big1.f").write_text(big + 'print("after")\n')
        nopos = "(the VM gave no exact position)"
        proc, rows = run(nift, d, ["big1.f"], {})
        check(proc.returncode == 1 and proc.stdout == "" and
              proc.stderr.startswith(f"error: {(d / 'big1.f').resolve().as_posix()} {nopos}: not compiled for the VM "
                                     "(limit:too many registers)") and "big1.f:1:1" not in proc.stderr,
              "unpositioned refusal of a file: no exact position claimed", f"err={proc.stderr[:300]!r}")
        proc, rows = run(nift, d, ["-e", big + 'print("after")'], {})
        check(proc.returncode == 1 and proc.stderr.startswith(f"error: <command-line> {nopos}: not compiled") and
              "<command-line>:1:1" not in proc.stderr,
              "unpositioned refusal of an -e program: no exact position claimed", f"err={proc.stderr[:300]!r}")
        proc, rows = run(nift, d, [], {}, stdin='print(1)\nfn(g()) { return 1 }\n\n\n' + big + 'print("after")\n')
        check(proc.returncode == 1 and proc.stdout == "1\n" and
              f"error: <stdin>:5 (the start line of the refused input; the VM gave no exact position): NIFT_VM "
              "session refused: limit:too many registers" in proc.stderr and "<stdin>:5:1" not in proc.stderr,
              "unpositioned session refusal (piped): the input's start line, labelled", f"err={proc.stderr[:300]!r}")
        (d / "s.f").write_text(big + 'print("after")\n')
        status, out = run_tty(nift, d, ["-i", "s.f"], {}, ['print("PROMPT")', "exit"])
        sf = (d / "s.f").resolve().as_posix()
        check(status == 0 and f"error: {sf}:1 (the start line of the refused input; the VM gave no exact position)"
              in out and f"{sf}:1:1" not in out and "\nafter" not in out and "PROMPT\n" in out,
              "unpositioned refusal in a tty -i file: the statement's start line, labelled; prompt usable",
              f"status={status} out={out[:400]!r}")

        # 18f. A syntax error inside a top-level statement interrupts the file at
        # that statement's start line; the error keeps its own position.
        for where_label, args, name in (("~/.niftrc", [], ".niftrc"), ("nift -i file", ["-i", "init.f"], "init.f")):
            d = work / f"syntax-stmt-{name.strip('.').replace('.', '-')}"
            d.mkdir()
            (d / name).write_text('print("s0")\nif(true) {\n  native::touch("IN")\n  print("inside")\n  z := (1 +\n}\n')
            path = (d / name).resolve().as_posix()
            status, out = run_tty(nift, d, args, {}, ['print("PROMPT")', "exit"])
            path_control(work, out, "inside", "inside")
            check(status == 0 and out.startswith("s0\nerror: ") and f"error: {path}:5:" in out and
                  "was interrupted at line 2;" in out and not (d / "IN").exists() and "inside" not in program_lines(out) and
                  "PROMPT\n" in out,
                  f"{where_label} on a tty: syntax error at 5:x interrupts at its statement's start (line 2)",
                  f"status={status} out={out!r}")

        # 18g. has_position both ways (owner): a genuine refusal at line 1,
        # column 1 is reported as the exact `:1:1`; one with no recorded
        # position says so - for a file, an -e program, a tty `-i` file and a
        # persistent tty session (a refusal inside a function defined earlier
        # keeps its recorded position across units).
        d = work / "has-position"
        d.mkdir()
        (d / "first.f").write_text('run("x")\nprint("after")\n')
        first = (d / "first.f").resolve().as_posix()
        proc, rows = run(nift, d, ["first.f"], {})
        check(proc.returncode == 1 and proc.stderr.startswith(f"error: {first}:1:1: not compiled for the VM "
                                                              "(callable:run)") and "no exact position" not in
              proc.stderr and rows and (rows[0].get("line"), rows[0].get("column")) == (1, 1),
              "has_position: a genuine 1:1 refusal of a file is reported exactly as :1:1", f"err={proc.stderr!r}")
        proc, rows = run(nift, d, ["-e", 'run("x")'], {})
        check(proc.returncode == 1 and proc.stderr.startswith("error: <command-line>:1:1: not compiled for the VM") and
              "no exact position" not in proc.stderr,
              "has_position: a genuine 1:1 refusal of an -e program is reported exactly as :1:1", f"err={proc.stderr!r}")
        status, out = run_tty(nift, d, ["-i", "first.f"], {}, ['print("PROMPT")', "exit"])
        check(f"error: {first}:1:1: NIFT_VM session refused: callable:run" in out and "no exact position" not in out
              and "PROMPT\n" in out and "\nafter" not in out,
              "has_position: a genuine 1:1 refusal in a tty -i file is reported exactly as :1:1", f"out={out!r}")
        # (session:coroutine is a whole-unit refusal: no recorded position.)
        status, out = run_tty(nift, d, [], {}, ['run("x")', 'fn(f(a)) { return a.size() }', "f(3)",
                                                "fn[async](g()){return 6}", 'print("PROMPT")', "exit"])
        check("error: <stdin>:1:1: NIFT_VM session refused: callable:run" in out and
              "<stdin>: in earlier input, at line 1, column 19 of the unit that defined it" in out and
              "error: <stdin>:4 (the start line of the refused input; the VM gave no exact position): NIFT_VM "
              "session refused: session:coroutine" in out and "<stdin>:4:1" not in out and "PROMPT\n" in out,
              "has_position in a persistent tty session: exact 1:1, a recorded position inside an earlier unit, and "
              "an unpositioned limit labelled", f"out={out[:1200]!r}")

        # 18h. Whole-program refusals of linked units (Review 8, release
        # obligations 1-3): a refusal with no recorded position says so; one
        # with a real position names it - never an invented `:1:1`. Each
        # entry point that links modules (a file, -e, `-`), and the genuine
        # 1:1 of each path is still reported exactly.
        nopos = "(the VM gave no exact position)"

        def refused(proc, where, reason, label, rows=None):
            ok = (proc.returncode == 1 and proc.stdout == "" and
                  proc.stderr.startswith(f"error: {where}: not compiled for the VM ({reason}): "))
            if rows is not None:
                ok = ok and [(r.get("outcome"), r.get("reason"), r.get("vm_executions")) for r in rows] == \
                    [("require-error", reason, 0)]
            check(ok, label, f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr[:400]!r} rows={rows}")

        d = work / "positions-coro-module"
        d.mkdir()
        (d / "lib.f").write_text("x := 1\n")
        coro = 'fn[async](g()) { return 6 }\nf := g()\nprint(await f)\n'
        # Since D-064 (weekend coro-module; D-063 on weekend/r2) the whole-unit coro-module refusal
        # exists only under the cooperative model; the parallel default runs it.
        coop = {"NIFT_VM_COOPERATIVE_THREADS": "1"}
        for name, text in (("main.f", '// header\n\nimport("lib.f")\n' + coro), ("first.f", 'import("lib.f")\n' + coro)):
            (d / name).write_text(text)
            path = (d / name).resolve().as_posix()
            proc, rows = run(nift, d, [name], coop)
            refused(proc, f"{path} {nopos}", "coro-module", f"coro-module ({name}): no exact position claimed", rows)
            check(":1:1" not in proc.stderr and f"{path}:" not in proc.stderr,
                  f"coro-module ({name}): no invented position", f"err={proc.stderr!r}")
            proc, rows = run(nift, d, ["-e", text], coop)
            refused(proc, f"<command-line> {nopos}", "coro-module", f"coro-module ({name} as -e): no exact position")
            proc, rows = run(nift, d, ["-"], coop, stdin=text)
            refused(proc, f"<stdin> {nopos}", "coro-module", f"coro-module ({name} as `-`): no exact position")
            proc, rows = run(nift, d, [name], {})
            check(proc.returncode == 0 and proc.stdout == "6\n" and proc.stderr == "",
                  f"coro-module ({name}): the parallel default runs the linked coroutine unit (D-064)",
                  f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr[:300]!r}")
        # The genuine 1:1 of a coroutine refusal (an async function's value at 1:1).
        (d / "genuine.f").write_text('g\n' + coro)
        proc, rows = run(nift, d, ["genuine.f"], {})
        refused(proc, f"{(d / 'genuine.f').resolve().as_posix()}:1:1", "coro-async-value",
                "coroutine refusal at a genuine 1:1 is reported exactly as :1:1", rows)
        check(nopos not in proc.stderr, "coroutine refusal at a genuine 1:1: no 'no exact position' label")
        proc, rows = run(nift, d, ["-e", 'g\n' + coro], {})
        refused(proc, "<command-line>:1:1", "coro-async-value", "coroutine refusal at a genuine 1:1 (-e): exact :1:1")

        d = work / "positions-name-clash"
        d.mkdir()
        (d / "lib.f").write_text("// a comment\n\nfn(f()) { return 1 }\n")
        (d / "main.f").write_text('console("hi")\nimport("lib.f")\nfn(f()) { return 2 }\n')
        lib = (d / "lib.f").resolve().as_posix()
        for args, stdin, label in ((["main.f"], None, "file"),
                                   (["-e", (d / "main.f").read_text()], None, "-e"),
                                   (["-"], (d / "main.f").read_text(), "`-`")):
            proc, rows = run(nift, d, args, {}, stdin=stdin)
            refused(proc, f"{lib}:3:4", "import-name-clash",
                    f"import-name-clash ({label}): the clashing definition's real position (lib.f:3:4)",
                    rows if label == "file" else None)
            check(f"{lib}:1:1" not in proc.stderr and "hi" not in proc.stdout,
                  f"import-name-clash ({label}): no invented lib.f:1:1, nothing ran", f"err={proc.stderr!r}")
        (d / "lib1.f").write_text("f := 1\n")
        (d / "one.f").write_text('import("lib1.f")\nf := 2\n')
        proc, rows = run(nift, d, ["one.f"], {})
        refused(proc, f"{(d / 'lib1.f').resolve().as_posix()}:1:1", "import-name-clash",
                "import-name-clash at a genuine 1:1 (the definition opens the module) is reported exactly", rows)
        check(nopos not in proc.stderr, "import-name-clash at a genuine 1:1: no 'no exact position' label")

        d = work / "positions-import-depth"
        d.mkdir()
        for k in range(71):
            (d / f"m{k}.f").write_text(f'y{k} := 1\n  import("m{k + 1}.f")\n')
            (d / f"n{k}.f").write_text(f'import("n{k + 1}.f")\nz{k} := 1\n')
        (d / "m71.f").write_text("y71 := 1\n")
        (d / "n71.f").write_text("z71 := 1\n")
        # (An -e or `-` entry that imports m0.f is itself one level: m62.f's import is beyond the limit.)
        for args, stdin, label, at in ((["m0.f"], None, "file", "m63.f"), (["-e", 'import("m0.f")'], None, "-e", "m62.f"),
                                       (["-"], 'import("m0.f")\n', "`-`", "m62.f")):
            proc, rows = run(nift, d, args, {}, stdin=stdin)
            refused(proc, f"{(d / at).resolve().as_posix()}:2:3", "import-limit",
                    f"import depth limit ({label}): the import beyond the limit at its real position ({at}:2:3)",
                    rows if label == "file" else None)
            check(":1:1" not in proc.stderr, f"import depth limit ({label}): no invented :1:1",
                  f"err={proc.stderr!r}")
        proc, rows = run(nift, d, ["n0.f"], {})
        refused(proc, f"{(d / 'n63.f').resolve().as_posix()}:1:1", "import-limit",
                "import depth limit at a genuine 1:1 (the import opens the module) is reported exactly", rows)
        check(nopos not in proc.stderr, "import depth limit at a genuine 1:1: no 'no exact position' label")

        # 19. Refusal-matrix gaps (tests/runtime2/vm_default/REFUSAL_MATRIX.md):
        # a plain `-` stdin program; `$[...]` in a redirected and in a background
        # (`&`) segment; NIFT_VM=1 sessions stop like the default.
        d = work / "matrix"
        d.mkdir()
        proc, rows = run(nift, d, ["-"], {}, stdin='native::touch("t.txt")\nr := run("echo", "hi")\n')
        problems = diagnostic_problems(proc.stderr, "<stdin>:2:6", "-")
        check(proc.returncode == 1 and proc.stdout == "" and not (d / "t.txt").exists() and not problems and
              [r.get("outcome") for r in rows] == ["require-error"],
              "`-` stdin program: refused as a whole before effects, exit 1", f"rc={proc.returncode} {problems}")
        for label, line, never in (("redirect", '/bin/echo $[run("x")] > red.txt; touch after.txt',
                                    ["red.txt", "after.txt"]),
                                   ("background `&`", '/usr/bin/touch bg.txt $[run("x")] &', ["bg.txt"])):
            for name in never + ["later.txt", "typed.txt"]:
                if (d / name).exists():
                    (d / name).unlink()
            proc, rows = run(nift, d, [], {}, stdin=line + '\nnative::touch("later.txt")\n')
            time.sleep(0.2)
            present = [n for n in never + ["later.txt"] if (d / n).exists()]
            check(proc.returncode == 1 and not present and "NIFT_VM session refused" in proc.stderr,
                  f"$[...] refusal in a {label} segment (piped): nothing on the line ran, session stopped",
                  f"rc={proc.returncode} files={present} err={proc.stderr!r}")
            for name in never + ["later.txt", "typed.txt"]:
                if (d / name).exists():
                    (d / name).unlink()
            status, out = run_tty(nift, d, [], {}, [line, 'native::touch("typed.txt")', "exit"])
            time.sleep(0.2)
            present = [n for n in never if (d / n).exists()]
            check(status == 0 and not present and (d / "typed.txt").exists(),
                  f"$[...] refusal in a {label} segment (tty): nothing on the line ran, usable prompt",
                  f"status={status} files={present} out={out!r}")
        for name in ("early.txt", "late.txt"):
            if (d / name).exists():
                (d / name).unlink()
        proc, rows = run(nift, d, [], {"NIFT_VM": "1"},
                         stdin='native::touch("early.txt")\nr := run("echo", "hi")\nnative::touch("late.txt")\n')
        check(proc.returncode == 1 and (d / "early.txt").exists() and not (d / "late.txt").exists() and
              "the session stopped here" in proc.stderr and all(r.get("legacy_executions", 0) == 0 for r in rows),
              "NIFT_VM=1 piped session: VM-owned, stops at the refusal like the default (no legacy fallback)",
              f"rc={proc.returncode} err={proc.stderr!r} rows={rows}")

        # Negative controls: the predicates reject the old default and a fallback environment.
        for label, extra in (("old default (NIFT_VM=0)", {"NIFT_VM": "0"}), ("fallback (NIFT_VM=1)",
                                                                             {"NIFT_VM": "1"})):
            d = work / f"control-{label[:3]}-{extra['NIFT_VM']}"
            d.mkdir()
            problems, _ = refusal_problems(nift, d, extra)
            check(bool(problems) and any("side effect" in p for p in problems),
                  f"control: refusal predicate rejects {label}")
        d = work / "control-supported"
        d.mkdir()
        check(bool(supported_problems(nift, d, {"NIFT_VM": "0"})),
              "control: VM-execution predicate rejects a legacy run with matching output")
        # The platform predicate must reject the old routing (a selection run on
        # legacy, as NIFT_VM=0 stands in for it), even with identical output.
        d = work / "control-platform"
        d.mkdir()
        (d / "p.f").write_text(PLATFORM_SCRIPT)
        proc, rows = run(nift, d, ["--platform=linux", "p.f"], {"NIFT_VM": "0"})
        check(proc.stdout == "linux\n" and not (len(rows) == 1 and rows[0].get("outcome") == "vm"),
              "control: platform VM-execution predicate rejects a legacy run with matching output")
        good = expected_diagnostic("p.f:1:1", "p.f")
        check(not diagnostic_problems(good, "p.f:1:1", "p.f"), "control: diagnostic predicate accepts the format")
        for drop in ("not executed", "NIFT_VM=0 nift p.f", ISSUES, REASON, "p.f:1:1", "the call `run()`"):
            check(bool(diagnostic_problems(good.replace(drop, "X"), "p.f:1:1", "p.f")),
                  f"control: diagnostic predicate rejects a diagnostic without {drop!r}")
        good = expected_session_diagnostic("<stdin>:3:6", "")
        check(bool(diagnostic_problems(good.replace(SESSION_NOTE, PROGRAM_NOTE), "<stdin>:3:6", "", session=True)),
              "control: session diagnostic predicate rejects the program note (no part of it ran)")
        check(not diagnostic_problems(good, "<stdin>:3:6", "", session=True),
              "control: session diagnostic predicate accepts the format")
        check(bool(diagnostic_problems(f"error: NIFT_VM session refused: {REASON}\n", "<stdin>:3:6", "",
                                       session=True)),
              "control: session diagnostic predicate rejects the old short refusal line")
        for drop in ("not executed", "NIFT_VM=0 nift\n", ISSUES, REASON, "<stdin>:3:6", "the call `run()`"):
            check(bool(diagnostic_problems(good.replace(drop, "X"), "<stdin>:3:6", "", session=True)),
                  f"control: session diagnostic predicate rejects a diagnostic without {drop!r}")

        if opts.work_prefix != "vm-default-":
            check(PATH_CONTROLLED == set(PATH_TOKENS),
                  "regression: every checked token was exercised inside a diagnostic's path",
                  f"missing {sorted(set(PATH_TOKENS) - PATH_CONTROLLED)}")

    print("PASS VM-default policy" if not FAILS else f"FAIL VM-default policy ({len(FAILS)} failures)")
    return 0 if not FAILS else 1


if __name__ == "__main__":
    sys.exit(main())
