#!/usr/bin/env python3
"""Deterministic black-box confirmation, independent of random harness/helpers.

Produces evidence, not a green regression claim while the core bug is unfixed.
Modified mode intentionally cannot detect changes preserving an older mtime.
"""
import argparse,json,os,pathlib,shutil,subprocess,tempfile,time

def run(binary,root,*args):
 r=subprocess.run([binary,*args],cwd=root,text=True,capture_output=True,timeout=20)
 if r.returncode:raise AssertionError((args,r.returncode,r.stdout,r.stderr))
 return r.stdout+r.stderr

def output_tree(root):
 return {p.relative_to(root/'public').as_posix():p.read_bytes().decode() for p in (root/'public').rglob('*') if p.is_file()}

def setup(root,mode,kind):
 for name in ['.nift','content','templates','data','public']:(root/name).mkdir(parents=True)
 config={'content-dir':'content/','content-ext':'.html','output-dir':'public/','output-ext':'.html','default-template':'','build-threads':3,'incremental-mode':mode,'minify-exts':[]}
 (root/'.nift/config.json').write_text(json.dumps({'config':config}))
 entries=[dict(name=n,title=n,template='templates/shared.html' if kind=='template' else '') for n in ['a','b','c']]
 for n in ['a','b','c']:(root/'content'/f'{n}.html').write_text('content-'+n)
 if kind=='template':
  dependency=root/'templates/shared.html';dependency.write_text('OLD\n@content\n');target='a';changed='NEW\n@content\n'
 elif kind=='explicit':
  dependency=root/'data/shared.html';dependency.write_text('OLD');target='a';changed='NEW'
  for n in ['a','b','c']:(root/'content'/f'{n}.html').write_text('@dep("data/shared.html")\n@input("data/shared.html")')
 else:
  dependency=root/'content/a.html';target='b';changed='NEW'
  for entry in entries:
   if entry['name'] in ['b','c']:
    entry['depends']=['a'];(root/'content'/f'{entry["name"]}.html').write_text('@input("public/a.html")')
 (root/'.nift/tracked.json').write_text(json.dumps({'tracked':entries}))
 return dependency,target,changed

def fresh_oracle(binary,root,dest):
 dest.mkdir()
 # Authored data only, not any incremental .hash/.info state or public output.
 for name in ['content','templates','data']:shutil.copytree(root/name,dest/name)
 (dest/'.nift').mkdir();(dest/'public').mkdir()
 for name in ['config.json','tracked.json']:shutil.copy2(root/'.nift'/name,dest/'.nift'/name)
 run(binary,dest,'build','--all')
 return output_tree(dest)

def campaign(binary):
 cases=[]
 with tempfile.TemporaryDirectory(prefix='nift-shared-baseline-') as td:
  base=pathlib.Path(td)
  for mode in ['modified','hash','hybrid']:
   for kind in ['template','explicit','generated']:
    for timing in ['advance','preserve','collision']:
     root=base/f'{mode}-{kind}-{timing}';root.mkdir();dependency,target,changed=setup(root,mode,kind)
     run(binary,root,'build','--all');before=output_tree(root);old_mtime=dependency.stat().st_mtime_ns
     omitted='c' if kind=='generated' else 'b';metadata=root/'.nift/public'/f'{omitted}.info.json';old_metadata=metadata.read_bytes()
     if timing=='collision':
      # Simulate a shared coarse clock quantum, not an older timestamp for C.
      infos=list((root/'.nift/public').glob('*.info.json'))
      quantum=max(p.stat().st_mtime_ns for p in infos)
      for p in infos:os.utime(p,ns=(quantum,quantum))
     stored_before={str(p.relative_to(root)):p.read_text() for p in (root/'.nift').rglob('*.hash')}
     dependency.write_text(changed)
     if timing=='preserve':stamp=old_mtime
     elif timing=='collision':stamp=metadata.stat().st_mtime_ns
     else:stamp=max(time.time_ns(),metadata.stat().st_mtime_ns+1_000_000)
     os.utime(dependency,ns=(stamp,stamp))
     target_log=run(binary,root,'build',target)
     after_target=output_tree(root)
     omitted_metadata_unchanged=old_metadata==metadata.read_bytes()
     stored_after={str(p.relative_to(root)):p.read_text() for p in (root/'.nift').rglob('*.hash')}
     status=run(binary,root,'status');ordinary=run(binary,root,'build');incremental=output_tree(root)
     oracle=fresh_oracle(binary,root,base/f'oracle-{root.name}')
     no_op=run(binary,root,'build')
     contractual=not (mode=='modified' and timing=='preserve')
     cases.append(dict(mode=mode,kind=kind,timing=timing,target=target,omitted=omitted,
       contractual=contractual,incremental_equals_fresh=incremental==oracle,
       before=before,after_target=after_target,incremental=incremental,fresh=oracle,
       omitted_metadata_unchanged_after_target=omitted_metadata_unchanged,
       stored_hashes_before=stored_before,stored_hashes_after_target=stored_after,
       target_log=target_log,status_after_target=status,ordinary_log=ordinary,no_op_log=no_op))
 return cases

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--output',required=True);p.add_argument('--require-clean',action='store_true');a=p.parse_args()
 cases=campaign(str(pathlib.Path(a.nift).resolve()));failures=[x for x in cases if x['contractual'] and not x['incremental_equals_fresh']]
 result=dict(classification='CONFIRMED CORE CORRECTNESS BUG' if failures else 'NOT REPRODUCED',cases=cases,contractual_failures=len(failures),cases_run=len(cases))
 pathlib.Path(a.output).write_text(json.dumps(result,indent=2)+'\n')
 print(result['classification'],len(failures),'contractual failures of',len(cases),'cases')
 for x in failures:print(x['mode'],x['kind'],x['timing'])
 if a.require_clean:assert not failures, 'contractual divergence from clean reconstruction'
