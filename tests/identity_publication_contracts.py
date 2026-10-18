#!/usr/bin/env python3
"""Native optional catalogue lifecycle and authoritative-metadata contracts."""
import argparse, json, os, pathlib, shutil, struct, subprocess, tempfile, hashlib
from shared_dependency_targeted_reproducer import setup
ap=argparse.ArgumentParser();ap.add_argument('--nift',required=True);ap.add_argument('--baseline',required=True);ap.add_argument('--probe',required=True);ap.add_argument('--fault-nift');ap.add_argument('--output',required=True);a=ap.parse_args()
binary=str(pathlib.Path(a.nift).resolve());baseline=str(pathlib.Path(a.baseline).resolve());probe=str(pathlib.Path(a.probe).resolve());fault=str(pathlib.Path(a.fault_nift).resolve()) if a.fault_nift else None
rows=[]
def run(executable,root,*args,env=None):
 result=subprocess.run([executable,*args],cwd=root,capture_output=True,text=True,env=env,timeout=45)
 return result
def build(root,*args,exe=binary,env=None):
 r=run(exe,root,'build',*args,env=env);assert r.returncode==0,(r.stdout,r.stderr);return r
def load(root,valid=True):
 r=run(probe,root,'load',str(root));assert r.returncode==(0 if valid else 2),(r.stdout,r.stderr)
 return json.loads(r.stdout)
def publish(root,*keys,env=None):
 r=run(probe,root,'publish',str(root),*keys,env=env);assert r.returncode==0,(r.stdout,r.stderr);return json.loads(r.stdout)['changed']
def state(root):
 return {f.name:(hashlib.sha256(f.read_bytes()).hexdigest(),f.stat().st_mtime_ns) for f in (root/'.nift/identity-stage1').glob('*.bin')}
def record(name,**extra):rows.append(dict(case=name,passed=True,**extra))
def equal_status(root,name):
 out=[]
 for exe in [baseline,binary]:
  r=run(exe,root,'status');out.append((r.returncode,r.stdout.split('time taken:')[0],r.stderr))
 assert out[0]==out[1],(name,out);record(name)
def fnv(data):
 h=14695981039346656037
 for c in data:h=((h^c)*1099511628211)&((1<<64)-1)
 return h
