from pathlib import Path
import tempfile
p=Path('.build/cp410-shell');root=Path(tempfile.mkdtemp(prefix='nift-shell-'));(root/'files').mkdir()
for i in range(120000):(root/'files'/f'obj-{i:06}.dat').write_bytes(b'x'*128)
(p/'short-root.txt').write_text(str(root));print('Prepared independent short-root 120k fixture:',root)
