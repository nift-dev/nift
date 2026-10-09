from pathlib import Path
import subprocess,json,re
p=Path(__file__).parent.resolve();root=Path('.build/cp410-shell/official-traverse-100000').resolve();rows=[]
code=(root/'source.f').read_text();expected=None
for label in ['baseline','traversal']:
 cg=p/(label+'-traversal.callgrind');r=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),str(p/(label+'-nift')),'-e',code],cwd=root,capture_output=True,text=True,check=True)
 if expected is None:expected=r.stdout
 assert r.stdout==expected
 log=p/(label+'-traversal.memcheck');r=subprocess.run(['valgrind','--tool=memcheck','--leak-check=full','--error-exitcode=99','--log-file='+str(log),str(p/(label+'-nift')),'-e',code],cwd=root,capture_output=True,text=True,check=True);assert r.stdout==expected;s=log.read_text();assert 'ERROR SUMMARY: 0 errors' in s and 'in use at exit: 0 bytes in 0 blocks' in s;m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',s)
 rows.append(dict(label=label,instructions=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1]),allocations=int(m[1].replace(',','')),bytes=int(m[3].replace(',','')),memcheck_errors=0));print(rows[-1],flush=True);(p/'traversal-profiles.json').write_text(json.dumps(rows,indent=2)+'\n')
