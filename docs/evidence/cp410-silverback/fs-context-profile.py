from pathlib import Path
import subprocess,json,re,tempfile
p=Path(__file__).parent.resolve();old=Path('.build/cp410-shell').resolve();wave=Path('.build/cp410-wave3').resolve();rows=[];payload=b'x'*128
for op in ['metadata','move','copy']:
 source=(old/f'{op}-1000/source.f').read_text()
 for label,binary in [('current',wave/'string-nift'),('resolved-context',p/'fs-context-nift'),('native-floor',old/'secondary-control')]:
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory)
   for d in ['files','moved','copied']:(root/d).mkdir()
   names=[f'files/obj-{i:06}-target.dat' for i in range(1000)]
   for name in names:(root/name).write_bytes(payload)
   (root/'targets.txt').write_text('\n'.join(names)+'\n')
   cg=p/f'fs-{op}-{label}.callgrind';cmd=[str(binary),op] if label=='native-floor' else [str(binary),'-']
   r=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),*cmd],cwd=root,input=None if label=='native-floor' else source,capture_output=True,text=True,check=True)
   assert r.stdout==('128000\n' if op=='metadata' else 'OK\n'),(label,r)
   if op in ['move','copy']:
    dest='moved' if op=='move' else 'copied'
    assert all((root/dest/Path(name).name).read_bytes()==payload for name in names)
    assert all((root/name).exists()==(op=='copy') for name in names)
   rows.append(dict(operation=op,label=label,instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),stdout=r.stdout,n=1000,scope='controlled standalone success paths only; dispatch/source/error/argument-side-effect equivalence not certified'))
   (p/'fs-context-instructions.json').write_text(json.dumps(rows,indent=2)+'\n')
  print(op,label,'PASS',flush=True)
