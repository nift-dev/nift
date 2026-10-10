import pathlib,subprocess,shutil
p=pathlib.Path('/tmp/nift-noop-investigation/core-stage');r=pathlib.Path('/home/nick/Repositories/nift/nift');shutil.copytree('/tmp/nift-noop-investigation/core',p,dirs_exist_ok=True,symlinks=True)
for x in r.glob('src/*.o'):shutil.copy2(x,p/'src'/x.name)
s=(r/'src/ProjectInfo.cpp').read_text();s='#include "Stage.h"\n'+s
for anchor,name in [('bool ProjectInfo::open() {','open'),('bool ProjectInfo::load_tracking() {','tracking'),('int ProjectInfo::build_all(bool force, bool explain, bool repair) {','build_all'),('int ProjectInfo::build_many(const std::vector<BuildJob>& initial_jobs, bool targeted, bool full_detail, std::size_t requested_count) {','build_many')]:
 assert anchor in s;s=s.replace(anchor,anchor+'\n stage::Scope stage_scope("'+name+'");',1)
a='    std::vector<BuildJob> jobs;\n    jobs.reserve(tracked.size());';assert a in s;s=s.replace(a,'    const auto dirty_started=std::chrono::steady_clock::now();\n'+a,1)
a='    const int result = build_many(jobs, false, explain, tracked.size());';assert a in s;s=s.replace(a,'    stage::emit("dirty_scan",dirty_started);\n'+a,1);(p/'src/ProjectInfo.cpp').write_text(s)
(p/'src/Stage.h').write_text('''#pragma once
#include <chrono>
#include <iostream>
namespace stage { using Clock=std::chrono::steady_clock; inline void emit(const char* name,Clock::time_point start){std::cerr<<"STAGE "<<name<<" "<<std::chrono::duration<double,std::milli>(Clock::now()-start).count()<<"\\n";} struct Scope{const char* name;Clock::time_point start;Scope(const char* n):name(n),start(Clock::now()){} ~Scope(){emit(name,start);}};}
''')
ffi=list((r/'.build/libffi').glob('*/install'))[0];flags=['-std=c++17','-O2','-pthread','-Isrc','-Iinclude','-Iminifypp/include','-Iminifypp/src','-Imarkuppp/include','-Imarkuppp/vendor/cmark','-I'+str(ffi/'include')]
subprocess.run(['g++',*flags,'-c','src/ProjectInfo.cpp','-o','src/ProjectInfo.o'],cwd=p,check=True)
objects=list((p/'src').glob('*.o'))+list((p/'minifypp/src').glob('*.o'))+list((p/'markuppp/src').glob('*.o'))+list((p/'markuppp/vendor/cmark').glob('*.o'))
subprocess.run(['g++','-pthread',*[str(x) for x in objects],str(ffi/'lib/libffi.a'),'-ldl','-o',str(p/'nift-stage')],check=True)
print('Stage-only probe compiled')
