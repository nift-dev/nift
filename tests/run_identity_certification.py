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
call('historical-publication', [
    sys.executable, 'tests/identity_publication_contracts.py', '--nift', str(historical_nift),
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
