from pathlib import Path
p=Path('.build/cp410-shell')
for n in (1000,10000,100000):
 for shape in ('deep','mixed'):
  root=p/f'{shape}-{n}';(root/'files').mkdir(parents=True,exist_ok=True)
  for i in range(n):
   parent=root/'files'
   if shape=='deep':parent=parent.joinpath(*[f'd{k}' for k in range(12)])
   else:parent=parent.joinpath(f'd{i%100:03}',*[f's{k}' for k in range(i%5)])
   parent.mkdir(parents=True,exist_ok=True);(parent/f'obj-{i:06}.dat').write_bytes(b'x'*128)
print('Prepared deep and mixed shapes')
