#!/usr/bin/env python3
"""Build lifecycle/order contracts with real outputs and bounded barrier fixtures."""
import argparse,json,os,pathlib,subprocess,tempfile,time
ap=argparse.ArgumentParser();ap.add_argument('--nift',default=os.environ.get('NIFT_BIN','./nift'));a=ap.parse_args();binary=pathlib.Path(a.nift).resolve();count=0

def run(p,*args,ok=True,env=None):
 r=subprocess.run([str(binary),*args],cwd=p,text=True,capture_output=True,timeout=45,env=dict(os.environ,**(env or {})))
 assert (r.returncode==0)==ok,(args,r.returncode,r.stdout,r.stderr)
 return r.stdout+r.stderr

def native_symlink(link,target):
 # MSYS os.name is POSIX, but its symlink emulation is invisible to native Nift.
 # Create and verify an actual Windows reparse-point link instead of a copy.
 if os.name=='nt' or os.environ.get('MSYSTEM'):
  def native(path):
   if os.environ.get('MSYSTEM'):return subprocess.run(['cygpath','-aw',str(path)],text=True,capture_output=True,check=True).stdout.strip()
   return str(path)
  command='$ErrorActionPreference = "Stop"; New-Item -ItemType SymbolicLink -Path $env:NIFT_TEST_SYMLINK_PATH -Target $env:NIFT_TEST_SYMLINK_TARGET | Out-Null; $item = Get-Item -LiteralPath $env:NIFT_TEST_SYMLINK_PATH -Force; if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0 -or $item.LinkType -ne "SymbolicLink") { exit 2 }'
  subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',command],env=dict(os.environ,MSYS2_ARG_CONV_EXCL='*',NIFT_TEST_SYMLINK_PATH=native(link),NIFT_TEST_SYMLINK_TARGET=native(target)),capture_output=True,text=True,check=True,timeout=30)
 else:link.symlink_to(target)

def project(p,entries):
 (p/'.nift').mkdir(exist_ok=True);(p/'content').mkdir(exist_ok=True);(p/'public').mkdir(exist_ok=True)
 (p/'.nift/config.json').write_text(json.dumps({'config':{'content-dir':'content/','content-ext':'.html','output-dir':'public/','output-ext':'.html','default-template':'','build-threads':4,'incremental-mode':'hash','minify-exts':[]}}))
 tracking(p,entries)
 for item in entries:(p/'content'/f'{item["name"]}.html').write_text(item['name'])

def tracking(p,entries):(p/'.nift/tracked.json').write_text(json.dumps({'tracked':entries}))
def item(n,**kw):return dict(name=n,title=n,**kw)
def write(path,text):return f'f := file({json.dumps(path)})\nf.open("w")\nf.write({json.dumps(text)})\nf.save()\nf.close()\n'
def output(text):return 'out_file := file(getenv("NIFT_HOOK_OUTPUT"))\nout_file.open("w")\nout_file.write('+json.dumps(text)+')\nout_file.save()\nout_file.close()\n'
def need(name):return f'if(!file("public/{name}.html").exists()) {{ missing_prerequisite() }}\n'
def check(label):
 global count;count+=1;print('PASS',label,flush=True)

