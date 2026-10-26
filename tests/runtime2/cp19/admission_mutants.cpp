#include "rt/session.h"
#include "rt/string.h"
#include "rt_test.h"
#include <iostream>
using namespace nift::rt;
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const std::string test = argv[1];
    const bool refused = std::string(argv[2]) == "refused";
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
        seed = "fn(g()){return 1};fn(makec()){caparr:=[[1]];return v=>{for(row:caparr){row.push(v)};return caparr.stringify()}};c:=makec();warm:=c(0)";
        probe = "fn(g()){return 3}";
        check = "g()"; expected = refused ? "1" : "3"; reason = "session:redefinition-callable-flow";
    } else if (test == "alias") {
        seed = "f:=()=>1;g:=()=>2;old:=f";
        probe = "f = g";
        check = "f()"; expected = refused ? "1" : "2"; reason = "session:callable-rebind";
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
    CHECK(r.admitted && r.execution.reason == StopReason::None && r.legacy_executions == 0);
    if (r.value.is(Kind::String)) CHECK(string_view_of(r.value) == expected);
    else CHECK(r.value.is(Kind::Int) && std::to_string(r.value.as_int()) == expected);
    const auto status = rt_test::finish("CP19 admission witness");
    if (!status) std::cout << "WITNESS " << test << ' ' << (refused ? "refused" : "admitted") << '\n';
    return status;
}
