#include "rt/session.h"
#include "rt/heap.h"
#include "rt/string.h"
#include "rt_test.h"
#include <iostream>
using namespace nift::rt;
int main(int argc, char** argv) {
    const std::string expect = argc > 1 ? argv[1] : "control";
    VmSession session;
    std::string output;
    auto r = session.execute("x:=5;fn(getx()){return x};f:=()=>x", "definitions", output);
    CHECK(r.admitted && r.execution.reason == StopReason::None);
    const auto f = session.value("f");
    if (expect == "owner") {
        CHECK(f.is(Kind::Function));
        if (f.is(Kind::Function)) CHECK(!session_function(f.heap_as<FunctionObject>())->program_owner);
        const auto status = rt_test::finish("CP19 session negative-control witness");
        if (!status) std::cout << "WITNESS owner\n";
        return status;
    }
    if (expect == "contract" || expect == "widened") {
        r = session.execute("nested:=[[1]]", "nested-state", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        r = session.execute("for(row:nested){row.push(()=>7)}", "nested-effect", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.vm_executions == 1);
        r = session.execute("derived:=nested[0][1];oldderived:=derived", "future-reader", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        const auto original = session.value("derived");
        r = session.execute("derived = ()=>9;print(\"ONCE\")", "use-widened-bound", output);
        if (expect == "contract") {
            CHECK(!r.admitted && r.refusal == "session:binding-contract" && r.vm_executions == 0);
            CHECK(output.empty() && session.value("derived").heap() == original.heap());
        } else {
            CHECK(r.admitted && r.execution.reason == StopReason::None && r.vm_executions == 1);
            CHECK(output == "ONCE\n" && session.value("derived").heap() != original.heap());
        }
        output.clear();
        r = session.evaluate("derived()", "future-call", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == (expect == "contract" ? 7 : 9));
        r = session.evaluate("oldderived()", "retained-alias", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 7);
        const auto status = rt_test::finish("CP19 coherent widening negative-control witness");
        if (!status) std::cout << "WITNESS " << expect << '\n';
        return status;
    }
    const auto count = session.binding_count();
    const auto expected_x = expect == "state" ? 0 : 5;
    CHECK(session.value("x").as_int() == expected_x);
    output.clear();
    r = session.evaluate("getx()", "read", output);
    CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == expected_x);
    r = session.execute("leaked:=77;print(\"FORBIDDEN\");bytes([65])", "refusal", output);
    CHECK(!r.admitted && !r.refusal.empty() && r.vm_executions == 0 && r.legacy_executions == 0);
    CHECK(output.empty() && session.value("x").as_int() == expected_x);
    CHECK(session.value("f").heap() == f.heap());
    CHECK(session.contains("leaked") == (expect == "publish"));
    CHECK(session.binding_count() == count + (expect == "publish" ? 1 : 0));
    output.clear();
    r = session.execute("x+=1;print(\"ONCE\");throw error(\"stop\")", "runtime", output);
    CHECK(r.admitted && r.execution.reason == StopReason::Error && r.vm_executions == 1);
    CHECK(output == "ONCE\n");
    CHECK(session.value("x").as_int() == expected_x + (expect == "runtime" ? 0 : 1));
    output.clear();
    r = session.execute("saved:=error(\"seed\");o:={\"fail\":()=>{throw error(\"cross\")}}", "original-source", output);
    CHECK(r.admitted && r.execution.reason == StopReason::None);
    r = session.execute("try{o.fail()}catch(e){saved=e}", "catcher", output);
    CHECK(r.admitted && r.execution.reason == StopReason::None);
    auto error = session.value("saved");
    CHECK(error.is(Kind::Error));
    if (error.is(Kind::Error)) {
        CHECK(error.heap_as<ErrorObject>()->source == (expect == "source" ? "catcher" : "original-source"));
        CHECK(error.heap_as<ErrorObject>()->message == "cross");
    }
    r = session.execute("a:=[0,0];tick:=0", "assignment-state", output);
    CHECK(r.admitted && r.execution.reason == StopReason::None);
    r = session.evaluate("(a[1]=(tick+=1))", "assignment-value", output);
    CHECK(r.admitted && r.execution.reason == StopReason::None);
    const int assigned = expect == "assignment" ? 2 : 1;
    CHECK(r.value.as_int() == assigned && session.value("tick").as_int() == assigned);
    std::weak_ptr<const Program> owner;
    CHECK(f.is(Kind::Function));
    if (f.is(Kind::Function)) owner = session_function(f.heap_as<FunctionObject>())->program_owner;
    CHECK(owner.expired() == (expect == "owner"));
    session.close();
    CHECK(owner.expired() == (expect == "owner"));
    const auto status = rt_test::finish("CP19 session negative-control witness");
    if (!status) std::cout << "WITNESS " << expect << '\n';
    return status;
}
