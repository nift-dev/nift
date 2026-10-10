#!/usr/bin/env python3
"""Disposable incremental source mutants. Production sources/objects/binary are never edited."""
import argparse, hashlib, json, pathlib, shlex, subprocess, tempfile, time
p=argparse.ArgumentParser();p.add_argument('--output',required=True);p.add_argument('--family',choices=['incremental'],default='incremental');a=p.parse_args()
repo=pathlib.Path(__file__).resolve().parents[1]
native_target=repo/('nift.exe' if (repo/'nift.exe').exists() else 'nift')
started=time.monotonic();results=[]
def run(cmd,timeout=180):
 return subprocess.run(cmd,cwd=repo,text=True,capture_output=True,timeout=timeout)
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
paths=[repo/'src'/n for n in ['Process.cpp','JobControl.cpp','ProjectInfo.cpp','CLI.cpp']];before={str(p):digest(p) for p in paths};before[str(native_target)]=digest(native_target)
with tempfile.TemporaryDirectory(prefix='nift-v411-mutants-') as td:
 temp=pathlib.Path(td)
 if a.family == 'incremental':
  # Control suite must pass before a mutant's failure can count as detection.
  control=run(['python3','tests/consumer_dependency_snapshots.py','--nift',str(native_target),'--output',str(temp/'control.json')])
  assert control.returncode==0,control.stderr
  plans={}
  for filename in ['ProjectInfo.cpp','CLI.cpp','Hooks.cpp']:
   plan=run(['make','-n','-W','src/'+filename]);assert plan.returncode==0,plan.stderr
   lines=plan.stdout.splitlines();compile_line=next(x for x in lines if '-c src/'+filename+' ' in x);link_line=next(x for x in reversed(lines) if ' -o '+native_target.name in x)
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
   obj=temp/(filename+'.o');binary=temp/(name+native_target.suffix)
   compile_cmd,link_cmd=plans[filename];compile_cmd=[str(mutant) if x=='src/'+filename else str(obj) if x=='src/'+filename.replace('.cpp','.o') else x for x in compile_cmd]
   built=run(compile_cmd);assert built.returncode==0,built.stderr
   link_cmd=[str(obj) if x=='src/'+filename.replace('.cpp','.o') else str(binary) if x==native_target.name else x for x in link_cmd];linked=run(link_cmd);assert linked.returncode==0,linked.stderr
   if ops==['__contracts__']:
    test=run(['python3','tests/consumer_dependency_snapshots.py','--nift',str(binary),'--output',str(temp/'contracts.json')])
   else:
    test=run(['python3','tests/randomized_incremental_differential.py','--nift',str(binary),'--seeds','3' if name=='shared-global-baseline' else '411','--modes','hash','--steps',str(len(ops)),'--operations',','.join(ops)])
   assert test.returncode!=0,(name,'mutant survived')
   if name=='late-native-hash':assert '==consumed' in test.stderr,(name,'wrong failing contract',test.stderr)
   if name=='drop-hook-dependencies':assert 'helper.f' in test.stderr,(name,'wrong failing contract',test.stderr)
   assert ('mode=hash seed=' in test.stderr) if ops!=['__contracts__'] else ('AssertionError' in test.stderr or 'KeyError' in test.stderr),(name,'non-contract failure',test.stderr)
   print('caught',name,flush=True)
   results.append(dict(name=name,family='incremental',operations=ops,seed=3 if name=='shared-global-baseline' else 411,caught=True,returncode=test.returncode,diagnostic=test.stderr.splitlines()[-1]))
   pathlib.Path(a.output).write_text(json.dumps(dict(passed=False,in_progress=True,caught=len(results),results=results),indent=2)+'\n')
assert len(results)==9, 'all nine mutants must execute'
assert all(digest(pathlib.Path(path))==value for path,value in before.items()),'production artifact changed'
pathlib.Path(a.output).write_text(json.dumps(dict(passed=True,mutants=len(results),caught=len(results),missed=0,results=results,elapsed_seconds=round(time.monotonic()-started,3),production_artifacts_unchanged=True),indent=2)+'\n')
print('mutation liveness PASS:',len(results),'caught')
