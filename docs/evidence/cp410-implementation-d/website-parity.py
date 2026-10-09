from pathlib import Path
import subprocess,json,hashlib,shutil
p=Path(__file__).resolve().parent;original=Path('/home/nick/Repositories/nift/nift-dev.github.io');rows=[]
def hashes(root):return {f.relative_to(root/'public').as_posix():hashlib.sha256(f.read_bytes()).hexdigest() for f in (root/'public').rglob('*') if f.is_file()}
for label,binary in [('accepted',p.parent/'strings/prototype/nift'),('file-ownership',p/'nift')]:
 root=p/('website-'+label)
 if root.exists():shutil.rmtree(root)
 shutil.copytree(original,root,ignore=shutil.ignore_patterns('.git'))
 q=subprocess.run([str(binary),'build','--all'],cwd=root,capture_output=True,text=True);(p/('website-'+label+'.log')).write_text(q.stdout+q.stderr);assert q.returncode==0,(label,q.returncode)
 files=hashes(root)
 if rows:assert files==rows[0]['public_hashes']
 rows.append(dict(label=label,public_hashes=files,status=q.returncode,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest()));print(label,len(files),'public artifacts',flush=True)
(p/'website-parity.json').write_text(json.dumps(rows,indent=2)+'\n')
