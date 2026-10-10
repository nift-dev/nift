#pragma once
#include <chrono>
#include <filesystem>
#include <map>
#include <set>
#include <mutex>
#include <fstream>
#include <cstdlib>
namespace diag {
struct Entry {unsigned long long calls=0, bytes=0; double ms=0; std::set<std::string> paths;};
struct State { std::mutex mutex; std::map<std::string,Entry> data; ~State(){const char* out=std::getenv("NIFT_DIAG_FILE");if(!out)return;std::ofstream f(out);f<<"metric,calls,unique,bytes,inclusive_ms\n";for(auto& x:data)f<<x.first<<","<<x.second.calls<<","<<x.second.paths.size()<<","<<x.second.bytes<<","<<x.second.ms<<"\n";} };
inline State& state(){static State s;return s;}
inline void hit(const char* name,const std::filesystem::path& p={},unsigned long long bytes=0){auto& s=state();std::lock_guard<std::mutex> l(s.mutex);auto& e=s.data[name];e.calls++;e.bytes+=bytes;if(!p.empty())e.paths.insert(p.generic_string());}
struct Scope {const char* name;std::chrono::steady_clock::time_point start;Scope(const char* n):name(n),start(std::chrono::steady_clock::now()){hit(n);}~Scope(){double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();auto& s=state();std::lock_guard<std::mutex> l(s.mutex);s.data[name].ms+=ms;} };
}
