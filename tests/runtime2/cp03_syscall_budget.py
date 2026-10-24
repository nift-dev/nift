#!/usr/bin/env python3
"""CP03 no-op syscall budget (owner-approved recalibration, 2026-10-24).

The previous guard compared one total against a single-host measurement
(20x20: 918 measured, limit 919). Linode and the review host both measure 921
for the same binary, and so does the commit that recorded 919. Every
filesystem category matches exactly across all three hosts; the difference
is in process-start and allocator syscalls (mmap/brk/mprotect/...), which
the categories do not count.

So the guard is split in two:
  * filesystem and path categories are pinned exactly. Any extra stat,
    openat, readlink or clock access fails. This is where reintroduced
    containment checks would appear.
  * the remainder (total minus the pinned categories) is bounded by the
    largest cross-host measurement plus ENV_TOLERANCE. The observed spread
    is 3 (20x20: 135/136/138) and 1 (200x10: 680/681).

Usage: cp03_syscall_budget.py <20x20|200x10> "<noop line>"  |  --selftest
"""
import re
import sys

PINNED = {
    "20x20": {"openat": 53, "stat": 643, "read": 27, "write": 1, "close": 53, "utimensat": 0,
              "rename": 0, "mkdir": 0, "unlink": 1, "readlink": 0, "getdents": 2, "futex": 3,
              "path.clock": 0},
    "200x10": {"openat": 413, "stat": 4223, "read": 207, "write": 1, "close": 413, "utimensat": 0,
               "rename": 0, "mkdir": 0, "unlink": 1, "readlink": 0, "getdents": 2, "futex": 3,
               "path.clock": 0},
}
# Largest measured remainder per probe: (host, total) evidence in the docstring.
REMAINDER_MEASURED = {"20x20": 138, "200x10": 681}
ENV_TOLERANCE = 5
# Categories that are already part of other counters, or are path markers
# rather than syscalls, are not subtracted from the total.
NOT_SYSCALLS = {"path.clock"}


def check(probe, line):
    """Return a list of failures (empty means within budget)."""
    fields = dict(re.findall(r"(\S+)=(\d+)", line))
    if "total" not in fields:
        return [f"no total in line: {line!r}"]
    fails = []
    pinned_sum = 0
    for name, limit in PINNED[probe].items():
        if name not in fields:
            fails.append(f"category {name} missing")
            continue
        value = int(fields[name])
        if value > limit:
            fails.append(f"{name}={value} > pinned {limit}")
        if name not in NOT_SYSCALLS:
            pinned_sum += value
    remainder = int(fields["total"]) - pinned_sum
    allowed = REMAINDER_MEASURED[probe] + ENV_TOLERANCE
    if remainder > allowed:
        fails.append(f"non-filesystem remainder {remainder} > {allowed} "
                     f"(measured max {REMAINDER_MEASURED[probe]} + {ENV_TOLERANCE})")
    return fails


def line_for(probe, total, **override):
    vals = dict(PINNED[probe])
    vals.update(override)
    body = " ".join(f"{k}={v}" for k, v in vals.items())
    return f"x n=0 nav=0 noop total={total} {body}"


def selftest():
    base20 = sum(v for k, v in PINNED["20x20"].items() if k not in NOT_SYSCALLS)
    base200 = sum(v for k, v in PINNED["200x10"].items() if k not in NOT_SYSCALLS)
    cases = [
        # The three recorded hosts all pass.
        ("20x20", line_for("20x20", 918), True, "DeepSeek host 918"),
        ("20x20", line_for("20x20", 919), True, "Codex local 919"),
        ("20x20", line_for("20x20", 921), True, "Linode/review host 921"),
        ("200x10", line_for("200x10", 5943), True, "200x10 5943"),
        ("200x10", line_for("200x10", 5944), True, "200x10 5944"),
        # Negative controls: one extra filesystem syscall fails, even when the
        # total stays under the old 919 limit.
        ("20x20", line_for("20x20", 919, stat=644), False, "one extra stat"),
        ("20x20", line_for("20x20", 918, openat=54), False, "one extra openat"),
        ("20x20", line_for("20x20", 918, readlink=1), False, "a readlink (containment)"),
        ("20x20", line_for("20x20", 918, **{"path.clock": 1}), False, "clock touched"),
        ("200x10", line_for("200x10", 5944 + 16000, readlink=16000), False, "CP03 pre-fix containment"),
        # Remainder bound.
        ("20x20", line_for("20x20", base20 + 143), True, "remainder at the bound"),
        ("20x20", line_for("20x20", base20 + 144), False, "remainder one over"),
        ("200x10", line_for("200x10", base200 + 687), False, "200x10 remainder over"),
        ("20x20", "x noop openat=53", False, "malformed line"),
    ]
    bad = 0
    for probe, line, want_ok, label in cases:
        got_ok = not check(probe, line)
        if got_ok != want_ok:
            bad += 1
            print(f"FAIL budget selftest: {label}: want {'pass' if want_ok else 'fail'}")
    if bad:
        return 1
    print(f"PASS CP03 syscall budget selftest: {len(cases)} cases")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest())
    probe, line = sys.argv[1], sys.argv[2]
    failures = check(probe, line)
    for f in failures:
        print(f"FAIL CP03 syscalls ({probe}): {f}")
    sys.exit(1 if failures else 0)