with tempfile.TemporaryDirectory(prefix='nift-pipeline-') as tmp:
 root=pathlib.Path(tmp)
 # A custom build bypasses even an invalid normal template. Pre/post order is visible.
 p=root/'custom';p.mkdir();project(p,[item('a',template='missing-template.html',**{'pre-build':'pre.f','build':'main.f','post-build':'post.f'})])
 (p/'helper.f').write_text('print("imported helper")\n');(p/'pre.f').write_text(write('pre-ready','ready'));(p/'main.f').write_text('import("./helper.f")\nif(!file("pre-ready").exists()) { bad_order() }\n'+output('custom'))
 (p/'post.f').write_text(need('a')+write('post-ready','ready'))
 run(p,'build','--all');assert (p/'public/a.html').read_text()=='custom';assert (p/'post-ready').exists()
 metadata=p/'.nift/public/a.info.json';state=json.loads(metadata.read_text());assert {'pre.f','main.f','post.f','helper.f','content/a.html'}<=set(state['dependencies']);stamp=metadata.stat().st_mtime_ns
 run(p,'build');assert metadata.stat().st_mtime_ns==stamp;check('custom replaces render; pre/main/post; dependency recording; incremental')
 for path in ['main.f','pre.f','post.f','helper.f','content/a.html']:
  source=p/path;source.write_text(source.read_text()+'\n');run(p,'build');assert metadata.stat().st_mtime_ns!=stamp;stamp=metadata.stat().st_mtime_ns
 check('all scripts and content invalidate')
 # A pre phase can create missing content before normal rendering.
 q=root/'pre-generates-content';q.mkdir();project(q,[item('a',**{'pre-build':'pre.f'})]);(q/'content/a.html').unlink();(q/'pre.f').write_text(write('content/a.html','created'));run(q,'build','--all');assert (q/'public/a.html').read_text()=='created';check('pre-build generates missing content before render')
 # Script-visible tracked metadata belongs to the incremental signature.
 q=root/'script-metadata';q.mkdir();entry=item('a',build='main.f',type='first');project(q,[entry]);(q/'main.f').write_text(output('ok'));run(q,'build','--all');stamp=(q/'.nift/public/a.info.json').stat().st_mtime_ns;entry['type']='second';tracking(q,[entry]);run(q,'build');assert (q/'.nift/public/a.info.json').stat().st_mtime_ns!=stamp;check('script-relevant tracked metadata invalidates')
 # No success metadata on any failed phase; output is mandatory.
 for phase in ['pre-build','build','post-build','missing-output']:
  q=root/phase;q.mkdir();entry=item('a',build='main.f');project(q,[entry]);(q/'main.f').write_text(output('ok'))
  if phase=='missing-output':(q/'main.f').write_text('print("no output")\n')
  else:
   path='main.f' if phase=='build' else phase+'.f';entry[phase]=path;tracking(q,[entry]);(q/path).write_text('missing_function()\n')
  run(q,'build','--all',ok=False);assert not (q/'.nift/public/a.info.json').exists();check('failure '+phase)
  (q/'main.f').write_text(output('repaired'))
  for key in ['pre-build','post-build']:
   if key in entry:(q/entry[key]).write_text('print("fixed")\n')
  run(q,'build','--repair');check('repair '+phase)
 # Dotted discovery, deprecated fallback and explicit selection.
 q=root/'discovery';q.mkdir();project(q,[item('a')]);(q/'content/a.build.f').write_text(output('sidecar'));(q/'content/a-pre-build.f').write_text(write('legacy','yes'))
 run(q,'build','--all');assert (q/'legacy').exists();assert (q/'public/a.html').read_text()=='sidecar'
 (q/'content/a.pre-build.f').write_text(write('dotted','yes'));out=run(q,'build','--all',ok=False);assert 'both dotted and deprecated' in out
 tracking(q,[item('a',**{'pre-build':'explicit.f'})]);(q/'explicit.f').write_text(write('explicit','yes'));run(q,'build','--all');assert (q/'explicit').exists();assert not (q/'dotted').exists();(q/'content/a.build.f').unlink();run(q,'build');assert (q/'public/a.html').read_text()=='a';check('sidecars; removal invalidation; conflict; explicit precedence')
 # Chain/diamond ordering, closure, clean prerequisites, order-only invalidation.
 for kind,deps in [('chain',{'b':['a'],'c':['b']}),('diamond',{'b':['a'],'c':['a'],'d':['b','c']})]:
  q=root/kind;q.mkdir();names=list(dict.fromkeys([x for key,vals in deps.items() for x in [*vals,key]]));entries=[item(n,build=n+'.f',depends=deps.get(n,[])) for n in names];project(q,entries)
  for n in names:(q/(n+'.f')).write_text(''.join(need(v) for v in deps.get(n,[]))+output(n))
  target=names[-1];out=run(q,'build',target,env={'NIFT_TEST_BUILD_DAG_STATS':'1'});assert 'BUILD_DAG nodes=' in out
  stamps={n:(q/'.nift/public'/f'{n}.info.json').stat().st_mtime_ns for n in names};run(q,'build',target)
  for n in names[:-1]:assert (q/'.nift/public'/f'{n}.info.json').stat().st_mtime_ns==stamps[n]
  (q/'content/a.html').write_text('changed');run(q,'build');assert (q/'.nift/public'/f'{target}.info.json').stat().st_mtime_ns!=stamps[target] # targeted run did rebuild it earlier
  before=(q/'.nift/public'/f'{target}.info.json').stat().st_mtime_ns;(q/'content/a.html').write_text('changed again');run(q,'build');assert (q/'.nift/public'/f'{target}.info.json').stat().st_mtime_ns==before
  check(kind+' ordering, targeted closure, clean prerequisites and no invalidation')
 # Failure blocks descendants while independent branches finish.
 q=root/'failure';q.mkdir();project(q,[item('a',build='bad.f'),item('b',depends=['a']),item('c',depends=['b']),item('independent')]);(q/'bad.f').write_text('missing_function()\n');out=run(q,'build','--all',ok=False);assert 'blocked because prerequisite' in out;assert (q/'public/independent.html').exists()
 for n in ['b','c']:assert not (q/'public'/f'{n}.html').exists();assert not (q/'.nift/public'/f'{n}.info.json').exists()
 (q/'bad.f').write_text(output('ok'));run(q,'build','--repair');check('transitive failure propagation; independent branch; recovery')
 # Validation before mutation, including an iterative long cycle.
 invalids=[([item('a',depends=['missing'])],'unknown'),([item('a',depends=['a'])],'self'),([item('a',depends=['b','b']),item('b')],'duplicate'),([item('a',depends=['b']),item('b',depends=['a'])],'a -> b -> a'),([item('a',depends='b')],'array'),([item('a',build=7)],'string'),([item('a',build='bad.txt')],'.f path'),([item('a',build='bad\x00.f')],'.f path')]
 for i,(entries,message) in enumerate(invalids):
  q=root/f'invalid{i}';q.mkdir();project(q,entries);out=run(q,'build','--all',ok=False);assert message in out;assert not (q/'.nift/.unfinished').exists()
 check('missing/self/duplicate/cycle/type validation before mutation')
 # A two-worker barrier proves independent custom scripts overlap; serial execution fails.
 q=root/'parallel';q.mkdir();project(q,[item('a',build='a.f'),item('b',build='b.f'),item('done',depends=['a','b'])])
 for n,other in [('a','b'),('b','a')]:
  (q/(n+'.f')).write_text(write(n+'-ready','yes')+f'for(i : range(0,10000)) {{\n if(file("{other}-ready").exists()) {{ break }}\n sleep(1)\n}}\nif(!file("{other}-ready").exists()) {{ parallelism_failed() }}\n'+output(n))
 out=run(q,'build','--all',env={'NIFT_TEST_BUILD_DAG_STATS':'1'});check('parallel independent branches with deterministic barrier')
 # Mixed normal/custom output producers use the same prerequisite scheduler.
 q=root/'mixed';q.mkdir();project(q,[item('a'),item('b',build='b.f',depends=['a']),item('c',depends=['b']),item('d',build='d.f',depends=['c'])]);(q/'b.f').write_text(need('a')+output('B'));(q/'content/c.html').write_text('@input("public/b.html")');(q/'d.f').write_text(need('c')+output('D'));run(q,'build','d');assert (q/'public/c.html').read_text()=='B';check('normal/custom composition')
 # Long validation is iterative rather than recursive.
 q=root/'long-cycle';q.mkdir();entries=[item('p'+str(i),depends=['p'+str((i+1)%10000)]) for i in range(10000)];project(q,entries);out=run(q,'status',ok=False);assert 'p0 -> p1 -> p2' in out and 'p9999 -> p0' in out;check('10k iterative cycle diagnostic')
 # A clean intermediate prerequisite still participates in transitive closure.
 q=root/'clean-intermediate';q.mkdir();project(q,[item('a'),item('b',depends=['a']),item('c',depends=['b'])]);run(q,'build','--all');b_stamp=(q/'.nift/public/b.info.json').stat().st_mtime_ns;(q/'content/a.html').write_text('changed');run(q,'build','c');assert (q/'public/a.html').read_text()=='changed';assert (q/'.nift/public/b.info.json').stat().st_mtime_ns==b_stamp;check('transitive closure across a clean intermediate')
 # Generated ordinary file dependencies propagate in the same invocation,
 # and multiple hash consumers cannot race a shared stored-hash refresh.
 for mode in ['hash','modified']:
  q=root/('file-dependency-'+mode);q.mkdir();project(q,[item('a'),item('b',depends=['a']),item('c',depends=['a'])]);config=json.loads((q/'.nift/config.json').read_text());config['config']['incremental-mode']=mode;(q/'.nift/config.json').write_text(json.dumps(config))
  for n in ['b','c']:(q/'content'/f'{n}.html').write_text('@input("public/a.html")')
  run(q,'build','--all')
  for i in range(6):
   value='version-'+str(i);(q/'content/a.html').write_text(value);run(q,'build')
   for n in ['a','b','c']:assert (q/'public'/f'{n}.html').read_text()==value
  check('same-pass ordinary file invalidation, multiple consumers '+mode)
 # A producer may read its old output through a symlink before replacing it.
 # Dependents must see the replacement, even if that alias was cached earlier.
 q=root/'output-alias';q.mkdir();project(q,[item('a'),item('b',depends=['a'])]);(q/'public/a.html').write_text('seed');native_symlink(q/'alias.html',q/'public/a.html')
 (q/'content/a.html').write_text('@input("alias.html")X');(q/'content/b.html').write_text('@input("alias.html")');run(q,'build','--all')
 assert (q/'public/b.html').read_text()=='seedX'
 (q/'content/a.html').write_text('@input("alias.html")Y');run(q,'build');assert (q/'public/b.html').read_text()=='seedXY';check('generated output alias cache freshness')
 # Tracking mutations preserve explicit pipeline metadata and graph integrity.
 q=root/'tracking-mutations';q.mkdir();entries=[item('a'),item('b',depends=['a'],build='b.f',type='article',frontmatter='none')];project(q,entries);(q/'b.f').write_text(output('B'))
 run(q,'track','extra','Extra','unused-template.html');saved=json.loads((q/'.nift/tracked.json').read_text())['tracked'];b=next(x for x in saved if x['name']=='b');assert b['depends']==['a'] and b['build']=='b.f' and b['type']=='article' and b['frontmatter']=='none'
 before=(q/'.nift/tracked.json').read_bytes();run(q,'untrack','a',ok=False);assert (q/'.nift/tracked.json').read_bytes()==before;assert not (q/'.nift/.unfinished').exists()
 run(q,'mv','a','renamed');saved=json.loads((q/'.nift/tracked.json').read_text())['tracked'];assert next(x for x in saved if x['name']=='b')['depends']==['renamed'];run(q,'build','b');run(q,'untrack','renamed','b');check('tracking serialization, rename references and safe prerequisite removal')
 # build-auto reloads scripts/edges on its next pass; publish config atomically.
 q=root/'auto';q.mkdir();project(q,[item('a'),item('b',build='b.f',depends=['a'])]);(q/'b.f').write_text(need('a')+output('first'))
 child=subprocess.Popen([str(binary),'build','--auto'],cwd=q,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 try:
  def wait_output(name,text):
   until=time.monotonic()+30
   while time.monotonic()<until:
    if child.poll() is not None:raise AssertionError(child.communicate())
    path=q/'public'/name
    if path.exists() and path.read_text()==text:return
    time.sleep(.02)
   raise AssertionError('watch output deadline '+name)
  wait_output('b.html','first');(q/'content/c.html').write_text('C');(q/'b2.f').write_text(need('c')+output('second'))
  updated=q/'.nift/tracked.next';updated.write_text(json.dumps({'tracked':[item('a'),item('b',build='b2.f',depends=['c']),item('c')]}));os.replace(updated,q/'.nift/tracked.json');wait_output('b.html','second');assert (q/'public/c.html').read_text()=='C';check('build-auto reloads changed script and dependency graph')
 finally:
  child.terminate();child.communicate(timeout=10)
 # Exact preservation on existing projects, nested invocation, both modes and reruns.
 for order in [('rewrite','redesign'),('redesign','rewrite')]:
  q=root/('-'.join(order));q.mkdir();run(q,'init');(q/'investigation').mkdir(exist_ok=True)
  for f in ['README.md','AGENTS.md','HANDOVER.md','REWRITE.md','REDESIGN.md','MIGRATION.md','investigation/STATUS.md']:(q/f).write_bytes(b'USER\x00 bytes\n')
  before={str(f.relative_to(q)):f.read_bytes() for f in q.rglob('*') if f.is_file()};sub=q/'nested';sub.mkdir()
  for mode in [*order,*order]:run(sub,'init','--'+mode,'--'+mode+'-existing=replace')
  for f,content in before.items():assert (q/f).read_bytes()==content,f
  check('existing project preservation/idempotency '+str(order))
 for mode in ['rewrite','redesign']:
  q=root/('fresh-'+mode);q.mkdir();run(q,'init','--'+mode);assert (q/'.nift/tracked.json').exists();assert (q/(mode.upper()+'.md')).exists();check('fresh '+mode)
print('PASS',count,'build pipeline groups')