def keys(loaded):return [bytes.fromhex(k).decode('utf8',errors='surrogateescape') for k in loaded['keys']]
with tempfile.TemporaryDirectory(prefix='nift-identity-publication-') as temporary:
 base=pathlib.Path(temporary).resolve();root=base/'project';root.mkdir();setup(root,'modified','explicit')
 equal_status(root,'missing catalogue is E3B fallback');load(root,False)
 build(root,'--all');first=load(root);assert 'data/shared.html' in keys(first);initial=state(root)
 build(root,'a');assert state(root)==initial;record('one-page rebuild known paths: zero catalogue bytes/mtime writes')
 # Workers discover the same spelling. IDs deduplicated and old IDs immutable.
 (root/'data/new.txt').write_text('new')
 for n in ['a','b','c']:(root/'content'/f'{n}.html').write_text('@dep("data/new.txt")@input("data/new.txt")')
 build(root,'--all');second=load(root);assert keys(second).count('data/new.txt')==1;assert second['keys'][:first['total']]==first['keys']
 for name,value in initial.items():
  if name.startswith("shard-"):assert state(root)[name]==value
 record('same spelling across workers: coordinator dedup/immutable old IDs/shards')
 previous_manifest=(root/'.nift/identity-stage1'/f"manifest-{first['generation']}.bin");assert previous_manifest.exists()
 # Workers discover different paths, then a targeted consumer adds one.
 for n in ['a','b','c']:
  (root/'data'/f'{n}.txt').write_text(n);(root/'content'/f'{n}.html').write_text(f'@dep("data/{n}.txt")@input("data/{n}.txt")')
 build(root,'--all');third=load(root);assert second['keys']==third['keys'][:second['total']]
 assert all('data/'+n+'.txt' in keys(third) for n in ['a','b','c']);record('different worker discoveries: stable IDs')
 (root/'data/target.txt').write_text('target');(root/'content/a.html').write_text('@dep("data/target.txt")@input("data/target.txt")');build(root,'a');assert 'data/target.txt' in keys(load(root));record('targeted build publishes new identity')
 # Failed consumers cannot manufacture clean history; successful sibling's
 # discoveries remain safe optional identities if the whole build fails.
 (root/'data/success.txt').write_text('success');(root/'content/a.html').write_text('@dep("data/success.txt")@input("data/success.txt")');(root/'content/b.html').write_text('@input("data/missing.txt")')
 r=run(binary,root,'build','--all');assert r.returncode!=0;assert 'data/success.txt' in keys(load(root));assert not (root/'.nift/.unfinished').exists();record('failed build after sibling discovery fails without a repair marker (D-020)')
 (root/'content/b.html').write_text('fixed');build(root);record('ordinary build after the fix converges (D-020)')
 # Malformed, unsupported-platform, coherent absolute and parent corruption.
 directory=root/'.nift/identity-stage1';good=(directory/'manifest.bin').read_bytes();good_files={f.name:f.read_bytes() for f in directory.glob('*.bin')}
 # Optional cache files cannot open blocking FIFO/device/directory streams.
 generation=struct.unpack_from('<Q',good,16)[0]
 for filename in ['manifest.bin',f'shard-{generation}-0.bin']:
  active=directory/filename;backup=directory/(filename+'.test-backup');active.rename(backup)
  try:
   active.mkdir();load(root,False);equal_status(root,'nonregular directory '+filename+' fallback');active.rmdir()
   if os.name!='nt':
    os.mkfifo(active)
    r=subprocess.run([probe,'load',str(root)],cwd=root,capture_output=True,text=True,timeout=5);assert r.returncode==2
    equal_status(root,'nonregular FIFO '+filename+' fallback');active.unlink()
  finally:
   if active.exists():
    if active.is_dir():active.rmdir()
    else:active.unlink()
   backup.rename(active)
 for name,mutation in [('corrupt',lambda b:b'bad'),('unsupported version',lambda b:b[:8]+struct.pack('<I',2)+b[12:]),('wrong platform',lambda b:b[:12]+struct.pack('<I',2 if os.name!='nt' else 1)+b[16:])]:
  (directory/'manifest.bin').write_bytes(mutation(good));load(root,False);equal_status(root,name+' -> E3B fallback');(directory/'manifest.bin').write_bytes(good)
 m=bytearray(good);generation=struct.unpack_from('<Q',m,16)[0];f=directory/f'shard-{generation}-0.bin';original=f.read_bytes();b=bytearray(original);count=struct.unpack_from('<I',b,12)[0];arena=20+count*16
 offset,length=struct.unpack_from('<II',b,36);assert length;b[arena+offset]=ord('/');f.write_bytes(b);struct.pack_into('<Q',m,44,fnv(b));(directory/'manifest.bin').write_bytes(m);load(root,False);equal_status(root,'coherent absolute spelling corruption -> E3B fallback');f.write_bytes(original);(directory/'manifest.bin').write_bytes(good)
 # Check structurally coherent corruption with recomputed checksums. Each
 # case starts from the same immutable bytes and must select E3B fallback.
 m=bytearray(good);generation=struct.unpack_from('<Q',m,16)[0];f=directory/f'shard-{generation}-0.bin';original=f.read_bytes()
 count=struct.unpack_from('<I',original,12)[0];arena=20+count*16
 target=next(i for i in range(1,count) if struct.unpack_from('<I',original,24+i*16)[0]>=3)
 offset,length=struct.unpack_from('<II',original,20+target*16)
 for label in ['parent','offset','flags','nul','dotdot','duplicate','length','id','checksum','torn']:
  b=bytearray(original);manifest=bytearray(good)
  if label=='parent':struct.pack_into('<I',b,28+target*16,0xffffffff)
  elif label=='offset':struct.pack_into('<I',b,20+target*16,offset+1)
  elif label=='flags':struct.pack_into('<I',b,32+target*16,1)
  elif label=='nul':b[arena+offset+length]=1
  elif label=='dotdot':b[arena+offset:arena+offset+3]=b'../'
  elif label=='duplicate':
   lengths={}
   for j in range(1,count):
    off,size=struct.unpack_from('<II',b,20+j*16)
    if size in lengths:
     other=lengths[size];otheroff=struct.unpack_from('<I',b,20+other*16)[0]
     b[arena+off:arena+off+size]=b[arena+otheroff:arena+otheroff+size]
     struct.pack_into('<I',b,28+j*16,struct.unpack_from('<I',b,28+other*16)[0]);break
    lengths[size]=j
   else:raise AssertionError('fixture needs equal-length identities for duplicate check')
  elif label=='checksum':struct.pack_into('<Q',manifest,44,fnv(b)^1)
  elif label=='length':struct.pack_into('<I',b,24+target*16,65537)
  elif label=='id':struct.pack_into('<I',b,8,1)
  elif label=='torn':b=b[:-1]
  if label!='checksum':struct.pack_into('<Q',manifest,44,fnv(b))
  f.write_bytes(b);(directory/'manifest.bin').write_bytes(manifest)
  load(root,False);equal_status(root,'catalogue '+label+' corruption fallback')
  f.write_bytes(original);(directory/'manifest.bin').write_bytes(good)
 # Unsupported spellings are excluded from publication without dependency rejection.
 assert not publish(root,'../escape','a//b','./a','/absolute');record('unsupported discoveries do not publish or reject dependencies')
 if os.name=='nt':
  before=load(root);assert not publish(root,'C:/absolute','data\\backslash','CON','data/ü.txt','a:stream','name.','name ')
  assert load(root)['keys']==before['keys'];record('Windows uncertain spellings remain legacy without ID folding')
 # Publication failpoints, orphan shards, interrupted switch, previous usable.
 for phase in ['aliases','shards','validated','manifest-ready']:
  before=(directory/'manifest.bin').read_bytes();old=load(root);env=dict(os.environ,NIFT_TEST_IDENTITY_FAIL=phase)
  assert not publish(root,'data/failure-'+phase+'.txt',env=env);assert (directory/'manifest.bin').read_bytes()==before;assert load(root)['keys']==old['keys'];record('publication failure '+phase+': previous manifest usable')
 for phase in ['shards','manifest-ready','committed']:
  old=load(root);before=(directory/'manifest.bin').read_bytes();env=dict(os.environ,NIFT_TEST_IDENTITY_INTERRUPT=phase)
  r=run(probe,root,'publish',str(root),'data/interrupted-'+phase+'.txt',env=env);assert r.returncode==86,(phase,r.stdout,r.stderr)
  now=load(root)
  if phase!='committed':assert now['keys']==old['keys'] and (directory/'manifest.bin').read_bytes()==before
  else:assert 'data/interrupted-committed.txt' in keys(now)
  record('interruption '+phase+': complete old/new generation only')
 # Retire stale generations only after commit; unknown files survive.
 old=load(root);sentinel=directory/'user-not-a-shard.bin';sentinel.write_text('keep');assert publish(root,'data/final-generation.txt');now=load(root);assert sentinel.read_text()=='keep';assert (directory/f"manifest-{old['generation']}.bin").exists();record('post-commit generation hygiene preserves previous/unknown files')
 # Fresh .info authority remains live even with a valid catalogue and same mtime.
 meta=root/'.nift/public/a.info.json';data=meta.read_bytes();st=meta.stat();os.chmod(meta,0o644)
 try:
  meta.write_text('{');os.utime(meta,ns=(st.st_atime_ns,st.st_mtime_ns));equal_status(root,'same-mtime malformed .info remains authoritative')
 finally:meta.write_bytes(data);os.utime(meta,ns=(st.st_atime_ns,st.st_mtime_ns));os.chmod(meta,st.st_mode)
 # Native following/no-follow parity, including actual prepared catalogue paths.
 (root/'data/plain.txt').write_text('plain');(root/'data/directory').mkdir();assert publish(root,'data/plain.txt','data/directory','data/absent.txt','data/PLAIN.txt');assert 'data/PLAIN.txt' in keys(load(root))
 paths=['data/plain.txt','data/directory','data/absent.txt','data/PLAIN.txt']
 for name,target in [('inside','plain.txt'),('broken','absent-target'),('escape',str(base/'outside.txt'))]:
  (base/'outside.txt').write_text('outside')
  try:os.symlink(target,root/'data'/name)
  except OSError as e:raise AssertionError('native symlink certification unavailable') from e
  assert publish(root,'data/'+name);paths.append('data/'+name)
 r=run(probe,root,'native',str(root),*paths);assert r.returncode==0,(r.stdout,r.stderr);record('native following/no-follow observation parity')
 # Dependency aliases remain fresh and exact-E3B logical containment.
 for name in ['inside','broken','escape']:
  old_content=(root/'content/a.html').read_bytes();(root/'content/a.html').write_text('@dep("data/'+name+'")')
  # Authoritative forged saved edge checks safety even if rendering would fail.
  obj=json.loads(meta.read_text());prior=meta.read_bytes();mode=meta.stat().st_mode;os.chmod(meta,0o644);obj['dependencies'].append('data/'+name);meta.write_text(json.dumps(obj))
  try:equal_status(root,name+' symlink/escape saved dependency parity')
  finally:meta.write_bytes(prior);os.chmod(meta,mode);(root/'content/a.html').write_bytes(old_content)
 # Real inaccessible parent. Windows ACL is restored even on test failure.
 blocked=root/'data';old_mode=blocked.stat().st_mode
 import contextlib
 if os.name=='nt':
  from identity_windows_acl import denied_read, assert_denied
  access=denied_read(blocked)
 else:
  @contextlib.contextmanager
  def denied_posix():
   try:os.chmod(blocked,0);yield
   finally:os.chmod(blocked,old_mode)
  access=denied_posix()
 with access:
  if os.name=='nt':assert_denied(blocked)
  else:
   try:os.lstat(blocked/'plain.txt');raise AssertionError('inaccessible parent did not deny observation')
   except PermissionError:pass
  r=run(probe,root,'native-denied',str(root),'data/plain.txt');assert r.returncode==0,(r.stdout,r.stderr)
  equal_status(root,'actual inaccessible parent parity')
  observation=json.loads(r.stdout);assert observation['native_observations']
  rows[-1]['uncertain_native_leaf_observations']=observation['uncertain_native_leaf_observations']
  if os.name=='nt':assert observation['uncertain_native_leaf_observations']==1
 # A fresh parent containment observation must still reject an ancestor
 # changed into a directory symlink escaping the project.
 real=root/'real-data';blocked.rename(real);outside=base/'outside-directory';outside.mkdir();(outside/'plain.txt').write_text('outside')
 try:
  os.symlink(str(outside),blocked,target_is_directory=True)
  equal_status(root,'escaping directory ancestor parity')
 finally:
  if blocked.is_symlink():blocked.unlink()
  real.rename(blocked)
 moved=base/'relocated ü';root.rename(moved);root=moved;directory=root/'.nift/identity-stage1';assert load(root)['loaded'];equal_status(root,'relocation including Unicode root');record('relocation rebinding')
 # Repair after deleting acceleration state uses successful fresh consumers.
 shutil.rmtree(directory);build(root,'--repair');assert load(root)['loaded'];record('repair missing catalogue reconstructs optional identity state')
 # Hooks create new dependencies before rendering, and generated producer
 # edges are published only after successful coordinator completion.
 hooked=base/'hooked';hooked.mkdir();setup(hooked,'modified','explicit')
 (hooked/'.nift/tracked.json').write_text(json.dumps({'tracked':[dict(name='a',title='a',template='',**{'pre-build':'pre.f'})]}))
 (hooked/'pre.f').write_text('f := file("data/hook-new.txt")\nf.open("w")\nf.write("hook-created")\nf.save()\nf.close()\n')
 (hooked/'content/a.html').write_text('@input("data/hook-new.txt")')
 build(hooked,'--all');assert 'data/hook-new.txt' in keys(load(hooked));equal_status(hooked,'hook-generated new dependency catalogue parity')
 generated=base/'generated';generated.mkdir();setup(generated,'modified','explicit')
 entries=[dict(name='producer',title='producer',template=''),dict(name='b',title='b',template='',depends=['producer'])]
 (generated/'.nift/tracked.json').write_text(json.dumps({'tracked':entries}))
 (generated/'content/producer.html').write_text('generated')
 (generated/'content/b.html').write_text('@input("public/producer.html")')
 build(generated,'--all');assert 'public/producer.html' in keys(load(generated));equal_status(generated,'generated producer dependency catalogue parity')
 # The approved relaxed count bound is exercised by many small immutable
 # appends: no earlier partially filled shard is rewritten or re-packed.
 partial=base/'partial';partial.mkdir();before_keys=[]
 for i in range(12):
  assert publish(partial,'data/p'+str(i));current=load(partial)
  assert current['keys'][:len(before_keys)]==before_keys;before_keys=current['keys']
 manifest=(partial/'.nift/identity-stage1/manifest.bin').read_bytes();total,shards=struct.unpack_from('<II',manifest,24)
 assert shards>(total+255)//256 and shards<=total;record('partial immutable append shards accepted within bounded count')
 orphan_dir=partial/'.nift/identity-stage1';generation=current['generation']
 reserved=orphan_dir/f'manifest-{generation+1}.bin';torn=orphan_dir/f'shard-{generation+2}-0.bin'
 reserved.write_bytes(b'unknown preserved manifest');torn.write_bytes(b'torn unpublished shard')
 assert publish(partial,'data/after-orphans');latest=load(partial)
 assert latest['generation']==generation+3 and reserved.read_bytes()==b'unknown preserved manifest' and torn.read_bytes()==b'torn unpublished shard'
 record('torn unpublished files ignored and generation names never overwritten')
 if fault:
  before=(directory/'manifest.bin').read_bytes();(root/'data/cli-failure.txt').write_text('failure');(root/'content/a.html').write_text('@dep("data/cli-failure.txt")@input("data/cli-failure.txt")')
  build(root,'a',exe=fault,env=dict(os.environ,NIFT_TEST_IDENTITY_FAIL='manifest-ready'));assert (directory/'manifest.bin').read_bytes()==before;assert not (root/'.nift/.unfinished').exists();record('CLI publication failure leaves successful consumer semantics intact')
  (root/'data/cli-interrupted.txt').write_text('interrupted');(root/'content/a.html').write_text('@dep("data/cli-interrupted.txt")@input("data/cli-interrupted.txt")')
  r=run(fault,root,'build','a',env=dict(os.environ,NIFT_TEST_IDENTITY_INTERRUPT='manifest-ready'));assert r.returncode==86;assert (root/'.nift/.unfinished').exists();load(root);build(root,'--repair');record('CLI interruption before switch retains recovery protocol')
  (root/'data/cli-committed.txt').write_text('committed');(root/'content/a.html').write_text('@dep("data/cli-committed.txt")@input("data/cli-committed.txt")')
  r=run(fault,root,'build','a',env=dict(os.environ,NIFT_TEST_IDENTITY_INTERRUPT='committed'));assert r.returncode==86;assert (root/'.nift/.unfinished').exists()
  assert 'data/cli-committed.txt' in keys(load(root));build(root,'--repair');record('CLI interruption after switch retains complete catalogue and recovery protocol')
pathlib.Path(a.output).write_text(json.dumps({'passed':True,'platform':os.name,'cases':rows},indent=2));print(len(rows),'identity lifecycle contracts passed')
