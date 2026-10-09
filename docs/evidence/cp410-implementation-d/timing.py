from pathlib import Path
import subprocess,tempfile,json,resource,time,statistics,os,hashlib
p=Path(__file__).resolve().parent;source=Path('.build/cp410-shell/official-create-small-1000/source.f').read_text();rows=[];cpu=min(os.sched_getaffinity(0));os.sched_setaffinity(0,{cpu})
binaries={'accepted':p.parent/'strings/prototype/nift','file-recipes':p/'nift','native-atomic':Path('.build/cp410-shell/write-control-exact').resolve()}
payload=(('0123456789abcdef'*7)+'0123456789abcde\n').encode()
for repeat in range(6):
 for label in (list(binaries) if repeat%2==0 else list(reversed(binaries))):
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);(root/'files').mkdir();(root/'targets.txt').write_text(''.join('files/'+str(i)+'\n' for i in range(1000)));script=root/'source.f';script.write_text(source);rss=p/'rss.time'
   args=['atomic','1000'] if label=='native-atomic' else [str(script)]
   before=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.monotonic()
   q=subprocess.run(['/usr/bin/time','-f','%M','-o',str(rss),str(binaries[label]),*args],cwd=root,capture_output=True,text=True,check=True)
   wall=time.monotonic()-start;after=resource.getrusage(resource.RUSAGE_CHILDREN)
   assert q.stdout=='OK\n' and not q.stderr,(label,q.stdout,q.stderr)
   assert all((root/'files'/str(i)).read_bytes()==payload for i in range(1000))
   rows.append(dict(label=label,repeat=repeat,cpu=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,wall=wall,rss_kib=int(rss.read_text()),affinity=cpu,binary_sha256=hashlib.sha256(binaries[label].read_bytes()).hexdigest()));(p/'timing.json').write_text(json.dumps(rows,indent=2)+'\n')
 print('PASS balanced triple',repeat,flush=True)
summary={label:{key:statistics.median(row[key] for row in rows if row['label']==label) for key in ['cpu','wall','rss_kib']} for label in binaries};(p/'timing-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(summary,flush=True)
