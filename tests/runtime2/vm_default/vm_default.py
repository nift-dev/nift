#!/usr/bin/env python3
"""VM-default policy: the VM is the default scripting engine, with no silent
legacy fallback.

  NIFT_VM unset/empty  VM required: an unsupported program is refused before
                       any effect, with the full diagnostic and exit status 1
  NIFT_VM=require      identical to the default
  NIFT_VM=0            explicit legacy interpreter (escape hatch), unchanged
  NIFT_VM=1            VM first with legacy fallback, unchanged (no hint)

Execution engine is proven by NIFT_VM_STATS records (vm_executions /
legacy_executions), never by output alone. Website builds keep the legacy
renderer: byte-identical output under the default and NIFT_VM=0, and no VM
statistics record. Interactive sessions are VM-owned by default (D-054) and a
refused session unit gets the same full diagnostic, located in the user's terms
(`<stdin>:line:column` over the whole input stream, or the -i unit's path).

A non-native --platform cannot be honoured by the VM (its platform() is always
`native`): under the default/require policy every script entry (file, -e/-c,
stdin, -i, the REPL) is refused before any effect with the stable refusal id
`platform-non-native`; NIFT_VM=1 records the fallback and runs legacy;
NIFT_VM=0 is legacy. A refusal inside an imported module names the module
position and nothing in the main program or the module runs.

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
import tempfile
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
PLATFORM_REASON = "platform-non-native"
PLATFORM_FEATURE = "a non-native `--platform` selection (the VM's `platform()` is always `native`)"
PLATFORM_SCRIPT = 'touch("side.txt")\nprint(platform())\n'


def notes(hint):
    return ("note: the program was not executed: no part of it ran\n"
            f"note: to run it on the legacy interpreter: NIFT_VM=0 nift{(' ' + hint) if hint else ''}\n"
            f"note: please report unsupported programs at {ISSUES}\n")


def expected_diagnostic(where, hint, reason=REASON, feature=RUN_FEATURE):
    return (f"error: {where}: not compiled for the VM ({reason}): {feature} is not supported by the "
            "Runtime 2.0 VM yet\n" + notes(hint))


def expected_session_diagnostic(where, hint, reason=REASON, feature=RUN_FEATURE):
    return (f"error: {where}: NIFT_VM session refused: {reason}: {feature} is not supported by the "
            "Runtime 2.0 VM yet\n" + notes(hint))


def diagnostic_problems(stderr, where, hint, reason=REASON, feature=RUN_FEATURE, session=False):
    """Every required part, and the exact deterministic text."""
    problems = []
    parts = [where + ":", "NIFT_VM session refused" if session else "not compiled for the VM", reason, feature,
             "not executed", f"NIFT_VM=0 nift{(' ' + hint) if hint else ''}\n", ISSUES]
    for part in parts:
        if part not in stderr:
            problems.append(f"missing {part!r}")
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
        for label, extra in (("unset", {}), ("empty", {"NIFT_VM": ""}), ("require", {"NIFT_VM": "require"})):
            d = work / f"supported-{label}"
            d.mkdir()
            problems = supported_problems(nift, d, extra)
            check(not problems, f"supported script runs on the VM (NIFT_VM {label})", "; ".join(problems))

        # 2. Default (and empty, require) refuses an unsupported script before any effect.
        results = {}
        for label, extra in (("unset", {}), ("empty", {"NIFT_VM": ""}), ("require", {"NIFT_VM": "require"})):
            d = work / f"refused-{label}"
            d.mkdir()
            problems, proc = refusal_problems(nift, d, extra)
            results[label] = (proc.returncode, proc.stdout, proc.stderr.replace(str(d.resolve()), "$D"))
            check(not problems, f"unsupported script refused with the full diagnostic (NIFT_VM {label})",
                  "; ".join(problems))
        check(results["unset"] == results["require"] == results["empty"],
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

        # 7. Interactive session (non-tty stdin): VM-owned by default (D-054). A
        # refused unit gets the full diagnostic at its position in the input
        # stream; the session keeps its state and continues (VM-default contract
        # upgrade from the short `NIFT_VM session refused: <reason>` line).
        d = work / "session"
        d.mkdir()
        lines = 'x := 4\nprint(x)\nr := run("echo", "hi")\nprint(x + 1)\n'
        sessions = {}
        for label, extra in (("unset", {}), ("require", {"NIFT_VM": "require"})):
            proc, rows = run(nift, d, [], extra, stdin=lines)
            sessions[label] = (proc.returncode, proc.stdout, proc.stderr, [r.get("outcome") for r in rows])
            problems = diagnostic_problems(proc.stderr, "<stdin>:3:6", "", session=True)
            check(proc.returncode == 0 and proc.stdout == "4\n5\n" and not problems and
                  [r.get("outcome") for r in rows] == ["vm", "vm", "require-error", "vm"] and
                  rows[2].get("reason") == REASON and (rows[2].get("line"), rows[2].get("column")) == (1, 6) and
                  all(r.get("legacy_executions", 0) == 0 for r in rows),
                  f"REPL session (NIFT_VM {label}) is VM-owned; a refused unit gets the full diagnostic at "
                  "<stdin>:3:6, keeps state and never falls back",
                  f"rc={proc.returncode} out={proc.stdout!r} {problems} rows={rows}")
        check(sessions["unset"] == sessions["require"], "REPL session: NIFT_VM=require is identical to the default",
              repr(sessions))
        # A multi-line unit: the position is the stream line of the refused call.
        proc, rows = run(nift, d, ["-i"], {}, stdin='x := 4\nif(x > 1) {\n  r := run("echo", "hi")\n}\nprint(x + 1)\n')
        problems = diagnostic_problems(proc.stderr, "<stdin>:3:8", "-i", session=True)
        check(proc.returncode == 0 and proc.stdout == "5\n" and not problems,
              "nift -i on piped stdin: a multi-line refused unit is located at its stream line (<stdin>:3:8)",
              f"rc={proc.returncode} out={proc.stdout!r} {problems}")
        proc, rows = run(nift, d, [], {"NIFT_VM": "0"}, stdin=lines.replace("print(x + 1)", "print(type(r))"))
        check(proc.returncode == 0 and proc.stdout == "4\nobject\n" and rows == [],
              "NIFT_VM=0 REPL session is legacy", f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r}")
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

        # 8. --platform=<non-native>: the VM cannot honour it (platform() is
        # always native), so the default/require policy refuses every script
        # entry before any effect; NIFT_VM=1 falls back; NIFT_VM=0 is legacy.
        d = work / "platform"
        d.mkdir()
        (d / "p.f").write_text(PLATFORM_SCRIPT)
        where = f"{(d / 'p.f').resolve().as_posix()}:1:1"

        def clean():
            for name in ("side.txt", "t.txt", "r.txt"):
                if (d / name).exists():
                    (d / name).unlink()

        def platform_refused(args, stdin, where, hint, label, extra):
            clean()
            proc, rows = run(nift, d, args, extra, stdin=stdin)
            problems = diagnostic_problems(proc.stderr, where, hint, PLATFORM_REASON, PLATFORM_FEATURE)
            effects = [n for n in ("side.txt", "t.txt", "r.txt") if (d / n).exists()]
            check(proc.returncode == 1 and proc.stdout == "" and not effects and not problems and
                  len(rows) == 1 and rows[0].get("outcome") == "require-error" and
                  rows[0].get("reason") == PLATFORM_REASON and rows[0].get("vm_executions") == 0 and
                  rows[0].get("legacy_executions") == 0,
                  f"--platform non-native refused before effects with the full diagnostic: {label}",
                  f"rc={proc.returncode} out={proc.stdout!r} effects={effects} {problems} rows={rows}")
            return proc

        for label, extra in (("unset", {}), ("empty", {"NIFT_VM": ""}), ("require", {"NIFT_VM": "require"})):
            platform_refused(["--platform=linux", "p.f"], None, where, "--platform=linux p.f",
                             f"script file (NIFT_VM {label})", extra)
            platform_refused(["p.f", "--platform=windows"], None, where, "p.f --platform=windows",
                             f"script file, selector after the path (NIFT_VM {label})", extra)
        platform_refused(["--platform=windows", "-e", 'touch("t.txt")'], None, "<command-line>:1:1",
                         "--platform=windows -e 'touch(\"t.txt\")'", "-e program", {})
        platform_refused(["--platform=linux", "-c", 'touch("t.txt")'], None, "<command-line>:1:1",
                         "--platform=linux -c 'touch(\"t.txt\")'", "-c program", {})
        platform_refused(["--platform=linux", "-"], 'touch("t.txt")\n', "<stdin>:1:1", "--platform=linux -",
                         "stdin program", {})
        platform_refused(["--platform=linux", "-i", "p.f"], "", where, "--platform=linux -i p.f",
                         "-i script", {})
        platform_refused(["--platform=linux", "-i", "-e", 'touch("t.txt")'], 'touch("r.txt")\n',
                         "<command-line>:1:1", "--platform=linux -i -e 'touch(\"t.txt\")'", "-i -e program", {})
        platform_refused(["--platform=linux"], 'touch("r.txt")\nprint(platform())\n', "<stdin>:1:1",
                         "--platform=linux", "REPL session (NIFT_VM unset)", {})
        platform_refused(["--platform=linux"], 'touch("r.txt")\nprint(platform())\n', "<stdin>:1:1",
                         "--platform=linux", "REPL session (NIFT_VM require)", {"NIFT_VM": "require"})
        clean()
        proc, rows = run(nift, d, ["--platform=linux", "p.f"], {"NIFT_VM": "0"})
        check(proc.returncode == 0 and proc.stdout == "linux\n" and proc.stderr == "" and (d / "side.txt").exists()
              and rows == [], "--platform=linux under NIFT_VM=0 runs on legacy and honours the platform",
              f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} rows={rows}")
        clean()
        proc, rows = run(nift, d, ["--platform=linux", "p.f"], {"NIFT_VM": "1"})
        check(proc.returncode == 0 and proc.stdout == "linux\n" and proc.stderr == "" and (d / "side.txt").exists()
              and len(rows) == 1 and rows[0].get("outcome") == "fallback" and
              rows[0].get("reason") == PLATFORM_REASON and rows[0].get("legacy_executions") == 1 and
              rows[0].get("vm_executions") == 0,
              "--platform=linux under NIFT_VM=1 records the fallback and runs legacy (no hint)",
              f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} rows={rows}")
        clean()
        proc, rows = run(nift, d, ["--platform=linux"], {"NIFT_VM": "1"}, stdin='touch("r.txt")\n')
        check(proc.returncode == 0 and proc.stderr == "" and (d / "r.txt").exists(),
              "--platform=linux REPL under NIFT_VM=1 keeps its (VM-owned) session unchanged",
              f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} rows={rows}")
        clean()
        proc, rows = run(nift, d, ["--platform=linux"], {"NIFT_VM": "0"}, stdin='touch("r.txt")\nprint(platform())\n')
        check(proc.returncode == 0 and proc.stdout == "linux\n" and (d / "r.txt").exists() and rows == [],
              "--platform=linux REPL under NIFT_VM=0 is legacy and honours the platform",
              f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} rows={rows}")
        for label, args in (("--platform=native", ["--platform=native", "p.f"]), ("no selector", ["p.f"])):
            clean()
            proc, rows = run(nift, d, args, {})
            check(proc.returncode == 0 and proc.stdout == "native\n" and proc.stderr == "" and
                  (d / "side.txt").exists() and len(rows) == 1 and rows[0].get("outcome") == "vm" and
                  rows[0].get("vm_executions") == 1 and rows[0].get("legacy_executions") == 0,
                  f"{label}: the script runs on the VM by default, as before",
                  f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r} rows={rows}")

        # 9. A refusal inside an imported module: located in the module, and
        # nothing in the main program or the module ran.
        d = work / "module"
        d.mkdir()
        (d / "m.nft").write_text('touch("mod-side.txt")\nfn(go()) {\n  r := run("echo", "hi")\n  return r\n}\n'
                                 'export(go)\n')
        (d / "main.f").write_text('touch("main-side.txt")\nprint("before")\nimport("m.nft")\nprint(type(go()))\n')
        for label, extra in (("unset", {}), ("require", {"NIFT_VM": "require"})):
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
        # The REPL/platform predicates must reject the fallback environment (a
        # default that silently became NIFT_VM=1 passes every != Off check).
        d = work / "control-platform"
        d.mkdir()
        (d / "p.f").write_text(PLATFORM_SCRIPT)
        proc, rows = run(nift, d, ["--platform=linux", "p.f"], {"NIFT_VM": "1"})
        check(proc.returncode != 1 or (d / "side.txt").exists() or bool(diagnostic_problems(
              proc.stderr, f"{(d / 'p.f').resolve().as_posix()}:1:1", "--platform=linux p.f", PLATFORM_REASON,
              PLATFORM_FEATURE)), "control: platform refusal predicate rejects NIFT_VM=1")
        proc, rows = run(nift, d, ["--platform=linux"], {"NIFT_VM": "1"}, stdin='touch("r.txt")\n')
        check(proc.returncode != 1 or (d / "r.txt").exists(),
              "control: REPL platform refusal predicate rejects NIFT_VM=1")
        good = expected_diagnostic("p.f:1:1", "p.f")
        check(not diagnostic_problems(good, "p.f:1:1", "p.f"), "control: diagnostic predicate accepts the format")
        for drop in ("not executed", "NIFT_VM=0 nift p.f", ISSUES, REASON, "p.f:1:1", "the call `run()`"):
            check(bool(diagnostic_problems(good.replace(drop, "X"), "p.f:1:1", "p.f")),
                  f"control: diagnostic predicate rejects a diagnostic without {drop!r}")
        good = expected_session_diagnostic("<stdin>:3:6", "")
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
