from pathlib import Path
import subprocess,json
p=Path('.build/cp410-shell').resolve();codes=['print(1)','print("x")','print(len("abc"))','print(to_string(1))','print("abc".to_upper())','print("aAaA".replace("A","x"))','print("é中".to_upper())','print("abc".replace("","x"))','print(unknown(1))','print(exists("missing"))','print(ls("missing/*").size())','print(error("x").message)','print(print())','print( len("abc") )','print("alpha".to_upper().replace("A","a"))']
rows=[]
for code in codes:
 outputs=[]
 for label in ('baseline','dispatch'):
  r=subprocess.run([str(p/(label+'-nift')),'-e',code],cwd=p,capture_output=True,text=True);outputs.append((r.returncode,r.stdout,r.stderr))
 assert outputs[0]==outputs[1],(code,outputs)
 rows.append({'source':code,'result':outputs[0]})
Path('docs/evidence/cp410-shell/dispatch-parity.json').write_text(json.dumps(rows,indent=2,ensure_ascii=False)+'\n');print('PASS',len(rows),'exact dispatch success/error comparisons')
