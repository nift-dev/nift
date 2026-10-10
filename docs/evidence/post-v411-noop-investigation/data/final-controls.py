import pathlib,json,subprocess,os,statistics
p=pathlib.Path('/tmp/nift-noop-investigation');os.sched_setaffinity(0,set(json.loads((p/'identity.json').read_text())['cpus']));rows=[]
# Final control: unchanged bytes and zero actions for every remaining fixture.
import hashlib
for d in sorted(p.glob('fixture-*')):
 system=d.name.rsplit('-',1)[1];m=json.loads((d/'manifest.json').read_text());before={a['id']:hashlib.sha256((d/a['output']).read_bytes()).hexdigest() for a in m['actions']};trace=d/'final.trace';trace.unlink(missing_ok=True);cmd=['/home/nick/Repositories/nift/nift/nift','build'] if system=='nift' else ['make' if system=='make' else '/tmp/build-systems-tools/ninja/usr/bin/ninja','-j4'];r=subprocess.run(cmd,cwd=d,env=os.environ|{'BUILD_TRACE':str(trace)},capture_output=True,text=True);after={a['id']:hashlib.sha256((d/a['output']).read_bytes()).hexdigest() for a in m['actions']};assert r.returncode==0 and before==after and not trace.exists();rows.append({'fixture':d.name,'zero_actions':True,'output_hash_map_unchanged':True,'outputs_verified':len(before)})
(p/'final-controls.json').write_text(json.dumps(rows,indent=2)+'\n');print('Final controls PASS',len(rows))
