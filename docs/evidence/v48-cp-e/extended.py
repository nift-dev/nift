import subprocess,json
rows=[]
for mode in ['pointer','callback','retain','reuse','local','subset','alias']:
 for n in [1000,10000,100000,200000]:
  for size in [0,1,23,100,1024]:
   r=subprocess.run(['.build/cp-e/probe',str(n),str(size),mode],capture_output=True,text=True)
   rows.append(dict(mode=mode,n=n,size=size,returncode=r.returncode,output=r.stdout,error=r.stderr));open('.build/cp-e/extended-matrix.json','w').write(json.dumps(rows,indent=2))
   print(mode,n,size,r.returncode,flush=True)
   if r.returncode:raise SystemExit(r.stderr)
