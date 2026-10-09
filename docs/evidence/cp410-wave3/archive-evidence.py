from pathlib import Path
import json,hashlib,shutil,tarfile
s=Path(__file__).parent.resolve();d=Path('docs/evidence/cp410-wave3');d.mkdir(parents=True,exist_ok=True)
for file in s.iterdir():
 if file.is_file() and file.suffix in ['.json','.log','.py','.sh','.callgrind','.memcheck','.cpp','.inc'] and file.name not in ['experiment-status.json']:
  shutil.copy2(file,d/file.name)
shutil.copytree(s/'safety',d/'safety',dirs_exist_ok=True)
with tarfile.open(d/'rejected-architecture-prototypes.tar.gz','w:gz') as archive:
 for dirname in ['snapshot']:
  for f in sorted((s/dirname).rglob('*')):
   if f.is_file() and not f.is_symlink() and f.suffix in ['.cpp','.h','.py','.inc','.json','.log']:archive.add(f,arcname=str(f.relative_to(s)))
with tarfile.open(d/'dropped-jsonic-investigation.tar.gz','w:gz') as archive:
 for dirname in ['jsonic','jsonic-stack-membership-v1']:
  if not (s/dirname).exists():continue
  for f in sorted((s/dirname).rglob('*')):
   if f.is_file() and f.suffix in ['.cpp','.h','.py','.json','.log','.callgrind','.memcheck','.patch']:archive.add(f,arcname=str(f.relative_to(s)))
freeze=json.loads(Path('docs/evidence/cp410-shell/oracle-freeze.json').read_text());root=Path(freeze['root']);checks={name:hashlib.sha256((root/name).read_bytes()).hexdigest()==sha for name,sha in freeze['files'].items()};assert all(checks.values());(d/'frozen-evidence-check.json').write_text(json.dumps({'root':str(root),'checked_files':len(checks),'unchanged':True},indent=2)+'\n')
print('Archived certification, rejected models, cancelled dependency work; frozen files unchanged:',len(checks))
