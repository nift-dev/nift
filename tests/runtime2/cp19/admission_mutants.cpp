#include "rt/session.h"
#include "rt/heap.h"
#include "rt/string.h"
#include "rt_test.h"
#include <iostream>
using namespace nift::rt;
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const std::string test = argv[1];
    const bool refused = std::string(argv[2]) == "refused";
    const bool internal = std::string(argv[2]) == "internal";
    const bool replayed = std::string(argv[2]) == "replayed";
    std::string seed, probe, check, expected, reason;
    if (test == "wrapper") {
        seed = "a:=[[1]];f:=()=>a.stringify()";
        probe = "fn(makec()){a:=[[1]];return v=>{for(row:a){row.push(v)};return a.stringify()}};c:=makec();c(0);saved:=f()";
        check = "f()"; expected = "[[1]]"; reason = "session:dynamic-shadow";
    } else if (test == "field") {
        seed = "v:=99";
        probe = "struct(Point){v:=1;fn(get()){return this.v}};p:=Point()";
        check = "v"; expected = "99"; reason = "session:struct-dynamic-shadow";
    } else if (test == "loop") {
        seed = "fn(g()){return 1};fn(makec()){caparr:=[[()=>g()]];return v=>{for(row:caparr){row.push(v)};return 1}};c:=makec();warm:=c(0)";
        probe = "fn(g()){return 3}";
        check = "g()"; expected = refused ? "1" : "3"; reason = "session:redefinition-callable-flow";
    } else if (test == "alias") {
        seed = "f:=()=>1;g:=()=>2;old:=f";
        probe = "f = g";
        check = "f()"; expected = refused ? "1" : "2"; reason = "session:callable-rebind";
    } else if (test == "schema") {
        seed = "struct(Point){v:=1;fn(fail()){throw error(\"old\")}};p:=Point()";
        probe = "struct(Point){v:=2;fn(fail()){throw error(\"new\")}};q:=Point()";
        check = refused ? "p.v" : "p.v*10+q.v";
        expected = refused ? "1" : "12"; reason = "session:struct-redeclared";
    } else if (test == "order") {
        seed = "fn(g()){return 1};old:=g;reader:=()=>g()";
        probe = "before:=g();aliasbefore:=old();readbefore:=reader();fn(g()){return 3};"
                "after:=g();aliasafter:=old();readafter:=reader()";
        check = refused ? "g()" : "before+aliasbefore+readbefore+after+aliasafter+readafter";
        expected = refused ? "1" : "12"; reason = "session:redefinition-order";
    } else if (test == "direct") {
        seed = "a:=[1];f:=()=>a.stringify()"; probe = "a.push(\"bad\")";
        check = "f()"; expected = refused ? "[1]" : "[1,\"bad\"]";
        reason = "session:binding-shape-contract";
    } else if (test == "nested") {
        seed = "a:=[[1]];f:=()=>a.stringify()"; probe = "for(row:a){row.push(\"bad\")}";
        check = "f()"; expected = refused ? "[[1]]" : "[[1,\"bad\"]]";
        reason = "session:binding-shape-contract";
    } else if (test == "capture") {
        seed = "fn(makec()){a:=[1];return v=>{a.push(v);return a.stringify()}};c:=makec();warm:=c(0)";
        probe = "c(\"bad\")"; check = "c(1)";
        expected = refused ? "[1,0,1]" : "[1,0,\"bad\",1]"; reason = "session:capture-contract";
    } else if (test == "numeric") {
        seed = "a:=[1];reader:=()=>a[0]*2"; probe = "a[0] = \"bad\"";
        check = "reader()";
    } else if (test == "native") {
        seed = "a:=[[1,2]];reader:=()=>a[0].contains(\"b\")"; probe = "a.push(\"abc\")";
        check = "reader()";
    } else if (test == "native-once") {
        seed = "a:=[[1],[2]];n:=-1;reader:=()=>a[(n+=1)].contains(\"b\")"; probe = "a.push(\"abc\")";
        check = "reader()";
    } else if (test == "shared") {
        seed = "fn(makeo()){a:=[1];return {\"putv\":v=>{a[0]=v;return a.stringify()},"
               "\"readv\":()=>a[0]*2}};o:=makeo();warm:=o.putv(0)";
        probe = "o.putv(\"bad\")"; check = "o.readv()";
    } else if (test == "selector") {
        seed = "fn(g()){return 1};fn(makec()){rows:=[[1]];for(row:rows){return ()=>row[0]}};c:=makec();a:=[1]";
        probe = "fn(g()){return 3};a.push(\"bad\")"; check = "c()";
        expected = "1"; reason = "session:widening-callable-flow";
    } else return 2;
    VmSession session;
    std::string output;
    auto r = session.execute(seed, "seed", output);
    CHECK(r.admitted && r.execution.reason == StopReason::None);
    const auto count = session.binding_count();
    const auto original_f = session.value("f");
    r = session.execute(probe + ";leaked:=77;print(\"FORBIDDEN\");bytes([65])", "failed-compile", output);
    CHECK(!r.admitted && r.vm_executions == 0 && r.legacy_executions == 0);
    CHECK(output.empty() && !session.contains("leaked") && session.binding_count() == count);
    CHECK(!original_f.is(Kind::Function) || session.value("f").heap() == original_f.heap());
    r = session.execute(probe + ";print(\"ONCE\");throw error(\"stop\")", "probe", output);
    if (refused) {
        CHECK(!r.admitted && r.refusal == reason && r.vm_executions == 0);
        CHECK(output.empty());
    } else {
        CHECK(r.admitted && r.execution.reason == StopReason::Error && r.vm_executions == 1);
        CHECK(output == "ONCE\n");
    }
    output.clear();
    r = session.evaluate(check, "check", output);
    if (test == "numeric" || test == "shared") {
        CHECK(r.admitted && r.vm_executions == 1 && r.legacy_executions == 0 && !r.execution.catchable);
        CHECK(r.execution.reason == (internal ? StopReason::Internal : StopReason::Error));
        CHECK(r.execution.message == (internal ? "unexpected operands (compiler defect): arithmetic" :
            "lambda body error: arithmetic operators require numeric operands"));
    } else if (test == "native" && internal) {
        CHECK(r.admitted && r.vm_executions == 1 && r.execution.reason == StopReason::Internal);
        CHECK(r.execution.message == "unexpected operands (compiler defect): built-in contains");
    } else if (test == "native" || test == "native-once") {
        CHECK(r.admitted && r.vm_executions == 1 && r.execution.reason == StopReason::None);
        CHECK(r.value.is(Kind::Bool) && !r.value.as_bool());
        if (test == "native-once") CHECK(session.value("n").as_int() == (replayed ? 1 : 0));
    } else {
    CHECK(r.admitted && r.execution.reason == StopReason::None && r.legacy_executions == 0);
    if (r.value.is(Kind::String)) CHECK(string_view_of(r.value) == expected);
    else CHECK(r.value.is(Kind::Int) && std::to_string(r.value.as_int()) == expected);
    }
    if (test == "schema" && !refused) {
        r = session.execute("saved:=error(\"seed\");try{p.fail()}catch(e){saved=e}", "old-catcher", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        auto saved = session.value("saved");
        CHECK(saved.is(Kind::Error));
        if (saved.is(Kind::Error)) {
            CHECK(saved.heap_as<ErrorObject>()->message == "old");
            CHECK(saved.heap_as<ErrorObject>()->source == "seed");
        }
        r = session.execute("try{q.fail()}catch(e){saved=e}", "new-catcher", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        saved = session.value("saved");
        CHECK(saved.is(Kind::Error));
        if (saved.is(Kind::Error)) {
            CHECK(saved.heap_as<ErrorObject>()->message == "new");
            CHECK(saved.heap_as<ErrorObject>()->source == "probe");
        }
    }
    const auto status = rt_test::finish("CP19 admission witness");
    if (!status) std::cout << "WITNESS " << test << ' ' << (internal ? "internal" : replayed ? "replayed" : refused ? "refused" : "admitted") << '\n';
    return status;
}
