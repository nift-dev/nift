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
#   the control there (selector: 12.1 called m12's only use of may_call unreachable under B5; that
#   was WRONG and is corrected in 13.1: the call site is reachable (session closures capturing a
#   mutex/channel/atomic parameter) and m12 is observationally equivalent on the tested reachable
#   paths, production's may_call being already true there; m12 is an informational negative
#   control with the reachable-path probe tests/runtime2/cp19/m12_reachable.py
#   (make test-cp19-m12-reachable); shared: the U6 session heap subsumes the element-type
#   unification, identical inference digests).
#   If either mutant starts to differ from the control, the case FAILS so the classification is
#   re-checked.
# B16 (combined-b3-b5.md section 59, owner 2026-11-10), replacing the two failing rows:
# - `loop` is a two-sided D4 SENTINEL. The control's probe is refused by the B5 D4 exposure
#   (141b847e, U6), exactly: `session:current-callable-proof`, 0 executions, no output (mode
#   d4-refused). m3 is unexercised while D4 stands and must behave as the control. The row FAILS
#   as soon as the control is admitted again: resolving D4 must restore the original admitted
#   witness and m3's discrimination instead of passing silently. Not harmless: m3's effect (2) is
#   stale code after a session redefinition (release audit, unresolved admission precision).
# - `native` (m10) is a tripwire plus a combined-mutant witness. B4's redispatch masks m10, so
#   m10 alone and B4 mutant A4 alone must behave as the control; m10+A4 (redispatch removed) must
#   stop in the internal failure, so the hook stays live and detected. The row FAILS if m10 or A4
#   alone starts to differ from the control or if m10+A4 is no longer distinguishable.
p=argparse.ArgumentParser();p.add_argument('--dir',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--suffix',default='');a=p.parse_args();rows=[]
failed=[]
TRIPWIRE={'selector','shared'}
for case,i in [('wrapper',1),('field',2),('loop',3),('alias',4),('schema',5),('order',6),('direct',7),('nested',7),('capture',8),('numeric',9),('native',10),('native-once',11),('selector',12),('shared',13),('shared-scalar',13)]:
 mutant_expect='internal' if case in ('numeric','native','shared','shared-scalar') else 'replayed' if case=='native-once' else 'refused'
 if case=='loop':  # D4 sentinel (section 59)
  plan=[('admission-control','d4-refused',0),('admission-control','admitted',1),(f'admission-m{i}','d4-refused',0),(f'admission-m{i}','admitted',1)]
 elif case=='native':  # m10 tripwire plus the m10+A4 combined witness (section 59)
  plan=[('admission-control','admitted',0),('admission-control','internal',1),(f'admission-m{i}','admitted',0),(f'admission-m{i}','internal',1),
        ('admission-a4','admitted',0),('admission-a4','internal',1),('admission-m10-a4','internal',0),('admission-m10-a4','admitted',1)]
 elif case in TRIPWIRE:  # the mutant must behave exactly as the control (12.1, 13.1)
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
 if case=='loop':
  admitted_again=any(b.startswith('admission-control'+a.suffix+' admitted') for b in bad)
  what=('D4 RESOLVED? the loop control is admitted again: restore the original admitted witness and m3\'s discrimination (section 59)' if admitted_again
        else 'control' if any(b.startswith('admission-control') for b in bad) else 'm3 now differs from the control under D4: re-check (section 59)' if bad
        else 'D4 sentinel: control refused session:current-callable-proof exactly, m3 behaves as the control')
 elif case=='native':
  what=('control' if any(b.startswith('admission-control') for b in bad) else 'TRIPWIRE: m10 or A4 alone now differs from the control, or m10+A4 is no longer detected (section 59)' if bad
        else 'm10 and A4 each behave as the control; m10+A4 stops in the internal failure (combined witness)')
 elif case in TRIPWIRE:
  what='control' if any(b.startswith('admission-control') for b in bad) else 'TRIPWIRE: m%d now differs from the control (re-check b5-refsem.md 12.1/13.1)'%i if bad else 'tripwire m%d: behaves as the control (12.1/13.1)'%i
 else:
  what='control' if any(b.startswith('admission-control'+a.suffix+' admitted') for b in bad) else 'mutant m%d survives'%i if bad else ''
 print(('FAIL' if bad else 'PASS'),'case',case,'m%d'%i,(what+': '+'; '.join(bad)) if bad else what)
 if bad: failed.append(case)
if failed:
 print('FAIL admission mutants: %d of 15 cases fail: %s'%(len(failed),', '.join(failed)));raise SystemExit(1)
print('PASS thirteen compiled admission mutants (plus A4 and m10+A4), exact refusal/state witnesses, bidirectional control discrimination, the D4 sentinel and the m10 combined witness')
