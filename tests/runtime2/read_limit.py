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

D-077 #3 (owner 2026-11-12 04:25): the test contract distinguishes approved refusals from read-limit
failures, case by case (docs/runtime-2/host-deps-test-contract.md has the table of the 24):
  - a case of a capability whose VM refusal is an owner decision (DEFERRED below: run()/cmd(), D-065 #2;
    open_bytes() and read(), named deferrals by D-078 #2, earlier approved for implementation) runs its original program and assertions on the legacy channel (NIFT_VM=0, the
    supported path that keeps the capability: the read-limit invariant), and a refusal contract under the
    VM default: exit 1 before any part runs, the exact documented diagnostic at the callee's position. A
    refusal-contract pass is not evidence that the capability works on the VM; the summary counts it as
    deferred;
  - a case refused for an unapproved reason (host-handle-flow, statement:Command: PENDING-OWNER)
    keeps its original VM-default case, which stays FAILED; its safety invariant keeps coverage through the
    legacy channel and, where the VM supports an equivalent form, through that form;
  - a case that cannot run here is NOT RUN (exit 2 when nothing failed), never a pass.

Usage: NIFT_BIN=/path/to/nift python3 tests/runtime2/read_limit.py
"""
import collections
import hashlib
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
LIMIT = 64 * MiB                     # NIFT_READ_LIMIT for the memory cases
# Slack well below the limit, so a buffer bounded at 2 x limit (or a growth
# step holding 2 x limit) fails RLIMIT_AS and the RSS check (review R4).
SLACK = 8 * MiB
FSIZE_LIMIT = 512 * MiB
failures = []
not_run = []
# D-077 #3: reason -> (decision, the documented refusal text, the callee token the diagnostic points at)
DEFERRED = {
    "builtin-bytes:open_bytes": ("D-078 #2: named deferral; VM support after the first certification (approved for implementation before)",
                                 "bytes values with `open_bytes()` is not supported by the Runtime 2.0 VM yet", "open_bytes("),
    "callable:read": ("D-078 #2: named deferral; VM support after the first certification (approved for implementation before)",
                      "the call `read()` is not supported by the Runtime 2.0 VM yet", "read("),
    "callable:run": ("D-065 #2: run() and cmd() deferred from the initial release",
                     "the call `run()` is not supported by the Runtime 2.0 VM yet", "run("),
    "callable:cmd": ("D-065 #2: run() and cmd() deferred from the initial release",
                     "the call `cmd()` is not supported by the Runtime 2.0 VM yet", "cmd("),
}
# Case-name tags (review of the cert-prep work, C4 / F-P2-1): a tag is a PREFIX, never a suffix. certify's case-ID
# table (scripts/certify_fingerprints.py READ_LIMIT_TABLE) matches a name prefix followed by a space, so a suffixed
# name would map onto the original case's ID. Every tagged name starts with "[": no table entry with fixed leading
# text matches it, and an entry that starts with a name group captures the tag, so a tagged case never shares an ID
# with an original one (tests/runtime2/read_limit_names_selftest.py proves it on the real table).
LEGACY_CHANNEL = "[legacy channel, NIFT_VM=0] "
REFUSAL_CONTRACT = "[VM refusal contract, %s] "
VM_EQUIVALENT = "[VM equivalent form] "
EQUIVALENT_NAMES = ("read_line: line of exactly the limit, printed and measured by the harness",
                    "read(limit/2) x 16 from /dev/zero (8 x limit streamed)")
# The channel state: `legacy` routes run() to NIFT_VM=0 and suffixes check names; `record` collects the
# scripts run before each check (a refusal contract re-runs them under the VM default).
CHANNEL = {"legacy": False, "record": None, "pending": []}
deferred_cases = collections.Counter()
# D-078 #4 (owner 2026-11-12, attachment 2ea1c509): no blanket deferral. The cases below keep their original VM-default
# check and stay FAILED. This records, per case, the refusal reason the VM emits and the operation actually refused;
# where review evidence shows that the reason misnames the operation, that is a finding about a production diagnostic
# (not changed here), and the case is still FAILED. docs/runtime-2/host-deps-test-contract.md has the evidence.
OUTSTANDING = [
    ("ifstream(/dev/zero) >> token", "statement:Command",
     "stream extraction `s >> t`; finding F-HD2: the VM classifies a `>>` statement (also `x >> 1` on a number) as a "
     "shell command line, while legacy performs stream extraction"),
    ("ifstream(/dev/zero).read_val", "host-handle-flow",
     "stream value read (read_val), not a byte read: classification not established, stays open"),
]


def not_run_case(name, why):
    """D-077 #3: a case this environment cannot run is NOT RUN, not PASS."""
    not_run.append(name)
    print("NOT RUN", name, "(%s)" % why)


