#include "Process.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <iostream>
#ifndef _WIN32
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
extern char **environ;
#else
#include <windows.h>
#endif
namespace fs=std::filesystem;
static std::string read_all(const fs::path&p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
bool nift_find_executable(const std::string& name,std::string& path){fs::path p(name);if(p.has_parent_path()){if(fs::exists(p)){path=fs::absolute(p).string();return true;}return false;}const char* pe=std::getenv("PATH");if(!pe)return false;
#ifdef _WIN32
char sep=';'; const char* exts[]={"",".exe",".cmd",".bat"};
#else
char sep=':'; const char* exts[]={""};
#endif
std::stringstream ss(pe);std::string d;while(std::getline(ss,d,sep))for(auto e:exts){fs::path c=fs::path(d)/(name+e);std::error_code ec;if(fs::exists(c,ec)&&!ec){
#ifndef _WIN32
if(::access(c.c_str(),X_OK)!=0)continue;
#endif
path=c.string();return true;}}return false;}
#ifndef _WIN32
static int open_temp(std::string& path){char t[]="/tmp/nift-proc-XXXXXX";int fd=mkstemp(t);if(fd>=0)path=t;return fd;}
ProcessResult nift_run_pipeline(const std::vector<ProcessSpec>& specs,bool capture,bool stream){ProcessResult r;if(specs.empty()){r.error="empty pipeline";return r;}std::string op,ep;int ofd=-1,efd=-1;if(capture){ofd=open_temp(op);efd=open_temp(ep);if(ofd<0||efd<0){r.error="cannot create capture files";return r;}}
std::vector<pid_t> pids;int prev=-1;for(size_t i=0;i<specs.size();++i){int pp[2]={-1,-1};if(i+1<specs.size()&&pipe(pp)!=0){r.error="pipe failed";return r;}posix_spawn_file_actions_t a;posix_spawn_file_actions_init(&a);if(prev>=0)posix_spawn_file_actions_adddup2(&a,prev,STDIN_FILENO);else if(!specs[i].stdin_path.empty()){int fd=open(specs[i].stdin_path.c_str(),O_RDONLY);if(fd>=0){posix_spawn_file_actions_adddup2(&a,fd,STDIN_FILENO);posix_spawn_file_actions_addclose(&a,fd);}}
if(i+1<specs.size())posix_spawn_file_actions_adddup2(&a,pp[1],STDOUT_FILENO);else if(!specs[i].stdout_path.empty()){int fl=O_WRONLY|O_CREAT|(specs[i].append_stdout?O_APPEND:O_TRUNC);int fd=open(specs[i].stdout_path.c_str(),fl,0666);if(fd>=0){posix_spawn_file_actions_adddup2(&a,fd,STDOUT_FILENO);posix_spawn_file_actions_addclose(&a,fd);}}else if(capture)posix_spawn_file_actions_adddup2(&a,ofd,STDOUT_FILENO);
if(specs[i].merge_stderr)posix_spawn_file_actions_adddup2(&a,STDOUT_FILENO,STDERR_FILENO);else if(!specs[i].stderr_path.empty()){int fl=O_WRONLY|O_CREAT|(specs[i].append_stderr?O_APPEND:O_TRUNC);int fd=open(specs[i].stderr_path.c_str(),fl,0666);if(fd>=0){posix_spawn_file_actions_adddup2(&a,fd,STDERR_FILENO);posix_spawn_file_actions_addclose(&a,fd);}}else if(capture)posix_spawn_file_actions_adddup2(&a,efd,STDERR_FILENO);
if(prev>=0)posix_spawn_file_actions_addclose(&a,prev);if(pp[0]>=0)posix_spawn_file_actions_addclose(&a,pp[0]);if(pp[1]>=0)posix_spawn_file_actions_addclose(&a,pp[1]);std::vector<std::string> avs;avs.push_back(specs[i].program);avs.insert(avs.end(),specs[i].args.begin(),specs[i].args.end());std::vector<char*> av;for(auto&s:avs)av.push_back(s.data());av.push_back(nullptr);pid_t pid=0;int rc=posix_spawnp(&pid,specs[i].program.c_str(),&a,nullptr,av.data(),environ);posix_spawn_file_actions_destroy(&a);if(prev>=0)close(prev);if(pp[1]>=0)close(pp[1]);prev=pp[0];if(rc!=0){if(prev>=0)close(prev);r.error="cannot execute "+specs[i].program;r.exit_code=127;break;}pids.push_back(pid);r.launched=true;}if(prev>=0)close(prev);int status=0;for(pid_t p:pids){int s=0;waitpid(p,&s,0);status=s;}if(r.launched)r.exit_code=WIFEXITED(status)?WEXITSTATUS(status):(WIFSIGNALED(status)?128+WTERMSIG(status):1);if(ofd>=0)close(ofd);if(efd>=0)close(efd);if(capture){r.out=read_all(op);r.err=read_all(ep);fs::remove(op);fs::remove(ep);if(stream){std::cout<<r.out;std::cerr<<r.err;}}return r;}
ProcessResult nift_run_process(const ProcessSpec&s,bool capture,bool stream){return nift_run_pipeline({s},capture,stream);}
#else
ProcessResult nift_run_pipeline(const std::vector<ProcessSpec>&,bool,bool){ProcessResult r;r.error="process execution backend not yet available on this Windows build";return r;}
ProcessResult nift_run_process(const ProcessSpec&s,bool c,bool st){return nift_run_pipeline({s},c,st);}
#endif
