#!/usr/bin/env python3
"""CP21 interactive Host job-control contract.

The contract is behavioural and portable across POSIX terminals. Linux adds
/proc process-group/tty ownership assertions; other POSIX hosts use the shell's
job table, a small foreground probe and real terminal signals instead. No
scenario is skipped wholesale on macOS.

With --mutant N a compiled CP21 mutant must fail its named behavioural check
after the expected prerequisite has been observed. Dead binaries, setup
failures, unrelated failures and timeouts are rejected as non-detection.
"""
import argparse
import os
import pty
import re
import select
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ANSI = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]|\x1b\][^\x07]*\x07")
LINUX = sys.platform.startswith("linux") and os.environ.get("CP21_FORCE_PORTABLE") != "1"
PROBE = None
TTY_AUDIT = None

MUTANT = {
    1: {"scenario": "background_owner", "witness": "background-started",
        "check": "background-never-owns-terminal" if LINUX else "background-foreground-still-owns"},
    2: {"scenario": "stop_bg_fg", "witness": "background-stopped", "check": "bg-state-running"},
    3: {"scenario": "ctrl_z", "witness": "ctrl-z-jobs-queried", "check": "ctrl-z-stopped-state"},
    4: {"scenario": "ctrl_z", "witness": "ctrl-z-returned", "check": "ctrl-z-restores-terminal"},
    5: {"scenario": "jobs_ids", "witness": "second-background", "check": "job-ids-unique"},
}


class Timeout(Exception):
    pass


class Result:
    def __init__(self):
        self.checks = []
        self.witnesses = set()
        self.setup_error = ""
        self.completed = False

    def witness(self, name):
        self.witnesses.add(name)

    def check(self, name, condition, detail=""):
        print(f"{'PASS' if condition else 'FAIL'} {name}")
        self.checks.append((name, bool(condition)))
        if not condition and detail:
            print(f"  detail: {detail}")
        return condition

    def failed_checks(self):
        return [name for name, ok in self.checks if not ok]


def proc_stat(pid):
    if not LINUX:
        return None
    try:
        fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return {"state": fields[0], "pgrp": int(fields[2]), "tpgid": int(fields[5])}
    except (OSError, ValueError):
        return None


def ps_table():
    if LINUX:
        return {}
    command = ["ps", "-axo", "pid=,ppid=,pgid=,stat="]
    proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    if proc.returncode != 0:
        return {}
    table = {}
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) < 4:
            continue
        try:
            table[int(parts[0])] = {"ppid": int(parts[1]), "pgrp": int(parts[2]), "state": parts[3][0]}
        except ValueError:
            continue
    return table


def children(pid):
    found = []
    if LINUX:
        for entry in Path("/proc").glob("[0-9]*"):
            st = proc_stat(entry.name)
            if not st:
                continue
            try:
                fields = Path(f"/proc/{entry.name}/stat").read_text().rsplit(")", 1)[1].split()
                if int(fields[1]) == pid:
                    found.append(st)
            except (OSError, ValueError):
                pass
        return found
    for child, st in ps_table().items():
        if st["ppid"] == pid:
            found.append({"state": st["state"], "pgrp": st["pgrp"], "tpgid": -1})
    return found


