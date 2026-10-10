import pathlib,re,subprocess,shutil
p=pathlib.Path('/tmp/nift-status-experiment/probe');repo=pathlib.Path('/home/nick/Repositories/nift/nift')
(p/'src/Diag.h').write_text('''#pragma once
#include <chrono>
#include <filesystem>
#include <map>
#include <set>
#include <mutex>
#include <fstream>
#include <cstdlib>
namespace diag {
struct Entry {unsigned long long calls=0, bytes=0; double ms=0; std::set<std::string> paths;};
struct State { std::mutex mutex; std::map<std::string,Entry> data; ~State(){const char* out=std::getenv("NIFT_DIAG_FILE");if(!out)return;std::ofstream f(out);f<<"metric,calls,unique,bytes,inclusive_ms\\n";for(auto& x:data)f<<x.first<<","<<x.second.calls<<","<<x.second.paths.size()<<","<<x.second.bytes<<","<<x.second.ms<<"\\n";} };
inline State& state(){static State s;return s;}
inline void hit(const char* name,const std::filesystem::path& p={},unsigned long long bytes=0){auto& s=state();std::lock_guard<std::mutex> l(s.mutex);auto& e=s.data[name];e.calls++;e.bytes+=bytes;if(!p.empty())e.paths.insert(p.generic_string());}
struct Scope {const char* name;std::chrono::steady_clock::time_point start;Scope(const char* n):name(n),start(std::chrono::steady_clock::now()){hit(n);}~Scope(){double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();auto& s=state();std::lock_guard<std::mutex> l(s.mutex);s.data[name].ms+=ms;} };
}
''')
spec={
'FileSystem.cpp':[('DependencyStatus dependency_status(const fs::path& path) {','diag::hit("dependency_status",path);'),('std::optional<std::string> read_file_checked(const fs::path& path) {','diag::hit("read_file_checked",path);'),('bool path_exists(const fs::path& path) {','diag::hit("path_exists",path);'),('bool path_within(const fs::path& base, const fs::path& candidate) {','diag::Scope scope("path_within"); diag::hit("containment_candidate",candidate);'),('fs::file_time_type modified_time(const fs::path& path) {','diag::hit("modified_time",path);'),('std::uint64_t hash_path(const fs::path& path) {','diag::hit("actual_hash_path",path);')],
'JsonFile.cpp':[('bool load_json_file(const std::filesystem::path& path, json::Document& document, std::string& error) {','diag::Scope scope("load_json_file"); diag::hit("json_file",path);')],
'ProjectInfo.cpp':[('bool ProjectInfo::open() {','diag::Scope scope("project_open");'),('bool ProjectInfo::load_tracking() {','diag::Scope scope("load_tracking");'),('bool ProjectInfo::load_config() {','diag::Scope scope("load_config");'),('std::uint64_t ProjectInfo::current_hash_cached(const fs::path& dependency) const {','diag::hit("current_hash_request",dependency);'),('bool ProjectInfo::load_user_dependencies(const TrackedInfo& info, std::set<std::string>& dependencies, BuildError* build_error) const {','diag::Scope scope("user_dependencies");'),('bool ProjectInfo::metadata_path_is_safe(const fs::path& path) const {','diag::hit("metadata_path_safe",path);'),('bool ProjectInfo::dependency_changed(const fs::path& dependency, const filesystem::DependencyStatus& status, const filesystem::DependencyStatus& page_info_status, const json::Document& snapshots, const std::string& name) const {','diag::hit("dependency_changed",dependency);')],
'ProjectRead.cpp':[('std::string relative_of(const fs::path& root, const fs::path& path) {','diag::hit("relative_of",path);')]
}
for file,items in spec.items():
 path=p/'src'/file;s=path.read_text()
 for anchor,inject in items:
  if anchor not in s:print('MISSING',file,anchor);continue
  s=s.replace(anchor,anchor+'\n'+inject,1)
 if file=='ProjectInfo.cpp':
  m=re.search(r'std::vector<std::string> ProjectInfo::build_reasons\([^\n]+\) const \{',s);assert m;s=s[:m.end()]+'\n diag::Scope scope("build_reasons"); diag::hit("dirty_item",info.name);'+s[m.end():]
 if file=='FileSystem.cpp':s=s.replace('const std::streamoff size = file.tellg();','const std::streamoff size = file.tellg();\n    if(size>0)diag::hit("read_bytes",path,static_cast<unsigned long long>(size));',1)
 path.write_text('#include "Diag.h"\n'+s)
objects=[]
for x in repo.glob('src/*.o'):dest=p/'src'/x.name;shutil.copy2(x,dest)
for parent in ['minifypp/src','markuppp/src','markuppp/vendor/cmark']:
 for x in (repo/parent).glob('*.o'):shutil.copy2(x,p/parent/x.name)
ffi=list((repo/'.build/libffi').glob('*/install'));assert ffi
headers=ffi[0]/'include';lib=ffi[0]/'lib/libffi.a'
flags=['-std=c++17','-O2','-g','-pthread','-Isrc','-Iinclude','-Iminifypp/include','-Iminifypp/src','-Imarkuppp/include','-Imarkuppp/vendor/cmark','-I'+str(headers)]
for file in spec:
 subprocess.run(['g++',*flags,'-c','src/'+file,'-o','src/'+file.replace('.cpp','.o')],cwd=p,check=True)
objects=list((p/'src').glob('*.o'))+list((p/'minifypp/src').glob('*.o'))+list((p/'markuppp/src').glob('*.o'))+list((p/'markuppp/vendor/cmark').glob('*.o'))
subprocess.run(['g++','-pthread',*[str(x) for x in objects],str(lib),'-ldl','-o',str(p/'nift-diag')],check=True)
print('Isolated diagnostic CLI compiled')
