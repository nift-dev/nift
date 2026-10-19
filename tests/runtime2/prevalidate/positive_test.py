#!/usr/bin/env python3
"""CP13 positive syntax corpus: valid but unusual Nift programs the
prevalidator must accept (owner principle: the pre-check never rejects a
program because it does not know a construct; it defers to the parser and
legacy).

For every tests/runtime2/prevalidate/positive/NAME.f with NAME.expected
(line 1: `exit N`, then legacy's exact stdout):
  1. legacy (NIFT_PREVALIDATE=off) gives exactly that exit status and stdout
     (the program is what the corpus says it is);
  2. the prevalidator (`rt-parse-dump prevalidate`) accepts it;
  3. the prevalidating binary (default) behaves exactly as legacy (stdout,
     stderr, exit status).
Then the operator differential (operators.txt): every line in four contexts
(as written, one statement per line, a called function body, a loop body);
when legacy runs it without error the prevalidator must accept it, and the
prevalidating binary must behave exactly as legacy in every case.
Any failure fails the gate.

  positive_test.py --nift ./nift --tool .build/rt-parse-dump [--record]

--record writes NAME.expected from legacy (for new entries only; review the
recorded output by hand before committing it).
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CORPUS = os.path.join(HERE, 'positive')
OPERATORS = os.path.join(HERE, 'operators.txt')


def operator_programs():
    for line in open(OPERATORS, encoding='utf-8'):
        s = line.rstrip('\n')
        if not s or s.startswith('#'):
            continue
        lines = s.replace('; ', '\n')
        yield s, 'top', s + '\n'
        yield s, 'lines', lines + '\n'
        if not s.startswith('fn('):
            yield s, 'fn', 'fn(h()) {\n' + lines + '\nreturn 0\n}\nh()\n'
            yield s, 'loop', 'i := 0\nwhile (i < 1) { i += 1\n' + lines + '\n}\n'


def run(cmd, cwd, extra=None, timeout=60):
    env = {'PATH': os.environ.get('PATH', '/usr/bin:/bin'), 'LANG': 'C', 'LC_ALL': 'C', 'TZ': 'UTC', 'HOME': cwd,
           'TMPDIR': cwd}
    env.update(extra or {})
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, stdin=subprocess.DEVNULL, timeout=timeout)
    return p.stdout, p.stderr, p.returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nift', required=True)
    ap.add_argument('--tool', required=True)
    ap.add_argument('--record', action='store_true')
    a = ap.parse_args()
    nift, tool = os.path.abspath(a.nift), os.path.abspath(a.tool)
    cases = sorted(glob.glob(os.path.join(CORPUS, '*.f')))
    if not cases:
        raise SystemExit('positive_test: empty corpus ' + CORPUS)
    failures = []
    work = tempfile.mkdtemp(prefix='nift-positive-')
    try:
        for path in cases:
            name = os.path.basename(path)[:-2]
            d = os.path.join(work, name)
            os.mkdir(d)
            shutil.copy(path, os.path.join(d, 'p.f'))
            legacy = run([nift, 'p.f'], d, {'NIFT_PREVALIDATE': 'off'})
            expected_path = path[:-2] + '.expected'
            if a.record:
                if os.path.exists(expected_path):
                    raise SystemExit(f'positive_test: {name}.expected exists; --record writes new entries only')
                with open(expected_path, 'wb') as fh:
                    fh.write(b'exit %d\n' % legacy[2] + legacy[0])
                print(f'recorded {name}: exit {legacy[2]}')
                continue
            if not os.path.exists(expected_path):
                failures.append(f'{name}: no {name}.expected')
                continue
            with open(expected_path, 'rb') as fh:
                head, _, want_out = fh.read().partition(b'\n')
            want_rc = int(head.split()[1])
            if (legacy[0], legacy[2]) != (want_out, want_rc):
                failures.append(f'{name}: legacy differs from the corpus: exit {legacy[2]}, stdout {legacy[0][:120]!r}')
            out, err, rc = run([tool, 'prevalidate', 'p.f'], d)
            if rc != 0 or not out.startswith(b'ok'):
                failures.append(f'{name}: FALSE REJECTION by the prevalidator: {out.strip()[-200:]!r} {err[-200:]!r}')
            full = run([nift, 'p.f'], d)
            if full != legacy:
                failures.append(f'{name}: prevalidating binary differs from legacy: exit {full[2]}, '
                                f'stdout {full[0][:120]!r}, stderr {full[1][-200:]!r}')
        operators = 0
        if not a.record:
            for k, (text, ctx, prog) in enumerate(operator_programs()):
                operators += 1
                d = os.path.join(work, f'op{k}')
                os.mkdir(d)
                with open(os.path.join(d, 'p.f'), 'w', encoding='utf-8') as fh:
                    fh.write(prog)
                legacy = run([nift, 'p.f'], d, {'NIFT_PREVALIDATE': 'off'})
                out, err, rc = run([tool, 'prevalidate', 'p.f'], d)
                if rc not in (0, 1) or not (out.startswith(b"ok") or out.startswith(b"reject")):
                    failures.append(f'operators [{ctx}] {text!r}: prevalidate tool failed: {rc} {err[-200:]!r}')
                elif legacy[2] == 0 and not out.startswith(b'ok'):
                    failures.append(f'operators [{ctx}] {text!r}: FALSE REJECTION: {out.strip()[-200:]!r}')
                full = run([nift, 'p.f'], d)
                if full != legacy:
                    failures.append(f'operators [{ctx}] {text!r}: prevalidating binary differs from legacy: '
                                    f'exit {full[2]} vs {legacy[2]}, stderr {full[1][-160:]!r}')
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for f in failures:
        print('FAIL', f)
    if a.record:
        return 0
    print(f'positive syntax corpus: {len(cases)} programs, operator differential: {operators} programs; '
          f'{len(failures)} failure(s)')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
