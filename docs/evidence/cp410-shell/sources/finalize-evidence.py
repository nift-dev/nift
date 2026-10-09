from pathlib import Path
import shutil,json,hashlib,tarfile,subprocess
p=Path('.build/cp410-shell');e=Path('docs/evidence/cp410-shell');(e/'sources').mkdir(exist_ok=True);(e/'profiles').mkdir(exist_ok=True)
for pattern in ('*.py','*.cpp','*.sh'):
 for f in p.glob(pattern):shutil.copy2(f,e/'sources'/f.name)
for pattern in ('*-callgrind.log','*-memcheck.log','*.strace','final-save-*.log','build.json','sandbox-native-test-failure.log'):
 for f in p.glob(pattern):shutil.copy2(f,e/'profiles'/f.name)
shutil.copytree(p/'safety',e/'safety',dirs_exist_ok=True)
assert (p/'safety/progress.log').read_text().endswith('COMPLETE\n')
with tarfile.open(e/'raw-profiles.tar.gz','w:gz') as tar:
 for f in sorted(p.glob('*')):
  if f.is_file() and f.suffix in ('.callgrind','.memcheck','.strace','.log','.json'):tar.add(f,arcname=f.name)
receipt=json.loads((e/'oracle-freeze.json').read_text());root=Path(receipt['root']);assert all(hashlib.sha256((root/n).read_bytes()).hexdigest()==v for n,v in receipt['files'].items())
(e/'oracle-final-verification.json').write_text(json.dumps({'series':'20261009-v4100-shell-expanded','pinned_files':len(receipt['files']),'unchanged':True,'official_rerun':False},indent=2)+'\n')
(e/'candidate.patch').write_text(subprocess.check_output(['git','diff','--','src/ParserExpression.cpp','src/ParserHelpers.cpp','Makefile','.github/workflows/checkpoint-10-cross-platform.yml','tests/v49_glob_key_guard.py'],text=True))
(e/'README.md').write_text('# Expanded shell performance investigation\n\nSee [the completed review](report.md). The frozen official series is unchanged. The final glob-only candidate is local, uncommitted and unpushed; full local checks pass, while its new hosted cross-platform certificate awaits review/publication. All ten accepted-baseline hosted walls are green. The dispatch experiment was rejected.\n\nFinal JSON receipts identify ordinary production binaries. `dispatch-experiment-*` receipts preserve the rejected candidate; initial traversal-baseline timing and exploratory write timing are explicitly excluded from acceptance conclusions. `sources` contains fixtures/generators/controls, `profiles` and `raw-profiles.tar.gz` preserve diagnostics, and `safety` contains the final certificate.\n')
(e/'manifest.json').write_text(json.dumps({'status':'STOP FOR REVIEW','accepted_baseline_commit':'8df185e2095037512d2d7315204cd5e3991a65be','candidate_sha256':hashlib.sha256((p/'candidate-nift').read_bytes()).hexdigest(),'retained_runtime':['remove intermediate glob sorting','canonicalize shared relative base once; keep per-match resolution','reserve result vector and avoid relative path copy'],'rejected_runtime':['native call_args predicate reorder'],'committed':False,'pushed':False,'hosted_baseline_passed':10,'hosted_candidate':'NOT RUN; review boundary','files':{str(f.relative_to(e)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(e.rglob('*')) if f.is_file() and f.name!='manifest.json'}},indent=2)+'\n')
p=Path('.build/cp410-capture-frame');e=Path('docs/evidence/cp410-capture-frame');(e/'sources').mkdir(exist_ok=True)
for pattern in ('*.py','*.cpp','*.h'):
 for f in p.glob(pattern):shutil.copy2(f,e/'sources'/f.name)
for directory,names in [('phases',('Phases.h','Alloc.cpp','build.json','Parser.h','Parser.cpp','ParserExpression.cpp')),('jsonic',('json.h','Phases.h','Alloc.cpp','probe.cpp','baseline-probe.cpp','canonical.json'))]:
 (e/'sources'/directory).mkdir(exist_ok=True)
 for name in names:shutil.copy2(p/directory/name,e/'sources'/directory/name)
with tarfile.open(e/'raw-profiles.tar.gz','w:gz') as tar:
 for f in sorted(p.rglob('*')):
  if f.is_file() and f.suffix in ('.callgrind','.memcheck','.log','.f','.json'):tar.add(f,arcname=str(f.relative_to(p)))
canonical=json.loads((p/'jsonic/canonical.json').read_text());assert hashlib.sha256(Path(canonical['header']).read_bytes()).hexdigest()==canonical['sha256']
(e/'manifest.json').write_text(json.dumps({'status':'STOP FOR REVIEW; architecture investigation only','production_architecture_modified':False,'production_jsonic_modified':False,'ordinary_allocator_model_binary_sha256':hashlib.sha256((p/'env-model-plain').read_bytes()).hexdigest(),'files':{str(f.relative_to(e)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(e.rglob('*')) if f.is_file() and f.name!='manifest.json'}},indent=2)+'\n')
print('PASS final evidence, source snapshots, raw archives, certificates and frozen oracle verification')
