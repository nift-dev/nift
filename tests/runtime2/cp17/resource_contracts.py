#!/usr/bin/env python3
"""CP17 R1: stream limits, I/O failures, empty-file save, closed codes and the
per-run handle limit, all under NIFT_VM=require."""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

# name, source, files, env, expected outcome, expected stdout, expected code, stderr fragment
CASES = [
    ("read_all_limit", 's := ifstream("p"); print(s.read_all()); close(s)', {"p": "abc\n"},
     {"NIFT_READ_LIMIT": "2"}, "vm-error", "", "limit.exceeded", "read limit"),
    ("read_line_limit", 's := ifstream("p"); print(s.read_line()); close(s)', {"p": "abc\n"},
     {"NIFT_READ_LIMIT": "2"}, "vm-error", "", "limit.exceeded", "read limit"),
    ("read_count_limit", 's := ifstream("p"); print(s.read(4)); close(s)', {"p": "abc\n"},
     {"NIFT_READ_LIMIT": "2"}, "vm-error", "", "limit.exceeded", "read limit"),
    ("read_all_invalid_setting", 's := ifstream("p"); print(s.read_all()); close(s)', {"p": "abc\n"},
     {"NIFT_READ_LIMIT": "invalid"}, "vm-error", "", "runtime.invalid_argument", "NIFT_READ_LIMIT"),
    ("read_all_budget", 's := ifstream("p"); print(s.read_all().length()); close(s)', {"p": "x" * 100000},
     {"NIFT_VM_HEAP_BUDGET": "16384"}, "vm-error", "", "limit.exceeded", "memory budget"),
    ("read_all_exact", 's := ifstream("p"); print(s.read_all()); close(s)', {"p": "ab"},
     {"NIFT_READ_LIMIT": "2"}, "vm", "ab\n", "", ""),
    ("read_zero", 's := ifstream("p"); print(s.read(0).length()); close(s)', {"p": "abc\n"},
     {}, "vm", "0\n", "", ""),
    ("read_short", 's := ifstream("p"); print(s.read(100)); close(s)', {"p": "abc\n"},
     {}, "vm", "abc\n\n", "", ""),
    ("read_empty", 's := ifstream("p"); print(s.read_all().length()); close(s)', {"p": ""},
     {}, "vm", "0\n", "", ""),
    ("eof_exact", 's := ifstream("p"); s.read(4); print(s.eof()); close(s)', {"p": "abcd"},
     {}, "vm", "true\n", "", ""),
    ("eof_short", 's := ifstream("p"); s.read(2); print(s.eof()); close(s)', {"p": "abcd"},
     {}, "vm", "false\n", "", ""),
    ("eof_empty", 's := ifstream("p"); print(s.eof()); close(s)', {"p": ""},
     {}, "vm", "true\n", "", ""),
    ("eof_repeated", 's := ifstream("p"); s.read(4); print(s.eof()); print(s.eof()); close(s)',
     {"p": "abcd"}, {}, "vm", "true\ntrue\n", "", ""),
    ("empty_save", 'f := file("new"); f.open("w"); f.save(); f.close(); print(exists("new"))', {},
     {}, "vm", "true\n", "", ""),
    ("empty_append", 'f := file("new"); f.open("a"); f.save(); f.close(); print(exists("new"))', {},
     {}, "vm", "true\n", "", ""),
    ("empty_unsaved_close", 'f := file("new"); f.open("w"); f.close(); print("SUCCESS")', {},
     {}, "vm-error", "", "", "unsaved changes"),
    ("closed_read_code", 'f := file("p"); f.open("r"); f.close(); print(f.read_all())', {"p": "abc\n"},
     {}, "vm-error", "", "runtime.handle_closed", "handle is closed"),
    ("closed_open_code", 'f := file("p"); f.open("r"); f.close(); f.open("r")', {"p": "abc\n"},
     {}, "vm-error", "", "runtime.handle_closed", "handle is closed"),
    ("handle_limit_one", 'f := file("p"); g := file("p"); print("SUCCESS")', {"p": "abc\n"},
     {"NIFT_HANDLE_LIMIT": "1"}, "vm-error", "", "limit.exceeded", "NIFT_HANDLE_LIMIT"),
    ("handle_limit_invalid", 'f := file("p"); print("SUCCESS")', {"p": "abc\n"},
     {"NIFT_HANDLE_LIMIT": "invalid"}, "vm-error", "", "runtime.invalid_argument", "NIFT_HANDLE_LIMIT"),
    ("handle_slot_reuse", 'i := 0; while (i < 3) { f := file("p"); f.open("r"); f.close(); i += 1 }; print("ok")',
     {"p": "abc\n"}, {"NIFT_HANDLE_LIMIT": "1"}, "vm", "ok\n", "", ""),
    ("handle_default", 'f := file("p"); g := file("p"); print("ok")', {"p": "abc\n"},
     {}, "vm", "ok\n", "", ""),
]

