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
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
# B29 (combined-b3-b5.md section 78): exact exceptions, one code line each, pinned by path, line
# number (review of C83-C99, L-3, section 90) and the line's stripped text; the general rule is
# unchanged. An entry that matches no line is a problem
# (it cannot outlive its line or move to another one).
ALLOWED = {
    ('src/rt/render.cpp', 11, 'return std::string("\\x1fnift:callable:") + (function.is_lambda'):
        'callable tag, not a handle: `nift eval --json` of a function keeps legacy\'s tag string '
        '(rt::callable_marker, eval_result.cpp; b4-dispatch.md review L1, carried for an owner decision)',
}


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


def allowed(path, number, line):
    key = (os.path.relpath(os.path.abspath(path), REPO).replace(os.sep, '/'), number, line.strip())
    return key if key in ALLOWED else None


def scan(paths, used=None):
    problems = []
    used = set() if used is None else used
    for root in paths:
        files = [root] if os.path.isfile(root) else sorted(
            os.path.join(d, f) for d, _, fs in os.walk(root) for f in fs if f.endswith(('.h', '.cpp', '.inc')))
        for path in files:
            with open(path, encoding='utf-8', errors='surrogateescape') as handle:
                code = code_only(handle.read())
            for number, line in enumerate(code.split('\n'), 1):
                if PATTERN.search(line):
                    key = allowed(path, number, line)
                    if key:
                        used.add(key)
                    else:
                        problems.append('%s:%d: legacy handle string in code' % (path, number))
    return problems


def self_test():
    bad = ['auto s = "\\x1fnift:file:0";', 'auto s = "\\x1Fnift:cmd:";', 'auto s = "\\037nift:x";',
           'auto s = "\\x1f" "nift:stream:";', 'auto s = "\\u001fnift:";']
    good = ['// legacy "\\x1fnift:" handles stay strings', '/* "\\x1fnift:page:" */ int x;', 'auto s = "nift:";']
    failures = [b for b in bad if not PATTERN.search(code_only(b))] + [g for g in good if PATTERN.search(code_only(g))]
    # B29: the allowlist is exact: the pinned line elsewhere, or a changed line in its file, is flagged.
    key = next(iter(ALLOWED))
    if not allowed(os.path.join(REPO, key[0]), key[1], '    ' + key[2]):
        failures.append('allowlisted line not accepted: ' + key[0])
    if allowed(os.path.join(REPO, 'src/rt/vm.cpp'), key[1], key[2]):
        failures.append('allowlisted line accepted in another file')
    if allowed(os.path.join(REPO, key[0]), key[1], key[2].replace('callable:', 'file:')):
        failures.append('a changed line accepted in the allowlisted file')
    # L-3: the same line elsewhere in the allowlisted file (a duplicate) is flagged.
    if allowed(os.path.join(REPO, key[0]), key[1] + 7, key[2]):
        failures.append('a duplicate of the allowlisted line accepted at another line of its file')
    for f in failures:
        print('self-test FAIL: %s' % f)
    print('self-test: %d cases, %d failures' % (len(bad) + len(good) + 4, len(failures)))
    return 1 if failures else 0


def main(argv):
    if argv == ['--self-test']:
        return self_test()
    used = set()
    roots = argv or ['src/rt']
    problems = scan(roots, used)
    covered = [k for k in ALLOWED if any(os.path.abspath(os.path.join(REPO, k[0])).startswith(os.path.abspath(r))
                                         for r in roots)]
    problems += ['%s:%d: allowlist entry matches no line: %s' % k for k in covered if k not in used]
    for k in sorted(used):
        print('allowed %s:%d: %s (%s)' % (k[0], k[1], k[2], ALLOWED[k]))
    for p in problems:
        print(p)
    print('no_handle_strings: %d problem(s)' % len(problems))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
