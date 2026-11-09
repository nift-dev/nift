#!/usr/bin/env python3
"""B01 (combined-b3-b5.md section 43): permanent negative controls for the VM's
read limit (NIFT_READ_LIMIT) on whole reads, stream reads and lines.

Each check runs the script on the VM (the default engine) and, where the
script's outcome is the point, with NIFT_VM=0 as well: the VM's exit status,
stdout and stderr (the fatal "<op>: <what> exceeds the read limit of N bytes
(NIFT_READ_LIMIT)" error and its line:column) must equal legacy's.

  A. A limit below one 64 KiB read chunk (1000 bytes): exactly the limit is
     read whole; one byte over is the limit error (open, file().open,
     ifstream read_all, read_line). Before B01 the per-chunk check wrapped
     (`limit - got`) and one byte over was read silently.
  B. A limit that is not a power of two (40 MiB + 12345), under RLIMIT_AS =
     baseline + 1.5 x limit + slack: /dev/zero, a FIFO one byte over, and a
     sparse regular file 3 x the limit stop with the limit error, never a
     std::bad_alloc (exit 70); a regular file of exactly the limit is read.
     Before B01 the 3x file was pre-sized whole and read_line doubled past
     the limit.
  C. Short reads count against the limit: a FIFO writer paced chunk by chunk
     (each chunk is written only once the reader has drained the previous
     one, so every read(2) the reader makes returns at most that one chunk,
     far less than it asked for) that crosses the limit hits the limit error;
     one that stays under it, or reaches it exactly, and then closes is read
     whole. The pacing is deterministic, with no sleeps: the writer blocks on
     the pipe's own fill level (FIONREAD) between chunks.

tests/runtime2/read_limit_mutants.sh rebuilds the read natives with each
NIFT_READ_LIMIT_MUTANT hook and requires this file to fail on every mutant
and to pass on the unmutated control.

Usage: NIFT_BIN=/path/to/nift python3 -B tests/runtime2/read_limit_controls.py
"""
import array
import fcntl
import os
import select
import shutil
import sys
import tempfile
import termios
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import read_limit as rl  # noqa: E402  (run(), baseline_vm(), limit_error(): one harness)

MiB = rl.MiB
SMALL = 1000                          # below one 64 KiB read chunk
LARGE = 40 * MiB + 12345              # not a power of two
PACED = 300000                        # the paced-FIFO limit: more than four 64 KiB requests
PACED_CHUNKS = (1, 999, 4096, 3, 65535, 4097, 17)   # every one shorter than the 64 KiB request
check = rl.check


def not_bad_alloc(proc):
    text = proc.stdout + proc.stderr
    return proc.returncode != 70 and "bad_alloc" not in text and "internal" not in text.lower()


def parity(name, script, work, limit, address_space=None, before=None):
    """The VM's outcome equals legacy's (exit status, stdout, stderr)."""
    results = []
    for engine in ("vm", "legacy"):
        if before:
            before()
        extra = {"NIFT_VM": "0"} if engine == "legacy" else None
        results.append(rl.run(script, work, limit=limit, address_space=address_space, env_extra=extra))
    vm, legacy = results
    same = (vm.returncode, vm.stdout, vm.stderr) == (legacy.returncode, legacy.stdout, legacy.stderr)
    check("%s: VM matches legacy (exit %d)" % (name, vm.returncode), same,
          "\nVM     %d %r %r\nlegacy %d %r %r" % (vm.returncode, vm.stdout[-300:], vm.stderr[-300:],
                                                  legacy.returncode, legacy.stdout[-300:], legacy.stderr[-300:]))
    return vm


def fatal(name, operation, body, work, limit, address_space=None, before=None):
    """`body` exceeds the limit: the fatal error (not catchable, nothing after it), as legacy."""
    script = 'print("BEFORE")\ntry {\n%s\n} catch(e) { print("CAUGHT " + e.message) }\nprint("AFTER")\n' % body
    proc = parity(name, script, work, limit, address_space, before)
    check("%s: fatal %s limit error, nothing after it" % (name, operation),
          rl.limit_error(proc, operation, limit) and proc.stdout.startswith("BEFORE") and not_bad_alloc(proc),
          (proc.stdout + proc.stderr)[-400:])


def reads(name, script, work, limit, expected, address_space=None, before=None):
    proc = parity(name, script, work, limit, address_space, before)
    check("%s: read whole" % name, proc.returncode == 0 and proc.stdout.split() == expected,
          (proc.stdout + proc.stderr)[-400:])


def operations(path):
    """(name, operation, whole-read body that prints the length) per read path."""
    return [
        ("open", "open", 'print(open("%s").length())' % path),
        ("file().open", "open", 'f := file("%s")\nf.open("r")\nprint(f.read_all().length())\nf.close()' % path),
        ("ifstream.read_all", "read_all", 's := ifstream("%s")\nx := s.read_all()\nprint(x.length())\nclose(s)' % path),
    ]


def small_limit(work):
    exact, over = work / "exact.txt", work / "over.txt"
    exact.write_bytes(b"a" * SMALL)
    over.write_bytes(b"a" * (SMALL + 1))
    for name, operation, body in operations(exact):
        reads("A %s: exactly the limit (%d)" % (name, SMALL), body + "\n", work, SMALL, [str(SMALL)])
    for name, operation, body in operations(over):
        fatal("A %s: one byte over" % name, operation, body, work, SMALL)
    (work / "line-exact.txt").write_bytes(b"b" * SMALL + b"\nnext\n")
    (work / "line-over.txt").write_bytes(b"b" * (SMALL + 1) + b"\nnext\n")
    script = 's := ifstream("%s")\nx := s.read_line()\nprint("LINE")\nprint(s.read_line())\nclose(s)\n'
    reads("A read_line: a line of exactly the limit", script % (work / "line-exact.txt"), work, SMALL, ["LINE", "next"])
    fatal("A read_line: a line one byte over", "read_line",
          's := ifstream("%s")\nx := s.read_line()' % (work / "line-over.txt"), work, SMALL)


