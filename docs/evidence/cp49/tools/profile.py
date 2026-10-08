import json,subprocess
from pathlib import Path
b=Path(__file__).resolve().parents[4]/'.build/cp49';binary=Path('nift').resolve();rows=json.loads((b/'probes.json').read_text());profile=b/'profiles';profile.mkdir(exist_ok=True)
for r in rows:
 if r['n'] not in [0,2000]:continue
 name=r['name'];
 if (profile/(name+'-inclusive.txt')).exists():continue
 out=profile/(name+'.callgrind');log=profile/(name+'.log')
 cmd=['valgrind','--tool=callgrind','--callgrind-out-file='+str(out),'--collect-jumps=yes',str(binary),r['path']]
 p=subprocess.run(cmd,capture_output=True,text=True,timeout=180);log.write_text('COMMAND '+repr(cmd)+'\n'+p.stdout+p.stderr);assert p.returncode==0,(name,p.stderr);assert '\n'.join(p.stdout.splitlines()[:-1])==r['expected'],name
 for mode in ['self','inclusive']:
  a=subprocess.run(['callgrind_annotate','--threshold=95','--inclusive='+('yes' if mode=='inclusive' else 'no'),str(out)],capture_output=True,text=True,check=True);(profile/(name+'-'+mode+'.txt')).write_text(a.stdout)
 print('PROFILE',name,flush=True)