class Pty:
    def __init__(self, binary, extra_env=None):
        self.root = tempfile.mkdtemp(prefix="cp21-test-")
        self.buf = b""
        self.transcript = b""
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            env = dict(os.environ)
            env.update(HOME=self.root, TERM="dumb", LANG="C", LC_ALL="C")
            if extra_env:
                env.update(extra_env)
            if TTY_AUDIT:
                env["CP21_TTY_AUDIT"] = str(Path(self.root) / "tty-audit")
            os.chdir(self.root)
            os.execve(binary, [binary], env)

    def read_for(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            ready, _, _ = select.select([self.fd], [], [], 0.05)
            if not ready:
                continue
            try:
                data = os.read(self.fd, 16384)
            except OSError:
                return False
            if not data:
                return False
            self.buf += data
        return True

    def until(self, predicate, timeout=4.0, what="condition"):
        end = time.time() + timeout
        while True:
            if predicate():
                return True
            remaining = end - time.time()
            if remaining <= 0:
                break
            ready, _, _ = select.select([self.fd], [], [], min(remaining, 0.05))
            if not ready:
                continue
            try:
                data = os.read(self.fd, 16384)
            except OSError:
                break
            if not data:
                break
            self.buf += data
        if predicate():
            return True
        raise Timeout(what)

    def until_text(self, needle, timeout=4.0):
        if isinstance(needle, str):
            needle = needle.encode()
        return self.until(lambda: needle in self.buf, timeout, repr(needle))

    def prompt(self, timeout=4.0, initial=False):
        def ready():
            data = ANSI.sub(b"", self.buf)
            if initial:
                return b"~$ " in data
            # The command echo itself contains prompt redraws. Wait for a
            # prompt after the Enter newline; later background output is fine.
            newline = data.find(b"\n")
            return newline >= 0 and b"~$ " in data[newline + 1:]
        self.until(ready, timeout, "prompt")

    def line(self, text):
        self.transcript += self.buf
        self.buf = b""
        os.write(self.fd, (text + "\r").encode())

    def output(self):
        return self.clean(self.buf)

    def snapshot(self):
        return {"shell": proc_stat(self.pid), "children": children(self.pid),
                "output": self.clean(self.transcript + self.buf)}

    def clean(self, data):
        text = ANSI.sub(b"", data).decode(errors="replace").replace(self.root, "<root>")
        text = text.replace("\r", "")
        return re.sub(r"\b[0-9]{5,}\b", "<pid>", text)

    def close(self):
        if LINUX:
            for st in children(self.pid):
                if st["pgrp"] > 1 and st["pgrp"] != os.getpgrp():
                    try:
                        os.killpg(st["pgrp"], signal.SIGKILL)
                    except OSError:
                        pass
        else:
            try:
                os.killpg(self.pid, signal.SIGKILL)
            except OSError:
                pass
        try:
            os.kill(self.pid, signal.SIGKILL)
        except OSError:
            pass
        try:
            os.waitpid(self.pid, 0)
        except ChildProcessError:
            pass
        try:
            os.close(self.fd)
        except OSError:
            pass


def shell_owns_tty(s):
    if not LINUX:
        return None
    shell = proc_stat(s.pid)
    return bool(shell and shell["pgrp"] == shell["tpgid"])


def initial_prompt(s):
    s.prompt(initial=True)


def probe_foreground(s):
    s.line(PROBE)
    s.until_text("foreground=")
    s.prompt()
    return "tty=1,1,1 foreground=1" in s.output()


def wait_jobs_state(s, job_id, state, timeout=4.0):
    end = time.time() + timeout
    while time.time() < end:
        s.line("jobs")
        s.prompt()
        if f"[{job_id}] {state}" in s.output():
            return True
        time.sleep(0.02)
    raise Timeout(f"job {job_id} {state}")


def standalone_line(output, marker):
    return any(line.strip() == marker for line in output.splitlines())


def foreground_pipeline(s, r):
    ok = probe_foreground(s)
    r.check("foreground-owns-terminal", ok, s.output())
    if LINUX:
        r.check("foreground-shell-owns-tty", shell_owns_tty(s) is True)
    s.line("printf hi | /bin/cat")
    s.until_text("hi")
    s.prompt()
    r.check("foreground-pipeline-runs", "hi" in s.output())


def background_tty_stolen(records):
    return any(requested > 0 and requested != group and requested == observed and rc == 0
               for kind, group, requested, observed, rc in records if kind == "handoff")


def read_tty_audit(path, shell_pid):
    records = []
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) != 6 or parts[0] not in ("ready", "handoff"):
            raise OSError("malformed terminal audit")
        pid, group, requested, observed, rc = map(int, parts[1:])
        if pid == shell_pid:
            records.append((parts[0], group, requested, observed, rc))
    if not any(kind == "ready" and group == observed and rc == 0
               for kind, group, requested, observed, rc in records):
        raise OSError("terminal audit missing shell ownership prerequisite")
    return records


