#!/usr/bin/env python3
"""Extract standalone Nift scripts embedded in the shell smoke tests (CP08).

Finds quoted heredocs that write files, e.g.

    cat > "$T/scripts/probe.f" <<'NIFT'
    ...
    NIFT

Every quoted heredoc in a test (scripts, modules, data files) is replayed in
order into a virtual file tree (leading `$VAR/` components are dropped, so
`$T/scripts/module.f` becomes `scripts/module.f`). For each `.f` heredoc the
tree *as it stands at that point of the test* is written to
OUT/<test>/<n>-<file>/, with an `rt-diff.only` marker naming the script, so
imports and data files written earlier in the same test resolve. Only quoted
heredocs are taken (their bodies reach the file byte for byte). Scripts that
also depend on shell commands (mkdir, printf, generated data) still fail
deterministically; rt-diff compares those diagnostics too, and --stats reports
how many extracted scripts run to completion.

Usage: extract_smoke.py --tests DIR --out DIR
"""
import argparse
import os
import re

START = re.compile(r"""cat\s*>\s*"?([^\s"<>]+)"?\s*<<-?\s*'([A-Za-z_][A-Za-z_0-9]*)'\s*$""")


def relative_target(target):
    parts = [p for p in target.replace("\\", "/").split("/") if p not in ("", ".")]
    while parts and parts[0].startswith("$"):
        parts.pop(0)
    if not parts or any(p == ".." or "$" in p for p in parts):
        return None
    return "/".join(parts)


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
        files = {}
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
            rel = relative_target(target)
            if rel is None:
                continue
            files[rel] = "\n".join(body) + "\n"
            if not rel.endswith(".f"):
                continue
            serial += 1
            directory = os.path.join(options.out, name[:-3], "%03d-%s" % (serial, os.path.basename(rel)[:-2]))
            for path, content in files.items():
                full = os.path.join(directory, path)
                os.makedirs(os.path.dirname(full), exist_ok=True)
                with open(full, "w", encoding="utf-8") as out:
                    out.write(content)
            with open(os.path.join(directory, "rt-diff.only"), "w") as marker:
                marker.write(rel + "\n")
            count += 1
    print("extracted %d scripts" % count)


if __name__ == "__main__":
    main()
