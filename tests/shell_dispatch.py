#!/usr/bin/env python3
"""CP19 Slice 1 (D-042): exactly-once shell dispatch regressions.

Normal mode checks the approved precedence and exactly-once semantics.
--mutant-kind runs the same normal suite against a test-only mutant binary and
requires the failures attributable to that planted defect (M1 fallback,
M2 quoting/escaping discarded, M3 eager interpolation).
"""
import argparse
import os
import pty
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

if os.name != "posix":
    print("SKIP shell dispatch (POSIX pty/coreutils regression)")
    sys.exit(77)

parser = argparse.ArgumentParser()
parser.add_argument("--nift", default=os.environ.get("NIFT", "./nift"))
parser.add_argument("--mutant-kind", choices=("m1", "m2", "m3", "m4"))
args = parser.parse_args()
NIFT = str(Path(args.nift).resolve())

ROOT = Path(tempfile.mkdtemp(prefix="nift-shell-dispatch-"))
BIN = ROOT / "bin"
BIN.mkdir()
STUB_LOG = ROOT / "stub.log"
failures = []


def check(name, condition, detail=""):
    if condition:
        print(f"PASS {name}")
    else:
        print(f"FAIL {name}" + (f": {detail}" if detail else ""))
        failures.append(name)


def stub(name, output=None, exit_code=0):
    path = BIN / name
    body = "#!/bin/sh\n"
    body += f'printf "%s %s\\n" "{name}" "$*" >> "$STUB_LOG"\n'
    if output is not None:
        body += f'echo "{output} $*"\n'
    body += f"exit {exit_code}\n"
    path.write_text(body, encoding="utf-8")
    path.chmod(0o755)


for name in ("fixtool", "mkdir", "touch", "touchy", "echo", "print"):
    stub(name, output=f"stub-{name}")
stub("mkdir", output="stub-mkdir")
stub("touch", output="stub-touch")
stub("touchy", output="stub-touchy")
# The touchy stub also appends to marker.txt so the mutant's function+external
# double side effect is visible in the same file.
touchy_stub = BIN / "touchy"
touchy_stub.write_text(
    "#!/bin/sh\nprintf x >> marker.txt\n"
    'printf "touchy %s\\n" "$*" >> "$STUB_LOG"\nexit 0\n',
    encoding="utf-8",
)
touchy_stub.chmod(0o755)

(ROOT / "localtool").write_text("#!/bin/sh\necho local-tool\n", encoding="utf-8")
(ROOT / "localtool").chmod(0o755)


def env():
    result = os.environ.copy()
    result["HOME"] = str(ROOT)
    result["PATH"] = str(BIN) + ":" + result.get("PATH", "/usr/bin:/bin")
    result["STUB_LOG"] = str(STUB_LOG)
    result.pop("NIFT_NO_PROCESS", None)
    return result


def reset_log():
    STUB_LOG.write_text("", encoding="utf-8")


def log_text():
    return STUB_LOG.read_text(encoding="utf-8") if STUB_LOG.exists() else ""


def run_shell(script, timeout=60):
    return subprocess.run(
        [NIFT],
        input=script,
        cwd=str(ROOT),
        env=env(),
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
    )


def drain(fd, timeout):
    data = b""
    end = time.time() + timeout
    while time.time() < end:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if not ready:
            continue
        try:
            chunk = os.read(fd, 4096)
        except OSError:
            break
        if not chunk:
            break
        data += chunk
    return data


def run_shell_pty(lines, timeout=10):
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(ROOT)
        os.execve(NIFT, [NIFT], env())
    output = drain(fd, 0.5)
    for line in lines:
        os.write(fd, (line + "\n").encode())
        time.sleep(0.35)
        output += drain(fd, 0.35)
    deadline = time.time() + timeout
    while time.time() < deadline:
        got, _ = os.waitpid(pid, os.WNOHANG)
        if got == pid:
            break
        time.sleep(0.05)
    else:
        os.kill(pid, signal.SIGKILL)
        try:
            os.waitpid(pid, 0)
        except OSError:
            pass
    try:
        os.close(fd)
    except OSError:
        pass
    return output.decode(errors="replace")


