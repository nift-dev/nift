// Logical task admission, CPU execution, timer parking and native stacks have
// separate budgets. Default capacity must not depend on hardware topology.
#include "rt/parallel_task.h"
#include "rt/shared_state.h"
#include "rt/future.h"
#include "rt/shared_state_internal.h"
#include "rt_test.h"
#include "rt_vm_tool.h"
#include <chrono>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <thread>
using namespace nift::rt;
namespace {
void run(std::size_t permits, const std::string& source, const std::string& expected, std::size_t compensation = ParallelRuntime::kTimerWorkerCapacity) {
 CompileLimits limits;limits.parallel_tasks=true;
 auto code=rt_vm_tool::compile(source,limits);CHECK(code.program!=nullptr);
 if(!code.program){std::fprintf(stderr,"admission refusal: %s\n",code.reason.c_str());return;}
 auto map=SourceMap::single("admission.f",source);
 auto pool=std::make_shared<ParallelRuntime>(permits);
 CHECK(pool->task_capacity()==16384);CHECK(pool->blocking_capacity()==1024);
 VmOptions options;options.parallel=pool;options.source_map=&map;
 const auto start=std::chrono::steady_clock::now();
 std::atomic<bool> stop{false};std::size_t peak=0;
 std::thread monitor([&]{while(!stop.load()){peak=std::max(peak,pool->executing());std::this_thread::sleep_for(std::chrono::milliseconds(1));}});
 {
  Vm vm(*code.program,options);std::string output;auto result=vm.run(output);
  if(result.reason!=StopReason::None||output!=expected)std::fprintf(stderr,"sleep admission permits=%zu: reason=%d error=%s output=[%s]\n",permits,int(result.reason),result.message.c_str(),output.c_str());
  CHECK(result.reason==StopReason::None);CHECK(output==expected);
 }
 stop=true;monitor.join();CHECK(peak<=permits);
 pool->wait_idle();CHECK(pool->active()==0);CHECK(pool->executing()==0);
 CHECK(pool->worker_count()<=permits+compensation);
 const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
 std::printf("admission: permits=%zu workers=%zu peak_executing=%zu wall=%.6f (local diagnostic only)\n",permits,pool->worker_count(),peak,wall);
 pool.reset();shared_collect();CHECK(shared_registry_live_for_test()==0);
}
void timer_releases_cpu() {
 CompileLimits limits;limits.parallel_tasks=true;
 auto code=rt_vm_tool::compile("fn[async](f()){sleep(2000);return 1}",limits);
 CHECK(code.program!=nullptr);if(!code.program)return;
 std::shared_ptr<const Program> program(std::move(code.program));
 auto pool=std::make_shared<ParallelRuntime>(1);
 auto map=std::make_shared<ProgramTransferMap>(*program);
 auto record=std::make_shared<ParallelTaskRecord>();record->program=program;record->proto_map=map;record->runtime=pool;
 record->input.entry_proto=1;record->input.proto_map=map.get();std::string why;
 CHECK(start_parallel_task(record,why));
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
 bool parked=false;
 while(std::chrono::steady_clock::now()<deadline){
  if(pool->worker_count()>0&&pool->active()==1&&pool->executing()==0&&pool->blocked()==1){parked=true;break;}
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 CHECK(parked);CHECK(record->timer_help_safe);
 pool.reset();CHECK(record->complete());record.reset();shared_collect();CHECK(shared_registry_live_for_test()==0);
}
void small_heap_isolates() {
 CompileLimits limits;limits.parallel_tasks=true;
 auto code=rt_vm_tool::compile("fn[async](f()){sleep(10);return 7}",limits);
 CHECK(code.program!=nullptr);if(!code.program)return;
 std::shared_ptr<const Program> program(std::move(code.program));
 auto map=std::make_shared<ProgramTransferMap>(*program);auto pool=std::make_shared<ParallelRuntime>(1);
 std::vector<std::shared_ptr<ParallelTaskRecord>> records;
 for(int i=0;i<8;++i){
  auto r=std::make_shared<ParallelTaskRecord>();r->program=program;r->proto_map=map;r->runtime=pool;
  r->input.entry_proto=1;r->input.proto_map=map.get();r->options.heap_budget=32u<<10;
  std::string why;CHECK(start_parallel_task(r,why));records.push_back(std::move(r));
 }
 pool->wait_idle();for(auto& r:records)CHECK(r->snapshot_state()==ParallelTaskState::Done);
 pool.reset();records.clear();shared_collect();CHECK(shared_registry_live_for_test()==0);
}
void blocking_limit() {
 CompileLimits limits;limits.parallel_tasks=true;
 auto code=rt_vm_tool::compile("fn[async](f(c)){recv(c);return 1};fn[async](snd(c)){send(c,99);return 1};fn[async](awaitit(p)){return await p}",limits);
 CHECK(code.program!=nullptr);if(!code.program)return;
 std::shared_ptr<const Program> program(std::move(code.program));
 auto map=std::make_shared<ProgramTransferMap>(*program);
 auto pool=std::make_shared<ParallelRuntime>(1,ParallelRuntime::kTaskCapacity,64);
 SharedOpError error;Value channel=shared_make_channel(0,error);
 std::vector<std::shared_ptr<ParallelTaskRecord>> records;
 auto start=[&](const Value& c,int entry){
  auto r=std::make_shared<ParallelTaskRecord>();r->program=program;r->proto_map=map;r->runtime=pool;
  r->input.entry_proto=entry;r->input.argc=1;r->input.args.push_back(c);r->input.proto_map=map.get();
  std::string why;CHECK(start_parallel_task(r,why));records.push_back(r);return r;
 };
 for(int i=0;i<64;++i)start(channel,1);
 auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 while(pool->blocked()!=64&&std::chrono::steady_clock::now()<deadline)
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 CHECK(pool->blocked()==64);
 auto failed=[&](const Value& c,int entry){
  auto r=start(c,entry);deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(!r->complete()&&std::chrono::steady_clock::now()<deadline)
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(r->complete());if(!r->complete())return;
  std::lock_guard<std::mutex> lock(r->mutex);
  CHECK(r->state==ParallelTaskState::Failed);CHECK(r->failure_reason==StopReason::Error);
  CHECK(r->failure_code=="limit.exceeded");CHECK(r->failure_message=="parallel blocking wait limit exceeded");
 };
 failed(channel,1);
 Value future=Value::adopt(new FutureObject());
 future.heap_as<FutureObject>()->parallel=records.front();
 future.heap_as<FutureObject>()->collect_parallel=shared_collect;
 failed(future,3);
 Value rendezvous=shared_make_channel(0,error);failed(rendezvous,2);
 {auto& reg=shared_detail::registry();std::lock_guard<std::recursive_mutex> lock(reg.mutex);
  auto entry=shared_detail::find_channel(rendezvous);CHECK(entry->buffer.empty());}
 Value full=shared_make_channel(1,error);
 CHECK(shared_channel_send(full,Value::integer(7),map.get(),{},error));failed(full,2);
 Value out;bool closed=false;CHECK(shared_channel_recv(full,map.get(),{},out,closed,error));CHECK(out.is(Kind::Int)&&out.as_int()==7);
 CHECK(pool->worker_count()<=65);pool.reset();
 for(auto& r:records)CHECK(r->complete());
 records.clear();future=Value();channel=Value();rendezvous=Value();full=Value();shared_collect();CHECK(shared_registry_live_for_test()==0);
}

}
int main(){
 std::setbuf(stdout,nullptr);
 timer_releases_cpu();
 small_heap_isolates();
 blocking_limit();
 for(std::size_t p:{1,2,4,8}){
  for(int n:{200,10000}){
   const std::string source="fn[async](f(i)){sleep(10);return i};hs:=[];i:=0;while(i<"+std::to_string(n)+"){hs.push(f(i));i+=1};s:=0;for(h:hs){s+=await h};print(s);print(await hs[0]);print(await hs[0])";
   run(p,source,std::to_string(std::int64_t(n)*(n-1)/2)+"\n0\n0\n");
  }
  run(p,"fn[async](f(i)){sleep(40);return i};hs:=[];i:=0;while(i<200){hs.push(f(i));i+=1};for(h:hs){cancel(h)};for(h:hs){try{await h}catch(e){if(e.code!=\"runtime.cancelled\"){throw e}}};print(200)","200\n");
  run(p,"fn[async](f()){sleep(10);return \"owned heap constant across timer helpers\"};hs:=[];i:=0;while(i<200){hs.push(f());i+=1};print(await hs[0]);print(await hs[199])","owned heap constant across timer helpers\nowned heap constant across timer helpers\n");
  run(p,"fn[async](f(n)){if(n==0){return 0};return (await f(n-1))+1};print(await f(100))","100\n",ParallelRuntime::kBlockedCapacity);
  // Helping a reader of its sleeping ancestor would deadlock the C++ stack.
  run(p,"fn[async](p()){sleep(10);return 7};fn[async](q(a)){return await a};a:=p();b:=q(a);print(await b)","7\n");
 }
 return rt_test::finish("cp23-admission-unit");
}
