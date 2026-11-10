"""Pin the representative array/object/struct admissions and refusals of the program API.

B2 (b2-refrep.md): 'array-admitted' pinned the copy-on-write result of a
callee's push into a shared argument ('2\n1\n'). Its subject (the clone) is
deleted, and the Phase A guard has refused that program since before B2
(reference-guard:argument), so the case failed at 919b5669 too; it is
replaced by 'array-argument' (that refusal) and 'array-local' (an admitted
array mutation with the same output).

B5 (b5-refsem.md, D-NEXT-refsem-b): the guard and the `aliasing` refusal are
deleted. 'array-argument' runs: the callee's push reaches the caller's array
(plan section 3 Call arguments; Q3 row 1; legacy copies: 2/1). 'struct-alias'
runs (struct instances are references since Phase A; legacy-equal 2/2).
'object-alias' stays refused before any effect, now by its index-assignment
target (`index-target`, the same refusal as 'object-index'), which the deleted
`aliasing` check used to pre-empt. 'struct-copy' already ran on the B4b tip
17f7964c (struct instances are references; B3/B4 admit a field store through
a parameter; legacy-equal 2/2): its stale `struct-value` expectation failed
there identically and is corrected here.

Checkpoint 1 (combined-b3-b5.md sections 94-95): `o["x"] = v` compiles. 'object-index' and
'object-alias' run with the B5 result (one object; legacy copies the argument and the alias: 2/1)."""
import importlib.util
from pathlib import Path
import sys

root=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('cow_program',root/'bindings/python/nift/program.py')
api=importlib.util.module_from_spec(spec);sys.modules[spec.name]=api;spec.loader.exec_module(api)
cases=[
 ('array-argument','fn(g(a)) { a.push(1); return a.length(); } b := [5]; print(g(b)); print(b.length());','2\n2\n',None),
 ('array-local','fn(g(n)) { a := [n]; a.push(1); return a.length(); } b := [5]; print(g(5)); print(b.length());','2\n1\n',None),
 ('object-member','fn(g(o)) { o.x = 2; return o.x; } b := {"x":1}; print(g(b)); print(b.x);',None,'static-error:member-assign'),
 ('object-index','fn(g(o)) { o["x"] = 2; return o["x"]; } b := {"x":1}; print(g(b)); print(b["x"]);','2\n2\n',None),
 ('object-alias','b := {"x":1}; c := b; c["x"] = 2; print(c["x"]); print(b["x"]);','2\n2\n',None),
 ('struct-copy','struct (S) { x := 1; } fn(g(s)) { s.x = 2; return s.x; } b := S(); print(g(b)); print(b.x);','2\n2\n',None),
 ('struct-alias','struct (S) { x := 1; } b := S(); c := b; c.x = 2; print(c.x); print(b.x);','2\n2\n',None),
 ('struct-unique-method','struct (S) { x := 1; fn(set()) { x = 2; return x; } } b := S(); print(b.set()); print(b.x);','2\n2\n',None),
]
for name,source,output,refusal in cases:
    try:
        with api.Program(source,library=sys.argv[1]) as program:
            assert refusal is None,name
            for _ in range(3):
                result=program.run();assert result.ok and result.output==output.encode(),(name,result)
    except api.ProgramCompileError as error:
        assert refusal and error.result.outcome==2 and error.result.message==refusal,(name,error.result)
        assert error.result.output==b'',name
print('PASS array/object/struct admission witnesses:',len(cases))
