from pathlib import Path
import hashlib,json,shutil
p=Path('.build/cp410-shell');e=Path('docs/evidence/cp410-shell')
o=Path('/home/nick/Repositories/nift/nift-experiments/lab-evidence/benchmarks/shell/20261009-v4100-shell-expanded')
manifest={str(f.relative_to(o)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(o.rglob('*')) if f.is_file()}
(e/'oracle-freeze.json').write_text(json.dumps({'root':str(o),'files':manifest,'identity':json.loads((o/'run-identity.json').read_text()),'policy':'Read-only external oracle. No official reruns or changes.'},indent=2)+'\n')
shutil.copy2('nift',p/'baseline-nift')
for n in (1000,10000,100000):
 root=p/('flat-'+str(n));(root/'files').mkdir(parents=True,exist_ok=True)
 d=max(20000 if n==100000 else n//5,1)
 for i in range(n+d): (root/'files'/f'obj-{i:06d}.dat').write_bytes(b'x'*128)
 (root/'glob.f').write_text('paths := ls("files/**/*.dat"); print(paths.size())\n')
(e/'baseline.json').write_text(json.dumps({'sha256':hashlib.sha256((p/'baseline-nift').read_bytes()).hexdigest(),'commit':'8df185e2095037512d2d7315204cd5e3991a65be','flat_expected_counts':[1200,12000,120000]},indent=2)+'\n')
print('Frozen',len(manifest),'oracle files; prepared three local traversal scales')
