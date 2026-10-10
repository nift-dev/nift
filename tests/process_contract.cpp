#include "Proc.h"
#include "ProcessPOSIX.h"
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <signal.h>
#endif
static void require(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
void nift_process_test_child(long long) {}
void nift_process_test_temp(const std::string&) {}
static std::string parent_env();
static std::mutex mutex;
static std::condition_variable cv;
static int entered=0;
static bool rendezvous=false;
bool nift_process_test_fail(const char* op,size_t){
 if(rendezvous && std::string(op)=="launch"){
  require(parent_env()=="original","parent transient environment mutation");
  std::unique_lock<std::mutex> lock(mutex);++entered;cv.notify_all();cv.wait(lock,[]{return entered==2;});
 }
 return false;
}
static std::string parent_env(){
#ifdef _WIN32
 SetLastError(ERROR_SUCCESS);wchar_t v[128];DWORD n=GetEnvironmentVariableW(L"NIFT_PROCESS_TEST_ENV",v,128);
 if(n==0)return GetLastError()==ERROR_ENVVAR_NOT_FOUND?"absent":"empty";
 std::wstring w(v,n);return std::string(w.begin(),w.end());
#else
 const char* p=getenv("NIFT_PROCESS_TEST_ENV");return p?(p[0]?p:"empty"):"absent";
#endif
}
int main(int argc,char**argv){
#ifndef _WIN32
 if(argc>2 && std::string(argv[1])=="fdprobe") { return fcntl(std::stoi(argv[2]),F_GETFD)==-1 ? 0 : 1; }
#endif
 if(argc>1 && std::string(argv[1])=="echo"){std::cout<<std::cin.rdbuf();return 0;}
 if(argc>1 && std::string(argv[1])=="child"){
  std::cout<<parent_env()<<'\n';std::cerr<<"err\n";return 7;
 }
#ifdef _WIN32
 if(argc>2 && std::string(argv[1])=="sentinel"){
  HANDLE h=reinterpret_cast<HANDLE>(std::stoull(argv[2]));DWORD flags;
  return GetHandleInformation(h,&flags)?1:0;
 }
 if(argc>1 && std::string(argv[1])=="block"){Sleep(60000);return 0;}
#else
 if(argc>1 && std::string(argv[1])=="block"){for(;;)pause();}
#endif
 std::string self=std::filesystem::absolute(argv[0]).string();ProcessSpec s;s.program=self;s.args={"child"};
 for(std::string parent:{"absent","empty","original"}){
#ifdef _WIN32
  SetEnvironmentVariableW(L"NIFT_PROCESS_TEST_ENV",parent=="absent"?nullptr:parent=="empty"?L"":L"original");
#else
  if(parent=="absent")unsetenv("NIFT_PROCESS_TEST_ENV");else setenv("NIFT_PROCESS_TEST_ENV",parent=="empty"?"":"original",1);
#endif
  for(std::string value:{"inherit","","override"}){
   s.env.clear();if(value!="inherit")s.env["NIFT_PROCESS_TEST_ENV"]=value;
   auto r=nift_run_process(s);std::string expected=value=="inherit"?parent:value.empty()?"empty":value;
   require(r.launched&&r.error.empty()&&r.exit_code==7&&r.out==expected+"\n"&&r.err=="err\n","env/capture/exit contract");
   require(parent_env()==parent,"parent environment changed");
  }
 }
 s.env.clear();s.env["NIFT_PROCESS_TEST_ENV"]="A";auto b=s;b.env["NIFT_PROCESS_TEST_ENV"]="B";
 ProcessResult ra,rb;
#ifdef _WIN32
 rendezvous=true;
#endif
 std::thread a([&]{ra=nift_run_process(s);}),t([&]{rb=nift_run_process(b);});a.join();t.join();rendezvous=false;
 require(ra.out=="A\n"&&rb.out=="B\n"&&parent_env()=="original","concurrent env isolation");
 auto dir=std::filesystem::temp_directory_path()/std::filesystem::path("nift-process-contract-"+std::to_string(
#ifdef _WIN32
 GetCurrentProcessId()
#else
 getpid()
#endif
 ));std::filesystem::create_directory(dir);
 for(int route=0;route<3;++route){auto redirect=s;std::string bad=(dir/"missing"/"file").string();if(route==0)redirect.stdin_path=bad;if(route==1)redirect.stdout_path=bad;if(route==2)redirect.stderr_path=bad;auto r=nift_run_process(redirect);require(r.exit_code!=7 && r.out.empty(),"failed redirect ran child");}
 auto echo=s;echo.args={"echo"};auto pipeline=nift_run_pipeline({s,echo,echo});require(pipeline.exit_code==0&&pipeline.out=="A\n"&&pipeline.err=="err\n","pipeline bytes/EOF");
 auto cwd=s;cwd.cwd=dir;require(nift_run_process(cwd).exit_code==7,"cwd contract");
 auto missing=s;missing.program="nift-missing-executable-411";auto mr=nift_run_process(missing);require(mr.exit_code!=7,"missing executable contract");
#ifdef _WIN32
 SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE sentinel=CreateEventW(&sa,TRUE,FALSE,nullptr);require(sentinel!=nullptr,"sentinel creation");
 auto check=s;check.args={"sentinel",std::to_string(reinterpret_cast<uintptr_t>(sentinel))};require(nift_run_process(check).exit_code==0,"unrelated inheritable handle leaked");CloseHandle(sentinel);
 DWORD before,after;GetProcessHandleCount(GetCurrentProcess(),&before);
 for(int i=0;i<30;++i){auto r=nift_run_process(s);require(r.exit_code==7,"repeated capture");}
 GetProcessHandleCount(GetCurrentProcess(),&after);require(before==after,"process handle leak");
#endif
#ifndef _WIN32
 int ends[2];{std::lock_guard<std::mutex> lock(nift_process_launch_mutex());require(nift_process_pipe(ends),"control pipe creation");}
 for(int fd:ends){auto probe=s;probe.args={"fdprobe",std::to_string(fd)};require(nift_run_process(probe).exit_code==0,"CLOEXEC descriptor leaked into exec");close(fd);}
 // PATH supplied to the child, relative entries after cwd, and ENOEXEC fallback.
 auto script=dir/"parent-prepared-tool";{std::ofstream f(script);f<<"printf prepared\n";}std::filesystem::permissions(script,std::filesystem::perms::owner_all);
 auto path=s;path.program="parent-prepared-tool";path.args.clear();path.env["PATH"]="";path.cwd=dir;
 auto pr=nift_run_process(path);require(pr.exit_code==0 && pr.out=="prepared","child PATH/ENOEXEC contract");
#endif
 std::filesystem::remove_all(dir);std::cout<<"process contracts PASS\n";
}
