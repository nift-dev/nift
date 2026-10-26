#!/usr/bin/env python3
"""Curated admission boundary, not a population-wide coverage estimate.

A = CP19 gap, B = explicit policy/invalid-input refusal, C = inherited debt.
The owner/reviewer can challenge the classification without changing raw runs.
"""
import argparse, json, os, subprocess, tempfile
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--nift', required=True)
p.add_argument('--base', required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
cases = [
    ('array-number', 'a:=[1]', 'a.push(2.5)', 'a.stringify()', '[1]\n', 'positive'),
    ('array-kind', 'a:=[1];f:=()=>a.stringify()', 'a.push("bad")', 'a.stringify()', '[1]\n', 'A'),
    ('nested-number', 'a:=[[1]]', 'for(v:a){v.push(2.5)}', 'a.stringify()', '[[1]]\n', 'positive'),
    ('nested-kind', 'a:=[[1]]', 'for(v:a){v.push("bad")}', 'a.stringify()', '[[1]]\n', 'A'),
    ('capture-number', 'fn(makec()){a:=[1];return v=>{a.push(v);return a.stringify()}};c:=makec();warm:=c(0)', 'c(2.5)', 'c(1)', '"[1,0,1]"\n', 'positive'),
    ('capture-kind', 'fn(makec()){a:=[1];return v=>{a.push(v);return a.stringify()}};c:=makec();warm:=c(0)', 'c("bad")', 'c(1)', '"[1,0,1]"\n', 'A'),
    ('lambda-fresh', 'f:=()=>1;old:=f', 'f = ()=>2', 'old()', '1\n', 'positive'),
    ('lambda-alias', 'f:=()=>1;g:=()=>2', 'f = g', 'f()', '1\n', 'A'),
    ('named-assignment', 'fn(f()){return 1}', 'f = ()=>2', 'f()', '1\n', 'B'),
    ('named-order', 'fn(f()){return 1}', 'fn(f()){return 2}', 'f()', '1\n', 'A'),
    ('named-call-value', 'fn(identv(v)){return v};a:=[1];warm:=identv(1)', 'identv(2)', 'identv(1)', '1\n', 'positive'),
    ('named-call-place', 'fn(identv(v)){return v};a:=[1];warm:=identv(1)', 'identv(a[0])', 'identv(1)', '1\n', 'positive'),
    ('wrapper-shadow-overreach', 'a:=[[1]];f:=()=>a.stringify();fn(g()){return 1};fn(makec()){a:=[[1]];return v=>{for(row:a){row.push(v)};return a.stringify()}};c:=makec();warm:=c(0)', 'fn(g()){return 3}', 'g()', '1\n', 'A'),
    ('replacement-loop-capture', 'fn(g()){return 1};fn(makec()){caparr:=[[1]];return v=>{for(row:caparr){row.push(v)};return caparr.stringify()}};c:=makec();warm:=c(0)', 'fn(g()){return 3}', 'g()', '1\n', 'A'),
    ('lambda-spelling', 'f:=v=>v+1', 'f=v=>v*2', 'f(3)', '4\n', 'B'),
    ('lexical-unrelated', 'x:=1', 'fn(localv()){x:=7;return x};localv()', 'x', '1\n', 'positive'),
    ('dynamic-named', 'x:=1;fn(readx()){return x}', 'fn(caller(x)){return readx()}', 'readx()', '1\n', 'B'),
    ('struct-live', 'struct(Point){v:=1;fn(get()){return v}};p:=Point()', 'p.v = 2', 'p.get()', '1\n', 'positive'),
    ('struct-capture', 'x:=5', 'struct(Point){x:=1};fn(makep()){p:=Point();return ()=>p.x};f:=makep()', 'x', '5\n', 'C'),
    ('struct-dynamic', 'v:=99', 'struct(Point){v:=1;fn(get()){return v}};p:=Point()', 'v', '99\n', 'B'),
    ('struct-explicit-field-shadow', 'v:=99', 'struct(Point){v:=1;fn(get()){return this.v}};p:=Point();print(p.get())', 'v', '99\n', 'A'),
    ('struct-replacement', 'struct(Point){v:=1};p:=Point()', 'struct(Point){v:=2}', '(p.v)', '1\n', 'A'),
    ('reserved-method', 'o:={"set":v=>v+1};x:=5', 'o.set(4)', 'x', '5\n', 'C'),
    ('builtin-bytes', 'x:=5', 'bytes([65])', 'x', '5\n', 'C'),
    ('prepared-loop', 'x:=0', 'while((x+=1)<3){print(x)}', 'x', '0\n', 'C'),
]
records = []
for name, seed, operation, check, preserved, classification in cases:
    probe = 'leaked:=999;print("FORBIDDEN");' + operation
    if name in ('wrapper-shadow-overreach','replacement-loop-capture'):
        probe = operation + ';leaked:=999;print("FORBIDDEN")'
    schema = ''
    if operation.startswith('struct('):
        depth = 0
        for i, char in enumerate(operation):
            if char == '{': depth += 1
            if char == '}':
                depth -= 1
                if depth == 0:
                    schema = operation[:i + 1]
                    operation = operation[i + 1:].lstrip(';')
                    break
        probe = schema + ';leaked:=999;print("FORBIDDEN");' + operation
    source = seed + '\n' + probe + '\n' + check + '\nexit\n'
    for lane, binary, mode in [('legacy', a.nift, '0'), ('mode1', a.nift, '1'),
                               ('require', a.nift, 'require'), ('base-unit', a.base, 'require')]:
        with tempfile.TemporaryDirectory(prefix='cp19-boundary-') as td:
            stats = Path(td) / 'stats'
            env = {k: v for k, v in os.environ.items() if not k.startswith('NIFT_')}
            env.update(HOME=td, NIFT_VM=mode, NIFT_VM_STATS=str(stats), NO_COLOR='1')
            # CP12b requires schemas before executable statements. Move only
            # that immutable declaration for the stateless counterfactual.
            unit = schema + ';' + seed + ';leaked:=999;print("FORBIDDEN");' + operation if schema else seed + ';' + probe
            if name == 'struct-capture': unit += ';print(f())'
            args = ['-c', unit] if lane == 'base-unit' else ['-i']
            r = subprocess.run([str(Path(binary).resolve()), *args], input=None if lane == 'base-unit'
                else source, env=env, cwd=td, capture_output=True, text=True, timeout=30)
            counters = [json.loads(s) for s in stats.read_text().splitlines()] if stats.exists() else []
            row = dict(case=name, classification=classification, lane=lane, seed=seed, probe=probe,
                check=check, argv=args, status=r.returncode, stdout=r.stdout, stderr=r.stderr, stats=counters)
            records.append(row)
            a.out.parent.mkdir(parents=True, exist_ok=True)
            a.out.write_text(json.dumps(records, indent=2) + '\n')
            if lane in ('mode1', 'require'):
                assert r.returncode == 0 and len(counters) == 3, row
                assert counters[0]['outcome'] == 'vm', row
                assert counters[2]['outcome'] == 'vm' and counters[2]['vm_executions'] == 1, row
                refused = counters[1]['outcome'] == 'require-error'
                if classification == 'positive':
                    assert not refused and counters[1]['outcome'] in ('vm', 'vm-error'), row
                else:
                    assert refused and counters[1]['vm_executions'] == 0 and counters[1]['legacy_executions'] == 0, row
                    assert r.stdout == preserved and 'FORBIDDEN' not in r.stdout, row
                assert all(s['legacy_executions'] == 0 for s in counters), row
print('PASS curated boundary:', len(cases), 'cases; legacy, both VM modes and base-unit counterfactuals')
