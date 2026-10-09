from pathlib import Path
import subprocess,json,re,tempfile
p=Path(__file__).parent.resolve();old=Path('.build/cp410-shell').resolve();rows=[];payload=('0123456789abcdef'*7+'0123456789abcde\n').encode()
for mode in ['direct','atomic']:
 with tempfile.TemporaryDirectory() as directory:
  root=Path(directory);(root/'files').mkdir();names=[f'files/obj-{i:06}-target.dat' for i in range(1000)];(root/'targets.txt').write_text('\n'.join(names)+'\n');cg=p/f'save-{mode}-native.callgrind';r=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),str(old/'write-control-exact'),mode,'1000'],cwd=root,capture_output=True,text=True,check=True);assert r.stdout=='OK\n';assert all((root/name).read_bytes()==payload for name in names);rows.append(dict(mode=mode,n=1000,instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),scope='fresh-file successful write floor; atomic includes write/close/rename but omits Nift registry/revert/permission/failure contracts'));print(rows[-1],flush=True)
(p/'save-control-instructions.json').write_text(json.dumps(rows,indent=2)+'\n')
# Current concat/string paired instruction floors; immutable old native binary.
for op in ['concat','string']:
 source=(old/f'{op}-1000/source.f').read_text()
 for label,binary in [('current',Path('.build/cp410-wave3/string-nift').resolve()),('native-floor',old/'secondary-control')]:
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);(root/'files').mkdir();names=[f'files/obj-{i:06}-target.dat' for i in range(1000)]
   for name in names:(root/name).write_bytes(payload)
   (root/'targets.txt').write_text('\n'.join(names)+'\n');cg=p/f'fs-{op}-{label}.callgrind';cmd=[str(binary),op] if label=='native-floor' else [str(binary),'-'];r=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),*cmd],cwd=root,input=None if label=='native-floor' else source,capture_output=True,text=True,check=True);assert r.stdout==('aLPHa\n' if op=='string' else 'OK\n')
   if op=='concat':assert (root/'output').read_bytes()==payload*1000
   rows.append(dict(operation=op,label=label,n=1000,instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),scope='controlled successful fixture, lower bound omits general error/ownership/value contracts'));(p/'additional-floor-instructions.json').write_text(json.dumps(rows,indent=2)+'\n');print(op,label,'PASS',flush=True)
