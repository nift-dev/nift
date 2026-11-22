#!/usr/bin/env python3
"""D-077 #3 (owner 2026-11-12 04:25): negative controls for the repaired host-deps test contract
(tests/runtime2/read_limit.py; docs/runtime-2/host-deps-test-contract.md).

The repaired suite answers two questions separately, and each answer must be able to fail:

  1. Refusal contracts (a deferred capability is refused in the approved manner). For each deferred reason:
     - control: its program under the VM default meets read_limit.refusal_contract();
     - changed reason: the same run checked against another deferred reason fails;
     - changed position: the program with the callee moved by one column, checked against the original
       position, fails;
     - refusal became execution: the same program on the legacy channel (it runs) fails the contract.
  2. Supported-path read-limit invariants (the legacy channel, and the VM-supported equivalents). For each
     operation, on a regular file or a bounded producer one byte over a small limit:
     - control: the run is the fatal limit error that read_limit.limit_error() requires;
     - limit removed: the same program with the limit raised past the input is NOT accepted by
       limit_error() (a build without the check would pass the input through the same way).

D-150 adds a direct bounded read from /dev/zero and a regular-file token. All device access is read-only.
A mutated build of the legacy read path is not part of this file (the existing
read_limit_mutants.sh mutates the VM natives and runs read_limit_controls.py; it needs a build).

Usage: NIFT_BIN=/path/to/nift python3 -B tests/runtime2/read_limit_contract_controls.py
"""
import os
from copy import copy
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import read_limit as rl  # noqa: E402  (one harness: run(), limit_error(), refusal_contract(), the channel)

SMALL = 1024
results = []


def outcome(name, ok, detail=""):
    results.append((name, ok))
    print("%s %s %s" % ("PASS" if ok else "FAIL", name, detail if not ok else ""))


def contract_fails(name, reason, proc, script):
    """True when read_limit.refusal_contract() reports a failure (its own check() line is printed, prefixed)."""
    before = len(rl.failures)
    print("  (control run, a FAIL here is expected) ", end="")
    rl.refusal_contract(name, reason, proc, script)
    failed = len(rl.failures) > before
    del rl.failures[before:]
    return failed


def on_legacy(fn):
    out = []
    rl.legacy_channel(lambda: out.append(fn()))
    return out[0]


def read_val_controls(work):
    name, reason = rl.READ_VAL_CASE, "host-handle-flow"
    script = rl.fatal_limit_script(rl.READ_VAL_BODY)
    vm = rl.run_read_val_refusal(script, work, SMALL)
    outcome("D-150 refusal includes exact diagnostic and zero execution",
            not contract_fails(name, reason, vm, script))
    outcome("D-150 does not approve other cases with the shared refusal reason",
            contract_fails("unrelated handle case", reason, vm, script))
    changed = copy(vm)
    changed.stderr = vm.stderr.replace("(host-handle-flow)", "(callable:read)", 1)
    outcome("D-150 rejects a changed refusal reason", contract_fails(name, reason, changed, script))
    shifted = script.replace("x := s.read_val()", "x :=  s.read_val()")
    outcome("D-150 rejects a changed source position",
            contract_fails(name, reason, rl.run_read_val_refusal(shifted, work, SMALL), script))
    missing_stats = copy(vm)
    missing_stats.vm_stats = {}
    outcome("D-150 rejects a refusal with missing execution evidence",
            contract_fails(name, reason, missing_stats, script))

    legacy = on_legacy(lambda: rl.run(script, work, limit=SMALL))
    outcome("D-150 legacy executes the stream read and raises its fatal limit error",
            rl.read_val_legacy_contract(legacy, script, "/dev/zero", SMALL), legacy.stderr)
    outcome("D-150 rejects execution in place of refusal", contract_fails(name, reason, legacy, script))
    outcome("D-150 rejects a VM refusal in place of legacy execution",
            not rl.read_val_legacy_contract(vm, script, "/dev/zero", SMALL))

    token = work / "read-val.txt"
    token.write_bytes(b'"' + b"a" * SMALL + b'"')
    finite = rl.fatal_limit_script(f's := ifstream("{token}")\nx := s.read_val()')
    limited = on_legacy(lambda: rl.run(finite, work, limit=SMALL))
    outcome("D-150 finite legacy control reaches the fatal limit",
            rl.read_val_legacy_contract(limited, finite, token, SMALL), limited.stderr)
    unlimited = on_legacy(lambda: rl.run(finite, work, limit=2 * SMALL))
    outcome("D-150 rejects legacy execution with the limit removed",
            unlimited.returncode == 0 and unlimited.stdout == "AFTER\n" and not unlimited.stderr and
            not rl.read_val_legacy_contract(unlimited, finite, token, SMALL), unlimited.stderr)