def background_owner(s, r):
    s.line("/bin/sleep 1 &")
    s.prompt()
    out = s.output()
    if "[1]" in out:
        r.witness("background-started")
    if TTY_AUDIT:
        # Read durable evidence after the handoff, before relying on a surviving
        # child. CP19 may already have reaped the mutant's child after tty EIO.
        records = read_tty_audit(Path(s.root) / "tty-audit", s.pid)
        if background_tty_stolen(records):
            r.check(MUTANT[1]["check"], False, records)
            return
    if LINUX:
        s.until(lambda: bool(children(s.pid)), 2.0, "background child")
        snap = s.snapshot()
        ok = shell_owns_tty(s) is True
        for kid in snap["children"]:
            ok = ok and kid["pgrp"] != snap["shell"]["pgrp"]
        if not r.check("background-never-owns-terminal", ok, snap):
            return
    fg_ok = probe_foreground(s)
    if not r.check("background-foreground-still-owns", fg_ok, s.output()):
        return
    s.line("jobs")
    s.prompt()
    r.check("background-running", "[1] Running" in s.output(), s.output())
    s.line("wait 1")
    s.prompt()
    s.line("jobs")
    s.prompt()
    r.check("background-done", "[1] Done (0)" in s.output(), s.output())


def ctrl_c(s, r):
    s.line("/bin/sh -c 'printf \"READY-%s\\n\" C; exec sleep 5'")
    s.until_text("READY-C")
    os.write(s.fd, b"\x03")
    s.prompt()
    if LINUX:
        r.check("ctrl-c-restores-terminal", shell_owns_tty(s) is True)
    r.check("ctrl-c-foreground-probe", probe_foreground(s))
    s.line("/bin/sh -c 'printf \"ALIVE-%s\\n\" C'")
    s.until_text("ALIVE-C")
    s.prompt()
    s.line("jobs")
    s.prompt()
    r.check("ctrl-c-status", "Done (130)" in s.output(), s.output())


def ctrl_z(s, r):
    s.line("/bin/sh -c 'printf \"READY-%s\\n\" Z; read line; printf \"GOT:%s\\n\" \"$line\"; exec sleep 5'")
    s.until_text("READY-Z")
    os.write(s.fd, b"\x1a")
    s.prompt()
    r.witness("ctrl-z-returned")
    if LINUX:
        restored = shell_owns_tty(s) is True
        if not r.check("ctrl-z-restores-terminal", restored):
            return
    s.line("jobs")
    s.prompt()
    out = s.output()
    r.witness("ctrl-z-jobs-queried")
    if "[1] Stopped" in out:
        r.witness("ctrl-z-stopped")
    if not r.check("ctrl-z-stopped-state", "[1] Stopped" in out, out):
        return
    s.line("fg 1")
    os.write(s.fd, b"HELLO-Z\r")
    try:
        s.until_text("GOT:HELLO-Z", 3.0)
        resumed = True
    except Timeout:
        resumed = False
    if not r.check("ctrl-z-fg-resumes", resumed, s.output()):
        return
    # Second stop/resume cycle: the job is foreground again after the read.
    os.write(s.fd, b"\x1a")
    s.prompt()
    s.line("jobs")
    s.prompt()
    r.check("ctrl-z-second-stop", "[1] Stopped" in s.output(), s.output())
    s.line("bg 1")
    s.prompt()
    try:
        wait_jobs_state(s, 1, "Running")
        second_running = True
    except Timeout:
        second_running = False
    r.check("ctrl-z-second-bg", second_running, s.output())
    s.line("fg 1")
    time.sleep(0.2)
    os.write(s.fd, b"\x03")
    s.prompt()
    r.check("ctrl-z-fg-interrupt", probe_foreground(s))


