from pathlib import Path
import json,subprocess,re,os
b=Path('.build/cp410-followup');p=b/'sort-probes';p.mkdir(exist_ok=True);phase=str((b/'phases/nift-phases').resolve());production=str((b/'accepted-nift').resolve());rows=[]
for n in [2000,4000,8000,16000]:
 for name in ['identity','captured','unused16','map','precreated']:
  prefix='offset := 1\n' if name=='captured' else ''.join(f'unused{k} := {k}\n' for k in range(16)) if name=='unused16' else ''
  selector='x => x+offset' if name=='captured' else 'x => x'
  setup=f'a := []\ni := 1\nwhile(i <= {n}) {{ a.push({n}-i+1); i += 1 }}\n'
  if name=='precreated':setup+='f := x => x\n';selector='f'
  expression=f'a.map({selector})' if name=='map' else f'a.sort_by({selector})'
  source=prefix+setup+'a = '+expression+f'\nprint(a[0]); print(a[{n}-1]); print(a[{n//2}-1])\n';folder=p/f'n{n:08d}'/f'{name:_<12}';folder.mkdir(parents=True,exist_ok=True);path=folder/'official-equivalent.f';path.write_text(source)
  expected=f'{n}\n1\n{n//2+1}\n' if name=='map' else f'1\n{n}\n{n//2}\n'
  q=subprocess.run([phase,str(path.resolve())],capture_output=True,text=True,check=True);assert q.stdout==expected,(name,n,q)
  phases={m[0]:dict(calls=int(m[1]),allocations=int(m[2]),bytes=int(m[3]),frees=int(m[4])) for m in re.findall(r'phase (\w+) calls=(\d+) allocations=(\d+) bytes=(\d+) frees=(\d+)',q.stderr)}
  events=re.search(r'events instances=(\d+) capture_entries=(\d+) frame_capture_entries=(\d+) comparisons=(\d+)',q.stderr);assert events and phases
  row=dict(name=name,n=n,expected=expected,path=str(path.resolve()),phases=phases,events=dict(zip(['instances','capture_entries','frame_capture_entries','comparisons'],map(int,events.groups()))));rows.append(row);(b/'sort-decomposition.json').write_text(json.dumps(rows,indent=2)+'\n');print('COUNTERS',name,n,row['events'],flush=True)
for row in rows:
 name,n,expected=row['name'],row['n'],row['expected'];base=b/'phases'/f'{name}-{n}';cg=str(base)+'.callgrind';xt=str(base)+'.xtree';log=str(base)+'.memcheck'
 q=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+cg,production,row['path']],capture_output=True,text=True,check=True,timeout=900);assert q.stdout==expected;row['production_instructions']=int(re.search(r'^summary: (\d+)',Path(cg).read_text(),re.M)[1])
 q=subprocess.run(['valgrind','--tool=memcheck','--error-exitcode=97','--leak-check=full','--xtree-memory=full','--xtree-memory-file='+xt,'--log-file='+log,production,row['path']],capture_output=True,text=True,check=True,timeout=900);assert q.stdout==expected;text=Path(log).read_text();assert 'ERROR SUMMARY: 0 errors' in text and 'in use at exit: 0 bytes in 0 blocks' in text,text[-1000:]
 m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',text);row['production_heap']=dict(zip(['allocations','frees','bytes'],[int(x.replace(',','')) for x in m.groups()]))
 q=subprocess.run(['callgrind_annotate','--inclusive=yes','--show=totFdBk','--threshold=100',xt],capture_output=True,text=True,check=True);Path(str(base)+'-frees.txt').write_text(q.stdout);dtor=[s for s in q.stdout.splitlines() if 'Parser::~Parser' in s];row['parser_destructor_frees_lines']=dtor
 if name=='identity' or n==2000:
  for key,entry in row['phases'].items():
   if not entry['calls'] or key=='destruction':continue
   pcg=str(base)+'-'+key+'.callgrind';q=subprocess.run(['valgrind','--tool=callgrind','--collect-atstart=no','--callgrind-out-file='+pcg,phase,row['path']],env={**os.environ,'CP51_PHASE':key},capture_output=True,text=True,check=True,timeout=900);assert q.stdout==expected;entry['instructions']=int(re.search(r'^summary: (\d+)',Path(pcg).read_text(),re.M)[1])
 (b/'sort-decomposition.json').write_text(json.dumps(rows,indent=2)+'\n');print('PROFILE',name,n,row['production_instructions'],row['production_heap'],dtor,flush=True)
print('PASS sort decomposition',flush=True)
