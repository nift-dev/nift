#!/usr/bin/env python3
"""CP17 gate: no legacy `\\x1fnift:` handle strings in VM paths.

Scans C++ sources under the given roots (default: src/rt) and fails when code
(not comments) spells the legacy handle-string prefix in any escape form:
`\\x1fnift`, `\\x1Fnift`, `\\037nift`, `\\u001fnift`, or a raw 0x1F byte
followed by `nift`. Comments may mention the legacy encoding.

  no_handle_strings.py [--self-test] [roots...]
"""
import os
import re
import sys

PATTERN = re.compile(r'(\\x0*1[fF]|\\0?37|\\u001[fF]|\x1f)"?\s*"?nift')


def code_only(text):
    """Blank out comments; keep string and char literals (where handle strings live)."""
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            out.append('\n' * text.count('\n', i, n if j < 0 else j))
            i = n if j < 0 else j + 2
        elif text[i] in '"\'':
            quote, j = text[i], i + 1
            while j < n and text[j] != quote:
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return ''.join(out)


def scan(paths):
    problems = []
    for root in paths:
        files = [root] if os.path.isfile(root) else sorted(
            os.path.join(d, f) for d, _, fs in os.walk(root) for f in fs if f.endswith(('.h', '.cpp', '.inc')))
        for path in files:
            with open(path, encoding='utf-8', errors='surrogateescape') as handle:
                code = code_only(handle.read())
            for number, line in enumerate(code.split('\n'), 1):
                if PATTERN.search(line):
                    problems.append('%s:%d: legacy handle string in code' % (path, number))
    return problems


def self_test():
    bad = ['auto s = "\\x1fnift:file:0";', 'auto s = "\\x1Fnift:cmd:";', 'auto s = "\\037nift:x";',
           'auto s = "\\x1f" "nift:stream:";', 'auto s = "\\u001fnift:";']
    good = ['// legacy "\\x1fnift:" handles stay strings', '/* "\\x1fnift:page:" */ int x;', 'auto s = "nift:";']
    failures = [b for b in bad if not PATTERN.search(code_only(b))] + [g for g in good if PATTERN.search(code_only(g))]
    for f in failures:
        print('self-test FAIL: %s' % f)
    print('self-test: %d cases, %d failures' % (len(bad) + len(good), len(failures)))
    return 1 if failures else 0


def main(argv):
    if argv == ['--self-test']:
        return self_test()
    problems = scan(argv or ['src/rt'])
    for p in problems:
        print(p)
    print('no_handle_strings: %d problem(s)' % len(problems))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
