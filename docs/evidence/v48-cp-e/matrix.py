import subprocess,json,time
from pathlib import Path
root=Path(__file__).resolve().parent
rows=[]
for mode,counts,sizes,repeats in [('drop',[1000,10000,100000,200000],[0,1,23,100,1024],1),('drop',[1000000],[100],1),('retain',[1000,10000,100000,200000],[23,100],1),('reuse',[200000],[23,100,1024],1),('local',[10000,200000],[23,100],1),('subset',[10000,200000],[23,100],1),('alias',[10000,200000],[23,100],1),('drop',[10000],[100],5)]:
 for n in counts:
  for size in sizes:
   r=subprocess.run([str(root/'probe'),str(n),str(size),mode,str(repeats)],capture_output=True,text=True)
   row=dict(mode=mode,n=n,size=size,repeats=repeats,returncode=r.returncode,output=r.stdout,error=r.stderr);rows.append(row)
   (root/'matrix.json').write_text(json.dumps(rows,indent=2));print(r.stdout.strip(),r.stderr.strip(),flush=True)
   if r.returncode:raise SystemExit(r.returncode)
