#include <algorithm>
#include "Proc.h"
#include <iostream>
#include <string>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
static std::vector<std::filesystem::path> captures;
void nift_process_test_temp(const std::string& path){captures.emplace_back(path);}
static std::string operation;
static size_t failure_stage;
#ifdef _WIN32
static std::vector<HANDLE> children;
void nift_process_test_child(long long pid){children.push_back(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,(DWORD)pid));}
#else
void nift_process_test_child(long long) {}
#endif
bool nift_process_test_fail(const char* op, size_t stage) {
 bool fail=operation==op&&stage==failure_stage;
#ifdef _WIN32
 if(fail)SetLastError(ERROR_NOT_ENOUGH_MEMORY);
#endif
 return fail;
}
static void require(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';for(const auto& path:captures){std::error_code ec;std::filesystem::remove(path,ec);}std::exit(1);}}
#ifndef _WIN32
static size_t fds(){size_t n=0;for(int i=0;i<1024;++i)if(fcntl(i,F_GETFD)!=-1)++n;return n;}

#endif
int main(int argc,char**argv){
 if(argc>1 && std::string(argv[1])=="exit")return 0;
 if(argc>1 && std::string(argv[1])=="block"){
#ifdef _WIN32
 Sleep(60000);
#else
 for(;;)pause();
#endif
 return 0;
 }
 if(argc<3)return 2;
 operation=argv[1];failure_stage=std::stoul(argv[2]);
#ifndef _WIN32
 size_t before=fds();
 ProcessSpec s;s.program="/bin/sleep";s.args={"60"};
 std::vector<ProcessSpec> specs(3,s);
 ProcessResult r;
 if(operation.rfind("dup2",0)==0){
  specs.resize(1);for(auto& spec:specs){spec.merge_stderr=true;spec.stdin_path="/dev/null";spec.stdout_path="/dev/null";}
  if(argc>3){ShellJobTable jobs;r=jobs.launch(specs,"routing fixture",true);}else r=nift_run_pipeline(specs);
  require(r.exit_code==126,"dup2 setup did not fail closed");
 }
 else if(operation=="temp")r=nift_run_process(s);
 else if(argc>3){ShellJobTable jobs;r=jobs.launch(specs,"failure fixture",false);}
 else r=nift_run_pipeline(specs);
 if(operation.rfind("dup2",0)!=0)require(!r.error.empty(),"missing failure diagnostic");
 require(fds()==before,"fd leak");for(const auto& path:captures)require(!std::filesystem::exists(path),"temp leak");
 int st;require(waitpid(-1,&st,WNOHANG)==-1&&errno==ECHILD,"live child or zombie");
#else
 // A successful launch control warms OS/CRT process initialization before
 // measuring retained handles from each failed operation. Report the control
 // counts so first-launch initialization cannot be hidden as failure cleanup.
 auto saved_operation=operation;operation.clear();
 ProcessSpec warm;warm.program=std::filesystem::absolute(argv[0]).u8string();warm.args={"exit"};
 for(int iteration=0;iteration<3;++iteration){DWORD start,end;GetProcessHandleCount(GetCurrentProcess(),&start);auto control=nift_run_process(warm);require(control.exit_code==0 && control.error.empty(),"launch control failed");for(HANDLE h:children){require(h && WaitForSingleObject(h,1000)==WAIT_OBJECT_0,"control child alive");CloseHandle(h);}children.clear();for(const auto& path:captures)require(!std::filesystem::exists(path),"control capture tempfile leak");captures.clear();GetProcessHandleCount(GetCurrentProcess(),&end);std::cout<<"control handles "<<iteration<<": "<<start<<" -> "<<end<<'\n';require(iteration==0 || start==end,"successful launch control leaks handles");}
 operation=saved_operation;
 for(int repeat=0;repeat<10;++repeat){children.clear();captures.clear();
 DWORD before,after;GetProcessHandleCount(GetCurrentProcess(),&before);
 ProcessSpec s;s.program=std::filesystem::absolute(argv[0]).string();s.args={"block"};
 auto r=nift_run_pipeline(std::vector<ProcessSpec>(3,s));require(!r.error.empty(),"missing failure diagnostic");
 for(HANDLE h:children){require(h && WaitForSingleObject(h,1000)==WAIT_OBJECT_0,"live child after failure");CloseHandle(h);}
 GetProcessHandleCount(GetCurrentProcess(),&after);if(before!=after)std::cerr<<"failure handles repeat "<<repeat<<": "<<before<<" -> "<<after<<'\n';require(before==after,"handle leak");
 for(const auto& path:captures)require(!std::filesystem::exists(path),"capture tempfile leak");
 }
#endif
 std::cout<<"PASS "<<operation<<' '<<failure_stage<<'\n';
}
