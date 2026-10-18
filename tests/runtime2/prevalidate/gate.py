#!/usr/bin/env python3
"""Zero-false-rejection gate for syntax prevalidation (D-022 #3; permanent).

The prevalidator (CP13 T8) is the CP10 rt parser applied to a whole
compilation unit before any instruction runs, on both engines. It may reject
only invalid syntax. This gate runs it over every Nift script the project
knows and requires that every rejection is a program legacy also rejects at
that position (a real syntax error), never a valid program.

Corpora (each file is one unit; imports are validated as their own units,
"when loaded"):
  in-repo   tests/rt/legacy-cases, tests/rt/syntax-errors, tests/runtime2/
            {vm-corpus,semantics,rt-diff-corpus,diagnostics/scripts,
            diagnostics/changes}, docs/evidence/cp410-competitiveness/probes,
            tests/*.f, and the scripts extracted from the shell smoke tests
  projects  tests/runtime2/prevalidate/projects/ — scripts, imports and
            template @script bodies from real Nift projects, vendored at the
            commits pinned in projects.tsv (sha256-checked; no network)
  suite     --regression-suite DIR: scripts extracted from the
            nift-regression-suite checkout (optional, offline)

Validator modes:
  --mode check   (target) `NIFT_PREVALIDATE=only nift FILE`: exit 0 = valid,
                 exit 65 = rejected with one diagnostic; never executes
  --mode proxy   (design phase, default) NIFT_VM=classify: rejected iff the
                 CP10 parser fails (outcome reason `parse-error`); Opaque
                 `legacy-runtime-error` units are counted as PENDING (they are
                 the candidates the LegacyMalformed split must decide)

A rejection is accepted only if legacy also fails on the unit: for in-repo
files legacy is run once in a temporary copy (exit != 0, no stdout, error on
the reported line); project and suite units are never executed — any
rejection there fails the gate until it is classified by hand in
expected_rejections.tsv (file, evidence). Passing is evidence, not proof,
that every valid program is accepted.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
ROOT = os.path.dirname(ROOT)


def find_f(base):
    out = []
    for d, _, names in os.walk(base):
        out += [os.path.join(d, n) for n in names if n.endswith(('.f', '.nft'))]
    return sorted(out)


def extract(src, dst, recursive):
    dirs = sorted({d for d, _, ns in os.walk(src) for n in ns if n.endswith('.sh')}) if recursive else [src]
    for i, d in enumerate(dirs):
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools/rt-diff/extract_smoke.py'), '--tests', d,
                        '--out', os.path.join(dst, str(i))], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return find_f(dst)


def corpora(work, suite):
    c = {
        'legacy-cases': find_f(os.path.join(ROOT, 'tests/rt/legacy-cases')),
        'syntax-errors': find_f(os.path.join(ROOT, 'tests/rt/syntax-errors')),
        'vm-corpus': find_f(os.path.join(ROOT, 'tests/runtime2/vm-corpus')),
        'semantics': find_f(os.path.join(ROOT, 'tests/runtime2/semantics')),
        'rt-diff-corpus': find_f(os.path.join(ROOT, 'tests/runtime2/rt-diff-corpus')),
        'diagnostics': find_f(os.path.join(ROOT, 'tests/runtime2/diagnostics')),
        'cp410-probes': find_f(os.path.join(ROOT, 'docs/evidence/cp410-competitiveness/probes')),
        'tests-f': sorted(os.path.join(ROOT, 'tests', n) for n in os.listdir(os.path.join(ROOT, 'tests'))
                          if n.endswith('.f')),
        'smoke-extract': extract(os.path.join(ROOT, 'tests'), os.path.join(work, 'smoke'), False),
        'projects': [],
    }
    with open(os.path.join(HERE, 'projects.tsv')) as fh:
        for line in fh:
            if line.startswith('#') or not line.strip():
                continue
            repo, url, commit, origin, fixture, kind, sha = line.rstrip('\n').split('\t')
            path = os.path.join(HERE, 'projects', fixture)
            data = open(path, 'rb').read()
            if hashlib.sha256(data).hexdigest() != sha:
                sys.exit(f'fixture changed (sha256): {fixture}; re-extract from {url}@{commit}')
            c['projects'].append(path)
    if suite:
        c['regression-suite'] = extract(suite, os.path.join(work, 'suite'), True)
    return c


def validate(nift, path, mode, work):
    """Return (verdict, reason, line) with verdict accept|reject|pending."""
    env = dict(PATH=os.environ['PATH'], HOME=work, LANG='C', LC_ALL='C')
    cwd, name = os.path.dirname(path), os.path.basename(path)
    if mode == 'check':
        env['NIFT_PREVALIDATE'] = 'only'
        p = subprocess.run([nift, name], cwd=cwd, env=env, capture_output=True, timeout=20, stdin=subprocess.DEVNULL)
        if p.returncode == 0:
            return 'accept', '', 0
        m = re.search(rb':(\d+):\d+: ', p.stderr)
        return 'reject', p.stderr.decode(errors='replace').strip()[-120:], int(m.group(1)) if m else 0
    stats = os.path.join(work, 'stats.jsonl')
    if os.path.exists(stats):
        os.remove(stats)
    env.update(NIFT_VM='classify', NIFT_VM_STATS=stats)
    subprocess.run([nift, name], cwd=cwd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   timeout=20, stdin=subprocess.DEVNULL)
    try:
        rec = json.loads(open(stats).read().splitlines()[-1])
    except (OSError, ValueError, IndexError):
        return 'reject', 'no classification (crash?)', 0
    reason = rec.get('reason', '')
    if reason == 'parse-error':
        return 'reject', reason, rec.get('line', 0)
    if reason == 'opaque:legacy-runtime-error':
        return 'pending', reason, rec.get('line', 0)
    return 'accept', reason, 0


def legacy_also_fails(nift, path, line, work):
    d = tempfile.mkdtemp(dir=work)
    try:
        shutil.copytree(os.path.dirname(path), os.path.join(d, 'c'), dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('*.stdout', '*.stderr', '*.exit'))
        env = dict(PATH=os.environ['PATH'], HOME=d, TMPDIR=d, LANG='C', LC_ALL='C')
        p = subprocess.run([nift, os.path.basename(path)], cwd=os.path.join(d, 'c'), env=env,
                           capture_output=True, timeout=20, stdin=subprocess.DEVNULL)
        m = re.search(rb':(\d+):\d+: ', p.stderr)
        same_line = bool(m) and (line == 0 or int(m.group(1)) == line)
        return p.returncode != 0 and not p.stdout and same_line, p.stderr.decode(errors='replace').strip()[-120:]
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nift', default=os.path.join(ROOT, 'nift'))
    ap.add_argument('--legacy', help='legacy binary for the "legacy also fails" check (default: --nift)')
    ap.add_argument('--mode', choices=('proxy', 'check'), default='proxy')
    ap.add_argument('--regression-suite')
    ap.add_argument('--verbose', action='store_true')
    a = ap.parse_args()
    legacy = a.legacy or a.nift
    expected = {}
    exp_file = os.path.join(HERE, 'expected_rejections.tsv')
    if os.path.exists(exp_file):
        for line in open(exp_file):
            if not line.startswith('#') and line.strip():
                f, why = line.rstrip('\n').split('\t', 1)
                expected[f] = why
    work = tempfile.mkdtemp()
    false_rejections = 0
    try:
        print('corpus\tunits\taccepted\trejected(real)\trejected(FALSE)\tpending')
        for name, files in corpora(work, a.regression_suite).items():
            acc = real = false = pend = 0
            for f in files:
                verdict, reason, line = validate(a.nift, f, a.mode, work)
                if verdict == 'accept':
                    acc += 1
                elif verdict == 'pending':
                    pend += 1
                else:
                    rel = os.path.relpath(f, ROOT)
                    if rel in expected:
                        ok, why = True, expected[rel]
                    elif name in ('projects', 'regression-suite'):
                        ok, why = False, 'unclassified rejection of external code (never executed here)'
                    else:
                        ok, why = legacy_also_fails(legacy, f, line, work)
                    if ok:
                        real += 1
                    else:
                        false += 1
                        print(f'FALSE-REJECTION {rel}:{line} [{reason}] legacy: {why}')
                    if a.verbose and ok:
                        print(f'  real {rel}:{line} [{reason}]')
            false_rejections += false
            print(f'{name}\t{len(files)}\t{acc}\t{real}\t{false}\t{pend}')
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print(f'zero-false-rejection gate ({a.mode}): {"PASS" if false_rejections == 0 else "FAIL"} '
          f'({false_rejections} false rejections). Evidence, not proof, that every valid program is accepted.')
    return 1 if false_rejections else 0


if __name__ == '__main__':
    sys.exit(main())