def clean():
    for path in ROOT.iterdir():
        if path.name in ("bin", "localtool"):
            continue
        if path.is_dir():
            shutil.rmtree(path)
        else:
            path.unlink()
    reset_log()


def script_and_check(name, script, check_fn, timeout=60):
    clean()
    result = run_shell(script, timeout)
    detail = f"rc={result.returncode} stdout={result.stdout!r} stderr={result.stderr!r}"
    check(name, check_fn(result), detail)
    return result


# 1. rm -i must prompt and respect the answer.
clean()
(ROOT / "a").write_text("a\n", encoding="utf-8")
run_shell_pty(["rm -i a", "n", "exit"])
check("rm -i keeps the file on n", (ROOT / "a").exists())

# 2. Missing files use rm's own failure and status.
result = script_and_check(
    "rm missing fails",
    "rm nope && echo BAD || echo GOOD\nexit\n",
    lambda r: "GOOD" in r.stdout and "BAD" not in r.stdout
    and ("No such file" in (r.stdout + r.stderr) or "cannot remove" in (r.stdout + r.stderr)),
)

# 3. Partial failure runs once.
clean()
(ROOT / "a").write_text("a\n", encoding="utf-8")
(ROOT / "d1").mkdir()
result = run_shell("rm a d1\nexit\n")
combined = result.stdout + result.stderr
check("rm a d1 removes a once", not (ROOT / "a").exists())
check("rm a d1 reports d1 once", combined.count("d1") == 1, combined)
check("rm a d1 does not re-run rm externally", "cannot remove 'a'" not in combined, combined)

# 4. POSIX bracket glob.
clean()
(ROOT / "a.txt").write_text("a", encoding="utf-8")
(ROOT / "b.txt").write_text("b", encoding="utf-8")
run_shell("rm [ab].txt\nexit\n")
check("rm [ab].txt expands once", not (ROOT / "a.txt").exists() and not (ROOT / "b.txt").exists())

# 5. Coreutils command forms reach the external tools.
clean()
(ROOT / "src").mkdir()
(ROOT / "src" / "f").write_text("data\n", encoding="utf-8")
run_shell("cp -r src dst\nexit\n")
check("cp -r external", (ROOT / "dst" / "f").exists())

clean()
(ROOT / "a").write_text("a\n", encoding="utf-8")
(ROOT / "b").write_text("b\n", encoding="utf-8")
run_shell("mv -n a b\nexit\n")
check("mv -n external", (ROOT / "a").exists() and (ROOT / "b").read_text(encoding="utf-8") == "b\n")

clean()
(ROOT / "f").write_text("one\ntwo\n", encoding="utf-8")
result = run_shell("cat -n f\nexit\n")
check("cat -n external", "1" in result.stdout and "one" in result.stdout and "2" in result.stdout, result.stdout)

clean()
(ROOT / "f").write_text("x", encoding="utf-8")
result = run_shell("ls -l f\nexit\n")
check("ls -l external", "f" in result.stdout and len(result.stdout.split()) >= 5, result.stdout)

# 5b. Quoted/escaped characters keep their literal meaning.
clean()
(ROOT / "f.txt").write_text("hello\nworld\n", encoding="utf-8")
(ROOT / "a").write_text("", encoding="utf-8")
(ROOT / "pre*fix-a.txt").write_text("", encoding="utf-8")
(ROOT / "preXfix-a.txt").write_text("", encoding="utf-8")
(ROOT / "prefix-a.txt").write_text("", encoding="utf-8")
(ROOT / "prefix-b.txt").write_text("", encoding="utf-8")

result = run_shell('grep "[a-z]" f.txt\nexit\n')
check("quoted bracket stays literal", "hello" in result.stdout and "world" in result.stdout,
      result.stdout + result.stderr)

result = run_shell('/bin/echo \\[a\\]\nexit\n')
check("escaped bracket stays literal", result.stdout.strip() == "[a]",
      result.stdout + result.stderr)

result = run_shell('/bin/echo "*"\nexit\n')
check("quoted star stays literal", result.stdout.strip() == "*",
      result.stdout + result.stderr)

result = run_shell('/bin/echo "a" "&&" "b"\nexit\n')
check("quoted operator stays literal",
      result.stdout.strip() == "a && b" and "command not found" not in (result.stdout + result.stderr),
      result.stdout + result.stderr)

