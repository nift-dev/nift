#!/usr/bin/env python3
"""Consumer history, migration, fanout, native observation, and recovery contracts."""
import argparse,json,pathlib,subprocess,tempfile,time,sys,os
from shared_dependency_targeted_reproducer import setup,run

def hash_bytes(data):
 h=14695981039346656037
 for v in data:h=((h^v)*1099511628211)&((1<<64)-1)
 return str(h)

def metadata(p,n):return p/'.nift/public'/f'{n}.info.json'
def hashes(p,n):return json.loads(metadata(p,n).read_text())['dependency-hashes']
def tracked(p,entries):(p/'.nift/tracked.json').write_text(json.dumps({'tracked':entries}))
def writable_json(p,value):p.chmod(0o600);p.write_text(json.dumps(value))

def fanout(binary,base,n):
 p=base/('fanout-'+str(n));p.mkdir();d,_,_=setup(p,'hash','template')
 entries=[dict(name=str(i),title=str(i),template='templates/shared.html') for i in range(n)]
 tracked(p,entries)
 for e in entries:(p/'content'/f'{e["name"]}.html').write_text('body')
 run(binary,p,'build','--all')
 edges=sum(len(hashes(p,str(i))) for i in range(n));size=sum(metadata(p,str(i)).stat().st_size for i in range(n))
 for targets in [['0'],[str(i) for i in range(max(1,n//2))],[str(i) for i in range(n-1)]]:
  old={str(i):metadata(p,str(i)).read_bytes() for i in range(n)}
  d.write_text(d.read_text()+'NEW\n')
  run(binary,p,'build',*targets)
  for i in range(n):
   name=str(i)
   if name not in targets:assert metadata(p,name).read_bytes()==old[name]
  status=run(binary,p,'status');assert 'up to date' not in status,status
  run(binary,p,'build');stamps=[metadata(p,str(i)).stat().st_mtime_ns for i in range(n)]
  run(binary,p,'build');assert stamps==[metadata(p,str(i)).stat().st_mtime_ns for i in range(n)]
 return dict(consumers=n,dependency_edges=edges,metadata_bytes=size)

def versions(binary,base):
 p=base/'versions';p.mkdir();d,_,_=setup(p,'hash','template');run(binary,p,'build','--all');key='templates/shared.html';h0=hashes(p,'c')[key]
 history={}
 for value,name in [('h1','a'),('h2','b'),('h3','a')]:d.write_text(value+'\n@content');run(binary,p,'build',name);history[value]=hash_bytes(d.read_bytes())
 assert hashes(p,'a')[key]==history['h3'];assert hashes(p,'b')[key]==history['h2'];assert hashes(p,'c')[key]==h0
 run(binary,p,'build');assert all(hashes(p,n)[key]==history['h3'] for n in ['a','b','c'])
 for defect in ['missing','partial','bad-type','bad-string','extra']:
  value=json.loads(metadata(p,'a').read_text())
  if defect=='missing':value.pop('dependency-hashes')
  elif defect=='partial':value['dependency-hashes'].pop(key)
  elif defect=='bad-type':value['dependency-hashes'][key]=17
  elif defect=='bad-string':value['dependency-hashes'][key]='nan'
  else:value['dependency-hashes']['unused']='123'
  writable_json(metadata(p,'a'),value);old=metadata(p,'a').stat().st_mtime_ns;run(binary,p,'build')
  assert (metadata(p,'a').stat().st_mtime_ns!=old)==(defect!='extra')
  old=metadata(p,'a').stat().st_mtime_ns;run(binary,p,'build');assert metadata(p,'a').stat().st_mtime_ns==old
 # Renamed/removed consumers do not own shared history.
 run(binary,p,'mv','a','renamed');run(binary,p,'build');run(binary,p,'untrack','b');d.write_text('h4\n@content');run(binary,p,'build');assert hashes(p,'c')[key]==hash_bytes(d.read_bytes())
 return True

def wait(p,name,proc):
 deadline=time.monotonic()+10
 while not (p/name).exists():
  if proc.poll() is not None:raise AssertionError(proc.communicate())
  if time.monotonic()>deadline:raise TimeoutError(name)
  time.sleep(.005)

def barrier(binary,base,kind):
 p=base/kind;p.mkdir();d,_,_=setup(p,'hash','explicit' if kind=='external' else 'template')
 # One consumer eliminates unrelated output timing.
 tracked(p,[dict(name='a',title='a',template='templates/shared.html' if kind!='external' else '',**({'build':'main.f'} if kind=='external' else {'post-build':'post.f'}))])
 (p/'gate.py').write_text("from pathlib import Path\nimport time\np=Path('.')\n(p/'ready').touch()\nend=time.monotonic()+10\nwhile not (p/'finish').exists():\n if time.monotonic()>end:raise RuntimeError('barrier timeout')\n time.sleep(.005)\n")
 command='r := run('+json.dumps(sys.executable)+', "gate.py")\n'
 if kind=='external':
  (p/'content/a.deps.json').write_text(json.dumps({'dependencies':['data/shared.html']}))
  (p/'main.f').write_text(command+'f := file(getenv("NIFT_HOOK_OUTPUT"))\nf.open("w")\nf.write("custom")\nf.save()\nf.close()\n')
 else:(p/'post.f').write_text(command)
 proc=subprocess.Popen([binary,'build','--all'],cwd=p,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 try:
  wait(p,'ready',proc)
  if kind=='interrupt':
   assert (p/'public/a.html').exists();assert not metadata(p,'a').exists();proc.kill();proc.communicate();assert (p/'.nift/.unfinished').exists()
   r=subprocess.run([binary,'build'],cwd=p,capture_output=True,text=True);assert r.returncode!=0
   (p/'finish').touch()
   deadline=time.monotonic()+10
   while True:
    r=subprocess.run([binary,'build','--repair'],cwd=p,capture_output=True,text=True)
    if r.returncode==0:break
    assert 'another build appears to be running' in r.stderr,(r.stdout,r.stderr)
    assert time.monotonic()<deadline
    time.sleep(.01)
   assert metadata(p,'a').exists();return dict(kind=kind,passed=True)
  consumed=hash_bytes(d.read_bytes());d.write_text('NEW\n@content' if kind=='native' else 'NEW');(p/'finish').touch();out,err=proc.communicate(timeout=15)
  if kind=='external':assert proc.returncode!=0 and 'changed during build' in out+err,(out,err);assert not metadata(p,'a').exists()
  else:
   assert proc.returncode==0,(out,err);assert hashes(p,'a')['templates/shared.html']==consumed
   assert 'dependency changed' in run(binary,p,'status');run(binary,p,'build');assert hashes(p,'a')['templates/shared.html']==hash_bytes(d.read_bytes())
  return dict(kind=kind,passed=True)
 finally:
  if proc.poll() is None:proc.kill();proc.communicate()

def generated_variants(binary,base):
 for variant in ['directory','pagination']:
  p=base/('generated-'+variant);p.mkdir();setup(p,'hash','template')
  producer='generated/p' if variant=='directory' else 'p'
  entries=[dict(name=producer,title='producer',template='')]+[dict(name=n,title=n,template='',depends=[producer]) for n in ['b','c']]
  src=p/'content'/f'{producer}.html';src.parent.mkdir(exist_ok=True)
  if variant=='pagination':
   entries[0]['template']='templates/normal.html';(p/'templates/normal.html').write_text('@content');entries[0]['paginate']={'items-per-page':1};(p/'content/p.paginate.html').write_text('$[paginate.items]')
   src.write_text('@item{first}@item{OLD}@paginate');key='public/p-2.html'
   for n in ['b','c']:(p/'content'/f'{n}.html').write_text('@input("public/p-2.html")')
  else:
   src.write_text('OLD');key='public/generated'
   for n in ['b','c']:(p/'content'/f'{n}.html').write_text('@dep("public/generated")@input("public/generated/p.html")')
  tracked(p,entries);run(binary,p,'build','--all');old=hashes(p,'c')[key]
  src.write_text(src.read_text().replace('OLD','NEW'));run(binary,p,'build','b')
  assert hashes(p,'b')[key]!=old;assert hashes(p,'c')[key]==old
  assert 'dependency changed' in run(binary,p,'status');run(binary,p,'build')
  assert hashes(p,'b')[key]==hashes(p,'c')[key]
  stamp=metadata(p,'c').stat().st_mtime_ns;run(binary,p,'build');assert stamp==metadata(p,'c').stat().st_mtime_ns
 return True

def conflicting_reads(binary,base):
 p=base/'conflicting';p.mkdir();setup(p,'hash','template')
 tracked(p,[dict(name='a',title='a',template='',build='main.f')])
 (p/'data/observed.json').write_text('1')
 (p/'gate.py').write_text("from pathlib import Path\nimport time\np=Path('.')\n(p/'ready').touch()\nend=time.monotonic()+10\nwhile not (p/'finish').exists():\n if time.monotonic()>end:raise RuntimeError('timeout')\n time.sleep(.005)\n")
 (p/'main.f').write_text('x := inject("data/observed.json")\nr := run('+json.dumps(sys.executable)+', "gate.py")\ny := inject("data/observed.json")\nf := file(getenv("NIFT_HOOK_OUTPUT"))\nf.open("w")\nf.write("output")\nf.save()\nf.close()\n')
 proc=subprocess.Popen([binary,'build','--all'],cwd=p,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 try:
  wait(p,'ready',proc);(p/'data/observed.json').write_text('2');(p/'finish').touch();out,err=proc.communicate(timeout=15)
  assert proc.returncode!=0 and 'multiple versions' in out+err,(out,err)
  assert not metadata(p,'a').exists()
 finally:
  if proc.poll() is None:proc.kill();proc.communicate()
 return True

def json_and_hooks(binary,base):
 p=base/'json-hooks';p.mkdir();setup(p,'hash','template')
 tracked(p,[dict(name='a',title='a',template='templates/shared.html',**{'pre build':'pre.f'})])
 (p/'data/data.json').write_text('{"x":1}');(p/'data/schema.json').write_text('{"type":"object"}')
 (p/'templates/shared.html').write_text('@json(doc, "data/schema.json", "data/data.json")\n@content')
 (p/'helper.f').write_text('x := 1\n');(p/'pre.f').write_text('import("./helper.f")\n')
 run(binary,p,'build','--all')
 for key in ['data/data.json','data/schema.json','pre.f','helper.f']:assert hashes(p,'a')[key]==hash_bytes((p/key).read_bytes()),(key,hashes(p,'a'))
 (p/'helper.f').write_text('x := 2\n');assert 'dependency changed: helper.f' in run(binary,p,'status');run(binary,p,'build')
 return True

if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('--nift',required=True);ap.add_argument('--output',required=True);ap.add_argument('--scaling',action='store_true');a=ap.parse_args();binary=str(pathlib.Path(a.nift).resolve());started=time.monotonic()
 with tempfile.TemporaryDirectory(prefix='nift-consumer-snapshots-') as td:
  base=pathlib.Path(td);result={'fanout':[fanout(binary,base,n) for n in ([2,3,10,128] if not a.scaling else [1000,4000,10000])],'generated_variants':generated_variants(binary,base),'conflicting_reads':conflicting_reads(binary,base),'versions_migration_rename':versions(binary,base),'json_schema_hook_import':json_and_hooks(binary,base),'barriers':[barrier(binary,base,k) for k in ['native','external','interrupt']]}
 result.update(passed=True,elapsed_seconds=round(time.monotonic()-started,3));pathlib.Path(a.output).write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
