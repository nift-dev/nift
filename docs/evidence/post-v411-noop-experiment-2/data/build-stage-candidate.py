import pathlib,subprocess,shlex
p=pathlib.Path('/tmp/nift-path-experiment');repo=pathlib.Path('/home/nick/Repositories/nift/nift')
s='#include "Stage.h"\n'+(repo/'src/ProjectInfo.cpp').read_text()
for anchor,name in [('bool ProjectInfo::open() {','open'),('bool ProjectInfo::load_tracking() {','tracking'),('int ProjectInfo::build_all(bool force, bool explain, bool repair) {','build_all'),('int ProjectInfo::build_many(const std::vector<BuildJob>& initial_jobs, bool targeted, bool full_detail, std::size_t requested_count) {','build_many')]:
 assert anchor in s;s=s.replace(anchor,anchor+'\n stage::Scope stage_scope("'+name+'");',1)
a='    std::vector<BuildJob> jobs;\n    jobs.reserve(tracked.size());';assert a in s;s=s.replace(a,'    const auto dirty_started=std::chrono::steady_clock::now();\n'+a,1)
a='    const int result = build_many(jobs, false, explain, tracked.size());';assert a in s;s=s.replace(a,'    stage::emit("dirty_scan",dirty_started);\n'+a,1)
(p/'stage-candidate.cpp').write_text(s);(p/'Stage.h').write_bytes(pathlib.Path('/tmp/nift-noop-investigation/core-stage/src/Stage.h').read_bytes())
lines=subprocess.check_output(['make','-n','-W','src/ProjectInfo.cpp','all'],cwd=repo,text=True).splitlines();compile_=next(x for x in lines if '-c src/ProjectInfo.cpp ' in x);link=next(x for x in reversed(lines) if x.endswith(' -o nift'))
cmd=shlex.split(compile_);cmd=[str(p/'stage-candidate.cpp') if x=='src/ProjectInfo.cpp' else str(p/'stage-candidate.o') if x=='src/ProjectInfo.o' else x for x in cmd];subprocess.run(cmd,cwd=repo,check=True)
cmd=shlex.split(link);cmd=[str(p/'stage-candidate.o') if x=='src/ProjectInfo.o' else str(p/'nift-stage-candidate') if x=='nift' else x for x in cmd];subprocess.run(cmd,cwd=repo,check=True);print('stage-only candidate probe built')
