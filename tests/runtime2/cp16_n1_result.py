#!/usr/bin/env python3
"""Validate exactly one complete CP16 N1 MUTANT-RESULT record.

  cp16_n1_result.py --log producer.log [--expected-checks 16] [--require-mutant]

The record is the only authority: a PASS phrase, a zero producer exit status or
a marker substring is not sufficient. Malformed, partial, duplicated, forged
or wrong-transition records are rejected.
"""
import argparse
import json
import sys

SCHEMA = "cp16-n1-mutant-result/v1"
EXPECTED = {
    ("open", "io.open_failed", "io.read_failed"),
    ("cat", "io.open_failed", "io.read_failed"),
    ("sha256_file", "io.open_failed", "io.read_failed"),
}


def fail(message):
    print("FAIL MUTANT-RESULT validation: " + message)
    return 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--expected-checks", type=int, default=16)
    ap.add_argument("--require-mutant", action="store_true")
    args = ap.parse_args()
    try:
        lines = open(args.log, errors="replace").read().splitlines()
    except OSError as error:
        return fail(f"cannot read log: {error}")
    records = [line[len("MUTANT-RESULT "):] for line in lines if line.startswith("MUTANT-RESULT ")]
    if len(records) != 1:
        return fail(f"expected exactly one MUTANT-RESULT, found {len(records)}")
    if any(line.startswith("FAIL") or "Traceback" in line for line in lines):
        return fail("producer log contains a failure or traceback")
    try:
        data = json.loads(records[0])
    except ValueError:
        return fail("malformed JSON")
    if not isinstance(data, dict):
        return fail("record is not an object")
    required = {"schema", "checks", "read_code_mismatches", "open_failure_codes",
                "harness_failures", "legacy_failures", "other_failures", "skipped_read_cases"}
    if set(data) != required:
        return fail(f"record keys {sorted(data)}, want {sorted(required)}")
    if data["schema"] != SCHEMA:
        return fail(f"schema {data['schema']!r}")
    if not isinstance(data["checks"], int) or isinstance(data["checks"], bool):
        return fail("checks is not an integer")
    if data["checks"] != args.expected_checks:
        return fail(f"checks {data['checks']}, want {args.expected_checks}")
    if type(data["skipped_read_cases"]) is not int or data["skipped_read_cases"] != 0:
        return fail(f"skipped_read_cases {data['skipped_read_cases']!r}, want integer 0")
    for key in ("open_failure_codes", "harness_failures", "legacy_failures", "other_failures"):
        if not isinstance(data[key], list) or data[key]:
            return fail(f"{key} is not an empty list: {data[key]!r}")
    mismatches = data["read_code_mismatches"]
    if not isinstance(mismatches, list) or len(mismatches) != 3:
        return fail(f"read_code_mismatches is not exactly three entries: {mismatches!r}")
    got = set()
    for item in mismatches:
        if not isinstance(item, dict) or set(item) != {"native", "actual", "expected"}:
            return fail(f"malformed read_code_mismatches entry: {item!r}")
        if not all(isinstance(item[k], str) for k in item):
            return fail(f"non-string read_code_mismatches entry: {item!r}")
        got.add((item["native"], item["actual"], item["expected"]))
    if got != EXPECTED:
        return fail(f"read-code transitions {sorted(got)}, want exactly {sorted(EXPECTED)}")
    print("PASS MUTANT-RESULT validation: exactly one complete, exact-transition record")
    return 0


if __name__ == "__main__":
    sys.exit(main())
