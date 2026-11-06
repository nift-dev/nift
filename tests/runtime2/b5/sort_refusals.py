#!/usr/bin/env python3
"""B5 D3 (owner 2026-11-07 06:55, decision 3): the plain `.sort()` refusals are a documented
precision limitation. Every program in docs/runtime-2/evidence/b5/sort-refusals.jsonl (the
gen_alias programs refused `collection-element:sort`: 452 sort-kind programs and the trim-kind
programs that also call an argument-less `.sort()` on the stored-into array) must be refused
before any observable effect: no stdout, no file created, changed or removed in its directory,
exit 1, an ordinary `error:` diagnostic naming the reason and saying nothing ran, no internal
failure. With --b4b NIFT it also reports how the B4b tip treated each program (a program the
B4b tip ADMITTED and B5 refuses would need the owner's release-blocker decision).

  sort_refusals.py --nift NIFT [--b4b NIFT]
"""
import argparse, collections, json, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, '..', '..', '..', 'docs', 'runtime-2', 'evidence', 'b5', 'sort-refusals.jsonl')
# method:sort: the program also has a comparator sort, refused first by the GCC 15.2 profile gate
# (b5-refsem.md, D2) on any other toolchain; on the certification toolchain it is collection-element:sort.
REASONS = ('collection-element:sort', 'method:sort')
INTERNAL = re.compile(r"compiler defect|internal failure|unexpected operands|\x1fnift:|cell reached")


def tree(d):
    out = {}
    for root, _, files in os.walk(d):
        for f in files:
            p = os.path.join(root, f)
            st = os.stat(p)
            out[os.path.relpath(p, d)] = (st.st_size, st.st_mtime_ns, open(p, 'rb').read())
    return out


def run(nift, src):
    with tempfile.TemporaryDirectory() as d:
        work = os.path.join(d, 'w'); os.mkdir(work)
        open(os.path.join(work, 't.f'), 'w').write(src)
        before = tree(work)
        stats = os.path.join(d, 's.jsonl')
        env = dict(PATH='/usr/bin:/bin', HOME=d, TMPDIR=d, LANG='C', LC_ALL='C', NIFT_VM='require',
                   NIFT_VM_STATS=stats)
        p = subprocess.run(['timeout', '30', nift, 't.f'], cwd=work, env=env, capture_output=True, text=True)
        after = tree(work)
        st = json.loads(open(stats).read().splitlines()[-1]) if os.path.exists(stats) else {}
        return p, before == after, st


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nift', required=True)
    ap.add_argument('--b4b')
    a = ap.parse_args()
    progs = [json.loads(l) for l in open(DATA)]
    bad, kinds, b4b, reasons = [], collections.Counter(), collections.Counter(), collections.Counter()
    for prog in progs:
        p, same, st = run(a.nift, prog['src'])
        kinds[prog['kind']] += 1
        reasons[st.get('reason')] += 1
        err = p.stderr
        ok = (p.returncode == 1 and p.stdout == '' and same and st.get('outcome') == 'require-error' and
              st.get('reason') in REASONS and err.startswith('error: ') and
              'not compiled for the VM (%s)' % st.get('reason') in err and 'no part of it ran' in err and
              not INTERNAL.search(err) and not st.get('dispatch_defects'))
        if not ok:
            bad.append((prog['i'], p.returncode, p.stdout[:40], same, st.get('outcome'), st.get('reason'), err[:120]))
        if a.b4b:
            q, _, s4 = run(a.b4b, prog['src'])
            b4b['admitted' if s4.get('outcome') in ('vm', 'vm-error') else 'refused:%s' % s4.get('reason')] += 1
    for b in bad[:20]:
        print('FAIL', *b)
    print('%s sort-refusals: %d programs (%s), reasons %s, %d not refused before effects' %
          ('PASS' if not bad else 'FAIL', len(progs), dict(kinds), dict(reasons), len(bad)))
    if a.b4b:
        print('B4b tip outcomes:', dict(b4b))
        if b4b.get('admitted'):
            print('NOTE: %d were admitted at the B4b tip: owner release-blocker decision required' % b4b['admitted'])
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
