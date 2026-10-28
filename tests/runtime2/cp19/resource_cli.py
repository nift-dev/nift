"""Cross-command resource contracts; differential parity is asserted only where normative."""
import argparse,json,os,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--nift',required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();binary=str(Path(a.nift).resolve());a.out.parent.mkdir(parents=True,exist_ok=True);rows=[]
cases=[
 dict(name='file',source='f:=file("p");f.open("r")\nf.read_all()\nf.close()\nexit\n',errors=0,needle='original',parity=True),
 dict(name='aliases',source='f:=file("p");f.open("r");g:=f\nf = file("new")\ng.read_all()\ng.close()\nexit\n',errors=0,needle='original',parity=True),
 dict(name='stream-input',source='s:=ifstream("p")\ns.read_line()\ns.close()\nexit\n',errors=0,needle='original',parity=True),
 dict(name='stream-output',source='s:=ofstream("out")\ns.write("committed")\ns.close()\nexit\n',errors=0,out='committed',parity=True),
 dict(name='rc',rc='f:=file("p");f.open("r")',source='f.read_all()\nf.close()\nexit\n',errors=0,needle='original',parity=True),
 dict(name='initial-file',contract='CP19 single VM session spans initial script and -i; legacy ends the initial resource context',initial='f:=file("p");f.open("r")',source='f.read_all()\nf.close()\nexit\n',errors=0,needle='original'),
 dict(name='inline',contract='CP19 single VM session spans initial inline script and -i; legacy ends the initial resource context',inline='f:=file("p");f.open("r")',source='f.read_all()\nf.close()\nexit\n',errors=0,needle='original'),
 dict(name='cwd',source='f:=file("p");f.open("r")\ncd sub\nf.read_all()\nf.close()\nexit\n',errors=0,needle='original',parity=True),
 dict(name='surviving-error',source='f:=file("p");f.open("r")\nthrow error("primary")\nf.read_all()\nf.close()\nexit\n',errors=1,needle='original',diagnostic='primary',parity=True),
 dict(name='temporary-unwind',source='fn(temp()){f:=file("p");f.open("rw");f.write("dirty");throw error("primary")};temp()\ntick:=1\ntick\nexit\n',errors=1,needle='1\n',diagnostic='primary',contract='D-056 exceptional abandonment'),
 dict(name='normal-abandonment',source='fn(temp()){f:=file("p");f.open("r")};temp()\ntick:=1\ntick\nexit\n',errors=1,needle='1\n',diagnostic='managed file left open',contract='D-056 normal abandonment'),
 dict(name='completed-before-error',source='fn(temp()){f:=file("p");f.open("r")};temp();throw error("primary")\ntick:=1\ntick\nexit\n',errors=2,needle='1\n',diagnostic='primary',contract='D-056 preserve primary and independent completed obligation'),
 dict(name='refusal-atomic',source='f:=file("p");f.open("r")\nf.close();a:=[f]\nf.read_all()\nf.close()\nexit\n',errors=1,needle='original',diagnostic='NIFT_VM session refused: host-handle-flow',refusals=1,contract='D-054 and inherited W13 positions'),
 dict(name='capture-refusal',source='f:=file("p");f.open("r")\nfn(capture()){f.close()}\nf.read_all()\nf.close()\nexit\n',errors=1,needle='original',diagnostic='NIFT_VM session refused: host-handle-flow',refusals=1,contract='Inherited W13 capture barrier across units'),
 dict(name='unrelated-widening',source='f:=file("p");f.open("r");a:=[1]\na.push("wide")\nf.read_all()\nf.close()\nexit\n',errors=0,needle='original',parity=True),
 dict(name='end-obligation',source='f:=file("p");f.open("rw");f.write("dirty")\nexit\n',errors=1,status=1,diagnostic='unsaved changes discarded',contract='D-056 session finalization'),
]
for case in cases:
 with tempfile.TemporaryDirectory(prefix='cp19-resource-cli-') as td:
  root=Path(td);oracle=None;vm_oracle=None
  for mode in ['0','1','require']:
   (root/'p').write_text('original\n');(root/'sub').mkdir(exist_ok=True);(root/'out').unlink(missing_ok=True)
   if 'rc' in case:(root/'.niftrc').write_text(case['rc'])
   else:(root/'.niftrc').unlink(missing_ok=True)
   stats=root/'stats';stats.unlink(missing_ok=True)
   argv=[binary,'-i']
   if 'initial' in case:(root/'initial.f').write_text(case['initial']);argv.append(str(root/'initial.f'))
   if 'inline' in case:argv+=['-c',case['inline']]
   env={k:v for k,v in os.environ.items() if not k.startswith('NIFT_')};env.update(HOME=td,NIFT_VM=mode,NIFT_VM_STATS=str(stats),NO_COLOR='1')
   r=subprocess.run(argv,cwd=root,env=env,input=case['source'],text=True,capture_output=True,timeout=45)
   counters=[json.loads(x) for x in stats.read_text().splitlines()] if stats.exists() else []
   row=dict(case=case['name'],mode=mode,contract=case.get('contract','legacy parity'),status=r.returncode,stdout=r.stdout,stderr=r.stderr,stats=counters,p=(root/'p').read_text(),out=(root/'out').read_text() if (root/'out').exists() else None);rows.append(row);a.out.write_text(json.dumps(rows,indent=2)+'\n')
   channels=(r.returncode,r.stdout,r.stderr,row['p'],row['out'])
   if mode=='0':oracle=channels;continue
   assert r.returncode==case.get('status',0),row
   assert sum(x.startswith('error: ') for x in r.stderr.splitlines())==case['errors'],row
   assert case.get('needle','') in r.stdout and case.get('diagnostic','') in r.stderr,row
   assert row['p']=='original\n' and row['out']==case.get('out'),row
   if case.get('parity'):assert channels==oracle,(oracle,row)
   if vm_oracle is None:vm_oracle=channels
   else:assert channels==vm_oracle,row
   expected=len(case['source'].splitlines())-1+int(bool(case.get('rc') or case.get('initial') or case.get('inline')))
   assert len(counters)==expected,row
   refused=sum(x['vm_executions']==0 for x in counters);assert refused==case.get('refusals',0),row
   assert all(x['legacy_executions']==0 and x['vm_executions'] in [0,1] for x in counters),row
print('PASS resource CLI: 16 cases, explicit D-056/W13 classifications, both VM modes and legacy observations')
