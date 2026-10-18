#!/usr/bin/env python3
"""Runtime 2.0 CP12: pinned expectations for the approved D-018 differences.

  vm_golden.py --nift ./nift [--timeout S]

Review round 2: the VM never vouches for its own output, so the differential
gates approve no D-018a (numbers) difference and the deleted-line rule is
gone. The real programs that genuinely need an approved difference (the
former-replay suite's D-018a, D-001#4 and unverifiable loop-index cases,
seed 1) are pinned here instead, in tests/runtime2/vm-golden/. Legacy is
not consulted: each program's expected stdout, diagnostic and exit status
are computed by this file from the program's own operations, independently
of the VM:

  * numbers with Python's exact ints (W = 2^53 - 1, BIG = 2000000000^2, ...);
  * error positions from the CP10 AST (vm_sites.py: the node that raises
    the message, found by exact evaluation of constant operands), or a
    hand derivation written below;
  * recursion results by the recursion's definition (D-001#4: the VM's
    100,000-frame budget replaces legacy's 64 frames).

Each file name says its template (vm_former_replay.py names); EXPECT below
gives the reason. The VM must run each program (outcome vm or vm-error)
and produce exactly the expected result.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import vm_sites  # noqa: E402

GOLDEN = os.path.join(HERE, "vm-golden")
W = 9007199254740991          # w := 9007199254740991
BIG = 2000000000 * 2000000000  # big := 2000000000 * 2000000000
OVERFLOW = b"signed 64-bit integer overflow"

# Lines a former-replay context prints around its statement's output (OUT).
CONTEXT = {
    "top": ["before", "OUT", "after"], "topif": ["before", "OUT", "after"], "loop": ["before", "OUT", "after"],
    "fntop": ["before", "OUT", "7", "after"], "fntopdecl": ["before", "OUT", "7"],
    "fnloop": ["before", "OUT", "7", "after"], "fnown": ["before", "OUT", "7"], "fnnest": ["before", "OUT", "7"],
    "fnnestloop": ["before", "OUT", "7"], "fnboth": ["before", "OUT", "7", "OUT", "7"],
}

REASONS = {
    "errors": "D-018a: legacy computes the loop-code expression in doubles; CP09 integers overflow int64 "
              "(error at the failing operation, found by exact evaluation)",
    "numbers": "D-018a: exact integers beyond 2^53 in loop code (Python ints); exactly-once evaluation of loud()",
    "calls-11": "D-001#4: 70 frames, beyond legacy's 64",
    "calls-4": "D-018c: c3(arr, k5) indexes [1, 2] at 5; legacy's loop wording omits the index",
    "recursion": "D-001#4: the 100,000-frame budget (and the memory budget) replace legacy's 64 frames",
    "output": "D-001#4 after megabytes of output",
}


def lines(*items):
    return "".join(f"{x}\n" for x in items).encode()


def in_context(context, out):
    result = []
    for item in CONTEXT[context]:
        result.extend(out if item == "OUT" else [item])
    return lines(*result)


def site_of_text(source, text):
    """Position of the unique AST node whose source text is `text`."""
    root = vm_sites.parse(source)
    found = {vm_sites.position(source, n.begin) for n in root.walk() if vm_sites.text(source, n) == text}
    assert len(found) == 1, (text, found)
    return found.pop()


def expect(name, source):
    """(stdout, (line, column, message) or None, exit status, reason)."""
    if name.startswith("errors-"):
        # The template's expression E overflows int64 (Python ints below);
        # on E's line, exact evaluation of the constant operands (vm_sites)
        # finds the failing operation (E's own, or a wrapper's around it).
        e = {"ovf": (b"big * 3", BIG * 3), "ovf2": (b"big + big + big", BIG + BIG + BIG),
             "sub": (b"neg - big - big - big", -1 - BIG - BIG - BIG), "largem": (b"w * w", W * W)}[name.split("-")[1]]
        assert not -(1 << 63) <= e[1] < (1 << 63), name
        start = source.index(e[0])
        begin = source.rfind(b"\n", 0, start) + 1
        end = source.find(b"\n", start)
        site = vm_sites.expected_site(source, OVERFLOW, (begin, end))
        assert site is not None, name
        return b"before\n", (site[0], site[1], OVERFLOW), 1, REASONS["errors"]
    if name.startswith("numbers-"):
        _, k, context = name.split("-", 2)
        out = {"8": [W * 3], "13": [BIG + 1], "21": ["loud", W + 2], "22": ["loud", W * 2],
               "24": ["loud", W + 2]}.get(k)
        if k == "20":
            # e := 10^15; e = e * e * ...: e * e = 10^30 > 2^63 - 1 fails first;
            # every nested product starts at the first `e` after "e = ".
            line = source.split(b"\n").index(next(x for x in source.split(b"\n") if x.startswith(b"e = e * e"))) + 1
            return b"before\n", (line, 5, OVERFLOW), 1, REASONS["numbers"]
        return in_context(context, out), None, 0, REASONS["numbers"]
    if name.startswith("calls-11-"):
        return in_context(name.split("-", 2)[2], [70]), None, 0, REASONS["calls-11"]
    if name.startswith("calls-4-"):
        line, column = site_of_text(source, b"a[i]")
        return b"before\n", (line, column, b"JSON array index 5 is out of range in 'a[i]'"), 1, REASONS["calls-4"]
    m = re.match(r"recursion-(\d)-(\d+)$", name)
    if m:
        k, n = int(m.group(1)), int(m.group(2))
        budget = n > 100000 - 1  # d(n) needs n + 1 frames below main
        if k == 0:
            if budget:
                return b"s\n", site_of_text(source, b"d(n - 1)") + (b"callable recursion depth exceeded: d",), 1, \
                    REASONS["recursion"]
            return lines("s", n), None, 0, REASONS["recursion"]
        if k == 1:  # d(0) returns 1 / 0
            if budget:
                return b"s\n", site_of_text(source, b"d(n - 1)") + (b"callable recursion depth exceeded: d",), 1, \
                    REASONS["recursion"]
            return b"s\n", site_of_text(source, b"1 / n") + (b"division by zero",), 1, REASONS["recursion"]
        if k == 2:  # each frame holds a string 8 bytes longer: quadratic memory
            if n >= 99990:
                return b"s\n", site_of_text(source, b's + "xxxxxxxx"') + \
                    (b"callable recursion depth exceeded (memory budget)",), 1, REASONS["recursion"]
            return lines("s", n), None, 0, REASONS["recursion"]
        if k == 3:
            return lines(n), None, 0, REASONS["recursion"]
    m = re.match(r"output-(\d+)-(recursion|budget)$", name)
    if m:
        n = int(m.group(1))
        body = "".join(f"{'x' * 20}{i}\n" for i in range(n)).encode()
        if m.group(2) == "recursion":
            return body + b"0\n", None, 0, REASONS["output"]
        return body, site_of_text(source, b"d(n - 1)") + (b"callable recursion depth exceeded: d",), 1, \
            REASONS["output"]
    raise KeyError(name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    ap.add_argument("--timeout", type=float, default=120)
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    work = tempfile.mkdtemp(prefix="nift-vm-golden-")
    failures, count = [], 0
    try:
        for file in sorted(os.listdir(GOLDEN)):
            if not file.endswith(".f"):
                continue
            name = file[:-2]
            with open(os.path.join(GOLDEN, file), "rb") as f:
                source = f.read()
            stdout, error, code, _reason = expect(name, source)
            d = os.path.join(work, name)
            os.makedirs(d)
            path = os.path.join(d, "p.f")
            with open(path, "wb") as f:
                f.write(source)
            stats = os.path.join(d, ".stats")
            env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C", "LC_ALL": "C", "HOME": d, "TMPDIR": d,
                   "NIFT_VM": "1", "NIFT_VM_STATS": stats}
            for k in ("ASAN_OPTIONS", "UBSAN_OPTIONS", "NIFT_VM_MUTANT"):
                if k in os.environ:
                    env[k] = os.environ[k]
            try:
                p = subprocess.run([nift, "p.f"], cwd=d, env=env, capture_output=True, stdin=subprocess.DEVNULL,
                                   timeout=args.timeout)
                got = (p.stdout, p.stderr, p.returncode)
            except subprocess.TimeoutExpired:
                got = (b"", b"timeout", "timeout")
            st = json.loads(open(stats).read().splitlines()[-1]) if os.path.exists(stats) else {}
            stderr = b"" if error is None else b"error: %s:%d:%d: %s\n" % (path.encode(), error[0], error[1], error[2])
            count += 1
            if st.get("outcome") not in ("vm", "vm-error"):
                failures.append(f"{name}: not run on the VM ({st.get('outcome')} {st.get('reason')})")
            elif got != (stdout, stderr, code):
                failures.append(f"{name}: VM {got[2]} {got[0][-200:]!r} {got[1]!r}, expected {code} "
                                f"{stdout[-200:]!r} {stderr!r}")
            shutil.rmtree(d, ignore_errors=True)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for f in failures:
        print("FAIL " + f)
    print(("PASS" if not failures else "FAIL") + f" vm golden: {count} pinned programs (expected results computed "
          f"independently of the VM), {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
