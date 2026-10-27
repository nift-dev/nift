#!/usr/bin/env python3
"""CP22 coroutine fixtures: legacy oracle, VM differential, refusals, mutants.

  run.py --nift ./nift --mode legacy      legacy (NIFT_VM unset) == recorded goldens
  run.py --nift ./nift --mode vm          NIFT_VM=require == legacy golden (or the
                                          case's .r2 golden under its EXPECT.tsv
                                          policy); real VM execution checked in
                                          NIFT_VM_STATS (outcome, vm_executions,
                                          legacy_executions, coroutine statistics)
  run.py --nift ./nift --mode refuse      NIFT_VM=1: refused before execution with
                                          the exact REFUSE.tsv reason (vm_executions
                                          0), output == legacy golden
  run.py --nift ./nift --mode trace       tr_*.f: the Host-op trace (NIFT_TRACE_HOST) of
                                          each context ("script", "script/async#N", ...)
                                          is the legacy one, in order (rt-diff's rule)
  run.py --nift ./nift --mode default     the contractual default (no test switches):
                                          each case runs on the VM == its golden, or is
                                          refused before execution for an admission
                                          reason and legacy == its golden
  run.py --nift ./nift --mode vm-only     VM-determinism fixtures (vm-only/*.f) ==
                                          their .vm goldens (legacy is racy there)
  run.py --nift ./nift --record           (re)write legacy goldens from the oracle
  run.py --nift M --mutant N --oracle ./nift --mutant-info M.json
                                          a -DNIFT_CORO_MUTANT=N build must be caught
                                          by its named witness fixtures (MUTANTS)

Crashes, signals, timeouts and missing or malformed statistics are harness
failures, never detections or passes. Every fixture runs in a fresh
temporary directory with a minimal environment.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
CASES = HERE / "cases"
REFUSE = HERE / "refuse"
VM_ONLY = HERE / "vm-only"
TIMEOUT = int(os.environ.get("CP22_TIMEOUT", "120"))

# Mutant witnesses: fixture -> the exact observable signature of the defect.
# A detection needs a real VM execution (statistics written, vm_executions 1)
# and this signature; anything else is a miss or a harness failure.
MUTANTS = {
    1: ("lost wakeup", {"a02_nested_many.f": {"exit": 70, "outcome": "internal-error",
                                              "reason": "coroutine deadlock"}}),
    2: ("double resume", {"vm-only/v_double_resume.f": {"stdout": b"b\nc\nm\n", "exit": 0, "outcome": "vm"}}),
    3: ("timer fires early", {"vm-only/v_timer_not_early.f": {"stdout": b"false\n", "exit": 0, "outcome": "vm"}}),
    4: ("task frames leaked", {"vm-only/v_heap_baseline.f": {"stdout": b"done\n", "exit": 0, "outcome": "vm",
                                                             "heap_live_min": 4000000}}),
    5: ("await result from the wrong future", {"await_done_order.f": {"stdout": b"60\n60\n", "exit": 0,
                                                                  "outcome": "vm"}}),
    6: ("task global writes reach the caller", {"iso_global_snapshot.f": {"stdout": b"11\n11\n", "exit": 0,
                                                                          "outcome": "vm"}}),
    7: ("drain skipped", {"drain_exit.f": {"stdout": b"main\n", "exit": 0, "outcome": "vm"}}),
    8: ("awaited Error re-stamped at the await", {"err_thrown.f": {"stdout": b"boom\nuser.raised\n3\n12\n",
                                                                  "exit": 1, "outcome": "vm-error"}}),
    9: ("structured completion skipped", {"drain_structured.f": {"stdout": b"2\nafter\ninner done\n", "exit": 0,
                                                                 "outcome": "vm"}}),
    10: ("no preemption", {"vm-only/v_preempt.f": {"stdout": b"false\n", "exit": 0, "outcome": "vm"}}),
    11: ("await accepts a thread handle", {"th_await_thread.f": {"stdout": b"4\n4\n", "exit": 0, "outcome": "vm"}}),
    12: ("cooperative admission disabled", {"refuse/r_ca_cpu_loop.f": {"exit": 0, "outcome": "vm"},
                                           "refuse/r_ca_blocking_host.f": {"stdout": b"false\n", "exit": 0,
                                                                           "outcome": "vm"}}),
    14: ("A1 off (blocking Host calls admitted)", {"refuse/r_ca_main_blocking.f": {"stdout": b"true\n1\n", "exit": 0,
                                                                                "outcome": "vm"}}),
    15: ("A2 off (task recursion admitted)", {"refuse/r_ca_recursion.f": {"stdout": b"610\n", "exit": 0,
                                                                          "outcome": "vm"}}),
    16: ("A3 off (non-suspending task loops admitted)", {"refuse/r_ca_cpu_loop.f": {"exit": 0, "outcome": "vm"}}),
    17: ("A4 off (data-proportional built-ins admitted)", {"refuse/r_ca_cpu_native.f": {"exit": 0, "outcome": "vm"}}),
    18: ("captured cells admitted (coro-capture off)", {"refuse/r_share_closure.f": {"exit": 0, "outcome": "vm"}}),
    19: ("A3 accepts a sleep anywhere in the body (continue skips it)", {"refuse/r_f4_continue.f": {"exit": 0,
                                                                                              "outcome": "vm"}}),
    20: ("calls through values (callable fields) not followed", {"refuse/r_f3_member_loop.f": {"exit": 0,
                                                                                             "outcome": "vm"}}),
    13: ("scheduling observations admitted", {"refuse/r_obs_print_default.f": {"stdout": b"task 1\n2\n", "exit": 0,
                                                                              "outcome": "vm"}}),
}


def table(path):
    rows = []
    if path.exists():
        for line in path.read_text().splitlines():
            line = line.rstrip()
            if line.strip() and not line.lstrip().startswith("#"):
                rows.append(line.split("\t"))
    return rows


# Non-contractual test switches (record §12): cooperative thread() and A5 off
# (scheduling observations admitted). Every VM run of the corpus sets them,
# except fixtures named `*_default.f` and the `default` mode, which check the
# contractual default.
SWITCHES = {"NIFT_VM_COOPERATIVE_THREADS": "1", "NIFT_VM_COOPERATIVE_OBSERVE": "1"}
# Reasons the contractual default may refuse a fixture for (record §12.1).
DEFAULT_REASONS = ("coro-observe:", "coro-task-", "coro-blocking-host:", "callable:thread")


def run(nift, folder, name, vm=None, extra=None, trace=None, switches=True):
    with tempfile.TemporaryDirectory(prefix="cp22-") as d:
        shutil.copy(folder / name, Path(d) / name)
        module = folder / (name[:-2] + ".m.nft")
        if module.exists():
            shutil.copy(module, Path(d) / "m.nft")
        stats = Path(d) / "stats.jsonl"
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": d, "TMPDIR": d, "LANG": "C",
               "LC_ALL": "C", "TZ": "UTC", "NIFT_PREVALIDATE": "off"}
        if vm:
            env.update({"NIFT_VM": vm, "NIFT_VM_STATS": str(stats)})
            # thread() compiles only with the cooperative opt-in (owner decision
            # pending); `*_default.f` fixtures check the default refusal.
            if switches and not name.endswith("_default.f"):
                env.update(SWITCHES)
        env.update(extra or {})
        fds = ()
        if trace is not None:
            trace_file = open(Path(d) / "trace.jsonl", "wb")
            fds = (trace_file.fileno(),)
            env["NIFT_TRACE_HOST"] = str(trace_file.fileno())
        try:
            p = subprocess.run([nift, name], cwd=d, env=env, stdin=subprocess.DEVNULL, capture_output=True,
                               timeout=TIMEOUT, pass_fds=fds)
            code = p.returncode
            out, err = p.stdout, p.stderr
        except subprocess.TimeoutExpired:
            return (b"", b"", "timeout"), None
        finally:
            if trace is not None:
                trace_file.close()
        if trace is not None:
            trace.extend((Path(d) / "trace.jsonl").read_text().replace(d, "$ROOT").splitlines())
        out, err = out.replace(d.encode(), b"$ROOT"), err.replace(d.encode(), b"$ROOT")
        st = None
        if vm and stats.exists():
            try:
                st = json.loads(stats.read_text().splitlines()[-1])
            except (OSError, IndexError, ValueError):
                st = None
        return (out, err, code), st


def golden(folder, name, suffix=""):
    stem = folder / name[:-2]
    try:
        return ((Path(str(stem) + suffix + ".stdout")).read_bytes(), (Path(str(stem) + suffix + ".stderr")).read_bytes(),
                int((Path(str(stem) + suffix + ".exit")).read_text().strip()))
    except OSError:
        return None


def harness_problem(result, st, executed=True):
    if result[2] == "timeout":
        return "harness: timeout"
    if not isinstance(result[2], int) or result[2] < 0 or result[2] >= 128:
        return f"harness: abnormal exit {result[2]}"
    if st is None:
        return "harness: statistics missing or malformed"
    for key in ("outcome", "vm_executions", "legacy_executions"):
        if key not in st:
            return f"harness: statistics without {key}"
    if executed and (st.get("vm_executions") != 1 or st.get("legacy_executions") != 0):
        return f"harness: not a real VM execution (vm={st.get('vm_executions')}, legacy={st.get('legacy_executions')})"
    return None


def check_vm(nift, name, policy):
    want = golden(CASES, name, ".r2") if policy else golden(CASES, name)
    if want is None:
        return [f"missing golden for {name}"]
    result, st = run(nift, CASES, name, "require")
    bad = harness_problem(result, st)
    if bad:
        return [bad + f" (stderr {result[1][:200]!r})"]
    problems = []
    if result != want:
        problems.append(f"VM {result!r} != expected {want!r}")
    expected_outcome = "vm" if want[2] == 0 else "vm-error"
    if st.get("outcome") != expected_outcome:
        problems.append(f"outcome {st.get('outcome')} != {expected_outcome} ({st.get('reason')})")
    if "coro_spawned" not in st:
        problems.append("no coroutine statistics (not a coroutine unit?)")
    if policy:
        legacy = golden(CASES, name)
        if legacy is None or legacy == want:
            problems.append(f"policy {policy}: the legacy golden must differ from the .r2 golden")
        elif not policy_ok(policy, legacy, want):
            problems.append(f"policy {policy} does not describe the difference {legacy!r} -> {want!r}")
    return problems


def check_default(nift, name, policy):
    """The contractual default (no switches, NIFT_VM=1): either the fixture
    runs on the VM with the expected output, or it is refused before
    execution for an admission reason and legacy produces its golden."""
    result, st = run(nift, CASES, name, "1", switches=False)
    bad = harness_problem(result, st, executed=False)
    if bad:
        return [bad], None
    if st.get("outcome") in ("vm", "vm-error"):
        want = golden(CASES, name, ".r2") if policy else golden(CASES, name)
        if st.get("vm_executions") != 1 or st.get("legacy_executions") != 0:
            return [f"not one VM execution ({st})"], None
        return ([] if result == want else [f"VM {result!r} != expected {want!r}"]), "vm"
    reason = st.get("reason", "")
    problems = []
    if st.get("outcome") != "fallback" or st.get("vm_executions") != 0:
        problems.append(f"outcome {st.get('outcome')} vm_executions={st.get('vm_executions')}")
    if not reason.startswith(DEFAULT_REASONS):
        problems.append(f"refused for {reason!r}, not an admission reason")
    if result != golden(CASES, name):
        problems.append(f"fallback output {result!r} != legacy golden")
    return problems, reason


POLICY_PREFIX = {"D-009#5-future": "future: ", "D-009#5-thread": "thread: "}


def correction_ok(policy, legacy, vm):
    """Deliberate semantic corrections (record §13): the VM's .r2 golden is the
    defined Runtime 2.0 behaviour; the legacy golden shows the legacy quirk.
      correction:lexical-scope   legacy resolves a free name in the caller's
                                 dynamic scope; same exit status, other stdout
      correction:lambda-registry legacy fails with `invalid lambda` (its per-worker
                                 lambda table); the VM succeeds"""
    if policy == "correction:lexical-scope":
        return legacy[2] == vm[2] == 0 and legacy[0] != vm[0]
    if policy == "correction:lambda-registry":
        return legacy[2] == 1 and b"invalid lambda" in legacy[1] and vm[2] == 0
    return False


def policy_ok(policy, legacy, vm):
    """D-009#5 through await (`future: `) or a thread's join (`thread: `):
    legacy reports a task's fatal error at the task's return statement with a
    `return: ` prefix after the task prefix; the VM at the failing expression
    without it. Same stdout and exit, one line."""
    if policy.startswith("correction:"):
        return correction_ok(policy, legacy, vm)
    prefix = POLICY_PREFIX.get(policy)
    if prefix is None:
        return False
    if legacy[0] != vm[0] or legacy[2] != vm[2] or legacy[2] != 1:
        return False
    shape = re.compile(r"^error: ([^\n]*):(\d+):(\d+): (" + re.escape(prefix) + r"[^\n]*)\n\Z")
    lm, vm_m = shape.match(legacy[1].decode()), shape.match(vm[1].decode())
    if not lm or not vm_m or lm.group(1) != vm_m.group(1):
        return False
    lmsg = lm.group(4)
    while prefix + "return: " in lmsg:
        lmsg = lmsg.replace(prefix + "return: ", prefix)
    # Same message once legacy's return: prefixes go; the VM's position is
    # the failing expression, on the same line, after legacy's `return`.
    return lmsg == vm_m.group(4) and lm.group(2) == vm_m.group(2) and int(vm_m.group(3)) > int(lm.group(3))


def check_refuse(nift, name, reason):
    want = golden(REFUSE, name)
    if want is None:
        return [f"missing golden for {name}"]
    result, st = run(nift, REFUSE, name, "1")
    bad = harness_problem(result, st, executed=False)
    if bad:
        return [bad]
    problems = []
    if st.get("outcome") != "fallback" or st.get("reason") != reason or st.get("vm_executions") != 0:
        problems.append(f"expected fallback:{reason}, got {st.get('outcome')}/{st.get('reason')} "
                        f"vm_executions={st.get('vm_executions')}")
    if result != want:
        problems.append(f"fallback output {result!r} != legacy {want!r}")
    return problems


def check_vm_only(nift, name):
    want = golden(VM_ONLY, name, ".vm")
    if want is None:
        return [f"missing .vm golden for {name}"]
    result, st = run(nift, VM_ONLY, name, "require")
    bad = harness_problem(result, st)
    if bad:
        return [bad]
    return [] if result == want else [f"VM {result!r} != {want!r}"]


def by_context(lines):
    contexts = {}
    for line in lines:
        entry = json.loads(line)
        ctx = entry.pop("ctx", "?")
        contexts.setdefault(ctx, []).append(json.dumps(entry, sort_keys=True))
    return contexts


def check_trace(nift, name):
    legacy_lines, vm_lines = [], []
    legacy, _ = run(nift, CASES, name, None, trace=legacy_lines)
    vm, st = run(nift, CASES, name, "require", trace=vm_lines)
    bad = harness_problem(vm, st)
    if bad:
        return [bad]
    problems = []
    if legacy != vm:
        problems.append(f"output differs: legacy {legacy!r} VM {vm!r}")
    lc, vc = by_context(legacy_lines), by_context(vm_lines)
    if not legacy_lines:
        problems.append("legacy wrote no trace (fixture or harness defect)")
    if lc != vc:
        problems.append(f"trace differs: legacy {lc} VM {vc}")
    return problems


def record(nift):
    for folder in (CASES, REFUSE):
        for path in sorted(folder.glob("*.f")):
            result, _ = run(nift, folder, path.name)
            if result[2] == "timeout":
                print(f"record: timeout {path.name}")
                return 1
            stem = folder / path.name[:-2]
            Path(str(stem) + ".stdout").write_bytes(result[0])
            Path(str(stem) + ".stderr").write_bytes(result[1])
            Path(str(stem) + ".exit").write_text(f"{result[2]}\n")
            print(f"recorded {folder.name}/{path.name}: exit {result[2]}")
    return 0


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def mutant(args):
    n = args.mutant
    label, witnesses = MUTANTS[n]
    info = json.loads(Path(args.mutant_info).read_text()) if args.mutant_info else {}
    if info.get("mutant") != n or info.get("sha256") != sha256(args.nift) or \
            info.get("normal_sha256") != sha256(args.oracle):
        print(f"FAIL coroutine mutant {n}: harness: provenance manifest does not match the binaries")
        return 1
    failures, detected = 0, []
    for key, sig in witnesses.items():
        if key.startswith("refuse/"):  # a refusal witness: the mutant runs what the oracle refuses
            folder, name = REFUSE, key.split("/", 1)[1]
            reasons = {r[0]: r[1] for r in table(HERE / "REFUSE.tsv")}
            base = check_refuse(args.oracle, name, reasons.get(name, ""))
        else:
            folder, name = (VM_ONLY, key.split("/", 1)[1]) if key.startswith("vm-only/") else (CASES, key)
            base = check_vm_only(args.oracle, name) if folder is VM_ONLY else check_vm(args.oracle, name, None)
        if base:
            failures += 1
            print(f"FAIL mutant {n} {key}: oracle build fails its own gate: {base}")
            continue
        result, st = run(args.nift, folder, name, "require")
        bad = harness_problem(result, st)
        if bad:
            failures += 1
            print(f"FAIL mutant {n} {key}: {bad}")
            continue
        ok = result[2] == sig["exit"] and st.get("outcome") == sig["outcome"]
        if "stdout" in sig:
            ok = ok and result[0] == sig["stdout"]
        if "reason" in sig:
            ok = ok and st.get("reason") == sig["reason"]
        if "heap_live_min" in sig:
            ok = ok and int(st.get("heap_live", 0)) >= sig["heap_live_min"]
        if ok:
            detected.append(key)
            print(f"ok   mutant {n} ({label}) {key}: detected (outcome={st.get('outcome')}, "
                  f"reason={st.get('reason')!r}, heap_live={st.get('heap_live')})")
        else:
            print(f"miss mutant {n} ({label}) {key}: stdout={result[0]!r} exit={result[2]} "
                  f"outcome={st.get('outcome')} reason={st.get('reason')!r} heap_live={st.get('heap_live')}; "
                  f"expected {sig}")
    missed = sorted(set(witnesses) - set(detected))
    if failures or missed:
        print(f"FAIL coroutine mutant {n} ({label}): missed {missed}; {failures} harness failure(s)")
        return 1
    print(f"PASS coroutine mutant {n} ({label}): detected by {', '.join(detected)}")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--mode", choices=["legacy", "vm", "refuse", "vm-only", "trace", "default"])
    ap.add_argument("--record", action="store_true")
    ap.add_argument("--mutant", type=int, choices=sorted(MUTANTS))
    ap.add_argument("--oracle")
    ap.add_argument("--mutant-info")
    ap.add_argument("--only")
    ap.add_argument("--cases", help="fixture directory override (negative controls only)")
    args = ap.parse_args()
    global CASES
    if args.cases:
        CASES = Path(args.cases).resolve()
    args.nift = os.path.abspath(args.nift)
    if args.record:
        return record(args.nift)
    if args.mutant:
        if not args.oracle:
            print("FAIL: --oracle is required with --mutant")
            return 1
        args.oracle = os.path.abspath(args.oracle)
        return mutant(args)
    expect = {row[0]: (row[1] if len(row) > 1 else "") for row in table(HERE / "EXPECT.tsv")}
    failures, count = 0, 0
    if args.mode == "legacy":
        for folder in (CASES, REFUSE):
            for path in sorted(folder.glob("*.f")):
                if args.only and args.only not in path.name:
                    continue
                count += 1
                result, _ = run(args.nift, folder, path.name)
                if result != golden(folder, path.name):
                    failures += 1
                    print(f"FAIL legacy {folder.name}/{path.name}: {result!r}")
    elif args.mode == "vm":
        for path in sorted(CASES.glob("*.f")):
            if args.only and args.only not in path.name:
                continue
            count += 1
            problems = check_vm(args.nift, path.name, expect.get(path.name, ""))
            if problems:
                failures += 1
                print(f"FAIL vm {path.name}: " + "; ".join(problems))
            else:
                print(f"ok   vm {path.name}")
    elif args.mode == "refuse":
        for name, reason in ((r[0], r[1]) for r in table(HERE / "REFUSE.tsv")):
            count += 1
            problems = check_refuse(args.nift, name, reason)
            if problems:
                failures += 1
                print(f"FAIL refuse {name}: " + "; ".join(problems))
            else:
                print(f"ok   refuse {name} ({reason})")
        listed = {r[0] for r in table(HERE / "REFUSE.tsv")}
        unlisted = sorted(p.name for p in REFUSE.glob("*.f") if p.name not in listed)
        if unlisted:
            failures += 1
            print(f"FAIL refuse: fixtures without a REFUSE.tsv reason: {unlisted}")
    elif args.mode == "trace":
        for path in sorted(CASES.glob("tr_*.f")):
            count += 1
            problems = check_trace(args.nift, path.name)
            if problems:
                failures += 1
                print(f"FAIL trace {path.name}: " + "; ".join(problems))
            else:
                print(f"ok   trace {path.name}")
    elif args.mode == "default":
        admitted, refused = [], {}
        for path in sorted(CASES.glob("*.f")):
            count += 1
            problems, outcome = check_default(args.nift, path.name, expect.get(path.name, ""))
            if problems:
                failures += 1
                print(f"FAIL default {path.name}: " + "; ".join(problems))
                continue
            if outcome == "vm":
                admitted.append(path.name)
            else:
                refused.setdefault(outcome.split(":")[0] + (":" + outcome.split(":")[1] if ":" in outcome else ""),
                                   []).append(path.name)
            print(f"ok   default {path.name} ({outcome})")
        print(f"default: {len(admitted)} admitted on the VM: {' '.join(admitted)}")
        for reason, names in sorted(refused.items()):
            print(f"default: {len(names)} refused ({reason}): {' '.join(names)}")
    elif args.mode == "vm-only":
        for path in sorted(VM_ONLY.glob("*.f")):
            count += 1
            problems = check_vm_only(args.nift, path.name)
            if problems:
                failures += 1
                print(f"FAIL vm-only {path.name}: " + "; ".join(problems))
            else:
                print(f"ok   vm-only {path.name}")
    else:
        ap.error("--mode, --record or --mutant is required")
    print(f"{'PASS' if failures == 0 and count else 'FAIL'} cp22 {args.mode}: {count} case(s), {failures} failure(s)")
    return 0 if failures == 0 and count else 1


if __name__ == "__main__":
    sys.exit(main())
