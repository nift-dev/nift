import argparse,json,subprocess
from pathlib import Path
# B5 fix-up: every case runs and is reported (the harness used to stop at the first failing
# assertion, which since the Phase A guard hid every case after it, including surviving mutants).
# Exit 1 when any case fails.
# B5 fix-up 2 (b5-refsem.md 12.1):
# - m13 is caught by the new `shared-scalar` case (sibling closures sharing a captured string
#   binding; the mutant stops in an internal failure where production refuses before any effect).
# - `shared` (m13) and `selector` (m12) no longer discriminate on B5. They stay as control
#   witnesses, and their mutant rows are TRIPWIRES: the mutant is expected to behave exactly like
#   the control there (selector: m12's only use of may_call is unreachable under B5, proof in 12.1;
#   shared: the U6 session heap subsumes the element-type unification, identical inference digests).
#   If either mutant starts to differ from the control, the case FAILS so the proof is re-checked.
# - Still failing, deliberately not waived: `loop` (control refused: D4, m3 has no B5 witness) and
#   `native` (m10 survives, pre-existing on 17f7964c: release-blocker audit).
p=argparse.ArgumentParser();p.add_argument('--dir',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--suffix',default='');a=p.parse_args();rows=[]
failed=[]
TRIPWIRE={'selector','shared'}
for case,i in [('wrapper',1),('field',2),('loop',3),('alias',4),('schema',5),('order',6),('direct',7),('nested',7),('capture',8),('numeric',9),('native',10),('native-once',11),('selector',12),('shared',13),('shared-scalar',13)]:
 mutant_expect='internal' if case in ('numeric','native','shared','shared-scalar') else 'replayed' if case=='native-once' else 'refused'
 if case in TRIPWIRE:  # the mutant must behave exactly as the control (12.1)
  plan=[('admission-control','admitted',0),('admission-control',mutant_expect,1),(f'admission-m{i}','admitted',0),(f'admission-m{i}',mutant_expect,1)]
 else:
  plan=[('admission-control','admitted',0),(f'admission-m{i}',mutant_expect,0),('admission-control',mutant_expect,1),(f'admission-m{i}','admitted',1)]
 bad=[]
 for binary,expect,status in plan:
  r=subprocess.run([str(a.dir/(binary+a.suffix)),case,expect],text=True,capture_output=True,timeout=30)
  row=dict(binary=binary,case=case,expect=expect,status=r.returncode,stdout=r.stdout,stderr=r.stderr,tripwire=case in TRIPWIRE);rows.append(row)
  a.out.write_text(json.dumps(rows,indent=2)+'\n')
  good=r.returncode==status and ((f'WITNESS {case} {expect}\n' in r.stdout) if status==0 else ('CHECK failed:' in r.stderr and 'WITNESS' not in r.stdout))
  if not good: bad.append('%s %s: exit %d (want %d)'%(binary+a.suffix,expect,r.returncode,status))
 if case in TRIPWIRE:
  what='control' if any(b.startswith('admission-control') for b in bad) else 'TRIPWIRE: m%d now differs from the control (re-check b5-refsem.md 12.1)'%i if bad else 'tripwire m%d: behaves as the control (12.1)'%i
 else:
  what='control' if any(b.startswith('admission-control'+a.suffix+' admitted') for b in bad) else 'mutant m%d survives'%i if bad else ''
 print(('FAIL' if bad else 'PASS'),'case',case,'m%d'%i,(what+': '+'; '.join(bad)) if bad else what)
 if bad: failed.append(case)
if failed:
 print('FAIL admission mutants: %d of 15 cases fail: %s'%(len(failed),', '.join(failed)));raise SystemExit(1)
print('PASS thirteen compiled admission mutants, exact refusal/state witnesses and bidirectional control discrimination')
