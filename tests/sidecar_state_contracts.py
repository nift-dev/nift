from pathlib import Path
import tempfile,subprocess,sys,json,os,copy
sys.path.insert(0,str(Path(__file__).resolve().parent))
from shared_dependency_targeted_reproducer import setup
from consumer_dependency_snapshots import python_tool
import argparse
a=argparse.ArgumentParser();a.add_argument('--nift',required=True);a.add_argument('--baseline',required=True);a.add_argument('--output',required=True);args=a.parse_args();bins={'e2':str(Path(args.baseline).resolve()),'e3b':str(Path(args.nift).resolve())};rows=[]
def run(d,label,*args):return subprocess.run([bins[label],*args],cwd=d,capture_output=True,text=True)
def fixture(d,mode):
 d.mkdir();setup(d,mode,'explicit')
 for n in ['one','two','three']:(d/f'data/{n}.txt').write_text(n)
 for f in (d/'data').iterdir():os.utime(f,ns=(1600000000000000000,)*2)
 return d/'content/a.deps.json'
with tempfile.TemporaryDirectory(prefix='e3b-contract-') as td:
 root=Path(td)
 # Both mismatch orderings, multiple changes, and a final authoritative match.
 for mode in ['modified','hash','hybrid']:
  for case,initial,pre,post in [('ABA','one',['two'],['one']),('BAB','two',['one'],['two']),('multiple','one',['three','two'],['three','one']),('stableB','two',['two'],['two']),('changed-final','one',['two'],['two'])]:
   observed=[]
   for label in ['e2','e3b']:
    d=root/f'{mode}-{case}-{label}';side=fixture(d,mode)
    raw=lambda n:json.dumps({'dependencies':[f'data/{n}.txt']})
    side.write_text(raw(initial))
    for phase,changes in [('pre',pre),('post',post)]:
     (d/f'{phase}.py').write_text('from pathlib import Path\n'+''.join('Path("content/a.deps.json").write_text('+repr(raw(n))+')\n' for n in changes))
     (d/f'{phase}.f').write_text('r := run('+json.dumps(python_tool())+', '+json.dumps(phase+'.py')+')\n')
    tracked=json.loads((d/'.nift/tracked.json').read_text());tracked['tracked'][0].update({'pre build':'pre.f','post build':'post.f'});(d/'.nift/tracked.json').write_text(json.dumps(tracked))
    r=run(d,label,'build','a')
    if case=='changed-final' and mode!='modified':
     assert r.returncode!=0 and 'changed during build' in r.stderr,(case,mode,label,r.stdout,r.stderr)
     assert not (d/'.nift/public/a.info.json').exists()
     observed.append(r.returncode);continue
    assert r.returncode==0,(case,mode,label,r.stdout,r.stderr)
    info=json.loads((d/'.nift/public/a.info.json').read_text());decl=f'data/{pre[-1]}.txt';assert decl in info['dependencies']
    if label=='e3b':
     if pre[-1]!=post[-1]:assert 'sidecar-state' not in info
     else:
      state=info['sidecar-state'];assert bytes.fromhex(state['bytes-hex'])==raw(pre[-1]).encode();assert [info['dependencies'][i] for i in state['declaration-indices']]==[decl]
    observed.append((d/'public/a.html').read_bytes())
    if pre[-1]!=post[-1]:
     (d/f'data/{post[-1]}.txt').unlink()
     follow=run(d,label,'build','a');assert follow.returncode!=0,(case,mode,label,follow.stdout,follow.stderr)
   assert observed[0]==observed[1];rows.append(dict(group='hooks',mode=mode,case=case,passed=True))
 # A missing declared saved edge must use the loader and preserve E2 diagnostics.
 for mode in ['modified','hash','hybrid']:
  observed=[]
  for label in ['e2','e3b']:
   d=root/f'missing-edge-{mode}-{label}';side=fixture(d,mode);side.write_text('{"dependencies":["data/one.txt"]}');os.utime(side,ns=(1600000000000000000,)*2);assert run(d,label,'build','--all').returncode==0
   (d/'data/one.txt').unlink();r=run(d,label,'status');observed.append((r.returncode,r.stdout,r.stderr))
  assert observed[0]==observed[1],(mode,observed);rows.append(dict(group='eligibility',mode=mode,case='missing-edge',passed=True))
 # Round-trip accepted whitespace, escape spelling, CRLF and UTF-8 bytes.
 for case,raw in [('crlf',b'{\r\n"dependencies": ["data/one.txt"]\r\n}\r\n'),('escape',b'{"dependencies":["data/\\u006fne.txt"]}'),('unicode','{"note":"café λ", "dependencies":["data/one.txt"]}'.encode())]:
  d=root/case;side=fixture(d,'modified');side.write_bytes(raw);r=run(d,'e3b','build','a');assert r.returncode==0,(case,r.stderr,r.stdout)
  info=json.loads((d/'.nift/public/a.info.json').read_text());assert bytes.fromhex(info['sidecar-state']['bytes-hex'])==raw;rows.append(dict(group='bytes',case=case,passed=True))
 # Malformed current bytes with forged matching identity: invalid structural state must force parsing.
 changes={
 'old-e2':lambda s,d:d.pop('sidecar-state'),
 'missing-version':lambda s,d:s.pop('version'), 'missing-present':lambda s,d:s.pop('present'), 'missing-bytes':lambda s,d:s.pop('bytes-hex'), 'missing-indices':lambda s,d:s.pop('declaration-indices'), 'missing-platform':lambda s,d:s.pop('platform'),
 'state-type':lambda s,d:d.update({'sidecar-state':[]}), 'present-type':lambda s,d:s.update(present='true'), 'indices-type':lambda s,d:s.update({'declaration-indices':None}), 'version':lambda s,d:s.update(version=2), 'encoding':lambda s,d:s.update({'bytes-hex':'zz'}),
 'index-type':lambda s,d:s.update({'declaration-indices':['0']}), 'index-fraction':lambda s,d:s.update({'declaration-indices':[0.5]}), 'index-negative':lambda s,d:s.update({'declaration-indices':[-1]}), 'index-range':lambda s,d:s.update({'declaration-indices':[999999]}), 'duplicate':lambda s,d:s.update({'declaration-indices':[0,0]}), 'platform':lambda s,d:s.update(platform='wrong'),
 'special-role':lambda s,d:(d['dependencies'].append('.nift/project.fingerprint'),s.update({'declaration-indices':[len(d['dependencies'])-1]}))}
 for case,change in changes.items():
  d=root/('corrupt-'+case);side=fixture(d,'modified');side.write_text('{"dependencies":["data/one.txt"]}');os.utime(side,ns=(1600000000000000000,)*2);assert run(d,'e3b','build','--all').returncode==0
  meta=d/'.nift/public/a.info.json';info=json.loads(meta.read_text());state=info['sidecar-state'];bad=b'{broken';side.write_bytes(bad);os.utime(side,ns=(1600000000000000000,)*2);state['bytes-hex']=bad.hex();change(state,info)
  stamp=meta.stat().st_mtime_ns;meta.chmod(0o644);meta.write_text(json.dumps(info));os.utime(meta,ns=(stamp,stamp));r=run(d,'e3b','build');assert r.returncode!=0 and 'invalid user dependencies JSON' in r.stdout+r.stderr,(case,r.stdout,r.stderr);rows.append(dict(group='migration-corruption',case=case,passed=True))
 # Old E2 metadata remains readable and acquires state only after a successful candidate build.
 d=root/'migration';side=fixture(d,'modified');side.write_text('{"dependencies":["data/one.txt"]}');assert run(d,'e2','build','--all').returncode==0;meta=d/'.nift/public/a.info.json';assert 'sidecar-state' not in json.loads(meta.read_text());assert run(d,'e3b','build').returncode==0;assert 'sidecar-state' not in json.loads(meta.read_text());assert run(d,'e3b','build','a').returncode==0;assert 'sidecar-state' in json.loads(meta.read_text());rows.append(dict(group='migration',case='old-success',passed=True))
Path(args.output).write_text(json.dumps(dict(passed=True,cases=rows),indent=2)+'\n');print('Candidate contracts PASS',len(rows))
