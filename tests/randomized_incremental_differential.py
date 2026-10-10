#!/usr/bin/env python3
"""Independent fresh-tree oracle; deterministic hostile operation decks.

Modified mode cannot detect bytes changed while an older mtime is preserved;
that operation explicitly requests --all in modified mode. Hash/hybrid must
invalidate it normally. Depends is order-only; generated outputs are also read
through @input so byte propagation is a contractual file dependency.
"""
import argparse, json, os, pathlib, random, shutil, subprocess, tempfile, time, sys
from incremental_state_transitions_adversarial import setup

OPS=['content','template','add','remove','rename','directory_rename','atomic',
     'touch','same_mtime','collision','coarse','tracking','depends','generated',
     'fanout','explicit_dependency','sidecar','sidecar_remove','build_script','symlink_add','symlink_retarget',
     'symlink_remove','recreate_dependency','interrupted','repair','targeted','full','noop']

def tree(root):
 return {p.relative_to(root/'public').as_posix():p.read_bytes() for p in (root/'public').rglob('*') if p.is_file()}
def script_output(text):
 return 'f := file(getenv("NIFT_HOOK_OUTPUT"))\nf.open("w")\nf.write('+json.dumps(text)+')\nf.save()\nf.close()\n'

class CampaignFailure(AssertionError):
 def __init__(self, context, original):
  self.context=context
  super().__init__(f"mode={context['mode']} seed={context['seed']} step={context['step']} history={context['history']}: {type(original).__name__}: {original}; retention_error={context['retention_error']}")

