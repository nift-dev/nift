#!/usr/bin/env python3
"""CP13: apply the message-to-code table (codes.tsv) to the diagnostic corpus.

Reads each case's legacy stderr golden (baseline <case>.stderr, or
changes/<case>.old.stderr), extracts the diagnostic message, strips the
compatibility prefixes, maps it to a code with code-table.tsv and compares
with changes/CODES.tsv (`code`; `catchable` must be `no` for a case ending in
a fatal diagnostic: adding codes never changes catchability). Rows of cases
without a fatal diagnostic are maintained by hand and not checked.

Design-phase tool (D-022 #2): once `--format=json` exists, run.sh compares
the JSON `code`/`catchable` fields with these columns directly; this script
then only guards the table's coverage. Exit 1 on any unmapped message or
mismatch. Light: pure text processing, no binary is run.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PREFIX = re.compile(r'^(?:(?:return|import|@import|thread|future): |lambda body error: )+')
SCRIPT_LINE = re.compile(r'^error: (?:\$ROOT/\S+?|<command-line>):\d+:\d+: (?P<m>.*)$', re.S)
EVAL_LINE = re.compile(r'^eval: (?P<m>.*)$', re.S)


def table():
    rows = []
    for line in (HERE / 'code-table.tsv').read_text().splitlines():
        if not line or line.startswith('#'):
            continue
        code, new, pattern, *_ = line.split('\t') + ['']
        rows.append((code, new, re.compile(pattern)))
    return rows


def message_of(stderr: str):
    text = stderr.rstrip('\n')
    if not text:
        return None
    if text.startswith('error: while building'):
        lines = text.split('\n')
        return lines[2].strip() if len(lines) > 2 else None
    m = SCRIPT_LINE.match(text) or EVAL_LINE.match(text)
    return m.group('m') if m else None


def code_for(message, rows):
    stripped = PREFIX.sub('', message)
    for code, _new, pattern in rows:
        if pattern.fullmatch(stripped):
            return code
    return None


def thrown_code(case: str, stderr: str):
    """An uncaught `throw` reports the thrown Error's own message; its code is
    the Error's code (user.raised unless error() was given one)."""
    m = re.search(r'(?:\$ROOT/(\S+?)|^  (\S+?)):(\d+)(?::\d+)?', stderr, re.M)
    if not m:
        return None
    rel, line = m.group(1) or m.group(2), int(m.group(3))
    for root in (HERE / 'scripts', HERE / 'sites' / case, HERE / 'changes'):
        src = root / rel
        if src.exists():
            text = src.read_text().split('\n')
            if 0 < line <= len(text) and re.search(r'\bthrow\b', text[line - 1]):
                return 'user.raised'
    return None


def golden(case: str, ext: str) -> str:
    for p in (HERE / 'scripts' / f'{case}.{ext}', HERE / 'sites' / f'{case}.{ext}',
              HERE / 'changes' / f'{case}.old.{ext}'):
        if p.exists():
            return p.read_text()
    return ''


def main():
    write = '--write' in sys.argv
    rows = table()
    manifest = HERE / 'changes' / 'CODES.tsv'
    lines = manifest.read_text().splitlines()
    out, problems, body = [], 0, []
    for line in lines:
        if line.startswith('#'):
            out.append(line)
        else:
            body.append(line)
    for line in body:
        cols = line.split('\t')
        while len(cols) < 4:
            cols.append('')
        case, suite, code_col, catch_col = cols[:4]
        stderr = golden(case, 'stderr')
        message = message_of(stderr)
        if message is None:
            # No fatal diagnostic: the code/catchable columns are maintained
            # by hand (the code a catch observes, catchable=yes) and not checked.
            out.append('\t'.join(cols))
            continue
        code = code_for(message, rows) or thrown_code(case, stderr) or 'UNMAPPED'
        catchable = 'no'
        if write:
            cols[2], cols[3] = code, catchable
        elif (code, catchable) != (code_col, catch_col):
            print(f'MISMATCH {case}: table gives {code}/{catchable}, manifest {code_col}/{catch_col}')
            problems += 1
        if code == 'UNMAPPED':
            print(f'UNMAPPED {case}: {message!r}')
            problems += 1
        out.append('\t'.join(cols))
    if write:
        manifest.write_text('\n'.join(out) + '\n')
    new_codes = sorted({c for c, n, _ in rows if n == 'new'})
    print(f'{len(body)} cases, {problems} problems; new codes: {", ".join(new_codes)}')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