def main():
    if not os.path.exists(rl.NIFT):
        sys.exit("nift binary not found: " + rl.NIFT)
    work = Path(tempfile.mkdtemp(prefix="read-limit-contract-"))
    try:
        over = work / "over.bin"
        over.write_bytes(b"a" * (SMALL + 1))
        stdin_over = work / "stdin.txt"
        stdin_over.write_bytes(b"s" * (SMALL + 1) + b"\n")
        head = shutil.which("head")
        programs = {
            "builtin-bytes:open_bytes": 'x := open_bytes("%s")\nprint("AFTER")\n' % over,
            "callable:read": 'x := read()\nprint("AFTER")\n',
            "callable:run": 'r := run("%s", "-c", "%d", "/dev/zero")\nprint("AFTER")\n' % (head, SMALL + 1),
            "callable:cmd": 'r := cmd("%s", "-c", "%d", "/dev/zero").run()\nprint("AFTER")\n' % (head, SMALL + 1),
        }
        reasons = sorted(programs)
        for reason in reasons:
            script = programs[reason]
            proc = rl.run(script, work, limit=SMALL)
            outcome("refusal contract control: %s" % reason, not contract_fails("control", reason, proc, script))
            other = reasons[(reasons.index(reason) + 1) % len(reasons)]
            outcome("refusal contract detects a changed reason: %s checked as %s" % (reason, other),
                    contract_fails("changed reason", other, proc, script))
            shifted = script.replace(":= ", ":=  ", 1)
            outcome("refusal contract detects a changed position: %s" % reason,
                    contract_fails("changed position", reason, rl.run(shifted, work, limit=SMALL), script))

            def legacy_run():
                with stdin_over.open("rb") as handle:
                    return rl.run(script, work, limit=SMALL, stdin=handle)
            outcome("refusal contract detects execution instead of refusal: %s" % reason,
                    contract_fails("executed", reason, on_legacy(legacy_run), script))
        # 2. supported-path invariants, one byte over SMALL; the limit "removed" by raising it past the input.
        invariants = [
            ("open_bytes", 'x := open_bytes("%s")\nprint("AFTER")\n' % over, True, None),
            ("read", 'x := read()\nprint("AFTER")\n', True, stdin_over),
            ("run", programs["callable:run"], True, None),
            ("run", programs["callable:cmd"], True, None),
            ("read_all_bytes", 's := ifstream("%s")\nx := s.read_all_bytes()\nprint("AFTER")\n' % over, True, None),
            ("read_val", 's := ifstream("%s")\nx := s.read_val()\nprint("AFTER")\n' % over, True, None),
            ("extraction", 's := ifstream("%s")\nt := ""\ns >> t\nprint("AFTER")\n' % over, True, None),
            ("open", 'x := open("%s")\nprint("AFTER")\n' % over, False, None),
            ("read_all", 's := ifstream("%s")\nx := s.read_all()\nprint("AFTER")\n' % over, False, None),
        ]
        for op, script, legacy, stdin_file in invariants:
            channel = "legacy channel" if legacy else "VM default"
            for limit, want in ((SMALL, True), (1 << 30, False)):
                def go():
                    if stdin_file:
                        with stdin_file.open("rb") as handle:
                            return rl.run(script, work, limit=limit, stdin=handle)
                    return rl.run(script, work, limit=limit)
                proc = on_legacy(go) if legacy else go()
                got = rl.limit_error(proc, op, limit)
                what = "control: the limit error" if want else "limit removed: detected"
                outcome("%s invariant (%s), %s" % (op, channel, what), got == want,
                        "exit %d %r" % (proc.returncode, (proc.stdout + proc.stderr)[-200:]))
        read_val_controls(work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    bad = [n for n, ok in results if not ok]
    if bad:
        print("FAIL read-limit contract controls: %d of %d" % (len(bad), len(results)))
        return 1
    print("PASS read-limit contract controls: %d controls (refusal contracts detect a changed reason, position or "
          "execution; the supported-path invariants detect a removed limit)" % len(results))
    return 0


if __name__ == "__main__":
    sys.exit(main())
