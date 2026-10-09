from pathlib import Path
import tempfile,subprocess,os,csv,json,hashlib,collections
p=Path(__file__).resolve().parent;accepted=p.parent/'strings/prototype/nift';observer=p/'prototype/nift';rows=[]
def summarize(file):

 if not file.exists():file.write_text('id,width,lookup_reads,member_assignments,source_copies,range_passes,insertions\n')
 values=list(csv.DictReader(file.open()));widths=collections.Counter();distribution=collections.Counter()
 for row in values:
  width=int(row['width']);reads=int(row['lookup_reads']);widths[width]+=1;distribution[(width,reads)]+=1
 return {'observed_value_instances':len(values),'maximum_width':max((int(r['width']) for r in values),default=0),'totals':{key:sum(int(r[key]) for r in values) for key in ['lookup_reads','member_assignments','source_copies','range_passes','insertions']},'width_histogram':dict(sorted(widths.items())),'width_read_distribution':[{'width':w,'reads':r,'objects':n} for (w,r),n in sorted(distribution.items())],'wide_repeated_read_objects':sum(n for (w,r),n in distribution.items() if w>=2000 and r>=2000)}
def run(label,args,root):
 expected=subprocess.run([str(accepted),*args],cwd=root,capture_output=True,text=True)
 file=p/(label+'.csv');env=os.environ.copy();env['NIFT_OBJECT_WORKLOAD']=str(file)
 actual=subprocess.run([str(observer),*args],cwd=root,env=env,capture_output=True,text=True)
 assert (actual.returncode,actual.stdout,actual.stderr)==(expected.returncode,expected.stdout,expected.stderr),(label,expected.stderr,actual.stderr)
 result=summarize(file);result.update(label=label,arguments=args,status=actual.returncode,stdout_sha256=hashlib.sha256(actual.stdout.encode()).hexdigest());rows.append(result);print(label,result['maximum_width'],result['totals'],flush=True);(p/'workloads.json').write_text(json.dumps(rows,indent=2)+'\n')
for name in ['official-json-transform','json-traverse-8000']:
 source=Path('.build/cp410/probes')/(name+'.f');run(name,[str(source.resolve())],Path.cwd())
def hashes(root):
 return {f.relative_to(root).as_posix():hashlib.sha256(f.read_bytes()).hexdigest() for f in root.rglob('*') if f.is_file() and (f.relative_to(root).parts[0]=='public' or f.name in ['MIGRATION.md','REWRITES.md','REDESIGN.md','HANDOVER.md','AGENTS.md'])}
for mode in ['', '--migration','--rewrite','--redesign']:
 with tempfile.TemporaryDirectory() as directory:
  root=Path(directory);name=mode[2:] or 'default'
  roots=[root/'accepted',root/'observer']
  for r in roots:r.mkdir()
  for number,command in enumerate([['init']+([mode] if mode else []),['status'],['build','--all'],['build']]):
   expected=subprocess.run([str(accepted),*command],cwd=roots[0],capture_output=True,text=True,check=True)
   file=p/(name+'-'+str(number)+'.csv');env=os.environ.copy();env['NIFT_OBJECT_WORKLOAD']=str(file)
   actual=subprocess.run([str(observer),*command],cwd=roots[1],env=env,capture_output=True,text=True,check=True)
   # CLI progress includes roots and timings; compare generated artifacts.
   assert hashes(roots[0])==hashes(roots[1]),(mode,command)
   result=summarize(file);result.update(label=name+'-'+str(number),arguments=command,status=0,artifact_hashes=hashes(roots[1]));rows.append(result);print(result['label'],result['maximum_width'],result['totals'],flush=True);(p/'workloads.json').write_text(json.dumps(rows,indent=2)+'\n')
  shapes=[]
  def walk(value):
   if isinstance(value,dict):shapes.append(len(value));[walk(v) for v in value.values()]
   elif isinstance(value,list):[walk(v) for v in value]
  for file in (roots[1]/'.nift').rglob('*.json'):
   try:walk(json.loads(file.read_text()))
   except (ValueError,UnicodeError):continue
  (p/(name+'-on-disk-json-shapes.json')).write_text(json.dumps({'note':'on-disk config/tracking shape; not RuntimeValue access counts','width_histogram':dict(sorted(collections.Counter(shapes).items()))},indent=2)+'\n')
print('COMPLETE actual probes and four real build/tracking lifecycles',flush=True)
