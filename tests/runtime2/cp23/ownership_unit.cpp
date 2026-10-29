// Regression witnesses for CP23's isolate, scheduler and neutral ownership boundaries.
#include "rt/parallel_task.h"
#include "rt/atom.h"
#include "rt/shared_state.h"
#include "rt/shared_gc.h"
#include "rt/aggregate.h"
#include "rt/heap.h"
#include "rt/heap_account.h"
#include "rt/transfer_heap.h"
#include "rt/future.h"
#include "rt/compare.h"
#include "rt_test.h"
#include "rt_vm_tool.h"
#include <cstdio>
#include <thread>
using namespace nift::rt;
namespace {
struct EmptyMap : TransferProtoMap {
 bool index_of(const Proto*, std::uint32_t&) const override { return false; }
 const Proto* proto_of(std::uint32_t) const override { return nullptr; }
};
void vm_case(const std::string& source, const std::string& expected, std::size_t workers=1) {
 CompileLimits limits; limits.parallel_tasks=true;
 auto compiled=rt_vm_tool::compile(source,limits);
 CHECK(compiled.program != nullptr);
 if (!compiled.program) { std::fprintf(stderr,"refusal: %s\n",compiled.reason.c_str()); return; }
 auto runtime=std::make_shared<ParallelRuntime>(workers);
 auto map=SourceMap::single("ownership.f",source);
 {
  VmOptions options; options.parallel=runtime; options.source_map=&map;
  Vm vm(*compiled.program,options); std::string output; auto result=vm.run(output);
  if (result.reason!=StopReason::None || output!=expected)
   std::fprintf(stderr,"stop=%d message=%s output=[%s] expected=[%s]\n",int(result.reason),result.message.c_str(),output.c_str(),expected.c_str());
  CHECK(result.reason==StopReason::None); CHECK(output==expected);
 }
 CHECK(runtime->active()==0);
 CHECK(runtime->worker_count()<=64*workers);
 runtime.reset(); shared_collect(); CHECK(shared_registry_live_for_test()==0);
}
void neutral_cycles() {
 EmptyMap map; SharedOpError error;
 CHECK(shared_registry_live_for_test()==0);
 {
  Value c=shared_make_channel(1,error);
  CHECK(shared_channel_send(c,c,&map,{},error));
  CHECK(shared_registry_live_for_test()==1);
 }
 shared_collect(); CHECK(shared_registry_live_for_test()==0);
 {
  Value a=shared_make_channel(1,error),b=shared_make_channel(1,error);
  CHECK(shared_channel_send(a,b,&map,{},error)); CHECK(shared_channel_send(b,a,&map,{},error));
  a=Value(); shared_collect(); CHECK(shared_registry_live_for_test()==2);
 }
 shared_collect(); CHECK(shared_registry_live_for_test()==0);
 {
  Value m=shared_make_mutex(nullptr,&map,error);
  CHECK(shared_mutex_lock(m,123,{},error));
  CHECK(shared_mutex_set(m,123,m,&map,error)); CHECK(shared_mutex_unlock(m,123,error));
 }
 shared_collect(); CHECK(shared_registry_live_for_test()==0);
 {
  auto task=std::make_shared<ParallelTaskRecord>();
  Value f=Value::adopt(new FutureObject()); f.heap_as<FutureObject>()->parallel=task;
  std::string why;
  CHECK(task->result.encode(f,map,why)==TransferStatus::Ok);
  task->state=ParallelTaskState::Done; task->has_result=true;
  std::weak_ptr<ParallelTaskRecord> weak=task;
  Value decoded; CHECK(task->result.decode(map,decoded,why)==TransferStatus::Ok);
  CHECK(equal(f,decoded)); CHECK(same(f,decoded)==Sameness::Same); CHECK(hash(f)==hash(decoded));
  f=Value(); task.reset(); shared_collect(); CHECK(!weak.expired());
  decoded=Value(); shared_collect(); CHECK(weak.expired());
 }
}
void pool_lifetime() {
 CompileLimits limits; limits.parallel_tasks=true;
 auto code=rt_vm_tool::compile("fn[async](f(c)){recv(c);return 1}",limits);
 CHECK(code.program!=nullptr); if(!code.program)return;
 std::shared_ptr<const Program> program(std::move(code.program));
 auto map=std::make_shared<ProgramTransferMap>(*program);
 SharedOpError op;Value channel=shared_make_channel(0,op);
 auto pool=std::make_shared<ParallelRuntime>(1);
 std::vector<std::shared_ptr<ParallelTaskRecord>> records;
 for(int i=0;i<64;++i){
  auto r=std::make_shared<ParallelTaskRecord>();r->program=program;r->proto_map=map;r->runtime=pool;
  r->input.entry_proto=1;r->input.argc=1;r->input.args.push_back(channel);r->input.proto_map=map.get();
  std::string why;CHECK(start_parallel_task(r,why));records.push_back(r);
 }
 CHECK(pool->active()==64);CHECK(pool->worker_count()<=64);
 auto extra=std::make_shared<ParallelTaskRecord>();std::string why;
 CHECK(!pool->submit(extra,why));CHECK(why=="parallel task admission limit exceeded");
 // No task is joined explicitly: destruction cancels every outstanding native
 // wait and joins every OS worker before returning.
 pool.reset();
 for(auto& r:records)CHECK(r->complete());
 records.clear();channel=Value();shared_collect();CHECK(shared_registry_live_for_test()==0);
 auto scalar=rt_vm_tool::compile("fn[async](f()){return 7}",limits);
 CHECK(scalar.program!=nullptr);if(!scalar.program)return;
 program=std::shared_ptr<const Program>(std::move(scalar.program));map=std::make_shared<ProgramTransferMap>(*program);
 pool=std::make_shared<ParallelRuntime>(1);
 for(int i=0;i<100;++i){
  auto r=std::make_shared<ParallelTaskRecord>();r->program=program;r->proto_map=map;r->runtime=pool;
  r->input.entry_proto=1;r->input.proto_map=map.get();
  CHECK(start_parallel_task(r,why));
  {std::unique_lock<std::mutex> lock(r->mutex);r->cv.wait(lock,[&]{return r->state!=ParallelTaskState::Pending;});}
  pool->wait_idle();CHECK(r->snapshot_state()==ParallelTaskState::Done);
 }
 CHECK(pool->worker_count()==1);
}
void cancel_before_dispatch() {
 CompileLimits limits;limits.parallel_tasks=true;
 auto compiled=rt_vm_tool::compile("fn[async](f()){i:=0;while(i<1000000){i+=1};return i}",limits);
 CHECK(compiled.program!=nullptr);if(!compiled.program)return;
 std::shared_ptr<const Program> program(std::move(compiled.program));
 auto map=std::make_shared<ProgramTransferMap>(*program);auto pool=std::make_shared<ParallelRuntime>(1);
 auto make=[&]{auto r=std::make_shared<ParallelTaskRecord>();r->program=program;r->proto_map=map;r->runtime=pool;r->input.entry_proto=1;r->input.proto_map=map.get();return r;};
 auto first=make(),queued=make();std::string why;
 CHECK(start_parallel_task(first,why));CHECK(start_parallel_task(queued,why));
 CHECK(pool->cancel_queued(queued));CHECK(queued->snapshot_state()==ParallelTaskState::Cancelled);
 pool->wait_idle();CHECK(first->snapshot_state()==ParallelTaskState::Done);CHECK(pool->active()==0);
}
void callable_domain() {
 CompileLimits limits; limits.parallel_tasks=true;
 auto a=rt_vm_tool::compile("fn(f()){return 1}",limits);
 auto b=rt_vm_tool::compile("fn(g()){return 2}",limits);
 CHECK(a.program && b.program);if(!a.program||!b.program)return;
 ProgramTransferMap first(*a.program),second(*b.program);
 Value f=Value::adopt(new FunctionObject());f.heap_as<FunctionObject>()->code=a.program->protos[1].get();
 TransferGraph graph;std::string why;Value out;
 CHECK(graph.encode(f,first,why)==TransferStatus::Ok);
 CHECK(graph.decode(second,out,why)==TransferStatus::ProtoUnavailable);
 CHECK(out.is(Kind::Null));
}
void large_symbol_context() {
 AtomTableScope symbols;
 CompileLimits limits;limits.parallel_tasks=true;
 const std::string source="fn[async](f()){o:={\"x\":41};return o.x+1};print(await f())";
 auto compiled=rt_vm_tool::compile(source,limits);CHECK(compiled.program!=nullptr);if(!compiled.program)return;
 for(int i=0;i<70000;++i)atom("unused-symbol-"+std::to_string(i));
 CHECK(AtomTable::current().size()>65536);
 auto map=SourceMap::single("large-symbols.f",source);VmOptions options;
 options.parallel=std::make_shared<ParallelRuntime>(1);options.source_map=&map;
 Vm vm(*compiled.program,options);std::string output;auto result=vm.run(output);
 CHECK(result.reason==StopReason::None);CHECK(output=="42\n");
}
void deep_graph() {
 EmptyMap map;Value value=Value::integer(7);
 for(int i=0;i<20000;++i){Value parent=make_array();array_push(parent,value);value=std::move(parent);}
 TransferGraph graph;std::string why;CHECK(graph.encode(value,map,why)==TransferStatus::Ok);
 Value decoded;CHECK(graph.decode(map,decoded,why)==TransferStatus::Ok);
 const Value* at=&decoded;
 for(int i=0;i<20000;++i)at=&at->heap_as<ArrayObject>()->items[0];
 CHECK(at->is(Kind::Int)&&at->as_int()==7);
}
void cyclic_heap() {
 EmptyMap map; TransferGraph graph; std::string why;
 {
  Value a=make_array(); auto* object=a.heap_as<ArrayObject>(); object->items.push_back(a);
  CHECK(graph.encode(a,map,why)==TransferStatus::Ok);
  object->items.clear();
 }
 const auto before=live_heap_bytes();
 {
  TransferHeapScope heap;
  for (int i=0;i<100;++i) {
   Value a; CHECK(graph.decode(map,a,why)==TransferStatus::Ok);
   CHECK(a.heap_as<ArrayObject>()->items[0].heap()==a.heap());
  }
  heap.collect();
 }
 CHECK(live_heap_bytes()==before);
 // A root VM/embedding boundary collects unreachable cycles but preserves a
 // Value explicitly retained by the embedding caller across that boundary.
 Value escaped;
 {
  TransferHeapBoundary boundary;
  Value a;CHECK(graph.decode(map,a,why)==TransferStatus::Ok);escaped=a;
 }
 CHECK(escaped.heap_as<ArrayObject>()->items[0].heap()==escaped.heap());
 escaped=Value();collect_transfer_heap();CHECK(live_heap_bytes()==before);
 {
  TransferHeapBoundary boundary;
  Value a;CHECK(graph.decode(map,a,why)==TransferStatus::Ok);
 }
 CHECK(live_heap_bytes()==before);

}
}
int main() {
 neutral_cycles(); cyclic_heap(); deep_graph(); large_symbol_context(); callable_domain(); pool_lifetime(); cancel_before_dispatch();
 vm_case("m:=mutex(0);m.lock();fn[async](p()){m.lock();m.unlock();return 1};a:=p();sleep(20);cancel(a);try{await a}catch(e){print(e.code)};m.unlock()", "runtime.cancelled\n");
 vm_case("fn[async](slow()){sleep(40);return 0};fn[async](fast()){print(\"before\");return 0};a:=slow();b:=fast();await b;print(\"after\");await a", "before\nafter\n");
 vm_case("c:=channel(0);fn[async](p()){print(\"before\");send(c,1);sleep(20);return 0};a:=p();recv(c);print(\"after\");await a", "before\nafter\n");
 vm_case("fn[async](p()){sleep(5);print(\"before\");return 7};fn[async](q(a)){await a;print(\"after\");return 0};a:=p();b:=q(a);await b;await a", "before\nafter\n");
 vm_case("fn[async](f()){ o:={\"x\":41}; return o.x+1 }; print(await f())", "42\n");
 vm_case("fn[async](f(o)){return o[\"x\"]+o.y}; o:={\"x\":20,\"y\":22}; print(await f(o))", "42\n");
 vm_case("struct(P){x:=1};fn[async](f()){p:=P();p.x=42;return p.x};print(await f())","42\n");
 vm_case("fn(make()){ x:=41; return ()=>x+1 }; f:=make(); t:=thread(f); print(t.join())", "42\n");
 vm_case("fn[async](inner(x)){return x+1}; fn[async](outer(x)){return await inner(x)}; a:=outer(1); b:=outer(2); print(await a); print(await b)", "2\n3\n");
 vm_case("c:=channel(0); fn[async](p()){i:=0; while(i<100){send(c,i);i+=1};return 7}; a:=p(); i:=0; while(i<100){recv(c);i+=1}; print(await a)","7\n");
 vm_case("c:=channel(0); fn[async](p()){i:=0;while(i<100){send(c,i);i+=1};return 7}; fn[async](q()){i:=0;while(i<100){recv(c);i+=1};return 9}; a:=p();b:=q();print(await a);print(await b)","7\n9\n");
 vm_case("c:=channel(0); fn[async](child()){recv(c);return 1};fn[async](parent()){return await child()}; p:=parent();sleep(20);cancel(p);try{await p}catch(e){print(e.code)}", "runtime.cancelled\n");
 vm_case("c:=channel(0);fn[async](p()){send(c,1);return 0};a:=p();sleep(20);close(c);try{await a}catch(e){print(e.code)}", "runtime.channel_closed\n");
 vm_case("fn[async](p()){return 1};i:=0;while(i<100){a:=p();await a;i+=1};print(i)","100\n");
 return rt_test::finish("cp23-ownership-unit");
}
