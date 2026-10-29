// Independent OS threads reconstruct isolate-owned handle boxes. Exact counts,
// per-sender FIFO, close/cancel outcomes and registry reclamation are asserted.
#include "rt/shared_state.h"
#include "rt/shared_gc.h"
#include "rt/parallel_task.h"
#include "rt/atom.h"
#include "rt_test.h"
#include <atomic>
#include <thread>
#include <vector>
using namespace nift::rt;
struct EmptyMap : TransferProtoMap {
 bool index_of(const Proto*,std::uint32_t&) const override {return false;}
 const Proto* proto_of(std::uint32_t) const override {return nullptr;}
};
void channel_fifo(int capacity) {
 EmptyMap map;SharedOpError error;Value c=shared_make_channel(capacity,error);
 auto backing=shared_handle_backing(c);auto id=c.handle_id();
 std::atomic<int> failures{0};std::vector<std::thread> senders;
 for(int p=0;p<4;++p) senders.emplace_back([&,p,backing,id]{
  AtomTableScope symbols;Value local=shared_handle_from_backing(HandleKind::Channel,id,backing);SharedOpError e;
  for(int i=0;i<300;++i) if(!shared_channel_send(local,Value::integer(p*1000+i),&map,{},e)) ++failures;
 });
 int seen[4]={0,0,0,0};
 for(int i=0;i<1200;++i){Value out;bool closed=false;
  if(!shared_channel_recv(c,&map,{},out,closed,error)||closed||!out.is(Kind::Int)){++failures;continue;}
  int p=int(out.as_int()/1000),n=int(out.as_int()%1000);
  if(p<0||p>3||n!=seen[p]++) ++failures;
 }
 for(auto& t:senders) { t.join(); }
 CHECK(failures==0);
 for(auto n:seen)CHECK(n==300);
 CHECK(shared_channel_close(c,error));Value out;bool closed=false;
 CHECK(shared_channel_recv(c,&map,{},out,closed,error));CHECK(closed);
}
void increments() {
 EmptyMap map;SharedOpError error;
 Value m=shared_make_mutex(nullptr,&map,error),a=shared_make_atomic_int(0,error);
 CHECK(shared_mutex_lock(m,99,{},error));CHECK(shared_mutex_set(m,99,Value::integer(0),&map,error));CHECK(shared_mutex_unlock(m,99,error));
 auto mb=shared_handle_backing(m),ab=shared_handle_backing(a);auto mid=m.handle_id(),aid=a.handle_id();
 std::atomic<int> failures{0};std::vector<std::thread> threads;
 for(int p=1;p<=8;++p)threads.emplace_back([&,p,mb,ab,mid,aid]{
  AtomTableScope symbols;auto mutex=shared_handle_from_backing(HandleKind::Mutex,mid,mb);auto atomic=shared_handle_from_backing(HandleKind::Atomic,aid,ab);SharedOpError e;
  for(int i=0;i<500;++i){Value out;
   if(!shared_atomic_fetch(atomic,Value::integer(1),false,out,e))++failures;
   if(!shared_mutex_lock(mutex,p,{},e)) {++failures;continue;}
   if(!shared_mutex_get(mutex,p,&map,out,e)||!shared_mutex_set(mutex,p,Value::integer(out.as_int()+1),&map,e))++failures;
   if(!shared_mutex_unlock(mutex,p,e))++failures;
  }
 });
 for(auto& t:threads) { t.join(); }
 CHECK(failures==0);
 Value out;CHECK(shared_atomic_load(a,out,error));CHECK(out.as_int()==4000);
 CHECK(shared_mutex_lock(m,99,{},error));CHECK(shared_mutex_get(m,99,&map,out,error));CHECK(out.as_int()==4000);CHECK(shared_mutex_unlock(m,99,error));
}
void close_cancel_races() {
 EmptyMap map;
 for(int i=0;i<100;++i){
  SharedOpError error;Value c=shared_make_channel(0,error);auto backing=shared_handle_backing(c);auto id=c.handle_id();
  auto task=std::make_shared<ParallelTaskRecord>();std::atomic<bool> started{false};bool sent=false;std::string code;
  std::thread sender([&,backing,id]{AtomTableScope symbols;auto local=shared_handle_from_backing(HandleKind::Channel,id,backing);SharedOpError e;started=true;sent=shared_channel_send(local,Value::integer(i),&map,task,e);code=e.code;});
  while(!started.load())std::this_thread::yield();
  {std::lock_guard<std::mutex> lock(task->mutex);task->cancelled=true;}
  CHECK(shared_channel_close(c,error));sender.join();
  CHECK(!sent);CHECK(code=="runtime.cancelled"||code=="runtime.channel_closed");
  Value out;bool closed=false;CHECK(shared_channel_recv(c,&map,{},out,closed,error));CHECK(closed);
 }
}
int main(){
 for(int i=0;i<5;++i){channel_fifo(0);channel_fifo(3);increments();shared_collect();CHECK(shared_registry_live_for_test()==0);}
 close_cancel_races();shared_collect();CHECK(shared_registry_live_for_test()==0);
 return rt_test::finish("cp23-contention-unit");
}
