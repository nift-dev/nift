from pathlib import Path
p=Path(__file__).resolve().parent;f=p/'contracts.py';s=f.read_text();a=s.index("for kind in ['named','builtin','method']:");b=s.index('rows=[]\n',a)
extra='''for kind in ['named','html_escape','type','min','method','method-argument']:
 for n in [20,40,48,50,60,80,96,110]:
  expr='f.exists()' if kind=='named' else '1' if kind=='min' else '\"x\"'
  if kind=='method':expr+=' .trim()'*n
  elif kind=='method-argument':
   for _ in range(n):expr='\"x\".replace(\"x\",'+expr+')'
  else:expr=('id(' if kind=='named' else kind+'(')*n+expr+')'*n
  cases.append(('stack-'+kind+'-'+str(n),'f:=file(\"data\");fn(id(x)){return x};print('+expr+')'))
'''
s=s[:a]+extra+s[b:];f.write_text(s)
print('101 exact file and valid native/method recursion oracle cases')
