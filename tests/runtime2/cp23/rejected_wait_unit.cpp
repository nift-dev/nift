// Refused waits must release only their own record registrations and roots.
#include "rt/parallel_task.h"
#include "rt/shared_state.h"
#include "rt/shared_state_internal.h"
#include "rt/future.h"
#include "rt_test.h"
#include "rt_vm_tool.h"
#include <chrono>
#include <functional>
#include <thread>
using namespace nift::rt;
namespace {
bool until(const std::function<bool()>& ready) {
 auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
 while(!ready()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 return ready();
}
void run() {
 CompileLimits limits;limits.parallel_tasks=true;
 auto code=rt_vm_tool::compile("fn[async](child()){return 7};fn[async](hold(c)){return recv(c)};fn[async](own(c)){p:=child();while(!c.load()){};return await p};fn[async](borrow(p)){return syncwait(p)};fn(syncwait(p)){scratch:=[1,2,3];try{return (await p)+scratch[0]}catch(e){print(\"unexpected catch\");throw e}}",limits);
 if(!code.program)std::fprintf(stderr,"compile refusal: %s\n",code.reason.c_str());
 CHECK(code.program!=nullptr);if(!code.program)return;
 std::shared_ptr<const Program> program(std::move(code.program));auto map=std::make_shared<ProgramTransferMap>(*program);
 auto pool=std::make_shared<ParallelRuntime>(1,16384,1);SharedOpError error;
 Value channel=shared_make_channel(0,error),gate=shared_make_atomic_bool(false,error);
 auto start=[&](int proto,const Value& arg,std::shared_ptr<ParallelWait> state={}){
  auto r=std::make_shared<ParallelTaskRecord>();r->program=program;r->proto_map=map;r->runtime=pool;
  r->input.entry_proto=proto;r->input.argc=1;r->input.args.push_back(arg);r->input.proto_map=map.get();r->options.parallel_wait=state;
  if(state)add_parallel_wait_state(r,state);
  std::string why;CHECK(start_parallel_task(r,why));return r;
 };
 auto held=start(2,channel);CHECK(until([&]{return pool->blocked()==1;}));
 auto legitimate=std::make_shared<ParallelWait>();add_parallel_wait_state(held,legitimate);
 {std::lock_guard<std::mutex> lock(held->mutex);held->waiters.push_back({777,nullptr});}
 Value future=Value::adopt(new FutureObject());future.heap_as<FutureObject>()->parallel=held;
 for(int i=0;i<3;++i){
  auto state=std::make_shared<ParallelWait>();std::weak_ptr<ParallelWait> weak=state;
  auto actor=start(4,future,state);CHECK(until([&]{return actor->complete()&&pool->active()==1;}));
  CHECK(actor->failure_code=="limit.exceeded");CHECK(!actor->failure_catchable);CHECK(actor->output.empty());
  {std::lock_guard<std::mutex> lock(held->mutex);CHECK(held->wait_states.size()==1);CHECK(held->waiters.size()==1);CHECK(held->waiters.front().task==777);CHECK(held->wait_observers.empty());}
  actor.reset();state.reset();shared_collect();CHECK(weak.expired());
 }
 auto actor=start(3,gate);std::shared_ptr<ParallelTaskRecord> own_record;
 CHECK(until([&]{auto& registry=shared_detail::registry();std::lock_guard<std::recursive_mutex> lock(registry.mutex);
  for(auto& entry:registry.tasks)if(auto r=entry.second.lock())if(r!=held&&r!=actor){own_record=r;return true;}
  return false;}));
 CHECK(shared_atomic_store(gate,Value::boolean(true),error));
 CHECK(until([&]{return actor->complete()&&pool->active()==1;}));CHECK(actor->failure_code=="limit.exceeded");CHECK(!actor->failure_catchable);CHECK(actor->output.empty());
 CHECK(own_record!=nullptr);
 if(own_record){std::lock_guard<std::mutex> lock(own_record->mutex);CHECK(own_record->waiters.empty());}
 actor.reset();pool.reset();CHECK(held->complete());
 own_record.reset();future=Value();held.reset();legitimate.reset();channel=Value();gate=Value();
 shared_collect();CHECK(shared_registry_live_for_test()==0);
}
void shared_target() {
 CompileLimits limits;limits.parallel_tasks=true;
 auto code=rt_vm_tool::compile("fn[async](reader(p)){return await p}",limits);
 CHECK(code.program!=nullptr);if(!code.program)return;
 std::shared_ptr<const Program> program(std::move(code.program));auto map=std::make_shared<ProgramTransferMap>(*program);
 auto pool=std::make_shared<ParallelRuntime>(1,16384,1);
 auto target=std::make_shared<ParallelTaskRecord>();target->proto_map=map;
 Value future=Value::adopt(new FutureObject());future.heap_as<FutureObject>()->parallel=target;
 auto state=std::make_shared<ParallelWait>();auto owner=std::make_shared<ParallelWait>();add_parallel_wait_state(target,owner);
 auto start=[&]{
  auto r=std::make_shared<ParallelTaskRecord>();r->program=program;r->proto_map=map;r->runtime=pool;
  r->input.entry_proto=1;r->input.argc=1;r->input.args.push_back(future);r->input.proto_map=map.get();
  r->options.parallel_wait=state;add_parallel_wait_state(r,state);std::string why;CHECK(start_parallel_task(r,why));return r;
 };
 auto first=start();CHECK(until([&]{return pool->blocked()==1;}));
 const ParallelWaitRegistration* identity=nullptr;
 {std::lock_guard<std::mutex> lock(target->mutex);CHECK(target->wait_observers.size()==1);
  if(!target->wait_observers.empty())identity=target->wait_observers.front().registration;}
 // The second scheduler shares the same notification target, but not its lease.
 for(int i=0;i<3;++i){auto rejected=start();CHECK(until([&]{return rejected->complete()&&pool->active()==1;}));
  CHECK(rejected->failure_code=="limit.exceeded");
  std::lock_guard<std::mutex> lock(target->mutex);CHECK(target->wait_observers.size()==1);
  if(!target->wait_observers.empty())CHECK(target->wait_observers.front().registration==identity);
  CHECK(target->wait_states.size()==1);}
 SharedOpError error;Value handle=Value::adopt(new FutureObject());handle.heap_as<FutureObject>()->parallel=first;
 CHECK(shared_cancel(handle,error));CHECK(until([&]{return first->complete()&&pool->active()==0;}));
 CHECK(first->failure_code=="runtime.cancelled");CHECK(target->snapshot_state()==ParallelTaskState::Pending);
 {std::lock_guard<std::mutex> lock(target->mutex);CHECK(target->wait_observers.empty());CHECK(target->wait_states.size()==1);}
 // Exercise cancellation both before publication and after a real observer exists.
 for(int i=0;i<20;++i){
  auto raced=start();
  if(i%2)CHECK(until([&]{std::lock_guard<std::mutex> lock(target->mutex);return !target->wait_observers.empty();}));
  Value cancel_handle=Value::adopt(new FutureObject());cancel_handle.heap_as<FutureObject>()->parallel=raced;
  CHECK(shared_cancel(cancel_handle,error));CHECK(until([&]{return raced->complete()&&pool->active()==0;}));
  CHECK(raced->failure_code=="runtime.cancelled");
  {std::lock_guard<std::mutex> lock(target->mutex);CHECK(target->wait_observers.empty());CHECK(target->wait_states.size()==1);}
 }
 auto successful=start();CHECK(until([&]{return pool->blocked()==1;}));
 {std::lock_guard<std::mutex> lock(target->mutex);std::string why;
  CHECK(target->result.encode(Value::integer(7),*map,why)==TransferStatus::Ok);
  target->has_result=true;target->state=ParallelTaskState::Done;}
 notify_parallel_wait_states(target);target->cv.notify_all();
 CHECK(until([&]{return successful->complete()&&pool->active()==0;}));CHECK(successful->snapshot_state()==ParallelTaskState::Done);
 {std::lock_guard<std::mutex> lock(target->mutex);CHECK(target->wait_observers.empty());CHECK(target->wait_states.size()==1);}
 {Value result;std::string why;CHECK(successful->result.decode(*map,result,why)==TransferStatus::Ok);CHECK(result.is(Kind::Int)&&result.as_int()==7);}
 pool.reset();successful.reset();handle=Value();first.reset();future=Value();target.reset();state.reset();owner.reset();
 shared_collect();CHECK(shared_registry_live_for_test()==0);
}

}
int main(){run();shared_target();return rt_test::finish("cp23-rejected-wait-unit");}
