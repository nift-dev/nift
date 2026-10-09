from pathlib import Path
import subprocess,json
p=Path('.build/cp410-capture-frame');binary=str(Path('.build/cp410-shell/baseline-nift').resolve());cases={
'injected-command-redeclaration':'f := x => { cmd := 7; return cmd }; print(f(0))',
'future-global-name':'f := x => late; late := 7; print(f(0))',
'nested-caller-name-snapshot':'f := x => (y => late); fn(mk()) { late := 11; return f(0) }; g := mk(); late := 22; print(g(0))',
'future-caller-name':'f := x => late; fn(call()) { late := 11; return f(0) }; print(call())',
'captured-name-over-caller-shadow':'v := 1; f := x => v; if(true) { v := 2; print(f(0)) }',
'live-captured-slot':'fn(mk()) { v := 1; f := x => v; v = 9; return f }; f := mk(); v := 100; print(f(0))',
'capture-declaration-membership':'v := 1; f := x => { v := 2; return v }; print(f(0))',
'parameter-replaces-capture':'x := 99; f := x => x; print(f(3)); print(x)',
'named-bare-precedence':'fn(v()) { return 8 }; v := 2; f := x => v; print(type(f(0)))',
'callable-binding-invocation-precedence':'fn(f()) { return 8 }; f := x => x+1; print(f(2))',
'unused-capture-transferability':'t := timer(); f := async x => x; r := f(3); print(await r)',
}
rows=[]
for name,code in cases.items():
 r=subprocess.run([binary,'-e',code],text=True,encoding='utf-8',capture_output=True);row={'name':name,'source':code,'returncode':r.returncode,'stdout':r.stdout,'stderr':r.stderr};rows.append(row);print(name,r.returncode,repr(r.stdout),repr(r.stderr))
Path('docs/evidence/cp410-capture-frame/lookup-oracles.json').write_text(json.dumps(rows,indent=2)+'\n')
