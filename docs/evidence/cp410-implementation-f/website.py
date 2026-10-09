from pathlib import Path
import subprocess,os,json,hashlib,shutil,csv,collections
p=Path(__file__).resolve().parent;original=Path('/home/nick/Repositories/nift/nift-dev.github.io');rows=[]
def hashes(root):return {f.relative_to(root/'public').as_posix():hashlib.sha256(f.read_bytes()).hexdigest() for f in (root/'public').rglob('*') if f.is_file()}
for label,binary in [('accepted',p.parent/'strings/prototype/nift'),('observer',p/'prototype/nift')]:
 root=p/('website-'+label)
 if root.exists():shutil.rmtree(root)
 shutil.copytree(original,root,ignore=shutil.ignore_patterns('.git'))
 env=os.environ.copy();env['NIFT_OBJECT_WORKLOAD']=str(p/('website-'+label+'.csv'))
 q=subprocess.run([str(binary),'build','--all'],cwd=root,env=env,capture_output=True,text=True);(p/('website-'+label+'.log')).write_text(q.stdout+q.stderr);assert q.returncode==0,(label,q.returncode)
 files=hashes(root)
 if rows:assert files==rows[0]['public_hashes']
 row=dict(label=label,public_hashes=files,status=q.returncode)
 if label=='observer':
  values=list(csv.DictReader((p/'website-observer.csv').open()));row['objects']=len(values);row['maximum_width']=max((int(r['width']) for r in values),default=0);row['totals']={key:sum(int(r[key]) for r in values) for key in ['lookup_reads','member_assignments','source_copies','range_passes','insertions']};row['width_histogram']=dict(sorted(collections.Counter(int(r['width']) for r in values).items()))
 rows.append(row);print(label,len(files),'public artifacts',row.get('maximum_width'),flush=True)
(p/'website-parity.json').write_text(json.dumps(rows,indent=2)+'\n')
