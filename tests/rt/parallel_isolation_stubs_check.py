#!/usr/bin/env python3
"""B09 F-2: drift guard for tests/rt/parallel_isolation_stubs.cpp.

The isolated built-in binaries replace four production functions from src/rt/parallel_task.cpp
with test stubs (combined-b3-b5.md section 56). This check fails when the stubs could stop
matching production:

1. signature: every global symbol the stubs object defines must be defined, with the identical
   mangled name, by the production parallel_task.o, and the stubs must define exactly the four
   expected functions (a signature change in production leaves a stub symbol that production no
   longer defines);
2. body fingerprint: the production definitions (and the running_pool declaration their
   no-runtime behaviour depends on) must match the pinned sha256; a change means the stubs must
   be reviewed and the fingerprint re-pinned in the same commit;
3. parity: the probe (tests/rt/parallel_isolation_probe.cpp) linked with the real objects and
   linked with the stubs must print identical output.

--selftest runs each check against planted drift and requires it to fail.

usage: parallel_isolation_stubs_check.py --stubs-obj O --production-obj O --source CPP
                                         --real-probe BIN --stub-probe BIN
       parallel_isolation_stubs_check.py --selftest --stubs-obj O --production-obj O --source CPP
"""
import argparse
import hashlib
import re
import subprocess
import sys

EXPECTED = {
    "nift::rt::ParallelBlock::ParallelBlock(nift::rt::ParallelWaitKind)",
    "nift::rt::ParallelBlock::~ParallelBlock()",
    "nift::rt::parallel_task_identity(std::shared_ptr<nift::rt::ParallelTaskRecord> const&)",
    "nift::rt::notify_parallel_wait_states(std::shared_ptr<nift::rt::ParallelTaskRecord> const&)",
}
# sha256 of the extracted production definitions (see extract()). Re-pin only after reviewing
# tests/rt/parallel_isolation_stubs.cpp against the new production code.
PINNED_BODIES = "1f4a1418bb4f50ad7acc463bd932222c9eaaf1f4766b4331b71bdf2911503890"

BODY_PATTERNS = [
    r"^thread_local ParallelRuntime\* running_pool = nullptr;$",
    r"^ParallelBlock::ParallelBlock\(ParallelWaitKind kind\).*?^}$",
    r"^ParallelBlock::~ParallelBlock\(\).*?}$",
    r"^std::uint64_t parallel_task_identity\(const std::shared_ptr<ParallelTaskRecord>& record\) \{.*?^}$",
    r"^void notify_parallel_wait_states\(const std::shared_ptr<ParallelTaskRecord>& record\) \{.*?^}$",
]