def stop_bg_fg(s, r):
    s.line("sh -c 'kill -STOP $$; exec sleep 5' &")
    s.prompt()
    r.witness("background-started")
    wait_jobs_state(s, 1, "Stopped")
    r.witness("background-stopped")
    s.line("bg 1")
    s.prompt()
    bg_out = s.output()
    if not r.check("bg-continues-job", "failed to continue job" not in bg_out, bg_out):
        return
    observed = None
    end = time.time() + 4.0
    while time.time() < end:
        s.line("jobs")
        s.prompt()
        out = s.output()
        if "[1] Running" in out:
            observed = "Running"
            break
        if "[1] Stopped" in out:
            observed = "Stopped"
            break
        if "[1] Done" in out:
            observed = "Done"
            break
        time.sleep(0.02)
    r.check("bg-state-observed", observed is not None, out if "out" in locals() else "")
    if observed is None:
        return
    # A missing SIGCONT leaves the exact child stopped. Done/signaled is an
    # unrelated termination and must not satisfy mutant detection.
    r.check("bg-state-live", observed in ("Running", "Stopped"), out)
    if not r.check("bg-state-running", observed == "Running", out):
        return
    s.line("fg 1")
    time.sleep(0.2)
    os.write(s.fd, b"\x03")
    s.prompt()
    r.check("stop-bg-fg-resume", probe_foreground(s))
    s.line("/bin/sh -c 'printf \"RESUME-%s\\n\" OK'")
    s.until_text("RESUME-OK")
    s.prompt()
    r.check("stop-bg-fg-command", "RESUME-OK" in s.output())


def jobs_ids(s, r):
    s.line("/bin/sleep 0.3 &")
    s.prompt()
    first = s.output()
    if "[1]" in first:
        r.witness("first-background")
    s.line("/bin/sleep 0.4 &")
    s.prompt()
    r.witness("second-background")
    s.line("jobs")
    s.prompt()
    out = s.output()
    if not r.check("job-ids-unique", "[1] Running" in out and "[2] Running" in out, out):
        return
    s.line("wait")
    s.prompt()
    s.line("jobs")
    s.prompt()
    r.check("job-ids-done", "[1] Done (0)" in s.output() and "[2] Done (0)" in s.output(), s.output())


def fg_reader_race(s, r):
    s.line("sh -c 'printf \"READY-%s\\n\" R; read line; printf \"GOT:%s\\n\" \"$line\"; exec sleep 5' &")
    s.until_text("READY-R")
    s.prompt()
    r.witness("background-started")
    wait_jobs_state(s, 1, "Stopped")
    r.witness("background-stopped")
    s.line("fg 1")
    os.write(s.fd, b"HELLO-R\r")
    try:
        s.until_text("GOT:HELLO-R", 3.0)
        resumed = True
    except Timeout:
        resumed = False
    if not r.check("fg-reader-resumes", resumed, s.output()):
        return
    if LINUX:
        r.check("fg-reader-owns-terminal", shell_owns_tty(s) is False)
    os.write(s.fd, b"\x03")
    s.prompt()
    r.check("fg-reader-interrupt", probe_foreground(s))


def fd_no_growth(s, r):
    if not LINUX:
        return
    before = len(os.listdir(f"/proc/{s.pid}/fd"))
    s.line("/bin/echo hi | /bin/cat | /bin/cat")
    s.until_text("hi")
    s.prompt()
    after = len(os.listdir(f"/proc/{s.pid}/fd"))
    r.check("foreground-fd-no-growth", after <= before, f"before={before} after={after}")


