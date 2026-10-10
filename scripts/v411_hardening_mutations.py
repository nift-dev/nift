#!/usr/bin/env python3
"""Disposable source mutants. Production sources/objects/binary are never edited."""
import argparse, hashlib, json, pathlib, shlex, subprocess, tempfile, time
p=argparse.ArgumentParser();p.add_argument('--output',required=True);p.add_argument('--family',choices=['process','incremental','all'],default='process');a=p.parse_args()
repo=pathlib.Path(__file__).resolve().parents[1];started=time.monotonic();results=[]
def run(cmd,timeout=180):
 return subprocess.run(cmd,cwd=repo,text=True,capture_output=True,timeout=timeout)
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
paths=[repo/'src'/n for n in ['Process.cpp','JobControl.cpp','ProjectInfo.cpp','CLI.cpp']];before={str(p):digest(p) for p in paths};before[str(repo/'nift')]=digest(repo/'nift')
with tempfile.TemporaryDirectory(prefix='nift-v411-mutants-') as td:
 temp=pathlib.Path(td)
 if a.family in ['process','all']:
  control=run(['make','test-process-hardening']);assert control.returncode==0,control.stderr
  process=(repo/'src/Process.cpp').read_text();job=(repo/'src/JobControl.cpp').read_text()
  mutants=[('capture-cleanup','Process.cpp','if(ofd>=0)close(ofd);if(efd>=0)close(efd);if(!op.empty())unlink(op.c_str());if(!ep.empty())unlink(ep.c_str());','', ['temp','1']),
   ('partial-pipeline-kill','Process.cpp','if(!r.error.empty())for(pid_t p:pids)kill(p,SIGKILL);','', ['fork','1']),
   ('job-child-reap','JobControl.cpp','while(waitpid(p,&st,0)<0&&errno==EINTR){}','(void)st;', ['fork','1','job']),
   ('cloexec','ProcessPOSIX.h','return fcntl(fd,F_SETFD,FD_CLOEXEC)>=0;','(void)fd;return true;', []),
   ('dup2-error','ProcessPOSIX.h','if(fail)return false;','if(fail)return true;', ['dup2-out','0'])]
  for name,file,old,new,case in mutants:
   source=(repo/'src'/file).read_text();assert source.count(old)==1,(name,source.count(old));mutant=temp/file;mutant.write_text(source.replace(old,new))
   out=temp/name
   if file=='ProcessPOSIX.h':
    (temp/'Process.cpp').write_text(process);(temp/'JobControl.cpp').write_text(job)
   cmd=['c++','-std=c++17','-pthread','-DNIFT_PROCESS_TEST_HOOKS','-I'+str(temp),'-Isrc','tests/process_contract.cpp' if name=='cloexec' else 'tests/process_failure.cpp',str(temp/'Process.cpp') if file=='ProcessPOSIX.h' else str(mutant) if file=='Process.cpp' else 'src/Process.cpp',str(temp/'JobControl.cpp') if file=='ProcessPOSIX.h' else str(mutant) if file=='JobControl.cpp' else 'src/JobControl.cpp','-o',str(out)]
   built=run(cmd);assert built.returncode==0,built.stderr
   # Timeout is expected only for the missing-kill mutant. Watchdog runs the
   # entire fixture in an isolated session so all test-owned children die.
   r=subprocess.Popen([str(out),*case],cwd=repo,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
   try:stdout,stderr=r.communicate(timeout=2);rc=r.returncode
   except subprocess.TimeoutExpired:
    import os,signal
    os.killpg(r.pid,signal.SIGKILL);stdout,stderr=r.communicate();rc=124
   assert rc!=0,(name,'mutant survived')
   if file=='ProcessPOSIX.h':mutant.unlink()
   results.append(dict(name=name,family='process',case=case,caught=True,returncode=rc,diagnostic=stderr.strip() or 'watchdog timeout'))
 if a.family in ['incremental','all']:
  plans={}
  for filename in ['ProjectInfo.cpp','CLI.cpp','Hooks.cpp']:
   plan=run(['make','-n','-W','src/'+filename]);assert plan.returncode==0,plan.stderr
   lines=plan.stdout.splitlines();compile_line=next(x for x in lines if '-c src/'+filename+' ' in x);link_line=next(x for x in reversed(lines) if ' -o nift' in x)
   plans[filename]=(shlex.split(compile_line),shlex.split(link_line))
  prefix='bool ProjectInfo::dependency_changed(const fs::path& dependency, fs::file_time_type page_info_mtime, const json::Document& snapshots, const std::string& name) const {'
  mutants=[('content-invalidation','ProjectInfo.cpp',prefix,prefix+'\n    if(dependency.parent_path().filename()=="content")return false;',['content']),
   ('template-invalidation','ProjectInfo.cpp',prefix,prefix+'\n    if(dependency.filename()=="template.html")return false;',['template']),
   ('generated-file-edge','ProjectInfo.cpp',prefix,prefix+'\n    if(dependency.parent_path().filename()=="public")return false;',['generated']),
   ('metadata-refresh','ProjectInfo.cpp','if (document["title"].string != info.title) reasons.push_back("tracked title changed");','',['tracking']),
   ('output-deletion','CLI.cpp','filesystem::remove_owned_file(project.output_path(info));','', ['add','remove']),
   ('rename-deletion','CLI.cpp','filesystem::remove_owned_file(project.output_path(info));','', ['rename']),
   ('shared-global-baseline','ProjectInfo.cpp','return value != std::to_string(current_hash_cached(dependency));','return filesystem::stored_hash_changed(root, dependency);',['atomic','targeted']),
   ('late-native-hash','ProjectInfo.cpp','    if (!write_page_info(info, result.dependencies, result.reqs, new_pagination_pages, snapshots)) {','    for (auto& entry : snapshots) entry.second = std::to_string(filesystem::hash_path(root/entry.first));\n    if (!write_page_info(info, result.dependencies, result.reqs, new_pagination_pages, snapshots)) {',['__contracts__']),
   ('drop-hook-dependencies','Hooks.cpp','dependencies->insert(rr.dependencies.begin(), rr.dependencies.end());','',['__contracts__'])]
  for name,filename,old,new,ops in mutants:
   source=(repo/'src'/filename).read_text();assert source.count(old)==1,(name,source.count(old));mutant=temp/filename;mutant.write_text(source.replace(old,new));
   obj=temp/(filename+'.o');binary=temp/name
   compile_cmd,link_cmd=plans[filename];compile_cmd=[str(mutant) if x=='src/'+filename else str(obj) if x=='src/'+filename.replace('.cpp','.o') else x for x in compile_cmd]
   built=run(compile_cmd);assert built.returncode==0,built.stderr
   link_cmd=[str(obj) if x=='src/'+filename.replace('.cpp','.o') else str(binary) if x=='nift' else x for x in link_cmd];linked=run(link_cmd);assert linked.returncode==0,linked.stderr
   if ops==['__contracts__']:
    test=run(['python3','tests/consumer_dependency_snapshots.py','--nift',str(binary),'--output',str(temp/'contracts.json')])
   else:
    test=run(['python3','tests/randomized_incremental_differential.py','--nift',str(binary),'--seeds','3' if name=='shared-global-baseline' else '411','--modes','hash','--steps',str(len(ops)),'--operations',','.join(ops)])
   assert test.returncode!=0,(name,'mutant survived')
   assert ('mode=hash seed=' in test.stderr) if ops!=['__contracts__'] else ('AssertionError' in test.stderr or 'KeyError' in test.stderr),(name,'non-contract failure',test.stderr)
   print('caught',name,flush=True)
   results.append(dict(name=name,family='incremental',operations=ops,seed=3 if name=='shared-global-baseline' else 411,caught=True,returncode=test.returncode,diagnostic=test.stderr.splitlines()[-1]))
   pathlib.Path(a.output).write_text(json.dumps(dict(passed=False,in_progress=True,caught=len(results),results=results),indent=2)+'\n')
assert a.family!='process' or len(results)==5,'all process mutants must execute'
assert all(digest(pathlib.Path(path))==value for path,value in before.items()),'production artifact changed'
pathlib.Path(a.output).write_text(json.dumps(dict(passed=True,mutants=len(results),caught=len(results),missed=0,results=results,elapsed_seconds=round(time.monotonic()-started,3),production_artifacts_unchanged=True),indent=2)+'\n')
print('mutation liveness PASS:',len(results),'caught')
