import argparse,json,subprocess
from pathlib import Path
# B5 fix-up: every case runs and is reported (the harness used to stop at the first failing
# assertion, which since the Phase A guard hid every case after it, including surviving mutants).
# Exit 1 when any case fails. Known state on B5 (b5-refsem.md 11.4): `loop` control refused
# (precision gap; m3 has no B5 witness), mutants m10 (pre-existing: survives on 17f7964c too),
# m12 and m13 survive (B5: no witness found in a 2,144-session differential). Not waived: listed.
p=argparse.ArgumentParser();p.add_argument('--dir',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--suffix',default='');a=p.parse_args();rows=[]
failed=[]
for case,i in [('wrapper',1),('field',2),('loop',3),('alias',4),('schema',5),('order',6),('direct',7),('nested',7),('capture',8),('numeric',9),('native',10),('native-once',11),('selector',12),('shared',13)]:
 mutant_expect='internal' if case in ('numeric','native','shared') else 'replayed' if case=='native-once' else 'refused'
 bad=[]
 for binary,expect,status in [('admission-control','admitted',0),(f'admission-m{i}',mutant_expect,0),('admission-control',mutant_expect,1),(f'admission-m{i}','admitted',1)]:
  r=subprocess.run([str(a.dir/(binary+a.suffix)),case,expect],text=True,capture_output=True,timeout=30)
  row=dict(binary=binary,case=case,expect=expect,status=r.returncode,stdout=r.stdout,stderr=r.stderr);rows.append(row)
  a.out.write_text(json.dumps(rows,indent=2)+'\n')
  good=r.returncode==status and ((f'WITNESS {case} {expect}\n' in r.stdout) if status==0 else ('CHECK failed:' in r.stderr and 'WITNESS' not in r.stdout))
  if not good: bad.append('%s %s: exit %d (want %d)'%(binary+a.suffix,expect,r.returncode,status))
 what='control' if any(b.startswith('admission-control'+a.suffix+' admitted') for b in bad) else 'mutant m%d survives'%i if bad else ''
 print(('FAIL' if bad else 'PASS'),'case',case,'m%d'%i,(what+': '+'; '.join(bad)) if bad else '')
 if bad: failed.append(case)
if failed:
 print('FAIL admission mutants: %d of 14 cases fail: %s'%(len(failed),', '.join(failed)));raise SystemExit(1)
print('PASS thirteen compiled admission mutants, exact refusal/state witnesses and bidirectional control discrimination')