def fg_handoff_failure(s, r):
    s.line("sh -c 'kill -STOP $$; exec sleep 5' &")
    s.prompt()
    r.witness("background-started")
    wait_jobs_state(s, 1, "Stopped")
    r.witness("background-stopped")
    s.line("fg 1 && /bin/echo FG-SUCCESS")
    s.prompt()
    out = s.output()
    r.check("fg-handoff-error", "cannot take terminal" in out, out)
    r.check("fg-handoff-conditional-not-run", not standalone_line(out, "FG-SUCCESS"), out)
    s.line("jobs")
    s.prompt()
    r.check("fg-handoff-state-stopped", "[1] Stopped" in s.output(), s.output())


def restore_failure(s, r):
    s.line("/bin/true && /bin/sh -c 'printf \"RESTORE-%s\\n\" SUCCESS'")
    s.prompt()
    out = s.output()
    r.check("restore-error-reported", "failed to restore terminal" in out, out)
    r.check("restore-conditional-not-run", not standalone_line(out, "RESTORE-SUCCESS"), out)
    s.line("/bin/sh -c 'printf \"RECOVERED-%s\\n\" OK'")
    s.until_text("RECOVERED-OK")
    s.prompt()
    r.check("restore-recovery", "RECOVERED-OK" in s.output(), s.output())


def bg_continue_failure(s, r):
    s.line("sh -c 'kill -STOP $$; exec sleep 5' &")
    s.prompt()
    r.witness("background-started")
    wait_jobs_state(s, 1, "Stopped")
    r.witness("background-stopped")
    s.line("bg 1 && /bin/echo BG-SUCCESS")
    s.prompt()
    out = s.output()
    r.check("bg-error-propagated", "failed to continue job" in out, out)
    r.check("bg-conditional-not-run", not standalone_line(out, "BG-SUCCESS"), out)
    s.line("jobs")
    s.prompt()
    r.check("bg-failure-state-stopped", "[1] Stopped" in s.output(), s.output())
    s.line("fg 1 && /bin/echo FG-SUCCESS")
    s.prompt()
    out = s.output()
    r.check("fg-error-propagated", "failed to continue job" in out, out)
    r.check("fg-conditional-not-run", not standalone_line(out, "FG-SUCCESS"), out)
    s.line("jobs")
    s.prompt()
    r.check("fg-failure-state-stopped", "[1] Stopped" in s.output(), s.output())


SCENARIOS = {
    "foreground_pipeline": foreground_pipeline,
    "background_owner": background_owner,
    "ctrl_c": ctrl_c,
    "ctrl_z": ctrl_z,
    "stop_bg_fg": stop_bg_fg,
    "jobs_ids": jobs_ids,
    "fg_reader_race": fg_reader_race,
    "fd_no_growth": fd_no_growth,
    "bg_continue_failure": bg_continue_failure,
    "fg_handoff_failure": fg_handoff_failure,
    "restore_failure": restore_failure,
}


def run_scenario(binary, name, extra_env=None):
    extra_env = dict(extra_env or {})
    if name == "background_owner" and TTY_AUDIT:
        existing = extra_env.get("LD_PRELOAD", os.environ.get("LD_PRELOAD", ""))
        extra_env["LD_PRELOAD"] = TTY_AUDIT + (":" + existing if existing else "")
    s = Pty(binary, extra_env)
    # The path is known before exec through HOME; no inherited audit fd.
    r = Result()
    try:
        initial_prompt(s)
        SCENARIOS[name](s, r)
        r.completed = True
    except Timeout as exc:
        r.setup_error = f"timeout: {exc}"
    except (OSError, ChildProcessError, ValueError) as exc:
        r.setup_error = f"{type(exc).__name__}: {exc}"
    finally:
        s.close()
    return r


def report_setup_error(name, result):
    print(f"FAIL {name}: {result.setup_error}")


