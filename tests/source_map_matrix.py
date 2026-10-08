import json,subprocess,sys
from pathlib import Path
probe=Path(sys.argv[1]).resolve()
root=probe.parent/(probe.stem+'-matrix')
(root/'cases').mkdir(parents=True,exist_ok=True)
contracts=json.loads((Path(__file__).resolve().parent/'source_map_contract.json').read_text())
cases=[]
def case(name,source,anchor,mode='script',column_stable=False,expected_file=None,anchor_occurrence=0):
 p=root/'cases'/(name+('.html' if mode=='template' else '.n'))
 p.write_text(source)
 original=(root/'cases'/expected_file).read_text() if expected_file else source
 at=original.index(anchor)
 for _ in range(anchor_occurrence):at=original.index(anchor,at+1)
 line=original.count('\n',0,at)+1;column=at-original.rfind('\n',0,at)
 cases.append(dict(name=name,mode=mode,file=p.as_posix(),source=source,expected_file=(root/'cases'/expected_file).as_posix() if expected_file else p.as_posix(),expected_line=line,anchor_column=column,column_stable=column_stable,anchor=anchor))
case('top-level','first := 1\n\nmissing_value\n','missing_value',column_stable=True)
case('top-level-tabs','first := 1\n\t\tmissing_value\n','missing_value',column_stable=True)
case('while','i := 0\nwhile(i < 1) {\n    missing_value\n    i += 1\n}\n','missing_value',column_stable=True)
case('for','first := 1\nfor(i : [1]) {\n    missing_value\n}\n','missing_value',column_stable=True)
case('nested-if','i := 0\nwhile(i < 1) {\n    if(true) {\n        missing_value\n    }\n    i += 1\n}\n','missing_value',column_stable=True)
case('function-loop','fn(fail_loop()) {\n    i := 0\n    while(i < 1) {\n        missing_value\n        i += 1\n    }\n}\nfail_loop()\n','missing_value',column_stable=True)
case('function-nested-loops','fn(fail_loop()) {\n    for(i : [1]) {\n        while(true) {\n            if(true) {\n                missing_value\n            }\n        }\n    }\n}\nfail_loop()\n','missing_value',column_stable=True)
case('prepared-callable','fn(bad()) {\n    return 1 / 0\n}\nfor(i : [1]) {\n    bad()\n}\n','1 / 0')
case('array-index','first := 1\nfor(i : [1]) {\n    a := [1]\n    a[9]\n}\n','a[9]')
case('object-member','first := 1\nfor(i : [1]) {\n    o := {"x": 1}\n    o.absent\n}\n','o.absent')
case('array-expression','first := 1\nfor(i : [1]) {\n    a := [1, missing_value]\n}\n','missing_value')
case('object-expression','first := 1\nfor(i : [1]) {\n    o := {"x": missing_value}\n}\n','missing_value')
case('multiline-args','first := 1\nfor(i : [1]) {\n    print(\n        missing_value\n    )\n}\n','missing_value')
case('multiline-array','first := 1\na := [\n    1,\n    missing_value\n]\n','missing_value')
case('multiline-object','first := 1\no := {\n    "x": missing_value\n}\n','missing_value')
case('multiline-arithmetic','first := 1\nx := (\n    1 + missing_value\n)\n','missing_value')
case('multiline-boolean','first := 1\nx := (\n    true && missing_value\n)\n','missing_value')
case('nested-calls','fn(id(x)) { return x }\nx := id(\n    id(missing_value)\n)\n','missing_value')
case('chained-index','first := 1\na := [{"x": [1]}]\na[0].x[9]\n','a[0].x[9]')
case('malformed','first := 1\nx := (1 + )\n','1 +')
case('invalid-callable','first := 1\nfor(i : [1]) {\n    absent_callable()\n}\n','absent_callable',column_stable=True)
case('invalid-collection','a := []\nfor(i : [1]) {\n    a.pop()\n}\n','a.pop()',column_stable=True)
case('legacy-fallback','first := 1\nfor(i : [1]) {\n    bytes([300])\n}\n','bytes([300])')
case('expression-lambda','first := 1\nbad := (x) => missing_value + x\nbad(1)\n','missing_value')
case('block-lambda','first := 1\nbad := (x) => {\n    return missing_value + x\n}\nbad(1)\n','return missing_value')
case('named-callable-value','fn(bad(x)) {\n    return 1 / 0\n}\nf := bad\nf(1)\n','return 1 / 0')
for operation,expr in [('map','[1].map(bad)'),('filter','[1].filter(bad)'),('reduce','[1].reduce(bad,0)'),('sort_by','[2,1].sort_by(bad)')]:
 params='a,x' if operation=='reduce' else 'x'
 case('callback-'+operation,f'fn(bad({params})) {{\n    return 1 / 0\n}}\n'+expr+'\n','return 1 / 0')
