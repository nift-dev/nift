#!/usr/bin/env python3
"""CP13 prevalidation rules decided against legacy (D-022 #3).

For every row of rules.tsv: the statement is placed in a small program
(context top / fn / uncalled), the prevalidator (`rt-parse-dump prevalidate`)
must give the expected verdict and rule, and — with --legacy NIFT — legacy
must behave as recorded:
  fails          exit != 0 with the error on the statement's line
  silent         exit 0 but print("b") after the statement never runs
                 (legacy drops the rest of the unit: Assessment A #1 class)
  runtime-error  exit != 0 (a valid-syntax statement legacy rejects when run;
                 accepted by prevalidation, conservative)
  ok             exit 0 and print("b") runs (for `uncalled`, legacy never
                 evaluates the statement: rejecting it is the approved
                 "before anything runs" change, not a false rejection)
Usage: rules_test.py --tool .build/rt-parse-dump [--legacy ./nift]
"""
import argparse, os, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))

def program(context, stmt):
    head = 'fn(f(a)) { return a }\n'
    if context == 'top':
        return head + 'print("a")\n' + stmt + '\nprint("b")\n', 3
    if context == 'fn':
        return head + 'fn(h()) {\n' + stmt + '\nreturn 0\n}\nprint("a")\nh()\nprint("b")\n', 3
    if context == 'uncalled':
        return head + 'fn(never()) {\n' + stmt + '\nreturn 0\n}\nprint("a")\nprint("b")\n', 3
    if context == 'dead':
        return head + 'print("a")\nif(0 == 1) {\n' + stmt + '\n}\nprint("b")\n', 4
    if context == 'after-return':
        return head + 'fn(h()) {\nreturn 0\n' + stmt + '\n}\nprint("a")\nh()\nprint("b")\n', 4
    raise ValueError(context)

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--tool', required=True); ap.add_argument('--legacy'); ap.add_argument('--verbose', action='store_true')
    a = ap.parse_args()
    bad = n = 0
    parity = [0, 0]  # [same text as legacy, compared]
    with tempfile.TemporaryDirectory() as d:
        for line in open(os.path.join(HERE, 'rules.tsv')):
            if line.startswith('#') or not line.strip():
                continue
            verdict, rule, legacy, context, stmt = line.rstrip('\n').split('\t')
            n += 1
            text, stmt_line = program(context, stmt)
            path = os.path.join(d, 'p.f')
            open(path, 'w').write(text)
            out = subprocess.run([a.tool, 'prevalidate', path], capture_output=True, text=True).stdout.strip()
            problems = []
            if verdict == 'accept' and not out.startswith('ok '):
                problems.append('prevalidator: ' + out)
            if verdict == 'reject':
                if not out.startswith('reject ') or not out.endswith(' ' + rule):
                    problems.append('prevalidator: ' + out)
                elif f'p.f:{stmt_line}:' not in out and rule != 'deferred-translator':
                    problems.append('position not on the statement line: ' + out)
            if a.legacy:
                p = subprocess.run([a.legacy, 'p.f'], cwd=d, capture_output=True, text=True, timeout=20,
                                   env=dict(PATH=os.environ['PATH'], HOME=d, LANG='C'))
                ran_b = p.stdout.rstrip('\n').endswith('b')
                on_line = f'p.f:{stmt_line}:' in p.stderr
                ok = {'fails': p.returncode != 0 and on_line, 'silent': p.returncode == 0 and not ran_b,
                      'runtime-error': p.returncode != 0, 'ok': p.returncode == 0 and ran_b}[legacy]
                if verdict == 'reject' and legacy == 'fails':
                    import re as _re
                    want = _re.search(r'p\.f:(\d+:\d+: .*)', p.stderr, _re.S)
                    got = _re.search(r'p\.f:(\d+:\d+: .*) \[native', out, _re.S)
                    parity[1] += 1
                    if want and got and want.group(1).strip() == got.group(1).replace('\\n', '\n').strip():
                        parity[0] += 1
                    elif a.verbose:
                        print(f'  text differs [{context}] {stmt!r}: legacy {want.group(1).strip() if want else "?"!r} '
                              f'prevalidator {got.group(1) if got else out!r}')
                if not ok:
                    problems.append(f'legacy is not "{legacy}": exit {p.returncode}, stdout {p.stdout!r}, '
                                    f'stderr {p.stderr.strip()[-90:]!r}')
            if problems:
                bad += 1
                print(f'FAIL [{context}] {stmt!r}: ' + '; '.join(problems))
    print(f'prevalidation rules: {n} cases, {bad} failures' + ('' if a.legacy else ' (legacy not checked)'))
    if a.legacy:
        print(f'message/position identical to legacy\'s run-time error: {parity[0]}/{parity[1]} '
              '(observation; the verdict and rule are the test)')
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