result = run_shell('find . -name f.txt -exec echo found {} \\;\nexit\n')
check("escaped semicolon stays literal",
      "found" in result.stdout and "f.txt" in result.stdout,
      result.stdout + result.stderr)

result = run_shell('/bin/echo "pre*fix-"*.txt\nexit\n')
check("mixed quoted/unquoted glob",
      "pre*fix-a.txt" in result.stdout and "preXfix-a.txt" not in result.stdout,
      result.stdout)

result = run_shell('/bin/echo "prefix-"*.txt\nexit\n')
check("quoted prefix with unquoted glob",
      "prefix-a.txt" in result.stdout and "prefix-b.txt" in result.stdout,
      result.stdout)

# 6. Pipeline and redirections.
result = script_and_check(
    "pipeline",
    "printf 'a\\nb\\n' | wc -l\nexit\n",
    lambda r: r.stdout.strip() == "2",
)
clean()
run_shell("printf hi > out.txt\nexit\n")
check("stdout redirection", (ROOT / "out.txt").exists() and (ROOT / "out.txt").read_text(encoding="utf-8") == "hi")
clean()
run_shell("sh -c \"echo out; echo err >&2\" 2>err.txt\nexit\n")
check("stderr redirection", (ROOT / "err.txt").exists() and "err" in (ROOT / "err.txt").read_text(encoding="utf-8"))

# 7. && and || use the real status.
result = script_and_check(
    "false chains",
    "false && echo BAD\nfalse || echo GOOD\nexit\n",
    lambda r: "BAD" not in r.stdout and "GOOD" in r.stdout,
)

# 8. A failed user function has one side effect and no PATH fallback.
clean()
(ROOT / "marker.txt").write_text("", encoding="utf-8")
script = """
fn(touchy(x)) {
  run("sh", "-c", "printf x >> marker.txt")
  missing.value
}
touchy arg || echo FAILED
exit
"""
result = run_shell(script)
marker = (ROOT / "marker.txt").read_text(encoding="utf-8")
check("user function side effect once", marker == "x", f"marker={marker!r} stderr={result.stderr!r}")
check("failed user function status non-zero", "FAILED" in result.stdout, result.stdout)
check("failed user function does not reach PATH", "touchy" not in log_text(), log_text())

# 9. mkdir/touch command form is external, never the native built-in.
clean()
result = run_shell("mkdir made\ntouch touched\nexit\n")
check("mkdir reaches PATH stub", "mkdir made" in log_text(), log_text())
check("touch reaches PATH stub", "touch touched" in log_text(), log_text())
check("mkdir native did not run", not (ROOT / "made").exists())
check("touch native did not run", not (ROOT / "touched").exists())

# 10. Precedence: user function, command escape, allow-list.
clean()
script = """
fn(fixtool(x)) { print("func:" + x) }
fixtool hello
command fixtool world
print hi
pwd
command print external
exit
"""
result = run_shell(script)
check("user function shadows PATH", "func:hello" in result.stdout, result.stdout)
check("command bypasses user function", "stub-fixtool world" in result.stdout, result.stdout)
check("print command form is native", "hi" in result.stdout, result.stdout)
check("pwd command form is native", str(ROOT) in result.stdout, result.stdout)
check("command print is external", "stub-print external" in result.stdout, result.stdout)

clean()
result = run_shell("./localtool\nexit\n")
check("explicit path executes", "local-tool" in result.stdout, result.stdout)

# 11. Interpolation runs exactly once after classification.
clean()
(ROOT / "bump.txt").write_text("", encoding="utf-8")
script = """
fn(bump()) {
  run("sh", "-c", "printf x >> bump.txt")
  return "value"
}
print $[bump()]
exit
"""
result = run_shell(script)
bump = (ROOT / "bump.txt").read_text(encoding="utf-8")
check("$[...] evaluated once", bump == "x", f"bump={bump!r} stderr={result.stderr!r}")
check("$[...] result dispatched", "value" in result.stdout, result.stdout)

