from pathlib import Path
import subprocess,json,re
p=Path(__file__).resolve().parent;source=json.loads(Path('.build/cp410-silverback/string-direct-sources.json').read_text())['utf8-large'];rows=[];expected=None
for label,binary in [('accepted',Path('.build/cp410-implementation/filesystem/nift').resolve()),('canonical-assignment',p/'prototype/nift'),('native-floor',p/'native-control')]:
 log=p/f'{label}-memcheck.log';q=subprocess.run(['valgrind','--tool=memcheck','--leak-check=full','--errors-for-leak-kinds=all','--error-exitcode=81','--log-file='+str(log),str(binary),*(['-'] if label!='native-floor' else [])],input=source if label!='native-floor' else None,text=True,capture_output=True,check=True)
 if expected is None:expected=q.stdout
 assert q.stdout==expected and not q.stderr,(label,q.stderr)
 raw=log.read_text();assert 'ERROR SUMMARY: 0 errors' in raw and 'in use at exit: 0 bytes in 0 blocks' in raw
 m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',raw);assert m
 rows.append(dict(label=label,allocations=int(m[1].replace(',','')),frees=int(m[2].replace(',','')),allocated_bytes=int(m[3].replace(',',''))));print(rows[-1],flush=True);(p/'resources.json').write_text(json.dumps(rows,indent=2)+'\n')
