#include <nift/nift.h>
#include <cassert>
#include <stdexcept>
int main(){
  nift::Engine e;
  assert(e.register_function("native_mul",[](const std::vector<nift::Value>& a)->nift::Value{
    if(a.size()!=2||!a[0].is_number()||!a[1].is_number()) throw std::runtime_error("expected two numbers");
    return nift::Value(a[0].number()*a[1].number());
  }));
  assert(e.register_function("native_fail",[](const std::vector<nift::Value>&)->nift::Value{throw std::runtime_error("boom");}));
  assert(e.register_function("native_nonstandard_fail",[](const std::vector<nift::Value>&)->nift::Value{throw 7;}));
  for(const char* name:{"error","try","catch","throw"}){
    assert(e.register_function(name,[name](const std::vector<nift::Value>&)->nift::Value{return nift::Value(std::string(name));}));
  }
  auto load=e.execute("fn(scale(x)) { return native_mul(x, 2) }\nfn(nested(x)) { return scale(x) + native_mul(x, 3) }\nreturn 0\n"); assert(load.ok());
  auto direct=e.evaluate("native_mul(6,7)"); assert(direct.ok()&&direct.value().number()==42);
  auto call=e.call("nested",{nift::Value(5)}); assert(call.ok()&&call.value().number()==25);
  auto bad=e.evaluate("native_fail()"); assert(!bad.ok()); assert(bad.error().message.find("boom")!=std::string::npos);
  auto nonstandard=e.evaluate("native_nonstandard_fail()"); assert(!nonstandard.ok());
  assert(nonstandard.error().message=="host callable 'native_nonstandard_fail' failed");
  for(const char* name:{"error","try","catch","throw"}){
    auto named=e.evaluate(std::string(name)+"()"); assert(named.ok()); assert(named.value().string()==name);
  }
  return 0;
}