def defined_symbols(obj):
    out = subprocess.run(["nm", "-g", "--defined-only", obj], capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("FAIL nm %s: %s" % (obj, out.stderr.strip()))
    syms = set()
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms.add(parts[2])
    return syms


def demangle(names):
    out = subprocess.run(["c++filt"], input="\n".join(sorted(names)), capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit("FAIL c++filt: %s" % out.stderr.strip())
    return dict(zip(sorted(names), out.stdout.splitlines()))


def check_signatures(stub_syms, prod_syms):
    errors = []
    names = demangle(stub_syms) if stub_syms else {}
    for sym in sorted(stub_syms):
        if sym not in prod_syms:
            errors.append("stub symbol %s (%s) is not defined by parallel_task.o" % (sym, names.get(sym)))
    got = set(names.values())
    # constructors/destructors emit C1/C2 (D1/D2) variants with the same demangled name
    if got != EXPECTED:
        errors.append("stubs define %s, expected %s" % (sorted(got), sorted(EXPECTED)))
    return errors


def extract(source_text):
    parts = []
    for pattern in BODY_PATTERNS:
        m = re.search(pattern, source_text, re.M | re.S)
        if not m:
            return None, "production definition not found: %s" % pattern
        parts.append(m.group(0))
    return "\n".join(parts), None


def check_bodies(source_text, pinned):
    text, err = extract(source_text)
    if err:
        return [err], None
    digest = hashlib.sha256(text.encode()).hexdigest()
    if digest != pinned:
        return ["production definitions changed (sha256 %s, pinned %s): review "
                "tests/rt/parallel_isolation_stubs.cpp and re-pin" % (digest, pinned)], digest
    return [], digest


def run_probe(binary):
    out = subprocess.run([binary], capture_output=True, text=True, timeout=60)
    return out.returncode, out.stdout, out.stderr


def check_parity(real, stub):
    errors = []
    if real[0] != 0:
        errors.append("real probe exited %d: %s" % (real[0], real[2].strip()))
    if stub[0] != 0:
        errors.append("stub probe exited %d: %s" % (stub[0], stub[2].strip()))
    if real[1] != stub[1]:
        errors.append("probe output differs:\n--- real\n%s--- stubs\n%s" % (real[1], stub[1]))
    if real[1].count("\n") < 18:
        errors.append("real probe printed %d lines, expected 18" % real[1].count("\n"))
    return errors


def selftest(a, source_text, stub_syms, prod_syms):
    failures = 0

    def expect_fail(name, errors):
        nonlocal failures
        if errors:
            print("PASS selftest %s: rejected (%s)" % (name, errors[0].splitlines()[0]))
        else:
            print("FAIL selftest %s: drift not detected" % name)
            failures += 1

    # a signature change: production now defines a different overload
    renamed = {s.replace("ParallelWaitKind", "ParallelWaitKinX") for s in prod_syms}
    expect_fail("signature drift", check_signatures(stub_syms, renamed))
    # a stub removed
    expect_fail("missing stub", check_signatures({s for s in stub_syms if "notify" not in s}, prod_syms))
    # production gains behaviour on a thread without a pool
    drifted = source_text.replace("ParallelBlock::~ParallelBlock() {", "ParallelBlock::~ParallelBlock() { note_block();", 1)
    expect_fail("body drift", check_bodies(drifted, PINNED_BODIES)[0] if drifted != source_text
                else ["planted body drift did not apply"])
    drifted = source_text.replace("if (!record) return 0;", "if (!record) return 1;", 1)
    expect_fail("identity drift", check_bodies(drifted, PINNED_BODIES)[0] if drifted != source_text
                else ["planted identity drift did not apply"])
    # differing probe output
    real = (0, "main block kind=0 admitted=1\n" * 18, "")
    expect_fail("parity drift", check_parity(real, (0, real[1].replace("admitted=1", "admitted=0", 1), "")))
    expect_fail("parity crash", check_parity(real, (-5, "", "trap")))
    # the unmodified inputs pass
    clean = check_signatures(stub_syms, prod_syms) + check_bodies(source_text, PINNED_BODIES)[0]
    if clean:
        print("FAIL selftest control: %s" % clean)
        failures += 1
    else:
        print("PASS selftest control: unmodified stubs and production accepted")
    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stubs-obj", required=True)
    ap.add_argument("--production-obj", required=True)
    ap.add_argument("--source", required=True)
    ap.add_argument("--real-probe")
    ap.add_argument("--stub-probe")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--print-fingerprint", action="store_true")
    a = ap.parse_args()
    with open(a.source) as f:
        source_text = f.read()
    if a.print_fingerprint:
        text, err = extract(source_text)
        print(err or hashlib.sha256(text.encode()).hexdigest())
        return 0 if not err else 1
    stub_syms = defined_symbols(a.stubs_obj)
    prod_syms = defined_symbols(a.production_obj)
    if a.selftest:
        failures = selftest(a, source_text, stub_syms, prod_syms)
        print("%s parallel isolation stubs selftest: %d failure(s)" % ("PASS" if not failures else "FAIL", failures))
        return 1 if failures else 0
    if not a.real_probe or not a.stub_probe:
        sys.exit("FAIL --real-probe and --stub-probe are required")
    errors = check_signatures(stub_syms, prod_syms)
    errors += check_bodies(source_text, PINNED_BODIES)[0]
    real, stub = run_probe(a.real_probe), run_probe(a.stub_probe)
    errors += check_parity(real, stub)
    for e in errors:
        print("FAIL %s" % e)
    if errors:
        return 1
    print("PASS parallel isolation stubs: %d symbols match parallel_task.o, production bodies match the "
          "pinned fingerprint, probe output identical (%d lines)" % (len(stub_syms), real[1].count("\n")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
