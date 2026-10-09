from pathlib import Path
import subprocess,tempfile,json,hashlib
p=Path(__file__).parent.resolve();rows=[]
def hashes(root):
 return {x.relative_to(root).as_posix():hashlib.sha256(x.read_bytes()).hexdigest() for x in root.rglob('*') if x.is_file() and (x.relative_to(root).parts[0]=='public' or x.name in ['MIGRATION.md','REWRITES.md','REDESIGN.md','HANDOVER.md','AGENTS.md'])}
for mode in ['', '--migration','--rewrite','--redesign']:
 expected=None
 for label in ['accepted','file-recipes']:
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);binary=str(p.parent/'strings/prototype/nift' if label=='accepted' else p/'nift');commands=[['init']+([mode] if mode else []),['status'],['build','--all'],['build']]
   stages=[]
   for cmd in commands:
    r=subprocess.run([binary,*cmd],cwd=root,capture_output=True,text=True);assert r.returncode==0,(mode,label,cmd,r.stderr);stages.append(dict(command=cmd,files=hashes(root)))
   if expected is None:expected=stages
   assert stages==expected,(mode,label)
   rows.append(dict(mode=mode or 'default',label=label,stages=stages))
 print(mode or 'default','PASS',flush=True);(p/'real-build-parity.json').write_text(json.dumps(rows,indent=2)+'\n')
