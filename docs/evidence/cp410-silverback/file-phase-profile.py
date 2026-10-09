from pathlib import Path
import subprocess,json,re,os
p=Path(__file__).parent.resolve();b=p/'file-phases';fixtures=Path('.build/cp410-shell').resolve();rows=[]
for n in [1,1000,10000,100000]:
 root=fixtures/f'official-create-small-{n}';names=(root/'targets.txt').read_text().splitlines();code=(root/'source.f').read_text()
 for name in names:(root/name).unlink(missing_ok=True)
 r=subprocess.run([str(b/'nift'),'-e',code],cwd=root,text=True,capture_output=True,check=True);assert r.stdout=='OK\n';phases={name:{'calls':int(c),'allocations':int(a),'bytes':int(by)} for name,c,a,by in re.findall(r'phase (\w+) calls=(\d+) allocations=(\d+) bytes=(\d+)',r.stderr)};rows.append(dict(n=n,phases=phases));print(n,'PASS logical/allocator phases',flush=True)
(p/'file-phase-counts.json').write_text(json.dumps(rows,indent=2)+'\n')
root=fixtures/'official-create-small-1000';names=(root/'targets.txt').read_text().splitlines();code=(root/'source.f').read_text();phases={}
for phase in ['FileConstruction','Registry','CheckedPath','PathResolution','Cwd','Open','Write','Save','Comparison','ParentCheck','Permissions','TempName','Stream','AtomicReplace','SavedState','Close','FileTeardown']:
 for name in names:(root/name).unlink(missing_ok=True)
 cg=p/f'file-{phase}.callgrind';r=subprocess.run(['valgrind','--tool=callgrind','--collect-atstart=no','--callgrind-out-file='+str(cg),str(b/'nift'),'-e',code],cwd=root,text=True,capture_output=True,check=True,env={**os.environ,'CP51_PHASE':phase});assert r.stdout=='OK\n';phases[phase]=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]);(p/'file-phase-instructions.json').write_text(json.dumps(phases,indent=2)+'\n');print(phase,phases[phase],flush=True)