DEV_FULL = [
    ("close_output_failure", 's := ofstream("/dev/full"); s.write("hello"); close(s); print("SUCCESS")',
     "stream.close_failed", "close: output flush failed"),
    ("flush_output_failure", 's := ofstream("/dev/full"); s.write("hello"); s.flush(); print("SUCCESS")',
     "stream.flush_failed", "flush: output failure"),
    ("method_close_output_failure", 's := ofstream("/dev/full"); s.write("hello"); s.close(); print("SUCCESS")',
     "stream.close_failed", "close: output flush failed"),
    ("cleanup_output_failure", 's := ofstream("/dev/full"); s.write("hello")',
     "stream.close_failed", "close: output flush failed"),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nift", required=True)
    args = ap.parse_args()
    nift = os.path.abspath(args.nift)
    failures = 0
    cases = list(CASES)
    if Path("/dev/full").exists():
        for name, source, code, message in DEV_FULL:
            cases.append((name, source, {}, {}, "vm-error", "", code, message))
    for name, source, files, extra, outcome, stdout, code, fragment in cases:
        with tempfile.TemporaryDirectory(prefix="cp17-resource-") as tmp:
            root = Path(tmp)
            for path, text in files.items():
                (root / path).write_text(text)
            (root / "case.f").write_text(source + "\n")
            env = {k: v for k, v in os.environ.items()
                   if not k.startswith("NIFT_VM") and k not in ("NIFT_READ_LIMIT", "NIFT_HANDLE_LIMIT")}
            env.update(NIFT_VM="require", NIFT_VM_STATS=str(root / "stats.jsonl"), NIFT_PREVALIDATE="off")
            env.update(extra)
            proc = subprocess.run([nift, "case.f"], cwd=root, env=env, stdin=subprocess.DEVNULL,
                                  capture_output=True, timeout=60)
            rows = [json.loads(line) for line in (root / "stats.jsonl").read_text().splitlines()
                    if line.strip()] if (root / "stats.jsonl").exists() else []
            record = rows[-1] if rows else {}
            problems = []
            if record.get("outcome") != outcome:
                problems.append(f"outcome {record.get('outcome')}/{record.get('reason')}")
            if proc.stdout.decode(errors="replace") != stdout:
                problems.append(f"stdout {proc.stdout!r}")
            if code and record.get("code") != code:
                problems.append(f"code {record.get('code')!r} (want {code})")
            if fragment and fragment.encode() not in proc.stderr:
                problems.append(f"stderr lacks {fragment!r}: {proc.stderr!r}")
            if outcome == "vm" and proc.returncode != 0:
                problems.append(f"exit {proc.returncode}")
            if problems:
                failures += 1
                print(f"FAIL {name}: " + "; ".join(problems))
            else:
                print(f"ok   {name}")
    print(f"{'PASS' if not failures else 'FAIL'} cp17 resource contracts: {len(cases)} cases, {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