def main():
    global PROBE, TTY_AUDIT
    parser = argparse.ArgumentParser()
    parser.add_argument("nift")
    parser.add_argument("--probe", required=True, help="portable foreground-probe binary")
    parser.add_argument("--mutant", type=int, choices=sorted(MUTANT))
    parser.add_argument("--fault-delay", action="store_true")
    parser.add_argument("--fault-cont", action="store_true")
    parser.add_argument("--fault-handoff", action="store_true")
    parser.add_argument("--fault-restore", action="store_true")
    args = parser.parse_args()
    binary = os.path.abspath(args.nift)
    PROBE = os.path.abspath(args.probe)
    if sys.platform.startswith("linux"):
        TTY_AUDIT = str(Path(PROBE).parent / "cp21-tty-audit.so")
        if not Path(TTY_AUDIT).is_file():
            print("FAIL missing terminal audit library " + TTY_AUDIT)
            return 1
    if not os.access(binary, os.X_OK):
        print(f"FAIL missing binary {binary}")
        return 1
    if not os.access(PROBE, os.X_OK):
        print(f"FAIL missing probe {PROBE}")
        return 1

    if args.fault_delay:
        result = run_scenario(binary, "fg_reader_race",
                              {"LD_PRELOAD": os.environ.get("CP21_DELAY_LIB", "")})
        if result.setup_error:
            report_setup_error("fault-delay", result)
            return 1
        failed = result.failed_checks()
        print("PASS fault-delay fg reader" if not failed else f"FAIL fault-delay: {failed}")
        return 1 if failed else 0

    if args.fault_cont:
        result = run_scenario(binary, "bg_continue_failure",
                              {"LD_PRELOAD": os.environ.get("CP21_FAIL_CONT_LIB", "")})
        if result.setup_error:
            report_setup_error("fault-cont", result)
            return 1
        failed = result.failed_checks()
        print("PASS fault-cont failure propagation" if not failed else f"FAIL fault-cont: {failed}")
        return 1 if failed else 0

    if args.fault_handoff:
        result = run_scenario(binary, "fg_handoff_failure",
                              {"LD_PRELOAD": os.environ.get("CP21_RESTORE_LIB", ""),
                               "CP21_FAIL_HANDOFF": "1"})
        if result.setup_error:
            report_setup_error("fault-handoff", result)
            return 1
        failed = result.failed_checks()
        print("PASS fault-handoff acquisition failure" if not failed else f"FAIL fault-handoff: {failed}")
        return 1 if failed else 0

    if args.fault_restore:
        result = run_scenario(binary, "restore_failure",
                              {"LD_PRELOAD": os.environ.get("CP21_RESTORE_LIB", ""),
                               "CP21_FAIL_RESTORE": "1"})
        if result.setup_error:
            report_setup_error("fault-restore", result)
            return 1
        failed = result.failed_checks()
        print("PASS fault-restore exit status and recovery" if not failed else f"FAIL fault-restore: {failed}")
        return 1 if failed else 0

    if args.mutant:
        spec = MUTANT[args.mutant]
        result = run_scenario(binary, spec["scenario"])
        if result.setup_error:
            print(f"FAIL CP21 mutant {args.mutant}: no behavioural evidence ({result.setup_error})")
            return 1
        failed = result.failed_checks()
        if spec["witness"] not in result.witnesses:
            print(f"FAIL CP21 mutant {args.mutant}: missing witness {spec['witness']}")
            return 1
        if failed != [spec["check"]]:
            print(f"FAIL CP21 mutant {args.mutant}: expected only {spec['check']}, saw {failed}")
            return 1
        print(f"PASS CP21 mutant {args.mutant} detected by {spec['check']}")
        return 0

    failures = []
    for name in SCENARIOS:
        if name in ("bg_continue_failure", "fg_handoff_failure", "restore_failure"):
            continue
        result = run_scenario(binary, name)
        if result.setup_error:
            failures.append((name, result.setup_error))
            report_setup_error(name, result)
            continue
        failures.extend((check, "") for check in result.failed_checks())
    if failures:
        print("FAILED:", ", ".join(name for name, _ in failures))
        return 1
    print("PASS CP21 interactive Host job control")
    return 0


if __name__ == "__main__":
    sys.exit(main())
