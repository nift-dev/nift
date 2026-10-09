from pathlib import Path
import subprocess,tempfile,json,re,hashlib
p=Path(__file__).resolve().parent;source=Path('.build/cp410-shell/official-create-small-1000/source.f').read_text();rows=[]
for label,binary in [('accepted',p.parent/'strings/prototype/nift'),('file-recipes',p/'nift')]:
 with tempfile.TemporaryDirectory() as d:
  root=Path(d);(root/'out').mkdir();(root/'targets.txt').write_text(''.join('out/'+str(i)+'\n' for i in range(1000)));script=root/'source.f';script.write_text(source);cg=p/(label+'-save.callgrind');log=p/(label+'-save-callgrind.log')
  q=subprocess.run(['valgrind','--tool=callgrind','--log-file='+str(log),'--callgrind-out-file='+str(cg),str(binary),str(script)],cwd=root,capture_output=True,text=True,check=True);assert q.stdout=='OK\n' and not q.stderr,(q.stdout,q.stderr)
  files={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in (root/'out').iterdir()};assert len(files)==1000
  if rows:assert files==rows[0]['files']
  rows.append(dict(label=label,instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),files=files,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest()));print(label,rows[-1]['instructions'],flush=True);(p/'save-instructions.json').write_text(json.dumps(rows,indent=2)+'\n')
