from pathlib import Path
import shutil,tarfile,json,hashlib
p=Path(__file__).parent.resolve();d=Path('docs/evidence/cp410-silverback');d.mkdir(parents=True,exist_ok=True)
for f in p.iterdir():
 if f.is_file() and f.suffix in ['.json','.md','.py','.log','.txt','.strace'] and not f.name.startswith(('hosted-','final-','watch-final','dispatch-final')):
  shutil.copy2(f,d/f.name)
for name in ['instructions.json','resources.json','profile-final.log','resources.log']:shutil.copy2(p/'object'/name,d/('object-'+name))
shutil.copy2(p/'hosted-959869d-failure.log',d/'windows-relative-guard-failure.log')
(d/'windows-relative-guard-fix.json').write_text(json.dumps({'failed_run':37887584492,'failed_sha':'959869d','cause':'Same cygpath wildcard-to-U+F02A transport in a second fixture, absolute relative-scaling guard','fix':'Convert directory, append wildcard after conversion; feed UTF-8 stdin; identify failing pattern in assertion','local_guard':'PASS 32/128/512 plus absolute/empty; instrumentation expectation unchanged','runtime_change':False},indent=2)+'\n')
with tarfile.open(d/'private-prototypes-and-raw-profiles.tar.gz','w:gz') as archive:
 for f in sorted(p.rglob('*')):
  if f.is_file() and f.suffix in ['.cpp','.h','.py','.callgrind','.memcheck','.strace','.json','.md','.log','.txt'] and not f.name.startswith(('hosted-','final-','watch-final','dispatch-final')) and f.name!='archive.py':archive.add(f,arcname=str(f.relative_to(p)))
manifest={str(f.relative_to(d)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(d.rglob('*')) if f.is_file() and f.name!='manifest.json'};(d/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print('Archived',len(manifest),'review files')
