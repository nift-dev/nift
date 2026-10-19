#!/usr/bin/env python3
"""feature-host-deps (D-028): bounded whole reads (NIFT_READ_LIMIT).

Every operation that reads a whole file, the rest of a stream, one stream
line/token or a process capture into memory stops at the configured limit and
fails with a fatal, uncatchable limit error that names the operation and the
limit. Nothing is truncated silently, streaming still handles files larger
than the limit, and a page build that hits the limit keeps its last-good
output and valid build state.

Every case that could allocate runs under a hard address-space limit
(RLIMIT_AS, i.e. `ulimit -v`) of baseline + 1.5 x limit + slack, so a bounding
bug fails the test instead of exhausting the host; the exceeding cases also
assert that peak RSS stays within baseline + limit + slack (the coordinator's
"never keep allocating beyond the bound").

Usage: NIFT_BIN=/path/to/nift python3 tests/runtime2/read_limit.py
"""
import os
import resource
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

NIFT = os.path.abspath(os.environ.get("NIFT_BIN") or "./nift")
MiB = 1 << 20
LIMIT = 16 * MiB                     # NIFT_READ_LIMIT for the memory cases
SLACK = 16 * MiB                     # allocator/arena slack on top of the bound
failures = []


def check(name, condition, detail=""):
    if condition:
        print("PASS", name)
    else:
        failures.append(name)
        print("FAIL", name, detail)


def baseline_vm():
    """VmPeak of a trivial script, in bytes (the process's own floor)."""
    with tempfile.TemporaryDirectory() as d:
        script = Path(d) / "vm.f"
        script.write_text('x := open("/proc/self/status")\nprint(x)\n')
        out = subprocess.run([NIFT, str(script)], capture_output=True, text=True, timeout=60).stdout
    peak = hwm = 0
    for line in out.splitlines():
        if line.startswith("VmPeak:"):
            peak = int(line.split()[1]) * 1024
        if line.startswith("VmHWM:"):
            hwm = int(line.split()[1]) * 1024
    return peak or 64 * MiB, hwm or 16 * MiB


def run(script_text, cwd, limit=LIMIT, address_space=None, env_extra=None, timeout=60, stdin=None):
    script = Path(cwd) / "case.f"
    script.write_text(script_text)
    env = dict(os.environ)
    env.pop("NIFT_READ_LIMIT", None)
    if limit is not None:
        env["NIFT_READ_LIMIT"] = str(limit)
    if env_extra:
        env.update(env_extra)

    def limit_memory():
        if address_space:
            resource.setrlimit(resource.RLIMIT_AS, (address_space, address_space))

    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        proc = subprocess.Popen([NIFT, str(script)], cwd=cwd, stdout=out, stderr=err, env=env,
                                preexec_fn=limit_memory, stdin=stdin if stdin is not None else subprocess.DEVNULL)
        deadline = time.monotonic() + timeout
        while True:
            pid, status, usage = os.wait4(proc.pid, os.WNOHANG)
            if pid:
                break
            if time.monotonic() > deadline:
                proc.kill()
                pid, status, usage = os.wait4(proc.pid, 0)
                break
            time.sleep(0.01)
        proc.returncode = os.waitstatus_to_exitcode(status)
        out.seek(0)
        err.seek(0)
        proc.stdout = out.read().decode(errors="replace")
        proc.stderr = err.read().decode(errors="replace")
    proc.peak_rss = usage.ru_maxrss * 1024  # KiB on Linux, this child only
    return proc


def limit_error(proc, operation, limit=LIMIT):
    text = proc.stdout + proc.stderr
    return (proc.returncode != 0 and ("%s:" % operation) in text and
            ("exceeds the read limit of %d bytes (NIFT_READ_LIMIT)" % limit) in text and
            "CAUGHT" not in text and "AFTER" not in text)


