from pathlib import Path
import subprocess,json,re
p=Path('.build/cp410-capture-frame').resolve();binary=str(Path('.build/cp410-shell/baseline-nift').resolve());rows=[]
for x in json.loads(Path('docs/evidence/cp410-capture-frame/capture-counts.json').read_text()):
 stem=f'whole-{x["kind"]}-{x["n"]}-{x["unused"]}';cg=p/(stem+'.callgrind');cmd=[binary,x['source']];r=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(cg),*cmd],capture_output=True,text=True,check=True);assert r.stdout==f'1\n{x["n"]}\n'
 row={k:x[k] for k in ('n','unused','kind')};row['instructions']=int(re.search(r'^summary: (\d+)',cg.read_text(),re.M)[1])
 if x['n'] in (2000,16000) and x['unused'] in (0,100):
  log=p/(stem+'.memcheck');r=subprocess.run(['valgrind','--tool=memcheck','--leak-check=full','--error-exitcode=99','--log-file='+str(log),*cmd],capture_output=True,text=True,check=True);s=log.read_text();assert 'ERROR SUMMARY: 0 errors' in s and 'in use at exit: 0 bytes in 0 blocks' in s;m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',s);row.update(allocations=int(m[1].replace(',','')),bytes=int(m[3].replace(',','')),memcheck_errors=0)
 rows.append(row);Path('docs/evidence/cp410-capture-frame/whole-core.json').write_text(json.dumps(rows,indent=2)+'\n');print(row,flush=True)
print('COMPLETE 40 baseline whole-process instruction profiles, eight whole-process Memchecks')