def campaign(binary,seeds,steps,modes,retain,selected_ops=None):
 started=time.monotonic();cases=[];builds=oracles=operations=0
 with tempfile.TemporaryDirectory(prefix='nift-v411-diff-') as td:
  base=pathlib.Path(td);outside=base/'outside';outside.write_bytes(b'outside-sentinel')
  for mode in modes:
   for seed in seeds:
    root=base/f'{mode}-{seed}'
    history=[];step=None;op=None;deck=[];sequence_operations=0
    retained=None;retention_error=None
    try:
     root.mkdir();setup(root,mode,['a','b','c','move/x','move/y'])
     cfg=json.loads((root/'.nift/config.json').read_text());cfg['config'].update({'build-threads':4,'minify-exts':[]});(root/'.nift/config.json').write_text(json.dumps(cfg))
     parts=root/'templates/parts';parts.mkdir();(parts/'shared.html').write_text('shared-0');(parts/'manual.html').write_text('manual-0');(parts/'one.html').write_text('ONE');(parts/'two.html').write_text('TWO')
     (root/'templates/template.html').write_text('@dep("templates/parts/manual.html")\n@input("parts/shared.html")\n@content\n')
     tracking=root/'.nift/tracked.json'
     def entries():return json.loads(tracking.read_text())['tracked']
     def save(items):tracking.write_text(json.dumps({'tracked':items}))
     es=entries()
     for e in es:
      if e['name'] in ['b','c']:e['depends']=['a'];(root/'content'/f'{e["name"]}.html').write_text('@input("public/a.html")')
     save(es)
     def run(*args,expect=True):
      nonlocal builds
      if args[0]=='build':builds+=1
      r=subprocess.run([binary,*args],cwd=root,text=True,capture_output=True,timeout=20)
      if expect and r.returncode:raise AssertionError((args,r.returncode,r.stdout,r.stderr))
      return r
     run('build','--all');rng=random.Random(seed);deck=(selected_ops or OPS).copy();
     if selected_ops is None:rng.shuffle(deck)
     def alter(path,text,preserve=False):
      stamp=path.stat().st_mtime_ns if path.exists() else None;path.write_text(text)
      if preserve and stamp is not None:os.utime(path,ns=(stamp,stamp))
     for step in range(steps):
      op=deck[step] if step<len(deck) else rng.choice(selected_ops or OPS);history.append(op);operations+=1;sequence_operations+=1
      tag=f'{seed}-{step}-{rng.randrange(100000)}';flags=[];es=entries()
      if op=='content':alter(root/'content/a.html','CONTENT-'+tag)
      elif op=='template':alter(root/'templates/template.html','TEMPLATE-'+tag+'\n@dep("templates/parts/manual.html")\n@input("parts/shared.html")\n@content\n')
      elif op=='add':
       name='extra/p'+str(step);p=root/'content'/f'{name}.html';p.parent.mkdir(parents=True,exist_ok=True);p.write_text(tag);run('track',name,name,'templates/template.html')
      elif op=='remove':
       extras=[e['name'] for e in es if e['name'].startswith('extra/')]
       if not extras:
        name='extra/remove';p=root/'content'/f'{name}.html';p.parent.mkdir(parents=True,exist_ok=True);p.write_text(tag);run('track',name,name,'templates/template.html');run('build');extras=[name]
       run('rm',rng.choice(extras))
      elif op=='rename':
       candidates=[e['name'] for e in es if e['name'].startswith(('move/','moved/'))];old=rng.choice(candidates);run('mv',old,old+'r')
      elif op=='directory_rename':
       names=[e['name'] for e in es if e['name'].startswith(('move/','moved/'))]
       old='move' if names[0].startswith('move/') else 'moved';new='moved' if old=='move' else 'move'
       # Use tracked mv to authorize old-output deletion. Raw tracking edits
       # deliberately preserve orphan public output under the repair contract.
       for name in names:run('mv',name,new+name[len(old):])
      elif op=='atomic':
       path=root/'content/a.html';temp=path.with_suffix('.swap');temp.write_text('ATOMIC-'+tag);temp.replace(path)
      elif op=='touch':os.utime(root/'content/a.html',None)
      elif op=='same_mtime':
       alter(root/'content/a.html','PRESERVED-'+tag,True)
       if mode=='modified':flags=['--all']
      elif op=='collision':
       path=root/'content/a.html';alter(path,'COLLISION-'+tag);stamp=(root/'.nift/public/a.info.json').stat().st_mtime_ns;os.utime(path,ns=(stamp,stamp))
      elif op=='coarse':
       path=root/'content/a.html';alter(path,'COARSE-'+tag);stamp=(int(time.time())+1)*10**9;os.utime(path,ns=(stamp,stamp))
      elif op=='tracking':
       for e in es:e['title']='TITLE-'+tag
       save(es)
      elif op=='depends':
       for e in es:
        if e['name']=='c':e['depends']=['a','b'] if e.get('depends')==['a'] else ['a']
       save(es)
      elif op in ['generated','fanout']:alter(root/'content/a.html','PRODUCER-'+tag)
      elif op=='explicit_dependency':alter(parts/'manual.html','MANUAL-'+tag)
      elif op=='sidecar':alter(root/'content/a.build.f',script_output('SIDECAR-'+tag))
      elif op=='sidecar_remove':
       p=root/'content/a.build.f'
       if not p.exists():p.write_text(script_output('BEFORE-REMOVAL'));run('build')
       p.unlink()
      elif op=='build_script':
       for e in es:
        if e['name']=='a':e['build']='main.f'
       save(es);alter(root/'main.f',script_output('SCRIPT-'+tag))
      elif op.startswith('symlink'):
       if os.name=='nt' or os.environ.get('MSYSTEM'):history[-1]+=':native-symlink-unavailable';continue
       link=parts/'alias.html'
       if op=='symlink_remove':
        if link.is_symlink():link.unlink()
        alter(root/'templates/template.html','@dep("templates/parts/manual.html")\n@input("parts/shared.html")\n@content\n')
       else:
        if link.is_symlink():link.unlink()
        link.symlink_to('one.html' if op=='symlink_add' else 'two.html')
        alter(root/'templates/template.html','@input("parts/alias.html")\n@content\n')
      elif op=='recreate_dependency':
       path=parts/'shared.html';path.unlink();path.write_text('RECREATED-'+tag)
      elif op=='interrupted':
       # Native script barrier: SIGKILL only after the hook has written ready.
       if os.name=='nt':history[-1]+=':posix-interruption-unavailable';continue
       (root/'barrier.f').write_text('f := file("ready")\nf.open("w")\nf.write("ready")\nf.save()\nf.close()\nfor(i : range(0,10000)) { if(file("release").exists()) { break }; sleep(1) }\n')
       for e in es:
        if e['name']=='a':e['pre-build']='barrier.f'
       save(es);builds+=1
       child=subprocess.Popen([binary,'build','--all'],cwd=root,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
       try:
        deadline=time.monotonic()+10
        while not (root/'ready').exists() and time.monotonic()<deadline and child.poll() is None:time.sleep(.005)
        assert (root/'ready').exists(),'hook barrier not reached'
       finally:child.kill();child.wait(timeout=3)
       for e in es:e.pop('pre-build',None)
       save(es);(root/'barrier.f').unlink();(root/'ready').unlink();flags=['--repair']
      elif op=='repair':flags=['--repair']
      elif op=='targeted':
       # Targeted build of changed a must include its prerequisite closure, but
       # unrelated consumers remain stale until the subsequent ordinary build.
       alter(root/'content/a.html','TARGET-'+tag);run('build','c')
      elif op=='full':flags=['--all']
      run('build',*flags)
      incremental=tree(root)
      # Copy authored inputs and control files only. Never copy output, page
      # metadata, hashes, unfinished markers, or incremental state into oracle.
      fresh=base/'oracle'
      if fresh.exists():shutil.rmtree(fresh)
      fresh.mkdir()
      for path in root.iterdir():
       if path.name in ['public','.nift']:continue
       dest=fresh/path.name
       if path.is_dir():shutil.copytree(path,dest,symlinks=True)
       elif path.is_symlink():dest.symlink_to(os.readlink(path))
       else:shutil.copy2(path,dest)
      (fresh/'.nift').mkdir();(fresh/'public').mkdir()
      for name in ['config.json','tracked.json','watch.json']:
       if (root/'.nift'/name).exists():shutil.copy2(root/'.nift'/name,fresh/'.nift'/name)
      oracle=subprocess.run([binary,'build','--all'],cwd=fresh,text=True,capture_output=True,timeout=20);builds+=1;oracles+=1
      assert oracle.returncode==0,(oracle.stdout,oracle.stderr)
      expected=tree(fresh)
      assert incremental==expected,dict(missing=sorted(expected.keys()-incremental.keys()),extra=sorted(incremental.keys()-expected.keys()),changed=[k for k in expected.keys()&incremental.keys() if expected[k]!=incremental[k]])
      for ent in entries():
       rel=ent['name']+'.info.json';left=json.loads((root/'.nift/public'/rel).read_text());right=json.loads((fresh/'.nift/public'/rel).read_text())
       for field in ['name','title','template','content','output','build-hooks','dependencies','requires']:
        lv,rv=left.get(field),right.get(field)
        if field in ['dependencies','requires']:lv,rv=sorted(lv or []),sorted(rv or [])
        assert lv==rv,f'public metadata mismatch {rel}:{field}'
      assert (fresh/'.nift/tracked.json').read_bytes()==tracking.read_bytes(),'tracking unexpectedly mutated'
      assert outside.read_bytes()==b'outside-sentinel','outside write'
      # Output set is also checked against authored tracking, independent of oracle.
      assert set(incremental)=={e['name']+'.html' for e in entries()},'stale/missing output'
      if op=='noop':
       info=root/'.nift/public';stamps={p:p.stat().st_mtime_ns for p in info.rglob('*.info.json')};run('build')
       assert tree(root)==incremental,'no-op changed output'
       # Future coarse mtimes intentionally permit conservative rebuilds.
    except Exception as exc:
     if retain:
      try:
       dest=pathlib.Path(retain)/f'{mode}-{seed}-step{step}'
       dest.parent.mkdir(parents=True,exist_ok=True)
       if root.exists():shutil.copytree(root,dest,dirs_exist_ok=True,symlinks=True)
       retained=str(dest)
      except Exception as retention_exc:
       retention_error=f'{type(retention_exc).__name__}: {retention_exc}'
     context=dict(mode=mode,seed=seed,step=step,operation=op,history=list(history),
         sequence_operations=sequence_operations,operations=operations,builds=builds,
         clean_oracles=oracles,completed_sequences=len(cases),deck=list(deck),
         retained=retained,retention_error=retention_error,
         elapsed_seconds=round(time.monotonic()-started,3))
     raise CampaignFailure(context,exc) from exc
    cases.append(dict(mode=mode,seed=seed,operations=history))
 return dict(passed=True,seeds=len(seeds)*len(modes),operations=operations,builds=builds,clean_oracles=oracles,cases=cases,elapsed_seconds=round(time.monotonic()-started,3))

def main():
 p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--seeds',default='0,1,2');p.add_argument('--steps',type=int,default=32);p.add_argument('--modes',default='modified,hash,hybrid');p.add_argument('--output');p.add_argument('--retain');p.add_argument('--operations');a=p.parse_args()
 try:
  result=campaign(str(pathlib.Path(a.nift).resolve()),[int(x) for x in a.seeds.split(',')],a.steps,a.modes.split(','),a.retain,a.operations.split(',') if a.operations else None)
 except CampaignFailure as exc:
  if a.output:
   try:pathlib.Path(a.output).write_text(json.dumps(dict(passed=False,**exc.context),indent=2)+'\n')
   except OSError as output_error:print(f'Could not save failure evidence: {output_error}',file=sys.stderr)
  raise
 if a.output:pathlib.Path(a.output).write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps({k:v for k,v in result.items() if k!='cases'}))
if __name__=='__main__':main()
