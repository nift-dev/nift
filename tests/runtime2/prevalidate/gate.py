#!/usr/bin/env python3
"""Zero-false-rejection gate for syntax prevalidation (D-022 #3; permanent).

The prevalidator (src/rt/prevalidate.cpp) is the CP10 rt parser applied to a
whole compilation unit before anything runs, on both engines. It may reject
only invalid syntax, and every rejection must cite the grammar or lexical
rule it violates (translator statement rules, deferred-region translator
rules, R1-R5 in rules.tsv). Legacy's exit status is never the criterion: a
program that parses and then fails at run time is valid and must be accepted.

Corpora (each file is one unit; imports and package modules are units of
their own, validated "when loaded"):
  in-repo   tests/rt/legacy-cases, tests/rt/syntax-errors, tests/runtime2/
            {vm-corpus,semantics,rt-diff-corpus,diagnostics}, docs/evidence/
            cp410-competitiveness/probes, tests/*.f, and the scripts extracted
            from the shell smoke tests
  projects  tests/runtime2/prevalidate/projects/: scripts, imports, package
            modules and template @script bodies from real Nift projects and
            all 25 nift-packages, vendored at the commits pinned in
            projects.tsv / repos.tsv (sha256-checked; no network)
  suite     --regression-suite DIR: scripts extracted from a
            nift-regression-suite checkout (offline)

Every rejection is listed with its rule (--record FILE writes them all as
TSV). A rejection passes only if
  * it cites a rule (always true for the tool), and
  * either the rule is the translator's statement grammar (legacy rejects
    the unit before running too; CP10 reproduces message and position), or
    the unit is listed in expected_rejections.tsv with the rule and the
    reviewer's reading of the line (why it violates that rule).
Legacy's behaviour is recorded as an observation only.
Anything else is UNREVIEWED and fails the gate (the reviewer must either list
it with the violated rule or fix the validator). Passing is evidence, not
proof, that every valid program is accepted.

Validator modes:
  --mode tool    `rt-parse-dump prevalidate` (--tool .build/rt-parse-dump)
  --mode check   (after the CLI wiring) `NIFT_PREVALIDATE=only nift FILE`
  --mode proxy   (design phase) NIFT_VM=classify parse errors only
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
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))


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
            if hashlib.sha256(open(path, 'rb').read()).hexdigest() != sha:
                sys.exit(f'fixture changed (sha256): {fixture}; re-extract from {url}@{commit}')
            c['projects'].append(path)
    if suite:
        c['regression-suite'] = extract(suite, os.path.join(work, 'suite'), True)
    return c


def validate(nift, path, mode, work, tool):
    """(verdict accept|reject|pending, rule, message, line)."""
    env = dict(PATH=os.environ['PATH'], HOME=work, LANG='C', LC_ALL='C')
    cwd, name = os.path.dirname(path), os.path.basename(path)
    if mode == 'tool':
        p = subprocess.run([tool, 'prevalidate', path], capture_output=True, timeout=20)
        out = p.stdout.decode(errors='replace').strip()
        if out.startswith('ok '):
            return 'accept', '', '', 0
        m = re.search(r':(\d+):\d+: (.*) \[([a-z_.]+)\] (\S+)$', out)
        return ('reject', m.group(4), f'{m.group(2)} [{m.group(3)}]', int(m.group(1))) if m else \
            ('reject', '', out, 0)
    if mode == 'check':
        env['NIFT_PREVALIDATE'] = 'only'
        p = subprocess.run([nift, name], cwd=cwd, env=env, capture_output=True, timeout=20, stdin=subprocess.DEVNULL)
        if p.returncode == 0:
            return 'accept', '', '', 0
        # NIFT_PREVALIDATE=only prints `<file>:<l>:<c>: <message> [<code>] <rule>`
        # (integration: the CLI wiring reports the rule like the tool does).
        err = p.stderr.decode(errors='replace').strip()
        m = re.search(r':(\d+):\d+: (.*) \[([a-z_.]+)\] (\S+)$', err)
        if m:
            return 'reject', m.group(4), f'{m.group(2)} [{m.group(3)}]', int(m.group(1))
        m = re.search(r':(\d+):\d+: (.*)', err)
        return 'reject', 'cli', err[-120:], int(m.group(1)) if m else 0
    stats = os.path.join(work, 'stats.jsonl')
    if os.path.exists(stats):
        os.remove(stats)
    env.update(NIFT_VM='classify', NIFT_VM_STATS=stats)
    subprocess.run([nift, name], cwd=cwd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   timeout=20, stdin=subprocess.DEVNULL)
    try:
        rec = json.loads(open(stats).read().splitlines()[-1])
    except (OSError, ValueError, IndexError):
        return 'reject', '', 'no classification', 0
    reason = rec.get('reason', '')
    if reason == 'parse-error':
        return 'reject', 'translator', reason, rec.get('line', 0)
    if reason == 'opaque:legacy-runtime-error':
        return 'pending', '', reason, rec.get('line', 0)
    return 'accept', '', '', 0


def legacy_observation(nift, path, line, work):
    """What legacy does with the unit (an observation, never the criterion)."""
    d = tempfile.mkdtemp(dir=work)
    try:
        shutil.copytree(os.path.dirname(path), os.path.join(d, 'c'), dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('*.stdout', '*.stderr', '*.exit'))
        # The engine's own behaviour, not the CLI's prevalidation (observation only).
        env = dict(PATH=os.environ['PATH'], HOME=d, TMPDIR=d, LANG='C', LC_ALL='C', NIFT_PREVALIDATE='off')
        p = subprocess.run([nift, os.path.basename(path)], cwd=os.path.join(d, 'c'), env=env,
                           capture_output=True, timeout=20, stdin=subprocess.DEVNULL)
        m = re.search(rb':(\d+):\d+: ', p.stderr)
        if p.returncode == 0:
            return 'legacy-exit-0'
        if m and int(m.group(1)) == line:
            return 'legacy-fails-on-line'
        return 'legacy-fails-elsewhere'
    except subprocess.TimeoutExpired:
        return 'legacy-timeout'
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nift', default=os.path.join(ROOT, 'nift'), help='legacy binary (observations, proxy/check)')
    ap.add_argument('--mode', choices=('tool', 'proxy', 'check'))
    ap.add_argument('--tool', help='rt-parse-dump binary (mode tool)')
    ap.add_argument('--regression-suite')
    ap.add_argument('--record', help='write every rejection as TSV')
    a = ap.parse_args()
    a.mode = a.mode or ('tool' if a.tool else 'proxy')
    a.nift = os.path.abspath(a.nift)
    a.tool = os.path.abspath(a.tool) if a.tool else None
    if a.mode == 'tool' and not a.tool:
        sys.exit('--mode tool needs --tool .build/rt-parse-dump')
    reviewed = {}
    for line in open(os.path.join(HERE, 'expected_rejections.tsv')):
        if not line.startswith('#') and line.strip():
            f, rule, why = (line.rstrip('\n').split('\t') + ['', ''])[:3]
            reviewed[f] = (rule, why)
    work = tempfile.mkdtemp()
    unreviewed, records = 0, []
    try:
        print('corpus\tunits\taccepted\trejected\tunreviewed\tpending')
        for name, files in corpora(work, a.regression_suite).items():
            acc = rej = bad = pend = 0
            for f in files:
                verdict, rule, message, line = validate(a.nift, f, a.mode, work, a.tool)
                if verdict == 'accept':
                    acc += 1
                    continue
                if verdict == 'pending':
                    pend += 1
                    continue
                rej += 1
                rel = os.path.relpath(f, ROOT) if f.startswith(ROOT) else f'{name}/{os.path.basename(f)}'
                external = name in ('projects', 'regression-suite')
                observed = 'not-run (external code)' if external else legacy_observation(a.nift, f, line, work)
                if rel in reviewed and reviewed[rel][0] == rule:
                    status = 'reviewed: ' + reviewed[rel][1]
                elif not rule:
                    status = 'UNREVIEWED (no rule cited)'
                elif rule == 'translator':
                    # legacy's own statement grammar: CP10 reproduces the
                    # translator's message and position exactly (CP10 gate 1)
                    status = 'translator statement rule (legacy rejects before running too)'
                else:
                    status = 'UNREVIEWED'
                if status.startswith('UNREVIEWED'):
                    bad += 1
                    print(f'{status} {rel}:{line} {rule}: {message} ({observed})')
                try:
                    excerpt = open(f, errors='replace').read().split('\n')[line - 1].strip()[:80] if line else ''
                except IndexError:
                    excerpt = ''
                records.append((name, rel, str(line), rule, message, observed, status, excerpt.replace('\t', ' ')))
            unreviewed += bad
            print(f'{name}\t{len(files)}\t{acc}\t{rej}\t{bad}\t{pend}')
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if a.record:
        with open(a.record, 'w') as fh:
            fh.write('# corpus\tunit\tline\trule\tmessage\tlegacy observation (not a criterion)\tstatus\tline text\n')
            for r in records:
                fh.write('\t'.join(r) + '\n')
    print(f'zero-false-rejection gate ({a.mode}): {"PASS" if unreviewed == 0 else "FAIL"} '
          f'({len(records)} rejections, each citing a rule; {unreviewed} unreviewed). '
          'Evidence, not proof, that every valid program is accepted.')
    return 1 if unreviewed else 0


if __name__ == '__main__':
    sys.exit(main())
