#!/usr/bin/env python3
"""CP13 JSON diagnostics checks (D-022 #1; docs/runtime-2/diagnostics-schema.md).

Always: every json/*.expected.jsonl record validates against
docs/runtime-2/diagnostics-schema-v1.json.

With --binary NIFT: runs every json/ case with NIFT_FORMAT=json (and once with
--format=json for scripts) and checks
  * stderr is JSON Lines only: every line parses, validates against the schema,
    and contains no terminal escape (no colour, no other formatting);
  * the exit status is the expected one (unchanged from text mode);
  * stdout equals <case>.expected.stdout when present (stdout is unchanged);
  * the records equal the expected ones as a multiset (page order in a build
    is not specified), ignoring context.engine; when a record reports
    frames_complete=false (legacy evaluator) its frames are compared by
    (kind, name) only and frames_complete itself is not compared;
  * `text` equals the text-mode rendering byte for byte.
Paths: the case copy's real path is replaced by $ROOT, as in run.sh.
"""
import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
SCHEMA = os.path.join(ROOT, 'docs/runtime-2/diagnostics-schema-v1.json')
CASES = os.path.join(HERE, 'json')


def validator():
    try:
        import jsonschema
    except ImportError:  # CI images without python3-jsonschema: parse-only
        print('note: python jsonschema not installed; records are only parsed, not schema-validated')

        class ParseOnly:
            def iter_errors(self, _):
                return []
        return ParseOnly()
    return jsonschema.Draft202012Validator(json.load(open(SCHEMA)))


def normal(rec, actual_complete=True):
    rec = json.loads(json.dumps(rec))
    if 'context' in rec:
        rec['context'].pop('engine', None)
    if not actual_complete and 'frames' in rec:
        rec['frames'] = [{'kind': f['kind'], 'name': f['name']} for f in rec['frames']]
        rec.pop('frames_complete', None)
    return json.dumps(rec, sort_keys=True)


def run_case(nift, path, flag, work):
    case = os.path.join(work, 'case')
    shutil.rmtree(case, ignore_errors=True)
    env = dict(PATH=os.environ['PATH'], HOME=work, TMPDIR=work, LANG='C', LC_ALL='C', TZ='UTC', NIFT_FORMAT='json')
    if os.path.isdir(path):  # site
        os.makedirs(case)
        init = subprocess.run([nift, 'init'], cwd=case, env=dict(env, NIFT_FORMAT='text'), capture_output=True)
        if init.returncode != 0:
            raise SystemExit(f'json_check: nift init failed for {path}: {init.stderr.decode(errors="replace")[-300:]}')
        shutil.copytree(path, case, dirs_exist_ok=True)
        argv = [nift, 'build', '--all']
    else:
        shutil.copytree(CASES, case, ignore=shutil.ignore_patterns('*.expected.*', 'sites'))
        name = os.path.basename(path)
        argv = [nift] + (open(path).read().splitlines() if name.endswith('.args') else [name])
        if flag:
            env.pop('NIFT_FORMAT')
            argv.insert(1, '--format=json')
    p = subprocess.run(argv, cwd=case, env=env, capture_output=True, timeout=30, stdin=subprocess.DEVNULL)
    real = os.path.realpath(case)
    fix = lambda b: b.decode(errors='replace').replace(real, '$ROOT').replace(case, '$ROOT')
    return p.returncode, fix(p.stdout), fix(p.stderr)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--binary')
    a = ap.parse_args()
    v = validator()
    problems = 0
    expected_files = sorted(glob.glob(os.path.join(CASES, '**', '*.expected.jsonl'), recursive=True))
    for f in expected_files:
        for i, line in enumerate(open(f), 1):
            for err in v.iter_errors(json.loads(line)):
                print(f'SCHEMA {os.path.relpath(f, HERE)}:{i}: {err.message}')
                problems += 1
    print(f'{len(expected_files)} expected files validated against schema 1')
    if a.binary:
        work = tempfile.mkdtemp()
        try:
            for exp in expected_files:
                base = exp[:-len('.expected.jsonl')]
                src = base if os.path.isdir(base) else (base + '.f' if os.path.exists(base + '.f') else base + '.args')
                for flag in ((False, True) if src.endswith('.f') else (False,)):
                    code, out, err = run_case(os.path.abspath(a.binary), src, flag, work)
                    label = os.path.relpath(base, CASES) + (' (--format=json)' if flag else '')
                    bad = []
                    recs = []
                    for line in err.splitlines():
                        if '\x1b' in line:
                            bad.append('terminal escape in stderr')
                        try:
                            r = json.loads(line)
                        except ValueError:
                            bad.append(f'non-JSON stderr line: {line[:80]!r}')
                            continue
                        bad += [f'schema: {e.message}' for e in v.iter_errors(r)]
                        recs.append(r)
                    want = [json.loads(l) for l in open(exp)]
                    if str(code) != open(base + '.expected.exit').read().strip():
                        bad.append(f'exit {code}')
                    if os.path.exists(base + '.expected.stdout') and out != open(base + '.expected.stdout').read():
                        bad.append('stdout differs')
                    got = sorted(normal(r, r.get('frames_complete', True)) for r in recs)
                    exp_n = sorted(normal(w, next((r.get('frames_complete', True) for r in recs
                                                   if r.get('code') == w.get('code')), True)) for w in want)
                    if got != exp_n:
                        bad.append('records differ')
                    print(('FAIL ' if bad else 'PASS ') + label + (': ' + '; '.join(bad[:4]) if bad else ''))
                    problems += bool(bad)
        finally:
            shutil.rmtree(work, ignore_errors=True)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
