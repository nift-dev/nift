#!/usr/bin/env python3
"""CP21 interactive Host job-control contract.

Drives a real PTY and checks the migrated interactive shell: foreground
terminal ownership, background groups, Ctrl-C, Ctrl-Z, fg/bg resume, job IDs,
completion and a bounded prompt-latency diagnostic.

With --mutant N the same scenarios are run against a deliberate CP21 mutant
build; the named scenario must fail (a mutant that still passes is a test
defect, not a success).
"""
import argparse
import os
import pty
import re
import select
import signal
import sys
import tempfile
import time
from pathlib import Path

ANSI = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]|\x1b\][^\x07]*\x07")
LINUX = sys.platform.startswith("linux")

MUTANT_SCENARIO = {
    1: "background_owner",
    2: "ctrl_z",
    3: "ctrl_z",
    4: "ctrl_z",
    5: "jobs_ids",
}

failures = []


class Timeout(Exception):
    pass


def proc_stat(pid):
    if not LINUX:
        return None
    try:
        fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return {"state": fields[0], "pgrp": int(fields[2]), "tpgid": int(fields[5])}
    except (OSError, ValueError):
        return None


def children(pid):
    found = []
    if not LINUX:
        return found
    for entry in Path("/proc").glob("[0-9]*"):
        st = proc_stat(entry.name)
        if not st:
            continue
        try:
            raw = Path(f"/proc/{entry.name}/stat").read_text().rsplit(")", 1)[1].split()
            if int(raw[1]) == pid:
                found.append(st)
        except (OSError, ValueError):
            pass
    return found


