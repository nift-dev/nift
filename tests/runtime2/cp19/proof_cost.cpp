#include "rt/session.h"
#include "rt/session_history.h"
#include "rt/parser.h"
#include "rt/cell.h"
#include "rt_test.h"
#include <chrono>
#include <iostream>
#include <malloc.h>
using namespace nift::rt;
namespace {
using Clock=std::chrono::steady_clock;
std::shared_ptr<const std::vector<SessionCallFact>> facts(VmSession& session) {
    const auto value=session.value("g");
    return static_cast<const Proto*>(value.heap_as<FunctionObject>()->code)->session_call_facts;
}
void snapshot(const char* label,VmSession& session) {
    malloc_trim(0);const auto data=mallinfo2();const auto proof=facts(session);
    std::cout<<"MEMORY "<<label<<" allocator_live="<<data.uordblks<<" facts="<<proof->size()
             <<" fact_bytes="<<sizeof(SessionCallFact)*proof->capacity()<<" code_units="<<session.retained_code_units()
             <<" Ty="<<sizeof(infer_detail::Ty)<<" Proto="<<sizeof(Proto)<<'\n';
}
}
int main() {
    const std::string source="fn(g(p)){return p(0)+1};g(v=>v+1)";
    Ast ast(source);auto parsed=parse_script(ast);CHECK(parsed.ok);
    const auto start=Clock::now();
    for(unsigned k=0;k<1000;++k) {
        auto result=compile_unit(ast,parsed.root);CHECK(result.program!=nullptr);
        if(result.program)for(const auto& proto:result.program->protos)CHECK(!proto->session_call_facts);
    }
    std::cout<<"STATELESS iterations=1000 elapsed_us="
             <<std::chrono::duration<double,std::micro>(Clock::now()-start).count()<<'\n';
    VmSession session;std::string output;
    auto r=session.execute("a:=[1];tick:=0;fn(g(p)){return p(0)+a[0]};warm:=g(v=>v+1)","seed",output);
    CHECK(r.admitted && r.execution.reason==StopReason::None);
    const auto unchanged=facts(session);
    const auto unrelated=Clock::now();
    for(unsigned k=0;k<1000;++k) {
        r=session.execute("tick+=1","unrelated",output);
        CHECK(r.admitted && r.execution.reason==StopReason::None && facts(session)==unchanged);
    }
    std::cout<<"UNRELATED iterations=1000 elapsed_us="
             <<std::chrono::duration<double,std::micro>(Clock::now()-unrelated).count()<<'\n';
    r=session.execute("a.push(\"bad\")","widen",output);CHECK(r.admitted && r.execution.reason==StopReason::None);
    for(unsigned k=0;k<1000;++k) {
        r=session.execute("g(v=>v+"+std::to_string(k)+")","current",output);
        CHECK(r.admitted && r.execution.reason==StopReason::None);
    }
    snapshot("1000",session);
    const auto begin=Clock::now();
    for(unsigned k=1000;k<10000;++k) {
        r=session.execute("g(v=>v+"+std::to_string(k)+")","current",output);
        CHECK(r.admitted && r.execution.reason==StopReason::None);
    }
    snapshot("10000",session);
    std::cout<<"RELATED iterations=9000 elapsed_us="
             <<std::chrono::duration<double,std::micro>(Clock::now()-begin).count()<<'\n';
    CHECK(session.retained_code_units()<=4 && facts(session)->size()==1);
    return rt_test::finish("CP19 proof cost and bounded memory");
}
