#!/usr/bin/env python3
"""Focused Linux F6 gate + five build-checked source mutants; no production hooks.
Run inside the shared heavy lane. --sanitize runs ASan/UBSan positive cases.
"""
import argparse,concurrent.futures,hashlib,json,os,shlex,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--mutants',action='store_true');a=p.parse_args()
root=Path(__file__).resolve().parents[2];os.chdir(root)
out=root/'.build'/('f6-san' if a.sanitize else 'f6');out.mkdir(parents=True,exist_ok=True)
flags=['-std=c++17','-O1' if a.sanitize else '-O2','-g','-Wall','-Wextra','-pedantic','-pthread','-Isrc','-DNIFT_HOST_TEST_HOOKS']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all']
compiler=shlex.split(os.environ.get('CXX','g++'))
sources=subprocess.check_output(['make','-s','--eval','f6-print: ; @echo $(RT_HOST_SOURCES)','f6-print'],text=True).split()
changed='src/rt/host/process_posix.cpp';others=[s for s in sources if s!=changed]+['tests/rt/host_f6_cleanup_unit.cpp']
def build(source,target):
    cmd=compiler+flags+['-c',str(source),'-o',str(target)]
    r=subprocess.run(cmd,capture_output=True,text=True,timeout=240)
    target.with_suffix('.log').write_text(shlex.join(cmd)+'\n'+r.stdout+r.stderr)
    if r.returncode:raise RuntimeError('build failed (not mutant detection): '+str(target))
def obj(s):return out/(s.replace('/','_')+'.o')
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:list(pool.map(lambda s:build(s,obj(s)),others))
text=(root/changed).read_text()
versions={'normal':text}
if a.mutants:
    versions.update({
      'skip-all':text.replace('absent = s.pgid > 1 && ::kill(-s.pgid, 0) == -1 && errno == ESRCH;', 'absent = true;'),
      'eperm-absent':text.replace('errno == ESRCH;', '(errno == ESRCH || errno == EPERM);'),
      'zombie-filter':text.replace('process.pgrp == s.pgid)', "process.pgrp == s.pgid && process.state != 'Z')"),
      'retain-absent':text.replace('runtime::forget_managed_pgid(s.pgid);','(void)s.pgid;'),
      'always-scan':text.replace('if (!absent) {','if (true) {'),
    })
cases=['completed','zombie','leaderless','eperm','eintr','einval'];rows=[]
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
for name,source in versions.items():
    if name!='normal':assert source!=text,'mutant replacement did not apply'
    cpp=out/(name+'.cpp');cpp.write_text(source);o=out/(name+'.o');build(cpp,o)
    binary=out/name
    cmd=compiler+flags+[str(obj(s)) for s in others]+[str(o),'-Wl,--wrap=kill','-Wl,--wrap=opendir','-o',str(binary)]
    r=subprocess.run(cmd,capture_output=True,text=True,timeout=240);(out/(name+'-link.log')).write_text(shlex.join(cmd)+'\n'+r.stdout+r.stderr)
    if r.returncode:raise RuntimeError('link failure: '+name)
    wanted={'skip-all':('zombie','existing/uncertain group bypassed'), 'eperm-absent':('eperm','existing/uncertain group bypassed'), 'zombie-filter':('zombie','required descendant attribution forgotten'), 'retain-absent':('completed','completed group attribution leaked'), 'always-scan':('completed','completed cleanup enumerated proc')}
    for case in cases if name=='normal' else [wanted[name][0]]:
        cmd=['tests/runtime2/cp16/run_contained.sh']+(['--sanitized'] if a.sanitize else [])+[str(binary),case]
        r=subprocess.run(cmd,capture_output=True,text=True,timeout=50,env=env)
        log=r.stdout+r.stderr;(out/(name+'-'+case+'.log')).write_text(log)
        if name=='normal':ok=r.returncode==0 and 'PASS F6 '+case in log
        else:ok=r.returncode==1 and wanted[name][1] in log
        rows.append({'variant':name,'case':case,'exit':r.returncode,'accepted':ok,'sha256':hashlib.sha256(binary.read_bytes()).hexdigest()})
        print(json.dumps(rows[-1]),flush=True)
        (out/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
        if not ok:raise RuntimeError('gate failure: '+name+'/'+case+'\n'+log)
