from pathlib import Path
import subprocess,tempfile,json,re,hashlib
p=Path(__file__).resolve().parent;rows=[]
for size,count in [(1,1000),(128,1000),(1048576,32)]:
 for label,binary in [('accepted',p.parent/'strings/prototype/nift'),('file-recipes',p/'nift')]:
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);(root/'payload').write_bytes(b'x'*size);source='payload:=open("payload");i:=0;while(i<'+str(count)+'){f:=file("out"+i.stringify());f.open("w");f.write(payload);f.save();f.close();i+=1};print("OK")';script=root/'source.f';script.write_text(source)
   log=p/f'resource-{size}-{label}-memcheck.log';q=subprocess.run(['valgrind','--tool=memcheck','--leak-check=full','--errors-for-leak-kinds=all','--error-exitcode=81','--log-file='+str(log),str(binary),str(script)],cwd=root,capture_output=True,text=True,check=True)
   assert q.stdout=='OK\n' and not q.stderr,(label,q.stdout,q.stderr);assert all((root/('out'+str(i))).read_bytes()==b'x'*size for i in range(count))
   raw=log.read_text();assert 'ERROR SUMMARY: 0 errors' in raw and 'in use at exit: 0 bytes in 0 blocks' in raw
   match=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',raw)
   row=dict(label=label,size=size,count=count,allocations=int(match[1].replace(',','')),allocated_bytes=int(match[3].replace(',','')),binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest());rows.append(row);print(row,flush=True);(p/'resources.json').write_text(json.dumps(rows,indent=2)+'\n')
