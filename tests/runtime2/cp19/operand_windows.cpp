#include "rt/session.h"
#include "rt/aggregate.h"
#include "rt/heap.h"
#include "rt_test.h"
using namespace nift::rt;
int main() {
    for(const auto* declaration:{"fn(addv(a,b)){return a*10+b}","addv:=(a,b)=>(a*10+b)"}) {
        VmSession session;std::string output;
        auto r=session.execute(std::string(declaration)+";x:=1;y:=2","seed",output);
        CHECK(r.admitted && r.execution.reason==StopReason::None);
        r=session.evaluate("addv(x,y)","call",output);
        CHECK(r.admitted && r.execution.reason==StopReason::None && r.value.is(Kind::Int) && r.value.as_int()==12);
    }
    VmSession session;std::string output;
    auto r=session.execute("fn(g(p)){return p+1};fn(h(p)){return p+2};a:=[g,h];x:=1;y:=2;v:=[x,y]",
                           "array",output);
    CHECK(r.admitted && r.execution.reason==StopReason::None);
    const auto array=session.value("a"),values=session.value("v");
    CHECK(array_size(array)==2 && array_items(array)[0].is(Kind::Function) && array_items(array)[1].is(Kind::Function));
    CHECK(array_size(values)==2 && array_items(values)[0].as_int()==1 && array_items(values)[1].as_int()==2);
    r=session.execute("s:=\"abc\";q:=\"b\";m:=\"boom\";c:=\"user.app.test\"","strings",output);
    CHECK(r.admitted && r.execution.reason==StopReason::None);
    r=session.evaluate("s.contains(q)","native",output);
    CHECK(r.admitted && r.execution.reason==StopReason::None && r.value.is(Kind::Bool) && r.value.as_bool());
    r=session.evaluate("error(m,c)","error",output);
    CHECK(r.admitted && r.execution.reason==StopReason::None && r.value.is(Kind::Error));
    if(r.value.is(Kind::Error))CHECK(r.value.heap_as<ErrorObject>()->message=="boom" &&
                                   r.value.heap_as<ErrorObject>()->code=="user.app.test");
    r=session.execute("struct(Point){v:=0;fn(addv(a,b)){return a*10+b}};p:=Point()","struct",output);
    CHECK(r.admitted && r.execution.reason==StopReason::None);
    r=session.evaluate("p.addv(x,y)","method",output);
    CHECK(r.admitted && r.execution.reason==StopReason::None && r.value.as_int()==12);
    r=session.execute("n:=0;fn(stepv()){n+=1;return n};fn(combinev(a,b)){return a*10+b}","effects",output);
    CHECK(r.admitted && r.execution.reason==StopReason::None);
    r=session.evaluate("combinev(stepv(),stepv())","once",output);
    CHECK(r.admitted && r.execution.reason==StopReason::None && r.value.as_int()==12 && session.value("n").as_int()==2);
    return rt_test::finish("CP19 contiguous operand windows");
}
