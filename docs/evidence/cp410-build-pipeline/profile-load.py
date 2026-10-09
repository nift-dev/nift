import pathlib,subprocess,json
p=pathlib.Path('.build/cp410-pipeline');root=pathlib.Path(json.loads((p/'baseline.json').read_text())['root']);rows=[]
for label,b in [('before',p/'nift-before'),('after',pathlib.Path('nift'))]:
 trace=(p/(label+'-status.callgrind')).resolve();r=subprocess.run(['valgrind','--tool=callgrind','--callgrind-out-file='+str(trace),str(b.resolve()),'status'],cwd=root,capture_output=True,text=True);assert r.returncode==0;(p/(label+'-status-profile.log')).write_text(r.stderr);rows.append({'label':label,'instructions':int(next(l.split(':')[1] for l in trace.read_text().splitlines() if l.startswith('summary:')))})
(p/'load-instructions.json').write_text(json.dumps(rows,indent=2));print(rows)