class Pty:
    def __init__(self, binary):
        self.root = tempfile.mkdtemp(prefix="cp21-test-")
        self.buf = b""
        self.transcript = b""
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.environ.update(HOME=self.root, TERM="dumb", LANG="C", LC_ALL="C")
            os.chdir(self.root)
            os.execv(binary, [binary])

    def read_for(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            ready, _, _ = select.select([self.fd], [], [], 0.05)
            if not ready:
                continue
            try:
                data = os.read(self.fd, 8192)
            except OSError:
                return False
            if not data:
                return False
            self.buf += data
        return True

    def until(self, predicate, timeout=4.0, what="condition"):
        end = time.time() + timeout
        while time.time() < end:
            if predicate():
                return True
            self.read_for(0.05)
        raise Timeout(what)

    def until_text(self, needle, timeout=4.0):
        if isinstance(needle, str):
            needle = needle.encode()
        return self.until(lambda: needle in self.buf, timeout, repr(needle))

    def prompt(self, timeout=4.0):
        self.until(lambda: ANSI.sub(b"", self.buf[-256:]).rstrip().endswith(b"~$"),
                   timeout, "prompt")

    def line(self, text):
        self.transcript += self.buf
        self.buf = b""
        os.write(self.fd, (text + "\r").encode())

    def snapshot(self):
        return {"shell": proc_stat(self.pid), "children": children(self.pid),
                "output": self.clean(self.transcript + self.buf)}

    def clean(self, data):
        text = ANSI.sub(b"", data).decode(errors="replace").replace(self.root, "<root>")
        text = text.replace("\r", "")
        return re.sub(r"\b[0-9]{5,}\b", "<pid>", text)

    def wait_child(self, state=None, timeout=4.0):
        def ready():
            kids = children(self.pid)
            if not kids:
                return False
            return state is None or any(kid["state"] == state for kid in kids)
        self.until(ready, timeout, f"child state {state or 'any'}")

    def close(self):
        try:
            if LINUX:
                for st in children(self.pid):
                    if st["pgrp"] > 1 and st["pgrp"] != os.getpgrp():
                        try:
                            os.killpg(st["pgrp"], signal.SIGKILL)
                        except OSError:
                            pass
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


def check(name, condition, detail=""):
    print(f"{'PASS' if condition else 'FAIL'} {name}")
    if not condition:
        failures.append((name, detail))


def shell_owns_tty(snap):
    shell = snap["shell"]
    return bool(shell and shell["pgrp"] == shell["tpgid"])


def fg_pipeline(s):
    s.line("/bin/sh -c 'for i in 0 1 2; do if [ -t $i ]; then echo fd$i-tty; else echo fd$i-pipe; fi; done'")
    s.until_text("fd2-tty")
    s.prompt()
    snap = s.snapshot()
    check("foreground-pipeline-tty", "fd2-tty" in snap["output"] and shell_owns_tty(snap), snap)
    return True


def background_owner(s):
    s.line("/bin/sleep 1 &")
    s.until_text("[1]")
    s.prompt()
    snap = s.snapshot()
    if not LINUX:
        return True
    ok = shell_owns_tty(snap)
    for kid in snap["children"]:
        ok = ok and kid["pgrp"] != snap["shell"]["pgrp"]
    check("background-never-owns-terminal", ok, snap)
    if not ok:
        return False
    s.line("wait 1")
    s.prompt()
    s.line("jobs")
    s.until_text("Done (0)")
    s.prompt()
    return True


def ctrl_c(s):
    s.line("/bin/sleep 5")
    s.wait_child("S")
    os.write(s.fd, b"\x03")
    s.prompt()
    snap = s.snapshot()
    check("ctrl-c-restores-shell", shell_owns_tty(snap), snap)
    s.line("/bin/echo ALIVE-C")
    s.until_text("ALIVE-C")
    s.prompt()
    s.line("jobs")
    s.until_text("Done (130)")
    s.prompt()
    return True


def ctrl_z(s):
    s.line("/bin/sleep 5")
    s.wait_child("S")
    os.write(s.fd, b"\x1a")
    s.prompt()
    snap = s.snapshot()
    restored = shell_owns_tty(snap)
    check("ctrl-z-restores-terminal", restored, snap)
    if not restored:
        return False
    s.line("jobs")
    s.until_text("Stopped")
    s.prompt()
    s.line("fg 1")
    s.wait_child("S")
    os.write(s.fd, b"\x03")
    s.prompt()
    s.line("/bin/echo ALIVE-Z")
    s.until_text("ALIVE-Z")
    s.prompt()
    snap = s.snapshot()
    check("ctrl-z-fg-resumes", shell_owns_tty(snap), snap)
    return shell_owns_tty(snap)


def stop_bg_fg(s):
    s.line("sh -c 'kill -STOP $$; sleep 2' &")
    s.wait_child("T")
    s.prompt()
    s.line("jobs")
    s.until_text("Stopped")
    s.prompt()
    s.line("bg 1")
    s.wait_child("S")
    s.line("jobs")
    s.until_text("Running")
    s.prompt()
    s.line("fg 1")
    s.wait_child("S")
    os.write(s.fd, b"\x03")
    s.prompt()
    s.line("/bin/echo STOP-RESUME")
    s.until_text("STOP-RESUME")
    s.prompt()
    snap = s.snapshot()
    check("stop-bg-fg-resume", shell_owns_tty(snap), snap)
    return shell_owns_tty(snap)


def jobs_ids(s):
    s.line("/bin/sleep 0.3 &")
    s.until_text("[1]")
    s.prompt()
    s.line("/bin/sleep 0.4 &")
    s.until_text("[2]")
    s.prompt()
    s.line("jobs")
    s.until_text("[1] Running")
    s.until_text("[2] Running")
    s.prompt()
    s.line("wait")
    s.prompt()
    s.line("jobs")
    s.until_text("[1] Done (0)")
    s.until_text("[2] Done (0)")
    s.prompt()
    out = s.snapshot()["output"]
    check("job-ids-unique", "[1] Running" in out and "[2] Running" in out, out)
    return "[1] Running" in out and "[2] Running" in out


def fd_no_growth(s):
    if not LINUX:
        return True
    before = len(os.listdir(f"/proc/{s.pid}/fd"))
    s.line("/bin/echo hi | /bin/cat | /bin/cat")
    s.until_text("hi")
    s.prompt()
    after = len(os.listdir(f"/proc/{s.pid}/fd"))
    check("foreground-fd-no-growth", after <= before, f"before={before} after={after}")
    return after <= before


def prompt_latency(binary):
    samples = []
    for _ in range(3):
        s = Pty(binary)
        try:
            s.prompt()
            s.transcript += s.buf
            s.buf = b""
            start = time.perf_counter()
            payload = ("/bin/true\r" * 200 + "/bin/echo CP21-MARK\r").encode()
            os.write(s.fd, payload)
            s.until_text("CP21-MARK", timeout=10.0)
            samples.append((time.perf_counter() - start) * 1000.0 / 201)
        finally:
            s.close()
    median = sorted(samples)[len(samples) // 2]
    check("prompt-latency-<=3ms", median <= 3.0, f"samples={samples} median={median:.4f}")
    print(f"prompt-latency median={median:.4f} ms/command samples={[round(x, 4) for x in samples]}")
    return median <= 3.0


def run_scenario(binary, name):
    s = Pty(binary)
    try:
        return {
            "foreground_pipeline": fg_pipeline,
            "background_owner": background_owner,
            "ctrl_c": ctrl_c,
            "ctrl_z": ctrl_z,
            "stop_bg_fg": stop_bg_fg,
            "jobs_ids": jobs_ids,
            "fd_no_growth": fd_no_growth,
        }[name](s)
    except Timeout as exc:
        check(name, False, f"timeout: {exc}")
        return False
    finally:
        s.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("nift")
    parser.add_argument("--mutant", type=int, choices=sorted(MUTANT_SCENARIO))
    parser.add_argument("--skip-latency", action="store_true",
                        help="sanitizer builds: the <=3 ms gate is a release-build diagnostic")
    args = parser.parse_args()
    binary = os.path.abspath(args.nift)
    if not os.access(binary, os.X_OK):
        print(f"FAIL missing binary {binary}")
        return 1

    if args.mutant:
        name = MUTANT_SCENARIO[args.mutant]
        passed = run_scenario(binary, name)
        if passed:
            print(f"FAIL CP21 mutant {args.mutant} not detected by {name}")
            return 1
        print(f"PASS CP21 mutant {args.mutant} detected by {name}")
        return 0

    for name in ("foreground_pipeline", "background_owner", "ctrl_c", "ctrl_z",
                 "stop_bg_fg", "jobs_ids", "fd_no_growth"):
        run_scenario(binary, name)
    if not args.skip_latency:
        prompt_latency(binary)
    if failures:
        print("FAILED:", ", ".join(name for name, _ in failures))
        return 1
    print("PASS CP21 interactive Host job control")
    return 0


if __name__ == "__main__":
    sys.exit(main())
