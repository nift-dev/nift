from pathlib import Path
import subprocess,json,re
p=Path(__file__).parent.resolve();root=Path('.build/cp410-shell/official-traverse-100000').resolve();rows=[]
for label,binary in [('current',Path('.build/cp410-wave3/string-nift').resolve()),('canonical-native',Path('.build/cp410-shell/native-optimized').resolve()),('prefix-cache',p/'native-prefix-cache')]:
 cmd=[str(binary),'-e','print(ls("files/**/*.dat").size())'] if label=='current' else [str(binary)]
 log=p/f'prefix-{label}.strace';r=subprocess.run(['strace','-c','-o',str(log),*cmd],cwd=root,capture_output=True,text=True,check=True);assert r.stdout=='120000\n';row=dict(label=label,stdout=r.stdout,syscall_summary=log.read_text())
 if label=='prefix-cache':
  cg=p/'prefix-cache-final.callgrind';r=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),*cmd],cwd=root,capture_output=True,text=True,check=True);assert r.stdout=='120000\n';row['instructions']=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]);log=p/'prefix-cache-final.memcheck';r=subprocess.run(['valgrind','--tool=memcheck','--leak-check=full','--error-exitcode=99','--log-file='+str(log),*cmd],cwd=root,capture_output=True,text=True,check=True);assert r.stdout=='120000\n';s=log.read_text();assert 'ERROR SUMMARY: 0 errors' in s and 'in use at exit: 0 bytes in 0 blocks' in s;m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',s);row.update(allocations=int(m[1].replace(',','')),bytes=int(m[3].replace(',','')),memcheck_errors=0)
 rows.append(row);(p/'prefix-cache-resources.json').write_text(json.dumps(rows,indent=2)+'\n');print(label,'PASS',flush=True)
