#!/usr/bin/env python3
"""Runtime 2.0 source lint for src/rt (CP09).

Rules
  function-length  no function body longer than 150 lines
  line-length      no line longer than 120 columns
  string-compare   hot-path heuristic: no comparison against a string literal
                   (`x == "lit"`, `"lit" != x`) and no strcmp/strncmp. Strings
                   must be resolved to atoms or enums before the hot path.
                   Allowed per line with `// rt-lint: allow-string-compare <why>`
                   or per file with `// rt-lint: cold-file <why>`.
  one-concern      every header states its single concern in a comment directly
                   after `#pragma once`; every .cpp includes its own header
                   first (one header/implementation pair per concern); no file
                   exceeds 400 lines.

Usage: rt-lint.py [paths...]   (default: src/rt)
       rt-lint.py --self-test  (each rule must fire on tests/rt/lint_fixtures)
"""
import os
import re
import sys

MAX_FUNCTION = 150
MAX_LINE = 120
MAX_FILE = 400
CONTROL = re.compile(r'\b(if|for|while|switch|catch|do|else|return|namespace|class|struct|union|enum)\b|=\s*$|\[[^\]]*\]\s*\(')
LITERAL_COMPARE = re.compile(r'(==|!=)\s*"|"\s*(==|!=)|\bstrn?cmp\s*\(')


def strip(line, in_comment):
    """Remove comments, string and char literal contents; return (code, in_comment)."""
    out, i = [], 0
    while i < len(line):
        if in_comment:
            end = line.find('*/', i)
            if end < 0:
                return ''.join(out), True
            i, in_comment = end + 2, False
        elif line.startswith('//', i):
            break
        elif line.startswith('/*', i):
            in_comment, i = True, i + 2
        elif line[i] in '"\'':
            quote, j = line[i], i + 1
            while j < len(line) and line[j] != quote:
                j += 2 if line[j] == '\\' else 1
            out.append(quote + quote)
            i = j + 1
        else:
            out.append(line[i])
            i += 1
    return ''.join(out), in_comment


def function_spans(lines):
    """Yield (start_line, length) for function bodies (blocks whose header looks
    like a function signature and whose parent is not itself a function)."""
    stack, header, in_comment = [], '', False
    for number, raw in enumerate(lines, 1):
        code, in_comment = strip(raw, in_comment)
        for ch in code:
            if ch == '{':
                head = header.strip()
                inside_function = any(kind == 'function' for kind, _ in stack)
                is_function = (not inside_function and '(' in head
                               and re.search(r'\)\s*(const|noexcept|override|final|\s)*(->\s*[\w:<>]+)?$', head)
                               and not CONTROL.search(head.split('(')[0]))
                stack.append(('function' if is_function else 'block', number))
                header = ''
            elif ch == '}':
                if stack:
                    kind, start = stack.pop()
                    if kind == 'function':
                        yield start, number - start + 1
                header = ''
            elif ch == ';':
                header = ''
            else:
                header += ch
        header += ' '


def lint_file(path, problems):
    with open(path, encoding='utf-8') as handle:
        text = handle.read()
    lines = text.split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    cold = 'rt-lint: cold-file' in text
    for number, line in enumerate(lines, 1):
        if len(line) > MAX_LINE:
            problems.append(f'{path}:{number}: line-length: {len(line)} > {MAX_LINE} columns')
    in_comment = False
    for number, line in enumerate(lines, 1):
        stripped, in_comment = strip(line, in_comment)
        quoted = re.sub(r'//.*', '', line) if not in_comment else ''
        if not cold and 'rt-lint: allow-string-compare' not in line and \
                LITERAL_COMPARE.search(quoted) and ('"' in stripped or 'cmp' in stripped):
            problems.append(f'{path}:{number}: string-compare: comparison against a string literal on a hot path')
    for start, length in function_spans(lines):
        if length > MAX_FUNCTION:
            problems.append(f'{path}:{start}: function-length: function body of {length} lines > {MAX_FUNCTION}')
    if len(lines) > MAX_FILE:
        problems.append(f'{path}:1: one-concern: {len(lines)} lines > {MAX_FILE}; split the file by concern')
    name = os.path.basename(path)
    if name.endswith('.h'):
        body = [l for l in lines if l.strip()]
        if len(body) < 2 or body[0].strip() != '#pragma once' or not body[1].startswith('//'):
            problems.append(f'{path}:1: one-concern: header must state its concern in a comment after #pragma once')
    elif name.endswith('.cpp'):
        includes = [l for l in lines if l.startswith('#include')]
        own = name[:-4] + '.h'
        header = os.path.join(os.path.dirname(path), own)
        if not os.path.exists(header) or not includes or not includes[0].rstrip().endswith(f'/{own}"'):
            problems.append(f'{path}:1: one-concern: must have {own} and include it first')


def lint(paths):
    problems = []
    for root in paths:
        files = [root] if os.path.isfile(root) else sorted(
            os.path.join(d, f) for d, _, fs in os.walk(root) for f in fs if f.endswith(('.h', '.cpp')))
        for path in files:
            lint_file(path, problems)
    return problems


def self_test():
    fixtures = os.path.join(os.path.dirname(__file__), '..', 'tests', 'rt', 'lint_fixtures')
    expected = {'bad_length.cpp': {'function-length', 'line-length'},
                'bad_compare.cpp': {'string-compare'},
                'bad_concern.h': {'one-concern'},
                'bad_concern.cpp': {'one-concern'},
                'good.cpp': set(), 'good.h': set()}
    failed = False
    for name, rules in sorted(expected.items()):
        found = {p.split(': ')[1] for p in lint([os.path.join(fixtures, name)])}
        if found != rules:
            print(f'FAIL rt-lint self-test {name}: expected {sorted(rules)}, got {sorted(found)}')
            failed = True
    print('FAIL rt-lint self-test' if failed else f'PASS rt-lint self-test: {len(expected)} fixtures')
    return 1 if failed else 0


def main(argv):
    if argv == ['--self-test']:
        return self_test()
    problems = lint(argv or ['src/rt'])
    for problem in problems:
        print(problem)
    print(f'{"FAIL" if problems else "PASS"} rt-lint: {len(problems)} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