def fifo_writer(fifo, total, paced=False, newline=False):
    """A writer thread for `fifo`: `total` bytes ("z"), then close. Paced: each
    chunk is written only once the pipe is empty again (the reader drained
    the previous chunk), so every read(2) returns at most one short chunk."""
    def drained(fd):
        level = array.array("i", [0])
        while True:
            fcntl.ioctl(fd, termios.FIONREAD, level, True)
            if level[0] == 0:
                return True
            # The reader is gone (it stopped at the limit): POLLERR on the write end.
            poller = select.poll()
            poller.register(fd, select.POLLOUT)
            for _, events in poller.poll(0):
                if events & (select.POLLERR | select.POLLHUP):
                    return False
            os.sched_yield()

    def body():
        try:
            fd = os.open(fifo, os.O_WRONLY)
            try:
                left, index = total, 0
                while left > 0:
                    size = min(left, PACED_CHUNKS[index % len(PACED_CHUNKS)] if paced else 65536)
                    index += 1
                    view = memoryview(b"z" * size)
                    while view:
                        view = view[os.write(fd, view):]
                    left -= size
                    if paced and not drained(fd):
                        return
                if newline:
                    os.write(fd, b"\n")
            finally:
                os.close(fd)
        except (BrokenPipeError, OSError):
            pass
    thread = threading.Thread(target=body, daemon=True)
    thread.start()
    return thread


def large_limit(work, address_space):
    zero = "/dev/zero"
    exact = work / "exact-large.bin"
    with open(exact, "wb") as handle:
        handle.truncate(LARGE)          # sparse: no disk
    triple = work / "triple.bin"
    with open(triple, "wb") as handle:
        handle.truncate(3 * LARGE)
    reads("B open: a regular file of exactly the limit under RLIMIT_AS", 'print(open("%s").length())\n' % exact,
          work, LARGE, [str(LARGE)], address_space)
    for name, operation, body in operations(triple)[:2]:
        fatal("B %s: a regular file 3 x the limit" % name, operation, body, work, LARGE, address_space)
    for name, operation, body in operations(zero):
        fatal("B %s(/dev/zero)" % name, operation, body, work, LARGE, address_space)
    fatal("B read_line(/dev/zero)", "read_line", 's := ifstream("%s")\nx := s.read_line()' % zero,
          work, LARGE, address_space)
    fifo = work / "large.fifo"
    os.mkfifo(fifo)
    threads = []
    for name, operation, body in operations(fifo):
        fatal("B %s(FIFO one byte over)" % name, operation, body, work, LARGE, address_space,
              before=lambda: threads.append(fifo_writer(fifo, LARGE + 1)))
    fatal("B read_line(FIFO line one byte over)", "read_line", 's := ifstream("%s")\nx := s.read_line()' % fifo,
          work, LARGE, address_space, before=lambda: threads.append(fifo_writer(fifo, LARGE + 1, newline=True)))
    for thread in threads:
        thread.join(30)
    check("B FIFO writers finished", not any(thread.is_alive() for thread in threads))


def paced_short_reads(work):
    fifo = work / "paced.fifo"
    os.mkfifo(fifo)
    threads = []

    def writer(total, newline=False):
        return lambda: threads.append(fifo_writer(fifo, total, paced=True, newline=newline))

    for name, operation, body in operations(fifo):
        for total in (PACED - 1, PACED):
            reads("C %s: paced short reads, %d bytes then close" % (name, total), body + "\n", work, PACED,
                  [str(total)], before=writer(total))
        fatal("C %s: paced short reads crossing the limit" % name, operation, body, work, PACED,
              before=writer(PACED + 1))
        fatal("C %s: paced short reads, writer far past the limit" % name, operation, body, work, PACED,
              before=writer(4 * PACED))
    line = 's := ifstream("%s")\nx := s.read_line()\nprint("LINE")\nprint(s.read_line())\nclose(s)\n' % fifo
    reads("C read_line: paced short reads, a line just under the limit", line, work, PACED, ["LINE", "null"],
          before=writer(PACED - 1, newline=True))
    fatal("C read_line: paced short reads, a line crossing the limit", "read_line",
          's := ifstream("%s")\nx := s.read_line()' % fifo, work, PACED, before=writer(PACED + 1, newline=True))
    for thread in threads:
        thread.join(30)
    check("C paced FIFO writers finished", not any(thread.is_alive() for thread in threads))


def main():
    if not os.path.exists(rl.NIFT):
        sys.exit("nift binary not found: " + rl.NIFT)
    vm_peak, _ = rl.baseline_vm()
    address_space = vm_peak + LARGE * 3 // 2 + rl.SLACK
    print("baseline VmPeak %d MiB; RLIMIT_AS %d MiB for the %d-byte limit" %
          (vm_peak // MiB, address_space // MiB, LARGE))
    work = Path(tempfile.mkdtemp(prefix="read-limit-controls-"))
    try:
        small_limit(work)
        large_limit(work, address_space)
        paced_short_reads(work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if rl.failures:
        print("%d failure(s)" % len(rl.failures))
        return 1
    print("all read-limit controls passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
