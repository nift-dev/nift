from pathlib import Path
import subprocess,re,json
p=Path('.build/cp410-implementation/traversal').resolve();fixture=Path('.build/cp410-shell/official-traverse-100000').resolve();rows=[]
for label,binary in [('accepted',Path('.build/cp410-implementation/snapshot-nift').resolve()),('shared',p/'nift')]:
 log=p/(label+'-memcheck.log');q=subprocess.run(['valgrind','--tool=memcheck','--leak-check=full','--errors-for-leak-kinds=all','--error-exitcode=81','--log-file='+str(log),str(binary),'-e','print(ls("files/**/*.dat").size())'],cwd=fixture,capture_output=True,text=True,check=True);assert q.stdout=='120000\n';s=log.read_text();m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',s);assert m and 'ERROR SUMMARY: 0 errors' in s and 'in use at exit: 0 bytes in 0 blocks' in s
 rows.append(dict(label=label,allocations=int(m[1].replace(',','')),frees=int(m[2].replace(',','')),allocated_bytes=int(m[3].replace(',',''))));(p/'resources.json').write_text(json.dumps(rows,indent=2)+'\n');print(rows[-1],flush=True)
 trace=p/(label+'-strace.log');q=subprocess.run(['strace','-c','-o',str(trace),'-e','trace=readlink,newfstatat,statx,getdents64,getcwd',str(binary),'-e','print(ls("files/**/*.dat").size())'],cwd=fixture,capture_output=True,text=True,check=True);assert q.stdout=='120000\n';print(trace.read_text(),flush=True)