def check(name, condition, detail=""):
    if CHANNEL["legacy"]:
        name = LEGACY_CHANNEL + name
    if CHANNEL["record"] is not None and CHANNEL["pending"]:
        CHANNEL["record"].append((name, CHANNEL["pending"]))
        CHANNEL["pending"] = []
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
    if CHANNEL["legacy"]:
        env["NIFT_VM"] = "0"
    if CHANNEL["record"] is not None:
        CHANNEL["pending"].append(dict(script=script_text, cwd=cwd, limit=limit, address_space=address_space,
                                       env_extra=env_extra))
    if limit is not None:
        env["NIFT_READ_LIMIT"] = str(limit)
    if env_extra:
        env.update(env_extra)

    def limit_memory():
        # Never fill the disk: no file written by nift or its children may
        # exceed FSIZE_LIMIT (captures included).
        cap = address_space or 4 << 30   # always bounded (standing rule)
        for kind, value in ((resource.RLIMIT_FSIZE, FSIZE_LIMIT), (resource.RLIMIT_AS, cap)):
            hard = resource.getrlimit(kind)[1]
            if hard != resource.RLIM_INFINITY:
                value = min(value, hard)
            resource.setrlimit(kind, (value, value))

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


def legacy_channel(body):
    """Runs body's cases on the legacy channel (NIFT_VM=0): the supported path of a capability the VM default
    refuses. Their names start with LEGACY_CHANNEL."""
    CHANNEL.update(legacy=True, record=None, pending=[])
    try:
        body()
    finally:
        CHANNEL.update(legacy=False, record=None, pending=[])


def refusal_position(script, token):
    """line:column (1-based) of the first `token` in the script: where the diagnostic must point."""
    for number, text in enumerate(script.split("\n"), 1):
        col = text.find(token)
        while col > 0 and (text[col - 1].isalnum() or text[col - 1] in "_."):
            col = text.find(token, col + 1)
        if col >= 0:
            return number, col + 1
    return None


def refusal_contract(name, reason, proc, script):
    decision, text, token = DEFERRED[reason]
    pos = refusal_position(script, token)
    want = "case.f:%d:%d: not compiled for the VM (%s): %s" % (pos[0], pos[1], reason, text) if pos else "?"
    first = next((l for l in proc.stderr.splitlines() if l.startswith("error: ")), "")
    ok = (proc.returncode == 1 and proc.stdout == "" and first.endswith(want) and
          "the program was not executed: no part of it ran" in proc.stderr)
    deferred_cases[reason] += 1
    check(REFUSAL_CONTRACT % reason + name, ok,
          "want %r; got exit %d stdout %r first %r" % (want, proc.returncode, proc.stdout[:80], first))


