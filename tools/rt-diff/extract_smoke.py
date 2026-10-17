#!/usr/bin/env python3
"""Extract standalone Nift scripts embedded in the shell smoke tests (CP08).

Finds quoted heredocs that write a `.f` file, e.g.

    cat > probe.f <<'NIFT'
    ...
    NIFT

and writes each body to OUT/<test-name>/<n>-<file>/<file>.f. Only quoted
heredocs are taken (their bodies reach the script byte for byte; unquoted ones
undergo shell expansion). Extracted scripts run out of their original test's
setup, so some fail deterministically; that still compares diagnostics.

Usage: extract_smoke.py --tests DIR --out DIR
"""
import argparse
import os
import re

START = re.compile(r"""cat\s*>\s*"?([^\s"<>]*?\.f)"?\s*<<-?\s*'([A-Za-z_]+)'\s*$""")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tests", required=True)
    parser.add_argument("--out", required=True)
    options = parser.parse_args()
    count = 0
    for name in sorted(os.listdir(options.tests)):
        if not name.endswith(".sh"):
            continue
        with open(os.path.join(options.tests, name), encoding="utf-8", errors="replace") as handle:
            lines = handle.read().split("\n")
        index = 0
        serial = 0
        while index < len(lines):
            match = START.search(lines[index])
            if not match:
                index += 1
                continue
            target, terminator = match.group(1), match.group(2)
            body = []
            index += 1
            while index < len(lines) and lines[index].strip() != terminator:
                body.append(lines[index])
                index += 1
            index += 1
            serial += 1
            base = os.path.basename(target)
            if "$" in base or not base.endswith(".f"):
                continue
            directory = os.path.join(options.out, name[:-3], "%03d-%s" % (serial, base[:-2]))
            os.makedirs(directory, exist_ok=True)
            with open(os.path.join(directory, base), "w", encoding="utf-8") as handle:
                handle.write("\n".join(body) + "\n")
            count += 1
    print("extracted %d scripts" % count)


if __name__ == "__main__":
    main()
