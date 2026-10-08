"""Fresh local production profiles; no official workload or runtime edits."""
import pathlib,json,subprocess,re,resource,time,statistics,hashlib
b=pathlib.Path('.build/cp49-identity');b.mkdir(parents=True,exist_ok=True)
rows=json.load(open('.build/cp49/probes.json'))+json.load(open('.build/cp49-campaign/extra-probes.json'))+json.load(open('.build/cp49-wave2/probes.json'))
names=['map-identity','sort-identity','sort-arithmetic','sort-aggregate','call-scalar','call-closure','call-callback']
rows=[r for r in rows if r['name'] in names and r['n']==2000]
# Same sort input, with explicit pre-created and additional visible binding controls.
base=pathlib.Path(next(r['path'] for r in rows if r['name']=='sort-identity')).read_text()
for name,prelude,selector in [('sort-precreated','','f'),('sort-captured','offset := 1\n','x => x + offset'),('sort-unused-50','\n'.join(f'unused{j} := {j}' for j in range(50))+'\n','x => x')]:
 p=b/(name+'.f'); control=base.replace('probe_timer := timer(); probe_timer.start()', 'probe_timer := timer(); f := x => x; probe_timer.start()') if name=='sort-precreated' else base
 p.write_text(prelude+control.replace('sort_by(x => x)','sort_by('+selector+')'));rows.append(dict(name=name,n=2000,path=str(p.resolve()),expected='2000'))
(b/'probes.json').write_text(json.dumps(rows,indent=2)+'\n')
binary=str(pathlib.Path('nift').resolve());results=[]
for r in rows:
 def oracle(p):assert p.returncode==0,p.stderr;assert '\n'.join(p.stdout.splitlines()[:-1])==r['expected'],p.stdout
 path=b/(r['name']+'.callgrind');p=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(path),binary,r['path']],capture_output=True,text=True,timeout=300);oracle(p)
 ir=int(re.search(r'^summary: (\d+)',path.read_text(),re.M)[1])
 for inclusive in ['no','yes']:
  p=subprocess.run(['callgrind_annotate','--auto=no','--inclusive='+inclusive,str(path)],capture_output=True,text=True,check=True);(b/(r['name']+('-inclusive.txt' if inclusive=='yes' else '-self.txt'))).write_text(p.stdout)
 xtree=b/(r['name']+'.xtree');p=subprocess.run(['valgrind','--error-exitcode=97','--leak-check=full','--xtree-memory=full','--xtree-memory-file='+str(xtree),binary,r['path']],capture_output=True,text=True,timeout=300);oracle(p);assert 'ERROR SUMMARY: 0 errors' in p.stderr;(b/(r['name']+'.memcheck')).write_text(p.stderr)
 m=re.search(r'total heap usage: ([\d,]+) allocs, ([\d,]+) frees, ([\d,]+) bytes allocated',p.stderr);assert m
 v=dict(name=r['name'],instructions=ir,allocations=int(m[1].replace(',','')),frees=int(m[2].replace(',','')),bytes=int(m[3].replace(',','')),samples=[],rss_kib=[]);assert v['allocations']==v['frees']
 p=subprocess.run(['callgrind_annotate','--auto=no','--inclusive=yes','--show=totB,totBk','--sort=totB',str(xtree)],capture_output=True,text=True,check=True);(b/(r['name']+'-allocations.txt')).write_text(p.stdout)
 for rep in range(16):
  before=resource.getrusage(resource.RUSAGE_CHILDREN);t=time.perf_counter();p=subprocess.run([binary,r['path']],capture_output=True,text=True);wall=time.perf_counter()-t;after=resource.getrusage(resource.RUSAGE_CHILDREN);oracle(p)
  if rep:v['samples'].append(dict(cpu_ms=1000*(after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime),wall_ms=1000*wall,in_process_ms=float(p.stdout.splitlines()[-1])))
 for _ in range(5):
  p=subprocess.run(['/usr/bin/time','-f','%M',binary,r['path']],capture_output=True,text=True);oracle(p);v['rss_kib'].append(int(p.stderr.strip()))
 for field in ['cpu_ms','wall_ms','in_process_ms']:v['median_'+field]=statistics.median(x[field] for x in v['samples'])
 v['median_rss_kib']=statistics.median(v['rss_kib']);results.append(v);(b/'metrics.json').write_text(json.dumps(dict(source=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),binary_sha256=hashlib.sha256(pathlib.Path(binary).read_bytes()).hexdigest(),results=results),indent=2)+'\n');print(r['name'],ir,v['allocations'],flush=True)
