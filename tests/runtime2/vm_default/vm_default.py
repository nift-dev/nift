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
PLATFORM_SCRIPT = 'touch("side.txt")\nprint(platform())\n'


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
        os.write(fd, (line + "\r").encode())
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
    args = ap.parse_args()
    nift = str(Path(args.nift).resolve())
    with tempfile.TemporaryDirectory(prefix="vm-default-") as work:
        work = Path(work)

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
        proc, rows = run(nift, d, ["-e", 'touch("t.txt")\nr := run("echo", "hi")'], {})
        hint = "-e 'touch(\"t.txt\")\nr := run(\"echo\", \"hi\")'"
        problems = diagnostic_problems(proc.stderr, "<command-line>:2:6", hint)
        check(proc.returncode == 1 and proc.stdout == "" and not (d / "t.txt").exists() and not problems and
              [r.get("outcome") for r in rows] == ["require-error"],
              "-e program refused by default before effects", f"rc={proc.returncode} {problems} rows={rows}")
        proc, rows = run(nift, d, ["-e", 'touch("t.txt")\nr := run("echo", "hi")\nprint(type(r))'],
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
        lines = 'x := 4\nprint(x)\ntouch("early.txt")\nr := run("echo", "hi");touch("refused.txt")\n' \
                'print("B")\ntouch("late.txt")\n'
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
        status, out = run_tty(nift, d, [], {}, ['print("A")', 'touch("early.txt")',
                                                'fn(gdef()){return 1};touch("refused.txt");r := run("echo")',
                                                "print(5)", "gdef()", "exit"])
        tty_diag = expected_session_diagnostic("<stdin>:3:48", "", note=TTY_SESSION_NOTE)
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
        proc, rows = run(nift, d, ["-i", "-e", 'touch("t.txt");r := run("echo", "hi")'], {}, stdin="")
        problems = diagnostic_problems(proc.stderr, "<command-line>:1:21",
                                       "-i -e 'touch(\"t.txt\");r := run(\"echo\", \"hi\")'", session=True)
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
        on_vm(["--platform=my.custom", "-e", 'touch("t.txt");print(platform())'], None, {}, "my.custom\n", "t.txt",
              "-e program")
        on_vm(["--platform=linux", "-c", 'touch("t.txt");print(platform())'], None, {}, "linux\n", "t.txt",
              "-c program")
        on_vm(["--platform=linux", "-"], 'touch("t.txt")\nprint(platform())\n', {}, "linux\n", "t.txt",
              "stdin program")
        on_vm(["--platform=linux", "-i", "p.f"], "print(platform())\n", {}, "linux\nlinux\n", "side.txt",
              "-i script and the session after it", units=2)
        on_vm(["--platform=linux", "-i", "-e", 'touch("t.txt")'], "print(platform())\n", {}, "linux\n", "t.txt",
              "-i -e program and the session after it", units=2)
        on_vm(["--platform=linux"], 'touch("r.txt")\nprint(platform())\n', {}, "linux\n", "r.txt",
              "REPL session", units=2)
        for label, args in (("--platform=native", ["--platform=native", "p.f"]), ("no selector", ["p.f"])):
            on_vm(args, None, {}, "native\n", "side.txt", f"{label} (as before)")
        clean()
        proc, rows = run(nift, d, ["--platform=linux", "p.f"], {"NIFT_VM": "0"})
        check(proc.returncode == 0 and proc.stdout == "linux\n" and proc.stderr == "" and (d / "side.txt").exists()
              and rows == [], "--platform=linux under NIFT_VM=0 runs on legacy with the same output",
              f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} rows={rows}")
        clean()
        proc, rows = run(nift, d, ["--platform=linux"], {"NIFT_VM": "0"}, stdin='touch("r.txt")\nprint(platform())\n')
        check(proc.returncode == 0 and proc.stdout == "linux\n" and (d / "r.txt").exists() and rows == [],
              "--platform=linux REPL under NIFT_VM=0 is legacy with the same output",
              f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} rows={rows}")
        # An unsupported program with a platform selection: refused before any
        # effect (never run on legacy), full diagnostic, selector in the hint.
        for args, where, hint, label in (
                (["--platform=linux", "case.f"], f"{(d / 'case.f').resolve().as_posix()}:7:6",
                 "--platform=linux case.f", "script file"),
                (["--platform=windows", "-e", 'touch("t.txt")\nr := run("echo", "hi")'], "<command-line>:2:6",
                 "--platform=windows -e 'touch(\"t.txt\")\nr := run(\"echo\", \"hi\")'", "-e program"),
                (["--platform=linux", "-"], "<stdin>:2:6", "--platform=linux -", "stdin program")):
            for mode, extra in (("unset", {}), ("require", {"NIFT_VM": "require"})):
                clean()
                proc, rows = run(nift, d, args, extra,
                                 stdin='touch("t.txt")\nr := run("echo", "hi")\n' if args[-1] == "-" else None)
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
        (d / "m.nft").write_text('touch("mod-side.txt")\nfn(go()) {\n  r := run("echo", "hi")\n  return r\n}\n'
                                 'export(go)\n')
        (d / "main.f").write_text('touch("main-side.txt")\nprint("before")\nimport("m.nft")\nprint(type(go()))\n')
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
        (d / "ok.f").write_text('touch("side.txt")\nprint("ran")\n')
        (d / "m.nft").write_text('touch("mod-side.txt")\nfn(go()) {\n  return 1\n}\nexport(go)\n')
        (d / "main.f").write_text('import("m.nft")\ntouch("side.txt")\nprint(go())\n')
        (d / "home").mkdir()
        (d / "home/.niftrc").write_text('touch("rc-side.txt")\n')
        entries = (("script file", ["ok.f"], None), ("module program", ["main.f"], None),
                   ("-e", ["-e", 'touch("side.txt")'], None), ("-c", ["-c", 'touch("side.txt")'], None),
                   ("stdin", ["-"], 'touch("side.txt")\n'), ("eval", ["eval", 'touch("side.txt")'], None),
                   ("REPL", [], 'touch("side.txt")\n'), ("-i", ["-i"], 'touch("side.txt")\n'),
                   ("-i script", ["-i", "ok.f"], ""), ("--platform", ["--platform=linux", "ok.f"], None),
                   ("--platform REPL", ["--platform=linux"], 'touch("side.txt")\n'))
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
            status, out = run_tty(nift, d, [], {}, [line, 'touch("after.txt")', "exit"])
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
        startup = ('print("{tag}-START")\ntouch("{tag}-early.txt")\nfn(greet()){{return "hello"}}\nearly := 5\n'
                   'r := run("echo", "x")\ntouch("{tag}-late.txt")\nlate := 6\n')
        typed = ["print(early)", "print(greet())", 'touch("typed.txt")', "print(late)", "exit"]
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
                    f"note: initialisation from {path} was interrupted here; later statements in it were not run\n"
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
            proc, rows = run(nift, d, [], {}, stdin="\n".join(lines + ['touch("later.txt")']) + "\n")
            present = [n for n in witnesses if (d / n).exists()]
            check(proc.returncode == 1 and not any(n in present for n in never + ["later.txt"]) and
                  "NIFT_VM session refused" in proc.stderr and "the session stopped here" in proc.stderr and
                  ("OK.txt" in present) == ("OK.txt" in lines[-1]),
                  f"shell-form refusal abandons the line (piped, {label}): no later segment or input ran",
                  f"rc={proc.returncode} files={present} err={proc.stderr!r}")
            for name in witnesses:
                if (d / name).exists():
                    (d / name).unlink()
            status, out = run_tty(nift, d, [], {}, lines + ['touch("typed.txt")', "exit"])
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

        # 18. A startup file whose reported refusal is not the earliest one:
        # the interruption point is the earliest refusal in source order, the
        # statements before it run exactly once, and no refusal is dropped.
        for label, args, name in (("~/.niftrc", [], ".niftrc"), ("nift -i file", ["-i", "init.f"], "init.f")):
            d = work / f"earliest-{name.strip('.').replace('.', '-')}"
            d.mkdir()
            (d / name).write_text('print("s1")\ntouch("s1.txt")\nfn(g()) { return run("a") }\nprint("s3")\n'
                                  'touch("s3.txt")\ns := 1\ns.foo()\n')
            path = (d / name).resolve().as_posix()
            status, out = run_tty(nift, d, args, {}, ["print(7)", "exit"])
            check(status == 0 and out.count("s1\n") == 1 and (d / "s1.txt").exists() and
                  not (d / "s3.txt").exists() and "s3" not in out and
                  f"error: {path}:3:18: NIFT_VM session refused: callable:run" in out and
                  f"{path}:7:" not in out and out.count("NIFT_VM session refused") == 1 and
                  "was interrupted here" in out and out.rstrip().endswith("7"),
                  f"{label} on a tty: interrupted at the earliest refusal (3:18); s1 ran once, s3 never",
                  f"status={status} out={out!r}")

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

    print("PASS VM-default policy" if not FAILS else f"FAIL VM-default policy ({len(FAILS)} failures)")
    return 0 if not FAILS else 1


if __name__ == "__main__":
    sys.exit(main())
