"""Pin admitted COW behavior and unchanged representative object/struct refusals."""
import importlib.util
from pathlib import Path
import sys

root=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('cow_program',root/'bindings/python/nift/program.py')
api=importlib.util.module_from_spec(spec);sys.modules[spec.name]=api;spec.loader.exec_module(api)
cases=[
 ('array-admitted','fn(g(a)) { a.push(1); return a.length(); } b := [5]; print(g(b)); print(b.length());','2\n1\n',None),
 ('object-member','fn(g(o)) { o.x = 2; return o.x; } b := {"x":1}; print(g(b)); print(b.x);',None,'static-error:member-assign'),
 ('object-index','fn(g(o)) { o["x"] = 2; return o["x"]; } b := {"x":1}; print(g(b)); print(b["x"]);',None,'index-target'),
 ('object-alias','b := {"x":1}; c := b; c["x"] = 2; print(c["x"]); print(b["x"]);',None,'aliasing'),
 ('struct-copy','struct (S) { x := 1; } fn(g(s)) { s.x = 2; return s.x; } b := S(); print(g(b)); print(b.x);',None,'struct-value'),
 ('struct-alias','struct (S) { x := 1; } b := S(); c := b; c.x = 2; print(c.x); print(b.x);',None,'aliasing'),
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
print('PASS COW semantic/admission witnesses:',len(cases))
