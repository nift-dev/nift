#!/usr/bin/env python3
"""Serialized native certification; never invokes release or publish workflows."""
import argparse,hashlib,json,os,pathlib,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--e3b',required=True);p.add_argument('--e2',required=True);p.add_argument('--publisher',required=True,type=pathlib.Path);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parent.parent;out=root/'.build/identity';out.mkdir(parents=True,exist_ok=True)
suffix='.exe' if os.name=='nt' else '';binary=root/('nift'+suffix)
def call(name,command,cwd=root):
 with (out/(name+'.log')).open('w') as log:
  result=subprocess.run(command,cwd=cwd,stdout=log,stderr=subprocess.STDOUT)
 if result.returncode:print((out/(name+'.log')).read_text());raise SystemExit(result.returncode)
def test(name,script,*args):call(name,[sys.executable,'tests/'+script,'--nift',str(binary),*map(str,args)])
publisher = a.publisher.resolve()
publisher_sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=publisher, text=True).strip()
assert publisher_sha == '6593892fc6d2ca3d98190fb33af1b14ee31763f5', publisher_sha
call('historical-build-probes', [sys.executable, 'tests/build_identity_probes.py', '--project-root', str(publisher)])
historical_tools = publisher / '.build/identity'
historical_nift = publisher / ('nift' + suffix)
probe = historical_tools / ('probe' + suffix)
call('spelling', [str(historical_tools / ('spelling' + suffix))])
historical_script = publisher / 'tests/identity_publication_contracts.py'
if os.name == 'nt':
    # Adapt only the pinned test's ACL setup. Its recursive icacls invocation
    # can deny traversal before finishing and leave the temporary tree locked.
    source = historical_script.read_text(encoding='utf-8')
    start = source.index(' # Real inaccessible parent.')
    end = source.index(' # A fresh parent containment observation', start)
    original = source[start:end]
    probe_assertion = "  r=run(probe,root,'native',str(root),'data/plain.txt');assert r.returncode==0,(r.stdout,r.stderr)"
    status_assertion = "  equal_status(root,'actual inaccessible parent parity')"
    assert probe_assertion in original.splitlines() and status_assertion in original.splitlines()
    preamble = ("import sys\n"
                f"sys.path[:0] = [{str(historical_script.parent)!r}, {str(root / 'tests')!r}]\n"
                "from identity_windows_acl import denied_read, assert_denied\n")
    replacement = (' blocked=root/"data"\n with denied_read(blocked):\n  assert_denied(blocked)\n'
                   + probe_assertion.replace("'native'", "'native-denied'") + '\n' + status_assertion + '\n')
    historical_script = out / 'historical_publication_windows.py'
    adapted = preamble + source[:start] + replacement + source[end:]
    compile(adapted, str(historical_script), 'exec')
    historical_script.write_text(adapted, encoding='utf-8')
call('historical-publication', [
    sys.executable, str(historical_script), '--nift', str(historical_nift),
    '--baseline', str(pathlib.Path(a.e3b).resolve()), '--probe', str(probe),
    '--fault-nift', str(historical_tools / ('nift-fault' + suffix)),
    '--output', str(out / 'historical-publication.json'),
], cwd=publisher)

test('default-fallback', 'identity_default_fallback.py', '--output', out/'default-fallback.json')
test('sidecar-state','sidecar_state_contracts.py','--baseline',a.e2,'--output',out/'sidecar-state.json')
test('sidecar-platform','sidecar_platform_contracts.py','--output',out/'sidecar-platform.json','--export',out/'sidecar-export.json')
test('snapshots','consumer_dependency_snapshots.py','--output',out/'snapshots.json')
test('targeted','shared_dependency_targeted_reproducer.py','--require-clean','--output',out/'targeted.json')
test('randomized','randomized_incremental_differential.py','--seeds','0,1,2','--steps','32','--output',out/'randomized.json')
test('transitions','incremental_state_transitions_adversarial.py')
test('pagination','pagination_incremental_equivalence.py')
receipt={'source_sha':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'platform':sys.platform,'passed':True,'historical_publisher_sha':publisher_sha,'historical_publisher_binary_sha256':hashlib.sha256(historical_nift.read_bytes()).hexdigest(),'e3b_binary_sha256':hashlib.sha256(pathlib.Path(a.e3b).read_bytes()).hexdigest(),'e2_binary_sha256':hashlib.sha256(pathlib.Path(a.e2).read_bytes()).hexdigest()}
(out/'receipt.json').write_text(json.dumps(receipt,indent=2));print(json.dumps(receipt,indent=2))