def main():
    if not os.path.exists(NIFT):
        sys.exit("nift binary not found: " + NIFT)
    vm_peak, vm_hwm = baseline_vm()
    address_space = vm_peak + LIMIT * 3 // 2 + SLACK
    rss_bound = vm_hwm + LIMIT + SLACK
    print("baseline VmPeak %d MiB, VmHWM %d MiB; RLIMIT_AS %d MiB; RSS bound %d MiB; limit %d MiB" %
          (vm_peak // MiB, vm_hwm // MiB, address_space // MiB, rss_bound // MiB, LIMIT // MiB))
    work = Path(tempfile.mkdtemp(prefix="read-limit-"))
    try:
        cases(work, address_space, rss_bound)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if failures:
        print("%d failure(s)" % len(failures))
        return 1
    print("all read-limit checks passed")
    return 0


def exceed(name, operation, body, work, address_space, rss_bound, limit=LIMIT):
    """`body` must exceed the limit; wrapped in try/catch to prove the error
    is not catchable, followed by a print that must never run."""
    script = 'try {\n%s\n} catch(e) { print("CAUGHT " + e.message) }\nprint("AFTER")\n' % body
    proc = run(script, work, limit=limit, address_space=address_space)
    check("%s: fatal %s limit error (exit %d)" % (name, operation, proc.returncode), limit_error(proc, operation, limit),
          (proc.stdout + proc.stderr)[-400:])
    if proc.peak_rss is not None:
        check("%s: peak RSS %d MiB within %d MiB" % (name, proc.peak_rss // MiB, rss_bound // MiB),
              proc.peak_rss <= rss_bound)


def cases(work, address_space, rss_bound):
    zero, full = "/dev/zero", "/dev/full"
    # --- regular files: exact boundary vs one byte over ----------------------
    small = 1024
    (work / "exact.bin").write_bytes(b"a" * small)
    (work / "over.bin").write_bytes(b"a" * (small + 1))
    (work / "empty.bin").write_bytes(b"")
    for op, expr in [("open", 'open("%s").length()'), ("open_bytes", 'open_bytes("%s").length()')]:
        proc = run('print(%s)\n' % (expr % (work / "exact.bin")), work, limit=small)
        check("%s: file of exactly the limit is read whole" % op, proc.returncode == 0 and proc.stdout.strip() == str(small),
              proc.stdout + proc.stderr)
        proc = run('print(%s)\n' % (expr % (work / "empty.bin")), work, limit=small)
        check("%s: empty file" % op, proc.returncode == 0 and proc.stdout.strip() == "0", proc.stdout + proc.stderr)
        exceed("%s one byte over" % op, op, 'x := %s' % (expr % (work / "over.bin")), work, address_space, rss_bound, small)
    proc = run('f := file("%s")\nf.open("r")\nprint(f.read_all().length())\nf.close()\n' % (work / "exact.bin"), work, limit=small)
    check("file().open: exact limit", proc.returncode == 0 and proc.stdout.strip() == str(small), proc.stdout + proc.stderr)
    exceed("file().open one byte over", "open", 'f := file("%s")\nf.open("r")' % (work / "over.bin"), work, address_space, rss_bound, small)
    proc = run('s := ifstream("%s")\nprint(s.read_all().length())\nclose(s)\n' % (work / "exact.bin"), work, limit=small)
    check("ifstream read_all: exact limit", proc.returncode == 0 and proc.stdout.strip() == str(small), proc.stdout + proc.stderr)
    exceed("ifstream read_all one byte over", "read_all", 's := ifstream("%s")\nx := s.read_all()' % (work / "over.bin"),
           work, address_space, rss_bound, small)
    (work / "line-exact.txt").write_bytes(b"b" * small + b"\nnext\n")
    (work / "line-over.txt").write_bytes(b"b" * (small + 1) + b"\n")
    proc = run('s := ifstream("%s")\nprint(s.read_line().length())\nprint(s.read_line())\nclose(s)\n' % (work / "line-exact.txt"),
               work, limit=small)
    check("read_line: line of exactly the limit", proc.returncode == 0 and proc.stdout.split() == [str(small), "next"],
          proc.stdout + proc.stderr)
    exceed("read_line one byte over", "read_line", 's := ifstream("%s")\nx := s.read_line()' % (work / "line-over.txt"),
           work, address_space, rss_bound, small)

    # --- character devices: never reach EOF / report size 0 -----------------
    for device in (zero, full):
        exceed("open(%s)" % device, "open", 'x := open("%s")' % device, work, address_space, rss_bound)
        exceed("open_bytes(%s)" % device, "open_bytes", 'x := open_bytes("%s")' % device, work, address_space, rss_bound)
        exceed("cat(%s)" % device, "cat", 'cat("%s")' % device, work, address_space, rss_bound)
        exceed("file(%s).open(r)" % device, "open", 'f := file("%s")\nf.open("r")' % device, work, address_space, rss_bound)
        # Q-H8: "w" still reads the existing content first; now bounded.
        exceed("file(%s).open(w)" % device, "open", 'f := file("%s")\nf.open("w")' % device, work, address_space, rss_bound)
    exceed("ifstream(/dev/zero).read_all", "read_all", 's := ifstream("/dev/zero")\nx := s.read_all()', work, address_space, rss_bound)
    exceed("ifstream(/dev/zero).read_all_bytes", "read_all_bytes", 's := ifstream("/dev/zero")\nx := s.read_all_bytes()',
           work, address_space, rss_bound)
    exceed("ifstream(/dev/zero).read_line", "read_line", 's := ifstream("/dev/zero")\nx := s.read_line()', work, address_space, rss_bound)
    exceed("ifstream(/dev/zero).read_bytes(huge)", "read_bytes", 's := ifstream("/dev/zero")\nx := s.read_bytes(%d)' % (8 * LIMIT),
           work, address_space, rss_bound)
    exceed("ifstream(/dev/zero).read(huge)", "read", 's := ifstream("/dev/zero")\nx := s.read(%d)' % (8 * LIMIT),
           work, address_space, rss_bound)
    exceed("ifstream(/dev/zero) >> token", "extraction", 's := ifstream("/dev/zero")\nt := ""\ns >> t', work, address_space, rss_bound)
    exceed("ifstream(/dev/zero).read_val", "read_val", 's := ifstream("/dev/zero")\nx := s.read_val()', work, address_space, rss_bound)
    # Bounded reads below the limit keep working on an endless device.
    proc = run('s := ifstream("/dev/zero")\nn := 0\nk := 0\nwhile(k < 64) { n = n + s.read_bytes(%d).length(); k = k + 1 }\nclose(s)\nprint(n)\n'
               % (LIMIT // 2), work, address_space=address_space)
    check("read_bytes(limit/2) x 64 from /dev/zero (32 x limit streamed)",
          proc.returncode == 0 and proc.stdout.strip() == str(32 * LIMIT), (proc.stdout + proc.stderr)[-300:])

    # --- streaming handles files larger than the limit -----------------------
    big = work / "big.txt"
    with big.open("wb") as handle:
        line = b"x" * 1023 + b"\n"
        for _ in range(3 * LIMIT // len(line)):
            handle.write(line)
    proc = run('s := ifstream("%s")\nn := 0\nwhile(s.read_line() != null) { n = n + 1 }\nclose(s)\nprint(n)\n' % big, work,
               address_space=address_space, timeout=300)
    check("read_line loop over a file 3 x the limit", proc.returncode == 0 and proc.stdout.strip() == str(3 * LIMIT // 1024),
          (proc.stdout + proc.stderr)[-300:])
    proc = run('print(sha256_file("%s").length())\n' % big, work, address_space=address_space)
    check("sha256_file of a file 3 x the limit", proc.returncode == 0 and proc.stdout.strip() == "64", proc.stdout + proc.stderr)
    exceed("open() of a regular file 3 x the limit", "open", 'x := open("%s")' % big, work, address_space, rss_bound)

    # --- FIFOs: a writer that stops at the limit, one past it, one that never stops
    fifo_cases(work, address_space, rss_bound)

    # --- a growing file: bounded whatever the writer does ---------------------
    grow = work / "grow.txt"
    grow.write_bytes(b"g" * (LIMIT - 4096))
    stop = threading.Event()

    def grower():
        with grow.open("ab") as handle:
            while not stop.is_set():
                handle.write(b"g" * 65536)
                handle.flush()
    thread = threading.Thread(target=grower)
    thread.start()
    try:
        proc = run('x := open("%s")\nprint(x.length())\n' % grow, work, address_space=address_space)
    finally:
        stop.set()
        thread.join()
    ok = (proc.returncode == 0 and int(proc.stdout.strip() or "0") <= LIMIT) or limit_error(proc, "open")
    check("growing file: whole value within the limit or a limit error (exit %d)" % proc.returncode, ok,
          (proc.stdout + proc.stderr)[-300:])

    # --- process capture -------------------------------------------------------
    head = shutil.which("head")
    if head:
        proc = run('r := run("%s", "-c", "%d", "/dev/zero")\nprint(r.stdout.length())\n' % (head, small), work, limit=small)
        check("run(): capture of exactly the limit", proc.returncode == 0 and proc.stdout.strip() == str(small), proc.stdout + proc.stderr)
        exceed("run(): capture one byte over", "run", 'r := run("%s", "-c", "%d", "/dev/zero")' % (head, small + 1),
               work, address_space, rss_bound, small)
        exceed("cmd().run(): capture of %d MiB" % (2 * LIMIT // MiB), "run",
               'r := cmd("%s", "-c", "%d", "/dev/zero").run()' % (head, 2 * LIMIT), work, address_space, rss_bound)
    else:
        print("SKIP process capture (no head)")

    # --- configuration --------------------------------------------------------
    for raw, expect in [("1K", 1024), ("2MiB", 2 * MiB), ("1g", 1 << 30), ("4096", 4096), ("3m", 3 * MiB)]:
        # A sparse regular file one byte over: rejected from its size, unread.
        sparse = work / "sparse.bin"
        with sparse.open("wb") as handle:
            handle.truncate(expect + 1)
        proc = run('x := open("%s")\nprint("AFTER")\n' % sparse, work, limit=raw, address_space=address_space)
        check("NIFT_READ_LIMIT=%s means %d bytes" % (raw, expect), limit_error(proc, "open", expect),
              (proc.stdout + proc.stderr)[-200:])
        sparse.unlink()
    for raw in ["0", "-1", "abc", "12Q", "99999999999999999999", "18014398509481984T"]:
        proc = run('x := open("%s")\n' % (work / "exact.bin"), work, limit=raw)
        check("NIFT_READ_LIMIT=%s is rejected loudly" % raw,
              proc.returncode != 0 and "invalid NIFT_READ_LIMIT value" in proc.stdout + proc.stderr, proc.stdout + proc.stderr)
    default_limit(work)

    # --- a page build that hits the limit keeps last-good output and state ----
    page_build(work)


def fifo_cases(work, address_space, rss_bound):
    fifo = work / "pipe"
    os.mkfifo(fifo)

    def writer(count, forever=False):
        def body():
            try:
                with open(fifo, "wb") as handle:
                    if forever:
                        block = b"y\n" * 32768
                        while True:
                            handle.write(block)
                    left = count
                    while left > 0:
                        chunk = min(left, 65536)
                        handle.write(b"f" * chunk)
                        left -= chunk
            except (BrokenPipeError, OSError):
                pass
        thread = threading.Thread(target=body, daemon=True)
        thread.start()
        return thread

    small = 4096
    thread = writer(small)
    proc = run('print(open("%s").length())\n' % fifo, work, limit=small, address_space=address_space)
    thread.join(10)
    check("FIFO: exactly the limit", proc.returncode == 0 and proc.stdout.strip() == str(small), proc.stdout + proc.stderr)
    thread = writer(small + 1)
    exceed("FIFO: one byte over", "open", 'x := open("%s")' % fifo, work, address_space, rss_bound, small)
    thread.join(10)
    thread = writer(0, forever=True)
    exceed("FIFO whose writer never stops (open_bytes)", "open_bytes", 'x := open_bytes("%s")' % fifo, work, address_space, rss_bound)
    thread.join(10)
    thread = writer(0, forever=True)
    exceed("FIFO whose writer never stops (ifstream read_all)", "read_all", 's := ifstream("%s")\nx := s.read_all()' % fifo,
           work, address_space, rss_bound)
    thread.join(10)
    check("FIFO writers finished (reader released the pipe)", not thread.is_alive())


def default_limit(work):
    """The documented default (1 GiB) applies when NIFT_READ_LIMIT is unset."""
    available = 0
    try:
        for line in open("/proc/meminfo"):
            if line.startswith("MemAvailable:"):
                available = int(line.split()[1]) * 1024
    except OSError:
        pass
    if available < 4 * (1 << 30):
        print("SKIP default 1 GiB limit (MemAvailable %d MiB < 4096)" % (available // MiB))
        return
    vm_peak, _ = baseline_vm()
    gib = 1 << 30
    proc = run('x := open_bytes("/dev/zero")\n', work, limit=None, address_space=vm_peak + gib * 3 // 2 + SLACK, timeout=300)
    check("default limit: open_bytes(/dev/zero) stops at 1073741824 bytes",
          limit_error(proc, "open_bytes", gib), (proc.stdout + proc.stderr)[-300:])


def page_build(work):
    site = work / "site"
    site.mkdir()
    subprocess.run([NIFT, "init"], cwd=site, capture_output=True, check=True)
    (site / "content" / "index.html").write_text('<p>GOOD:@script { return open("data.txt") }</p>\n')
    (site / "data.txt").write_text("v1\n")
    env = dict(os.environ, NIFT_READ_LIMIT="1024")
    first = subprocess.run([NIFT, "build", "--all"], cwd=site, capture_output=True, text=True, env=env)
    good = (site / "public" / "index.html").read_bytes()
    check("page build: initial build", first.returncode == 0 and b"GOOD:v1" in good, first.stdout + first.stderr)
    (site / "data.txt").write_text("x" * 1025)
    failed = subprocess.run([NIFT, "build"], cwd=site, capture_output=True, text=True, env=env)
    text = failed.stdout + failed.stderr
    check("page build: limit is a build failure naming the operation and limit",
          failed.returncode != 0 and "open:" in text and "exceeds the read limit of 1024 bytes (NIFT_READ_LIMIT)" in text, text)
    check("page build: last-good output preserved", (site / "public" / "index.html").read_bytes() == good)
    check("page build: no interrupted-state marker", not (site / ".nift" / ".unfinished").exists())
    (site / "data.txt").write_text("v2\n")
    fixed = subprocess.run([NIFT, "build"], cwd=site, capture_output=True, text=True, env=env)
    check("page build: next build after the fix succeeds without --repair",
          fixed.returncode == 0 and b"GOOD:v2" in (site / "public" / "index.html").read_bytes(), fixed.stdout + fixed.stderr)
    status = subprocess.run([NIFT, "status"], cwd=site, capture_output=True, text=True, env=env).stdout
    check("page build: up to date afterwards", "up to date" in status, status)


if __name__ == "__main__":
    sys.exit(main())