case('numeric-plan-fallback','first := 1\na := [1].map((x) => x / 0)\n','x / 0')
case('template-expression','first\nsecond\n$[1 / 0]\n','$[1 / 0]','template',True)
case('template-for','first\n@for(i : [1]) {\n    $[1 / 0]\n}\n','$[1 / 0]','template',True)
case('template-nested','first\n@for(i : [1]) {\n    @if(true) {\n        $[1 / 0]\n    }\n}\n','$[1 / 0]','template',True)
case('template-script','first\n@script {\n    missing_value\n}\n','missing_value','template',True)
(root/'cases'/'included.html').write_text('first\nsecond\n$[1 / 0]\n')
case('template-include','first\n@input("included.html")\n','$[1 / 0]','template',True,'included.html')
(root/'cases'/'module.n').write_text('first := 1\n\nmissing_value\n')
case('import-module','first := 1\nimport("module.n")\n','missing_value','script',True,'module.n')
case('recoverable','first := 1\nfor(i : [1]) {\n    ffi_open("./missing-library-cpf.so")\n}\n','ffi_open("./missing-library-cpf.so")')
case('explicit-throw','first := 1\nthrow error("sample", "user.sample")\n','throw')
(root/'cases'/'module_function.n').write_text('fn(module_bad()) {\n    return 1 / 0\n}\nexport(module_bad)\n')
case('imported-callable','import("module_function.n")\nfor(i : [1]) {\n    module_bad()\n}\n','1 / 0','script',False,'module_function.n')
case('same-lambda-text','a := (x) => missing_value + x\n\n\nb := (x) => missing_value + x\nb(1)\n','missing_value',anchor_occurrence=1)
case('try-catch','first := 1\ntry {\n    for(i : [1]) {\n        ffi_open("./missing-library-cpf.so")\n    }\n} catch(e) { print(e.code) }\n','ffi_open')
case('translation-malformed','first := 1\nwhile(true) {\n    missing_value\n','while')
case('template-multiline','first\n$[(\n    1 / 0\n)]\n','$[','template',True)
(root/'cases'/'outer.html').write_text('outer-first\n@content\n')
case('content-file','content-first\nsecond\n$[1 / 0]\n','$[','content',True)
results=[]
for c in cases:
 r=subprocess.run([str(probe),c['mode'],c['file']],capture_output=True,text=True)
 if r.returncode:raise SystemExit(r.stderr)
 c['actual']=json.loads(r.stdout);c['line_matches']=c['actual']['line']==c['expected_line'];c['path_matches']=c['actual']['path']==c['expected_file'];results.append(c)
 print(c['name'],f"expected {c['expected_line']}:{c['anchor_column']}",f"actual {c['actual']['line']}:{c['actual']['column']}",c['actual'].get('code'),c['actual']['message'])
def normalized_frames(frames):
 result=[]
 for frame in frames:
  frame=dict(frame)
  for key in ('path','label'):
   if frame[key].startswith('/') or (len(frame[key])>2 and frame[key][1]==':'):
    frame[key]=Path(frame[key]).name
  result.append(frame)
 return result

for c in results:
 a=c['actual'];contract=contracts[c['name']]
 for key in ('ok','stdout','message','code','disposition'):
  assert a.get(key)==contract.get(key),(c['name'],key,a,contract)
 assert normalized_frames(a.get('frames',[]))==contract.get('frames',[]),c
 if c['name']=='try-catch':continue
 original=Path(c['expected_file']).read_text()
 expected_excerpt=original.splitlines()[c['expected_line']-1]
 assert a['path']==a['diagnostic_path']==c['expected_file'],c
 assert a['line']==a['diagnostic_line']==c['expected_line'],c
 assert a['column']==a['diagnostic_column']==c['anchor_column'],c
 assert a['excerpt']==a['diagnostic_excerpt']==expected_excerpt,c
 assert a['source_length']==a['diagnostic_length'],c
 assert 1<=a['source_length']<=max(1,len(expected_excerpt)-c['anchor_column']+1),c
print('PASS 45 canonical source-map and frozen diagnostic contracts')
