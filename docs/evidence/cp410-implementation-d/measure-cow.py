from pathlib import Path
import tempfile,subprocess,json,re,hashlib
p=Path(__file__).resolve().parent
bins={'accepted':p.parent/'strings/prototype/nift','cow':p/'nift'}
rows=[]
for size,count in [(1,1000),(128,1000),(1048576,32)]:
 source='i:=0;while(i<'+str(count)+'){f:=file("out"+i.stringify());f.open("w");f.write("'+('x'*size)+'");f.save();f.close();i+=1};print("OK")'
 for label,binary in bins.items():
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);script=root/'source.f';script.write_text(source);log=p/f'cow-{size}-{label}-memcheck.log'
   q=subprocess.run(['valgrind','--tool=memcheck','--leak-check=full','--error-exitcode=81','--log-file='+str(log),str(binary),str(script)],cwd=root,capture_output=True,text=True,check=True)
   assert q.stdout=='OK\n' and not q.stderr,(q.stdout,q.stderr)
   assert all((root/('out'+str(i))).read_bytes()==b'x'*size for i in range(count))
   raw=log.read_text();assert 'ERROR SUMMARY: 0 errors' in raw and 'in use at exit: 0 bytes in 0 blocks' in raw
   m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',raw)
   rows.append(dict(size=size,count=count,label=label,allocations=int(m[1].replace(',','')),allocated_bytes=int(m[3].replace(',','')),binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest()))
   print(rows[-1],flush=True);(p/'cow-resources.json').write_text(json.dumps(rows,indent=2)+'\n')