def deferred(reason, body):
    """D-077 #3: body's original cases run on the legacy channel; then every script they ran is re-run under the
    VM default as a refusal contract (`reason` must refuse it before any part runs)."""
    CHANNEL.update(legacy=True, record=[], pending=[])
    try:
        body()
    finally:
        recorded = CHANNEL["record"]
        CHANNEL.update(legacy=False, record=None, pending=[])
    for name, scripts in recorded:
        for s in scripts:
            proc = run(s["script"], s["cwd"], limit=s["limit"], address_space=s["address_space"], env_extra=s["env_extra"])
            refusal_contract(name[len(LEGACY_CHANNEL):] if name.startswith(LEGACY_CHANNEL) else name, reason, proc,
                             s["script"])


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
    deferred_note = "; ".join("%s %d (%s)" % (r, n, DEFERRED[r][0]) for r, n in sorted(deferred_cases.items()))
    if deferred_note:
        print("DEFERRED capabilities, checked only as VM refusal contracts (not evidence that they work on the VM): " +
              deferred_note)
    print("")  # a blank line: certify collects the lines under a FAIL line up to the next blank line
    for name, reason, operation in OUTSTANDING:
        print("OUTSTANDING (FAILED unless it passed above; D-078 #4): %s: emitted reason %s; operation: %s" %
              (name, reason, operation))
    if failures:
        print("%d failure(s)" % len(failures))
        return 1
    if not_run:
        print("%d case(s) NOT RUN; this is not a pass" % len(not_run))
        return 2
    print("all read-limit checks passed (named deferred capabilities: refusal contracts only)" if deferred_note
          else "all read-limit checks passed")
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
    def whole_reads(op, expr):
        proc = run('print(%s)\n' % (expr % (work / "exact.bin")), work, limit=small)
        check("%s: file of exactly the limit is read whole" % op, proc.returncode == 0 and proc.stdout.strip() == str(small),
              proc.stdout + proc.stderr)
        proc = run('print(%s)\n' % (expr % (work / "empty.bin")), work, limit=small)
        check("%s: empty file" % op, proc.returncode == 0 and proc.stdout.strip() == "0", proc.stdout + proc.stderr)
        exceed("%s one byte over" % op, op, 'x := %s' % (expr % (work / "over.bin")), work, address_space, rss_bound, small)
    whole_reads("open", 'open("%s").length()')
    # D-077 #3: open_bytes() is refused by the VM default (builtin-bytes:open_bytes, D-078 #2).
    deferred("builtin-bytes:open_bytes", lambda: whole_reads("open_bytes", 'open_bytes("%s").length()'))
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
    # Also check the invariant through the original VM-equivalent form.
    proc = run('s := ifstream("%s")\nprint(s.read_line())\nprint(s.read_line())\nclose(s)\n' % (work / "line-exact.txt"),
               work, limit=small)
    check(VM_EQUIVALENT + EQUIVALENT_NAMES[0],
          proc.returncode == 0 and proc.stdout.split("\n")[:2] == ["b" * small, "next"], (proc.stdout + proc.stderr)[-300:])
    legacy_channel(lambda: check("read_line: line of exactly the limit",
                                 run('s := ifstream("%s")\nprint(s.read_line().length())\nprint(s.read_line())\nclose(s)\n'
                                     % (work / "line-exact.txt"), work, limit=small).stdout.split() == [str(small), "next"]))
    empty_length = ('s := ifstream("%s")\nprint(s.read_line().length())\n'
                    'print("UNREACHED")\n') % (work / "empty.bin")
    vm_eof = run(empty_length, work, limit=small, env_extra={"NIFT_VM": "require"})
    legacy_eof = run(empty_length, work, limit=small, env_extra={"NIFT_VM": "0"})
    check("read_line: length of EOF agrees with the legacy error",
          vm_eof.returncode == legacy_eof.returncode == 1 and vm_eof.stdout == legacy_eof.stdout == "" and
          vm_eof.stderr == legacy_eof.stderr and
          ":2:7: unknown value or malformed expression: s.read_line().length()" in vm_eof.stderr,
          vm_eof.stderr)
    exceed("read_line one byte over", "read_line", 's := ifstream("%s")\nx := s.read_line()' % (work / "line-over.txt"),
           work, address_space, rss_bound, small)

    # --- character devices: never reach EOF / report size 0 -----------------
    for device in (zero, full):
        exceed("open(%s)" % device, "open", 'x := open("%s")' % device, work, address_space, rss_bound)
        deferred("builtin-bytes:open_bytes", lambda: exceed("open_bytes(%s)" % device, "open_bytes",
                                                            'x := open_bytes("%s")' % device, work, address_space, rss_bound))
        exceed("cat(%s)" % device, "cat", 'native::cat("%s")' % device, work, address_space, rss_bound)
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
    proc = run('s := ifstream("/dev/zero")\nn := 0\nk := 0\nwhile(k < 16) { n = n + s.read_bytes(%d).length(); k = k + 1 }\nclose(s)\nprint(n)\n'
               % (LIMIT // 2), work, address_space=address_space)
    check("read_bytes(limit/2) x 16 from /dev/zero (8 x limit streamed)",
          proc.returncode == 0 and proc.stdout.strip() == str(8 * LIMIT), (proc.stdout + proc.stderr)[-300:])
    # D-077 #3: read_val and `>>` extraction still have unapproved refusals.
    # Keep all the original legacy assertions and the equivalent VM text read.
    proc = run('s := ifstream("/dev/zero")\nn := 0\nk := 0\nwhile(k < 16) { n = n + s.read(%d).length(); k = k + 1 }\nclose(s)\nprint(n)\n'
               % (LIMIT // 2), work, address_space=address_space)
    check(VM_EQUIVALENT + EQUIVALENT_NAMES[1],
          proc.returncode == 0 and proc.stdout.strip() == str(8 * LIMIT), (proc.stdout + proc.stderr)[-300:])

    packet = b"\x00\xff\xc3\xa9a"
    (work / "stream-bytes.bin").write_bytes(packet)
    binary_source = ('s := ifstream("stream-bytes.bin")\n'
                     'zero := s.read_bytes(0)\nhead := s.read_bytes(2)\ntail := s.read_all_bytes()\n'
                     'print(is_bytes(zero))\nprint(zero.length())\nprint(head.length())\nprint(tail.length())\n'
                     'print(sha256(head))\nprint(sha256(tail))\nclose(s)\n')
    expected = ["true", "0", "2", "3", hashlib.sha256(packet[:2]).hexdigest(), hashlib.sha256(packet[2:]).hexdigest()]
    binary = run(binary_source, work, limit=small, env_extra={"NIFT_VM": "require"})
    check("stream byte reads preserve length, kind and exact contents across zero-length and partial reads",
          binary.returncode == 0 and not binary.stderr and binary.stdout.splitlines() == expected,
          binary.stdout + binary.stderr)

    write_source = ('s := ifstream("stream-bytes.bin")\nchunk := s.read_all_bytes()\nclose(s)\n'
                    't := ofstream("stream-bytes-out.bin")\nt.%s(chunk)\nclose(t)\n')
    written_path = work / "stream-bytes-out.bin"
    raw = run(write_source % "write", work, limit=small, env_extra={"NIFT_VM": "require"})
    check("stream write preserves exact bytes",
          raw.returncode == 0 and written_path.is_file() and written_path.read_bytes() == packet,
          raw.stdout + raw.stderr)
    line = run(write_source % "write_line", work, limit=small, env_extra={"NIFT_VM": "require"})
    vm_line_bytes = written_path.read_bytes() if written_path.is_file() else None
    legacy_line = run(write_source % "write_line", work, limit=small, env_extra={"NIFT_VM": "0"})
    check("stream write_line refuses bytes without writing",
          line.returncode == legacy_line.returncode == 1 and line.stdout == legacy_line.stdout == "" and
          line.stderr == legacy_line.stderr and "write_line: value is not directly renderable" in line.stderr and
          vm_line_bytes == b"" and written_path.is_file() and written_path.read_bytes() == b"", line.stderr)

    def stream_controls_on_legacy():
        exceed("ifstream(/dev/zero).read_all_bytes", "read_all_bytes", 's := ifstream("/dev/zero")\nx := s.read_all_bytes()',
               work, address_space, rss_bound)
        exceed("ifstream(/dev/zero).read_bytes(huge)", "read_bytes",
               's := ifstream("/dev/zero")\nx := s.read_bytes(%d)' % (8 * LIMIT), work, address_space, rss_bound)
        exceed("ifstream(/dev/zero) >> token", "extraction", 's := ifstream("/dev/zero")\nt := ""\ns >> t', work,
               address_space, rss_bound)
        exceed("ifstream(/dev/zero).read_val", "read_val", 's := ifstream("/dev/zero")\nx := s.read_val()', work,
               address_space, rss_bound)
        proc = run('s := ifstream("/dev/zero")\nn := 0\nk := 0\nwhile(k < 16) { n = n + s.read_bytes(%d).length(); k = k + 1 }\n'
                   'close(s)\nprint(n)\n' % (LIMIT // 2), work, address_space=address_space)
        check("read_bytes(limit/2) x 16 from /dev/zero (8 x limit streamed)",
              proc.returncode == 0 and proc.stdout.strip() == str(8 * LIMIT), (proc.stdout + proc.stderr)[-300:])
    legacy_channel(stream_controls_on_legacy)

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
        # D-077 #3: run() and cmd() are refused by the VM default (D-065 #2).
        def run_capture():
            proc = run('r := run("%s", "-c", "%d", "/dev/zero")\nprint(r.stdout.length())\n' % (head, small), work, limit=small)
            check("run(): capture of exactly the limit", proc.returncode == 0 and proc.stdout.strip() == str(small),
                  proc.stdout + proc.stderr)
            exceed("run(): capture one byte over", "run", 'r := run("%s", "-c", "%d", "/dev/zero")' % (head, small + 1),
                   work, address_space, rss_bound, small)
        deferred("callable:run", run_capture)
        deferred("callable:cmd", lambda: exceed("cmd().run(): capture of %d MiB" % (2 * LIMIT // MiB), "run",
                                                'r := cmd("%s", "-c", "%d", "/dev/zero").run()' % (head, 2 * LIMIT),
                                                work, address_space, rss_bound))
    else:
        not_run_case("process capture", "no head")

    stream_errors(work)
    # D-077 #3: read() is refused by the VM default (callable:read, D-078 #2).
    deferred("callable:read", lambda: stdin_read(work, address_space, rss_bound))
    capture_cases(work, address_space, rss_bound)

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
    # D-077 #3: open_bytes() is refused by the VM default (builtin-bytes:open_bytes, D-078 #2). The refusal contract
    # never opens the FIFO (no part of the program runs), so only the legacy run needs a writer.
    deferred("builtin-bytes:open_bytes", lambda: exceed("FIFO whose writer never stops (open_bytes)", "open_bytes",
                                                        'x := open_bytes("%s")' % fifo, work, address_space, rss_bound))
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
    name = "default limit: open_bytes(/dev/zero) stops at 1073741824 bytes"
    script = 'x := open_bytes("/dev/zero")\n'
    # D-077 #3: open_bytes() is refused by the VM default (builtin-bytes:open_bytes, D-078 #2): the refusal contract
    # needs no memory; the invariant runs on the legacy channel, and only where 4 GiB are available.
    refusal_contract(name, "builtin-bytes:open_bytes", run(script, work, limit=None), script)
    if available < 4 * (1 << 30):
        not_run_case(LEGACY_CHANNEL + name, "MemAvailable %d MiB < 4096" % (available // MiB))
        return
    vm_peak, _ = baseline_vm()
    gib = 1 << 30

    def legacy():
        proc = run(script, work, limit=None, address_space=vm_peak + gib * 3 // 2 + SLACK, timeout=300)
        check(name, limit_error(proc, "open_bytes", gib), (proc.stdout + proc.stderr)[-300:])
    legacy_channel(legacy)


def sequenced(site, path):
    """Flakiness fix (2026-11-08, record section 25): an edit followed at once by a build can share
    the page-info's timestamp tick, so the modified rule rebuilds once more and `status` is not up
    to date. Age the site as a whole (order kept), then stamp the edit just before the clock
    (tests/runtime2/cp01/ticks.py): strictly newer than the last build, older than the next."""
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(Path(__file__).resolve().parent / "cp01"))
    import ticks
    ticks.age(str(site))
    return ticks.sequence([str(path)])


def page_build(work):
    site = work / "site"
    site.mkdir()
    subprocess.run([NIFT, "init"], cwd=site, capture_output=True, check=True)
    (site / "content" / "index.html").write_text('<p>GOOD:@script { return open("data.txt") }</p>\n')
    (site / "data.txt").write_text("v1\n")
    sequenced(site, site / "data.txt")
    env = dict(os.environ, NIFT_READ_LIMIT="1024")
    first = subprocess.run([NIFT, "build", "--all"], cwd=site, capture_output=True, text=True, env=env)
    good = (site / "public" / "index.html").read_bytes()
    check("page build: initial build", first.returncode == 0 and b"GOOD:v1" in good, first.stdout + first.stderr)
    (site / "data.txt").write_text("x" * 1025)
    sequenced(site, site / "data.txt")
    failed = subprocess.run([NIFT, "build"], cwd=site, capture_output=True, text=True, env=env)
    text = failed.stdout + failed.stderr
    check("page build: limit is a build failure naming the operation and limit",
          failed.returncode != 0 and "open:" in text and "exceeds the read limit of 1024 bytes (NIFT_READ_LIMIT)" in text, text)
    check("page build: last-good output preserved", (site / "public" / "index.html").read_bytes() == good)
    check("page build: no interrupted-state marker", not (site / ".nift" / ".unfinished").exists())
    (site / "data.txt").write_text("v2\n")
    sequenced(site, site / "data.txt")
    fixed = subprocess.run([NIFT, "build"], cwd=site, capture_output=True, text=True, env=env)
    check("page build: next build after the fix succeeds without --repair",
          fixed.returncode == 0 and b"GOOD:v2" in (site / "public" / "index.html").read_bytes(), fixed.stdout + fixed.stderr)
    status = subprocess.run([NIFT, "status"], cwd=site, capture_output=True, text=True, env=env).stdout
    check("page build: up to date afterwards", "up to date" in status, status)
    # Direct regression check of the sequencing (confirmation of c8817214, C3): force the fastest
    # build by stamping the page-info with the earliest time it legally could carry. An
    # unsequenced edit (the old test) then needs a rebuild; a sequenced one is up to date.
    def stamp(ns):
        for info in (site / ".nift").rglob("*.info.json"):
            os.utime(info, ns=(ns, ns))
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(Path(__file__).resolve().parent / "cp01"))
    import ticks
    ticks.age(str(site))
    (site / "data.txt").write_text("v3\n")
    built = subprocess.run([NIFT, "build"], cwd=site, capture_output=True, text=True, env=env)
    check("page build: forced race, build after the unsequenced edit", built.returncode == 0, built.stdout + built.stderr)
    stamp(os.stat(site / "data.txt").st_mtime_ns)
    status = subprocess.run([NIFT, "status"], cwd=site, capture_output=True, text=True, env=env).stdout
    check("page build: forced race, an unsequenced edit needs a rebuild", "up to date" not in status, status)
    built = subprocess.run([NIFT, "build"], cwd=site, capture_output=True, text=True, env=env)
    check("page build: forced race, rebuild", built.returncode == 0, built.stdout + built.stderr)
    (site / "data.txt").write_text("v4\n")
    floor = sequenced(site, site / "data.txt")
    built = subprocess.run([NIFT, "build"], cwd=site, capture_output=True, text=True, env=env)
    check("page build: forced race, build after the sequenced edit", built.returncode == 0, built.stdout + built.stderr)
    stamp(floor)
    status = subprocess.run([NIFT, "status"], cwd=site, capture_output=True, text=True, env=env).stdout
    check("page build: forced race, a sequenced edit is up to date", "up to date" in status, status)



def stream_errors(work):
    """review D1: an I/O error while reading a line is the catchable
    stream.read_failed, never an uncaught C++ exception."""
    (work / "adir").mkdir(exist_ok=True)
    for name, path in (("directory", work / "adir"), ("EIO (/proc/self/mem)", "/proc/self/mem")):
        if name.startswith("EIO") and not os.path.exists("/proc/self/mem"):
            not_run_case("read_line on a %s: catchable read error" % name, "no /proc/self/mem")
            continue
        script = ('x := ""\ntry { s := ifstream("%s"); x = s.read_line() } catch(e) { x = "CAUGHT " + e.code + " " + e.message }\n'
                  'print(x)\nprint("AFTER")\n' % path)
        proc = run(script, work, limit=None)
        check("read_line on a %s: catchable read error" % name,
              proc.returncode == 0 and "CAUGHT stream.read_failed read_line: input failure" in proc.stdout and "AFTER" in proc.stdout,
              (proc.stdout + proc.stderr)[-300:])


def stdin_read(work, address_space, rss_bound):
    """review D7: read() (one stdin line in a standalone script) is bounded."""
    small = 1024
    for size, ok in ((small, True), (small + 1, False)):
        data = work / "stdin.txt"
        data.write_bytes(b"s" * size + b"\nnext\n")
        with data.open("rb") as handle:
            proc = run('x := read()\nprint(x.length())\nprint(read())\nprint("AFTER")\n', work, limit=small, stdin=handle)
        if ok:
            check("read(): stdin line of exactly the limit", proc.returncode == 0 and proc.stdout.split() == [str(small), "next", "AFTER"],
                  proc.stdout + proc.stderr)
        else:
            check("read(): stdin line one byte over is a limit error", limit_error(proc, "read", small), proc.stdout + proc.stderr)
    with open("/dev/zero", "rb") as handle:
        proc = run('x := read()\nprint("AFTER")\n', work, address_space=address_space, stdin=handle)
    check("read(): endless stdin (/dev/zero) stops at the limit", limit_error(proc, "read"), (proc.stdout + proc.stderr)[-300:])
    check("read(): endless stdin peak RSS %d MiB within %d MiB" % (proc.peak_rss // MiB, rss_bound // MiB), proc.peak_rss <= rss_bound)


def leftover(tag):
    """PIDs whose command line contains `tag` (Linux /proc)."""
    found = []
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            with open("/proc/%s/cmdline" % entry, "rb") as handle:
                if tag.encode() in handle.read():
                    found.append(int(entry))
        except OSError:
            pass
    return found


def capture_cases(work, address_space, rss_bound):
    """review D5: run()/cmd().run() captures are bounded while the child runs:
    a never-ending producer is stopped promptly (it and its descendants),
    reaped, and the run fails with the normal limit error."""
    if not os.path.isdir("/proc") or not shutil.which("yes") or not shutil.which("head") or not shutil.which("sh"):
        not_run_case("capture-while-running cases", "needs /proc, yes, head, sh")
        return
    # D-077 #3: run() and cmd() are refused by the VM default (D-065 #2): the cases run on the legacy channel, each
    # followed by its VM refusal contract.
    deferred("callable:run", lambda: capture_run(work, address_space, rss_bound, [0, 1, 3, 4]))
    deferred("callable:cmd", lambda: capture_run(work, address_space, rss_bound, [2]))
    # Children reaped: no zombie children of this test (each nift reaped its own).
    zombies = [e for e in os.listdir("/proc") if e.isdigit() and _state(e) == "Z" and _ppid(e) == os.getpid()]
    check("no zombie processes left by the capture cases", not zombies, str(zombies))


def capture_run(work, address_space, rss_bound, which):
    tag = "nift-capture-probe-%d" % os.getpid()
    marker = work / "marker"
    cases = [
        ("run(yes) never ends", 'r := run("yes", "%s")' % tag),
        ("run(head -c 150000000 /dev/zero)", 'r := run("head", "-c", "150000000", "/dev/zero")'),
        ("cmd(yes).pipe(cat).run()", 'r := cmd("yes", "%s").pipe(cmd("cat")).run()' % tag),
        ("run(sh -c 'yes; touch marker') kills the descendant", 'r := run("sh", "-c", "yes %s; touch %s")' % (tag, marker)),
        ("stderr capture of an endless producer", 'r := run("sh", "-c", "yes %s >&2")' % tag),
    ]
    for name, body in [cases[i] for i in which]:
        started = time.monotonic()
        proc = run('try {\n%s\n} catch(e) { print("CAUGHT") }\nprint("AFTER")\n' % body, work, address_space=address_space, timeout=60)
        elapsed = time.monotonic() - started
        check("%s: fatal run limit error (exit %d, %.2fs)" % (name, proc.returncode, elapsed),
              limit_error(proc, "run") and elapsed < 20, (proc.stdout + proc.stderr)[-300:])
        time.sleep(0.5)
        left = leftover(tag)
        check("%s: no producer left running" % name, not left, str(left))
        for pid in left:
            try:
                os.kill(pid, 9)
            except OSError:
                pass
        check("%s: marker never created (child stopped before finishing)" % name, not marker.exists())
        check("%s: peak RSS %d MiB within %d MiB" % (name, proc.peak_rss // MiB, rss_bound // MiB), proc.peak_rss <= rss_bound)


def _state(pid):
    try:
        with open("/proc/%s/stat" % pid) as handle:
            return handle.read().rsplit(")", 1)[1].split()[0]
    except (OSError, IndexError):
        return ""


def _ppid(pid):
    try:
        with open("/proc/%s/stat" % pid) as handle:
            return int(handle.read().rsplit(")", 1)[1].split()[1])
    except (OSError, IndexError, ValueError):
        return -1


if __name__ == "__main__":
    sys.exit(main())
