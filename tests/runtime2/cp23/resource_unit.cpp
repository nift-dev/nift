// CP17 ownership exercised inside real worker VMs, including unwind/cancel
// and refusal at the transfer boundary. No ProcessGroup or JobTable owner added.
#include "rt/parallel_task.h"
#include "rt/shared_state.h"
#include "rt_test.h"
#include "rt_vm_tool.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#if defined(__linux__)
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#endif
using namespace nift::rt;
namespace {
int descriptors() {
#if defined(__linux__)
 int count=0;DIR* dir=opendir("/proc/self/fd");if(!dir)return -1;
 while(readdir(dir)) { ++count; }
 closedir(dir);return count;
#else
 return 0; // descriptor audit is Linux-only; functional cases remain portable
#endif
}
void check(const std::string& source,StopReason expected,const std::string& stdout_expected, std::size_t workers=2) {
 const int before=descriptors();CompileLimits limits;limits.parallel_tasks=true;
 auto compiled=rt_vm_tool::compile(source,limits);CHECK(compiled.program!=nullptr);
 if(!compiled.program){std::fprintf(stderr,"refusal: %s\n",compiled.reason.c_str());return;}
 auto map=SourceMap::single("resource.f",source);
 {
  VmOptions options;options.parallel=std::make_shared<ParallelRuntime>(workers);options.source_map=&map;
  Vm vm(*compiled.program,options);std::string output;auto result=vm.run(output);
  if(result.reason!=expected||output!=stdout_expected)std::fprintf(stderr,"reason=%d message=%s out=[%s]\n",int(result.reason),result.message.c_str(),output.c_str());
  CHECK(result.reason==expected);CHECK(output==stdout_expected);
 }
 CHECK(descriptors()==before);CHECK(shared_registry_live_for_test()==0);
}
}
int main(){
 auto root=std::filesystem::temp_directory_path()/"cp23-resource-unit";
#if defined(__linux__)
 root+=std::to_string(getpid());
#endif
 std::filesystem::create_directories(root);auto input=root/"input";auto output=root/"discard";
 {std::ofstream file(input);file<<"owned\n";}
 const std::string read="f:=file(\""+input.string()+"\");f.open(\"r\");";
 const std::string write="f:=file(\""+output.string()+"\");f.open(\"w\");f.write(\"discard\");";
 check("fn[async](p()){"+read+"x:=f.read_all();f.close();return x};print(await p())",StopReason::None,"owned\n\n");
 check("fn[async](p()){"+read+"return 1};print(await p())",StopReason::Error,"");
 check("fn[async](p()){try{"+write+"throw error(\"x\")}catch(e){return 1}};print(await p())",StopReason::None,"1\n");
 CHECK(!std::filesystem::exists(output));
 check("c:=channel(0);fn[async](p()){"+write+"recv(c);return 1};a:=p();sleep(20);cancel(a);try{await a}catch(e){print(e.code)}",StopReason::None,"runtime.cancelled\n");
 CHECK(!std::filesystem::exists(output));
 {
  CompileLimits limits;limits.parallel_tasks=true;
  auto refusal=rt_vm_tool::compile(read+"fn[async](p(x)){return type(x)};a:=p(f);await a",limits);
  CHECK(!refusal.program);CHECK(refusal.reason=="host-handle-flow");
 }
 check("fn[async](p()){s:=ifstream(\""+input.string()+"\");x:=s.read_all();close(s);return x};print(await p())",StopReason::None,"owned\n\n");
 check("fn[async](p()){s:=ifstream(\""+input.string()+"\");x:=s.read_all();close(s);return x.length()};a:=p();b:=p();print(await a);print(await b)",StopReason::None,"6\n6\n");
#if defined(__linux__)
 auto fifo=root/"fifo";CHECK(mkfifo(fifo.c_str(),0600)==0);
 check("fn[async](r()){s:=ifstream(\""+fifo.string()+"\");x:=s.read(1);close(s);return x};fn[async](w()){s:=ofstream(\""+fifo.string()+"\");s.write(\"x\");close(s);return 1};a:=r();b:=w();print(await a);print(await b)",StopReason::None,"x\n1\n",1);
#endif
 // Unsupported live resource kinds cannot acquire a second owner via a graph.
 auto compiled=rt_vm_tool::compile("print(1)");CHECK(compiled.program!=nullptr);
 ProgramTransferMap map(*compiled.program);TransferGraph graph;std::string why;
 for(auto kind:{HandleKind::File,HandleKind::Stream,HandleKind::Process,HandleKind::Command,HandleKind::Module,HandleKind::Timer,HandleKind::Ffi})
  CHECK(graph.encode(Value::handle(kind,7,1),map,why)==TransferStatus::RefusedHandle);
 std::filesystem::remove_all(root);
 return rt_test::finish("cp23-resource-unit");
}
