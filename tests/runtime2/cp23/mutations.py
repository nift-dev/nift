#!/usr/bin/env python3
"""Build real one-file mutations against the same objects; require a specific
assertion failure and a completed suite. Signals, dead binaries, build failures
and timeouts are harness failures, never mutation detections."""
import argparse,hashlib,json,pathlib,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser();parser.add_argument('--evidence',type=pathlib.Path,required=True);args=parser.parse_args();args.evidence.mkdir(parents=True,exist_ok=True)
def run(cmd,timeout=120):
 return subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True,timeout=timeout)
objects=run(['make','-s','--eval','cp23-vars: ; @echo $(CP23_CORE:.cpp=.o)','cp23-vars']).stdout.split()
if not objects or any(not p.endswith('.o') for p in objects):raise SystemExit('invalid object inventory')
base=ROOT/'.build/rt/cp23-ownership-unit'
p=run([str(base)]);(args.evidence/'normal.log').write_text(p.stdout+p.stderr)
if p.returncode or '0 failures' not in p.stderr+p.stdout:raise SystemExit('positive control failed')
mutants=[
 ('output-publication','src/rt/parallel_task.cpp','if (text.empty()) return;','return;','output=[after\nbefore\n]'),
 ('reclamation','src/rt/shared_gc.cpp','void shared_collect() noexcept {','void shared_collect() noexcept { return;','CHECK failed: shared_registry_live_for_test()==0'),
 ('future-identity','src/rt/compare.cpp','return f->parallel ? static_cast<const void*>(f->parallel.get()) : static_cast<const void*>(f);','return static_cast<const void*>(f);','CHECK failed: equal(f,decoded)'),
 ('callable-domain','src/rt/isolate_transfer_decode.cpp','if (n.proto_domain != protos_.domain())','if (false)','CHECK failed: graph.decode(second,out,why)==TransferStatus::ProtoUnavailable'),
 ('atom-member','src/rt/isolate_snapshot.cpp','*operand = atoms[*operand];','if (in.op == OpCode::GetMember && input.atom_names[*operand] == "y") *operand = static_cast<std::uint16_t>(atom("x").id); else *operand = atoms[*operand];','output=[40]'),
]
def detected(name,signature,code,output):
 if name=='atom-member':signature='output=[40\n]'
 return code==1 and 'cp23-ownership-unit:' in output and signature in output
results=[]
for name,source,old,new,signature in mutants:
 folder=ROOT/'.build/cp23-mutants'/name;folder.mkdir(parents=True,exist_ok=True)
 text=(ROOT/source).read_text()
 if text.count(old)!=1:raise SystemExit('ambiguous mutation '+name)
 altered=folder/'mutated.cpp';altered.write_text(text.replace(old,new));obj=folder/'mutated.o';binary=folder/'unit'
 commands=[['g++','-Isrc','-Iinclude','-std=c++17','-O2','-pthread','-c',str(altered),'-o',str(obj)],['g++','-Isrc','-Iinclude','-Itests/rt','-std=c++17','-O2','-pthread','tests/runtime2/cp23/ownership_unit.cpp',*[p for p in objects if p!=source[:-4]+'.o'],str(obj),'-ldl','.build/libffi/x86_64-linux-gnu-cc/install/lib/libffi.a','-o',str(binary)]]
 for cmd in commands:
  p=run(cmd);(args.evidence/(name+'-build.log')).open('a').write(p.stdout+p.stderr)
  if p.returncode:raise SystemExit('build failure '+name)
 p=run([str(binary)]);output=p.stdout+p.stderr;(args.evidence/(name+'.log')).write_text(output)
 # The atom witness embeds a newline before the diagnostic closing bracket.
 was_detected=detected(name,signature,p.returncode,output)
 row=dict(name=name,source=source,mutation_from=old,mutation_to=new,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),exit=p.returncode,detected=was_detected,commands=commands)
 results.append(row);(args.evidence/'results.json').write_text(json.dumps(results,indent=2)+'\n')
 if not was_detected:raise SystemExit('not detected '+name)
 print('DETECTED',name,flush=True)
controls=[]
normal_output=(args.evidence/'normal.log').read_text()
dead=run(['/bin/false'])
for name,source,old,new,signature in mutants:
 controls.append(dict(witness=name,input='normal',rejected=not detected(name,signature,0,normal_output)))
 controls.append(dict(witness=name,input='dead',rejected=not detected(name,signature,dead.returncode,dead.stdout+dead.stderr)))
 # A real timeout is rejected even if its stdout counterfeits a witness.
 marker='cp23-ownership-unit: 1 failure\n'+('output=[40\n]' if name=='atom-member' else signature)
 try:
  timed=run([sys.executable,'-c','import sys,time;print(sys.argv[1],flush=True);time.sleep(1)',marker],timeout=0.1)
  raise SystemExit('timeout control did not time out')
 except subprocess.TimeoutExpired:
  controls.append(dict(witness=name,input='timeout',rejected=not detected(name,signature,'timeout',marker)))
 for other,*unused in mutants:
  if other==name:continue
  log=(args.evidence/(other+'.log')).read_text()
  controls.append(dict(witness=name,input='unrelated:'+other,rejected=not detected(name,signature,1,log)))
(args.evidence/'controls.json').write_text(json.dumps(controls,indent=2)+'\n')
if not all(c['rejected'] for c in controls):raise SystemExit('mutation negative control accepted')
print('PASS',len(results),'real mutations;',len(controls),'normal/dead/timeout/unrelated controls rejected')
