#!/usr/bin/env python3
"""Verify the vendored Bitcoin Core SHA-256 subset (feature-sha256).

Every `NIFT-VERBATIM-BEGIN <file> <a>-<b> sha256=<h>` block in
third_party/bitcoin-sha256/sha256_bitcoin.h must
  * hash to <h> (the block was not edited locally), and
  * be listed with the same file, range and hash in NIFT-PROVENANCE.md.
With --upstream DIR (a checkout of the pinned upstream commit) each block is
also compared byte-for-byte with lines <a>..<b> of DIR/<file>, and the
recorded upstream file hashes are checked.
"""
import argparse
import hashlib
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
VENDOR = ROOT / "third_party" / "bitcoin-sha256"
HEADER = VENDOR / "sha256_bitcoin.h"
PROVENANCE = VENDOR / "NIFT-PROVENANCE.md"
BEGIN = re.compile(r"^// NIFT-VERBATIM-BEGIN (\S+) (\d+)-(\d+) sha256=([0-9a-f]{64})$")
END = "// NIFT-VERBATIM-END"


def blocks(text):
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        m = BEGIN.match(lines[i])
        if m:
            j = i + 1
            while j < len(lines) and lines[j] != END:
                if BEGIN.match(lines[j]):
                    raise SystemExit(f"nested NIFT-VERBATIM-BEGIN at line {j + 1}")
                j += 1
            if j == len(lines):
                raise SystemExit(f"unterminated block at line {i + 1}")
            body = "\n".join(lines[i + 1:j]) + "\n"
            yield m.group(1), int(m.group(2)), int(m.group(3)), m.group(4), body
            i = j
        i += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--upstream", type=pathlib.Path, help="checkout of the pinned upstream commit")
    args = ap.parse_args()
    provenance = PROVENANCE.read_text(encoding="utf-8")
    failures = []
    found = list(blocks(HEADER.read_text(encoding="utf-8")))
    if len(found) != 5:
        failures.append(f"expected 5 verbatim blocks, found {len(found)}")
    for rel, a, b, recorded, body in found:
        actual = hashlib.sha256(body.encode()).hexdigest()
        if actual != recorded:
            failures.append(f"{rel} {a}-{b}: block content hash {actual} != recorded {recorded}")
        if body.count("\n") != b - a + 1:
            failures.append(f"{rel} {a}-{b}: block has {body.count(chr(10))} lines, range has {b - a + 1}")
        row = f"| `{rel}` | {a}-{b} | `{recorded}` |"
        if row not in provenance:
            failures.append(f"{rel} {a}-{b}: not recorded in NIFT-PROVENANCE.md as {row}")
        if args.upstream:
            upstream_lines = (args.upstream / rel).read_text(encoding="utf-8").split("\n")[a - 1:b]
            if "\n".join(upstream_lines) + "\n" != body:
                failures.append(f"{rel} {a}-{b}: differs from upstream {args.upstream / rel}")
    if args.upstream:
        for rel, digest in re.findall(r"^\| `(\S+)` \| `([0-9a-f]{64})` \|$", provenance, re.M):
            actual = hashlib.sha256((args.upstream / rel).read_bytes()).hexdigest()
            if actual != digest:
                failures.append(f"upstream {rel}: sha256 {actual} != recorded {digest}")
    copying = hashlib.sha256((VENDOR / "COPYING").read_bytes()).hexdigest()
    if f"`{copying}`" not in provenance:
        failures.append(f"COPYING hash {copying} not recorded in NIFT-PROVENANCE.md")
        if b"\r\n" in (VENDOR / "COPYING").read_bytes():
            failures.append("COPYING has CRLF line endings: the checkout converted them "
                            "(third_party/bitcoin-sha256/** must be -text in .gitattributes)")
    for failure in failures:
        print("FAIL", failure)
    if failures:
        return 1
    print(f"vendored sha256 subset: {len(found)} verbatim blocks verified"
          + (" against upstream" if args.upstream else " against recorded hashes"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