# 11b. Interpolation is deferred until the selected segment dispatches.
clean()
skip_script = """
fn(bump()) {
  run("sh", "-c", "printf x >> bump.txt")
  return "hit"
}
%s
exit
"""
result = run_shell(skip_script % "false && echo $[bump()]")
bump_file = ROOT / "bump.txt"
check("skipped && interpolation",
      not bump_file.exists() or bump_file.read_text(encoding="utf-8") == "",
      (bump_file.read_text(encoding="utf-8") if bump_file.exists() else "") + result.stderr)

clean()
result = run_shell(skip_script % "true || echo $[bump()]")
check("skipped || interpolation",
      not bump_file.exists() or bump_file.read_text(encoding="utf-8") == "",
      (bump_file.read_text(encoding="utf-8") if bump_file.exists() else "") + result.stderr)

clean()
result = run_shell(skip_script % "false || echo $[bump()]")
check("executed || interpolation once",
      bump_file.exists() and bump_file.read_text(encoding="utf-8") == "x" and "hit" in result.stdout,
      (bump_file.read_text(encoding="utf-8") if bump_file.exists() else "") + result.stdout + result.stderr)

clean()
result = run_shell("""
fn(bump()) {
  run("sh", "-c", "printf x >> bump.txt")
  return "hit"
}
false && echo $[bump()] | cat
exit
""")
check("skipped pipeline interpolation",
      not bump_file.exists() or bump_file.read_text(encoding="utf-8") == "",
      (bump_file.read_text(encoding="utf-8") if bump_file.exists() else "") + result.stderr)

clean()
result = run_shell(skip_script % "true && echo $[bump()] | cat")
check("executed pipeline interpolation once",
      bump_file.exists() and bump_file.read_text(encoding="utf-8") == "x" and "hit" in result.stdout,
      (bump_file.read_text(encoding="utf-8") if bump_file.exists() else "") + result.stdout + result.stderr)

clean()
result = run_shell("""
fn(bump_a()) { run("sh", "-c", "printf a >> order.txt"); return "a" }
fn(bump_b()) { run("sh", "-c", "printf b >> order.txt"); return "b" }
echo $[bump_a()] $[bump_b()]
exit
""")
order_file = ROOT / "order.txt"
check("interpolation left-to-right",
      order_file.exists() and order_file.read_text(encoding="utf-8") == "ab",
      (order_file.read_text(encoding="utf-8") if order_file.exists() else "") + result.stderr)

clean()
otherbin = ROOT / "otherbin"
otherbin.mkdir()
(otherbin / "foo").write_text("#!/bin/sh\nprintf 'foo %s\\n' \"$*\" >> \"$STUB_LOG\"\necho stub-foo\n", encoding="utf-8")
(otherbin / "foo").chmod(0o755)
state_script = """
fn(bump()) {
  setenv("PATH", getenv("PATH") + ":%s")
  return "arg"
}
foo $[bump()]
exit
""" % str(otherbin)
result = run_shell(state_script)
check("interpolation cannot reclassify",
      "foo" not in log_text() and "nift: foo: command not found" in (result.stdout + result.stderr),
      result.stdout + result.stderr + log_text())

# 11c. C3: malformed syntax is rejected before anything runs.
def syntax_case(name, script, token, forbidden=""):
    clean()
    result = run_shell(script)
    combined = result.stdout + result.stderr
    check(name,
          f"nift: syntax error near '{token}'" in combined and
          (not forbidden or forbidden not in result.stdout),
          f"stdout={result.stdout!r} stderr={result.stderr!r}")

syntax_case("syntax rejects leading &&", "&& echo b\nexit\n", "&&", "b")
syntax_case("syntax rejects dangling >", "echo hi >\nexit\n", ">", "hi")
syntax_case("syntax rejects leading |", "| cat f.txt\nexit\n", "|", "cat")
syntax_case("syntax rejects dangling &&", "echo a &&\nexit\n", "&&", "a")
syntax_case("syntax rejects dangling ||", "echo a ||\nexit\n", "||", "a")
syntax_case("syntax rejects double ;;", "echo a ; ;\nexit\n", ";", "a")
syntax_case("syntax rejects double pipe", "echo a | | cat\nexit\n", "|", "a")
syntax_case("syntax rejects connector before pipe", "echo a && | cat\nexit\n", "&&", "a")
syntax_case("syntax rejects non-final &", "echo a & echo b\nexit\n", "&", "a")

