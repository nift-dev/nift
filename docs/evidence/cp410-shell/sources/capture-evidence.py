from pathlib import Path
import shutil,json,hashlib
p=Path('.build/cp410-shell');e=Path('docs/evidence/cp410-shell');(e/'sources').mkdir(exist_ok=True);(e/'profiles').mkdir(exist_ok=True)
for f in p.glob('*.py'):shutil.copy2(f,e/'sources'/f.name)
for f in p.glob('*.cpp'):shutil.copy2(f,e/'sources'/f.name)
for pattern in ('*-callgrind.log','*-memcheck.log','*.strace','build.json'):
 for f in p.glob(pattern):shutil.copy2(f,e/'profiles'/f.name)
files={str(f.relative_to(e)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(e.rglob('*')) if f.is_file() and f.name!='manifest.json'}
(e/'manifest.json').write_text(json.dumps({'phase':'IN PROGRESS; prototypes only, no accepted production change','files':files},indent=2)+'\n')
