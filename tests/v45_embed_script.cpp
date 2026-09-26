#include <nift/nift.h>
#include <cassert>
#include <string>
int main(){
  nift::Engine e;
  e.set_platform("android");
  auto r=e.execute("fn(add(a,b)) { return a + b }\nreturn {\"cmd\": cmd, \"target\": platform(), \"argc\": args.length()}\n","app",{"x","y"});
  assert(r.ok());
  assert(r.value().is_object());
  auto v=e.evaluate("add(20,22)"); assert(v.ok()); assert(v.value().is_number()); assert(v.value().number()==42);
  auto c=e.evaluate("cmd"); assert(c.ok() && c.value().is_string() && c.value().string()=="app");
  auto t=e.evaluate("platform()"); assert(t.ok() && t.value().string()=="android");
  nift::Engine e2; auto v2=e2.evaluate("1+2"); assert(v2.ok() && v2.value().number()==3);
  return 0;
}