clean()
marker = ROOT / "c3-marker"
(result := run_shell("/bin/touch c3-marker && echo x >\nexit\n"))
combined = result.stdout + result.stderr
check("syntax validates whole line before side effects",
      not marker.exists() and "nift: syntax error near '>'" in combined,
      f"marker={marker.exists()} stdout={result.stdout!r} stderr={result.stderr!r}")

clean()
result = run_shell("true || echo x >\nexit\n")
combined = result.stdout + result.stderr
check("syntax validates skipped segment",
      "x" not in result.stdout and "nift: syntax error near '>'" in combined,
      f"stdout={result.stdout!r} stderr={result.stderr!r}")

clean()
bump_file = ROOT / "bump.txt"
result = run_shell("""
fn(bump()) {
  run("sh", "-c", "printf x >> bump.txt")
  return "hit"
}
echo $[bump()] && |
exit
""")
combined = result.stdout + result.stderr
check("syntax validates before interpolation",
      (not bump_file.exists() or bump_file.read_text(encoding="utf-8") == "") and
      "nift: syntax error near '&&'" in combined,
      (bump_file.read_text(encoding="utf-8") if bump_file.exists() else "") + result.stdout + result.stderr)

clean()
result = run_shell("/bin/echo ok ;\nexit\n")
check("valid trailing semicolon",
      result.stdout.strip() == "ok" and "syntax error" not in (result.stdout + result.stderr),
      result.stdout + result.stderr)

clean()
result = run_shell('/bin/echo "&&" "|" ">" ";"\nexit\n')
check("quoted operators remain words",
      result.stdout.strip() == "&& | > ;" and "syntax error" not in (result.stdout + result.stderr),
      result.stdout + result.stderr)

# 12. A failed interpolation prevents dispatch.
clean()
result = run_shell("echo $[missing.value]\nexit\n")
check("failed interpolation prevents dispatch", "echo" not in log_text(), log_text())
check("failed interpolation is reported", "interpolation failed" in (result.stdout + result.stderr).lower(), result.stdout + result.stderr)

# 13. cd is a shell-state command.
clean()
(ROOT / "sub").mkdir()
result = run_shell("cd sub\npwd\nexit\n")
check("cd changes shell directory", str(ROOT / "sub") in result.stdout, result.stdout)

# 14. Env assignment reaches only the external command.
clean()
result = run_shell("FOO=bar sh -c 'printf %s \"$FOO\"'\nexit\n")
check("env assignment preserved", "bar" in result.stdout, result.stdout)

if args.mutant_kind:
    expected = {
        "m1": {
            "rm a d1 does not re-run rm externally",
            "user function side effect once",
            "failed user function does not reach PATH",
        },
        "m2": {
            "quoted bracket stays literal",
            "escaped bracket stays literal",
            "quoted star stays literal",
            "quoted operator stays literal",
            "escaped semicolon stays literal",
            "mixed quoted/unquoted glob",
            "quoted operators remain words",
        },
        "m3": {
            "skipped && interpolation",
            "skipped || interpolation",
            "skipped pipeline interpolation",
        },
        "m4": {
            "syntax rejects leading &&",
            "syntax rejects dangling >",
            "syntax rejects leading |",
            "syntax rejects dangling &&",
            "syntax rejects dangling ||",
            "syntax rejects double ;;",
            "syntax rejects double pipe",
            "syntax rejects connector before pipe",
            "syntax rejects non-final &",
            "syntax validates whole line before side effects",
            "syntax validates skipped segment",
            "syntax validates before interpolation",
        },
    }[args.mutant_kind]
    missing = sorted(expected.difference(failures))
    if missing:
        print("MUTANT NOT DETECTED: missing expected failures: " + ", ".join(missing))
        print("FAILED: " + ", ".join(failures))
        sys.exit(1)
    print(f"PASS mutant {args.mutant_kind} detected by: " + ", ".join(sorted(expected)))
    sys.exit(0)

shutil.rmtree(ROOT, ignore_errors=True)
if failures:
    print("FAILED: " + ", ".join(failures))
    sys.exit(1)
print("PASS shell dispatch regression")
