#include <nift/nift.h>
#include <cassert>
#include <thread>
#include <vector>
#include <atomic>
int main(){
  std::atomic<int> failures{0}; std::vector<std::thread> threads;
  for(int i=0;i<8;++i) threads.emplace_back([i,&failures]{
    nift::Engine e; e.set_target("worker"+std::to_string(i));
    e.register_function("host_id",[i](const std::vector<nift::Value>&){return nift::Value(i);});
    auto r=e.execute("fn(id()) { return host_id() }\nreturn id()\n");
    auto t=e.evaluate("target()"); if(!r.ok()||!r.value().is_number()||r.value().number()!=i||!t.ok()||t.value().string()!="worker"+std::to_string(i))++failures;
  });
  for(auto& t : threads) t.join();
  assert(failures.load() == 0);

  nift::Engine shared; shared.register_function("native_add",[](const std::vector<nift::Value>& a){return nift::Value(a[0].number()+a[1].number());});
  assert(shared.execute("fn(add(a,b)) { return native_add(a,b) }\nreturn 0\n").ok());
  threads.clear(); for(int i=0;i<16;++i)threads.emplace_back([i,&shared,&failures]{auto r=shared.call("add",{nift::Value(i),nift::Value(1)});if(!r.ok()||r.value().number()!=i+1)++failures;});
  for(auto& t : threads) t.join();
  assert(failures.load() == 0);

  // Owned async work is finalized before execute returns / runtime teardown.
  nift::Engine async_engine; auto ar=async_engine.execute("fn(f(x)) { return x + 1 }\na := async(f, 41)\nreturn await(a)\n"); assert(ar.ok()&&ar.value().number()==42);
  return 0;
}
