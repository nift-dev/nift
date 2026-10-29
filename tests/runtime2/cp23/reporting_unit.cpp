// Cold join diagnostics must never rewrite a caught Error.
#include "rt/parallel_task.h"
#include "rt_test.h"
#include "rt_vm_tool.h"
#include <string>
using namespace nift::rt;
namespace {
void check(const std::string& source, StopReason reason, const std::string& message, const std::string& output) {
 CompileLimits limits; limits.parallel_tasks=true;
 auto code=rt_vm_tool::compile(source,limits); CHECK(code.program!=nullptr); if(!code.program)return;
 auto map=SourceMap::single("report.f",source); VmOptions options;options.source_map=&map;
 options.parallel=std::make_shared<ParallelRuntime>(2);Vm vm(*code.program,options);
 std::string text;auto result=vm.run(text);CHECK(result.reason==reason);CHECK(result.message==message);CHECK(text==output);
}
}
int main(){
 const std::string setup="m:=mutex(0);m.lock();fn(bad(m)){m.unlock();return 1};";
 check(setup+"t:=thread(bad,m);t.join()",StopReason::Error,"thread: mutex: unlock by non-owner","");
 check(setup+"fn(outer(m)){t:=thread(bad,m);return t.join()};t:=thread(outer,m);t.join()",StopReason::Error,"thread: thread: mutex: unlock by non-owner","");
 check(setup+"t:=thread(bad,m);try{t.join()}catch(e){throw e}",StopReason::Error,"mutex: unlock by non-owner","");
 check(setup+"t:=thread(bad,m);prior_error:=error(\"sentinel\");try{t.join()}catch(e){prior_error=e};try{t.join()}catch(e){print(e==prior_error);print(e.message)};m.unlock()",StopReason::None,"","true\nmutex: unlock by non-owner\n");
 check("m:=mutex(0);m.unlock()",StopReason::Error,"mutex: unlock of unlocked mutex","");
 check("m:=mutex(0);m.lock();fn(bad(m)){try{m.unlock()}catch(e){return e.message}};t:=thread(bad,m);print(t.join());m.unlock()",StopReason::None,"","mutex: unlock by non-owner\n");
 return rt_test::finish("cp23-reporting-unit");
}
