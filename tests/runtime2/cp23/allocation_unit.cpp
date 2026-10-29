// One real allocation fails per run, separately on the submitting thread and
// worker threads. Fresh processes make termination/hangs/leaks observable.
#include "rt/parallel_task.h"
#include "rt/shared_state.h"
#include "rt/heap_account.h"
#include "rt/heap.h"
#include "rt/aggregate.h"
#include "rt/string.h"
#include "rt/transfer_heap.h"
#include "rt_vm_tool.h"
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <thread>
#include <cstring>
using namespace nift::rt;
namespace {
std::thread::id root;
std::atomic<bool> armed{false}, hit{false};
std::atomic<long> source_sites{0}, worker_sites{0};
long fail_site=-1;
bool fail_worker=false, persistent=false;
thread_local long sites=0;
}
void* operator new(std::size_t n) {
 if (armed.load(std::memory_order_relaxed)) {
  const bool worker=std::this_thread::get_id()!=root;
  const long index=sites++;
  (worker?worker_sites:source_sites).fetch_add(1,std::memory_order_relaxed);
  if (worker==fail_worker && index>=fail_site && fail_site>=0 &&
      ((index==fail_site && !hit.exchange(true)) || (persistent && hit.load()))) throw std::bad_alloc();
 }
 if (void* p=std::malloc(n?n:1)) return p;
 throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
int main(int argc,char** argv) {
 root=std::this_thread::get_id();
 fail_site=argc>1?std::strtol(argv[1],nullptr,10):-1;
 fail_worker=argc>2&&std::strcmp(argv[2],"worker")==0;
 persistent=argc>3&&std::strcmp(argv[3],"persistent")==0;
 std::string source="c:=channel(1); fn[async](f()){o:={\"x\":41};send(c,o);return o.x+1};p:=f();print(await p);recv(c)";
 if(argc>4&&std::strcmp(argv[4],"cancelled")==0)
  source="fn[async](f()){sleep(10);return 1};p:=f();sleep(2);cancel(p);try{await p}catch(e){print(e.code)}";
 if(argc>4&&std::strcmp(argv[4],"timers")==0)
  source="fn[async](f()){sleep(2);return 1};a:=f();b:=f();c:=f();d:=f();print(await a);print(await b);print(await c);print(await d)";
 const bool cycle=argc>4&&std::strcmp(argv[4],"cycle")==0;
 if(argc>4&&std::strcmp(argv[4],"unawaited")==0)
  source="c:=channel(1);fn[async](f()){o:={\"x\":41};send(c,o);return o.x+1};p:=f();print(0)";
 CompileLimits limits;limits.parallel_tasks=true;
 auto compiled=rt_vm_tool::compile(source,limits);
 if (!compiled.program) return 10;
 auto map=SourceMap::single("oom.f",source);
 TransferGraph graph;ProgramTransferMap protos(*compiled.program);
 if(cycle){
  Value root_value=make_array();auto* array=root_value.heap_as<ArrayObject>();
  array->items.push_back(root_value);array->items.push_back(make_string("a long heap string in a partial cyclic graph"));
  Value cell=Value::adopt(new CellObject());cell.heap_as<CellObject>()->value=root_value;
  array->items.push_back(cell);std::string error;
  if(graph.encode(root_value,protos,error)!=TransferStatus::Ok)return 14;
  cell.heap_as<CellObject>()->value=Value();array->items.clear();
 }
 const auto before=live_heap_bytes()-heap_charged_bytes();
 sites=0;armed=true;
 StopReason reason=StopReason::None;
 try {
  if(cycle){
   TransferHeapBoundary boundary;Value out;std::string error;
   if(graph.decode(protos,out,error)!=TransferStatus::Ok)return 15;
  } else {
  VmOptions options;options.parallel=std::make_shared<ParallelRuntime>(1);options.source_map=&map;
  Vm vm(*compiled.program,options);std::string output;reason=vm.run(output).reason;
  }
 } catch (const std::bad_alloc&) {reason=StopReason::Internal;}
 armed=false;
 const auto after=live_heap_bytes()-heap_charged_bytes();
 std::printf("site=%ld role=%s hit=%d source=%ld worker=%ld reason=%d live_before=%zu live_after=%zu registry=%zu\n",fail_site,fail_worker?"worker":"source",int(hit.load()),source_sites.load(),worker_sites.load(),int(reason),before,after,shared_registry_live_for_test());
 if (after!=before || shared_registry_live_for_test()!=0) return 11;
 if (fail_site>=0 && !hit) return 12;
 if (hit && reason!=StopReason::Internal) return 13;
 return 0;
}
