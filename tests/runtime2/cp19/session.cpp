#include "rt/session.h"
#include "rt/session_inventory.h"
#include "rt/parser.h"
#include "rt/heap.h"
#include "rt/aggregate.h"
#include "rt/heap_account.h"
#include "rt_test.h"
#include <iostream>
#include <filesystem>
using namespace nift::rt;
int main() {
    const auto before = live_heap_bytes();
    {
        VmSession session;
        auto execute = [&](const char* source, const char* expected, unsigned unit) {
            std::string output;
            auto r = session.execute(source, "unit-" + std::to_string(unit), output);
            if (!r.admitted || r.execution.reason != StopReason::None)
                std::cerr << "unit " << unit << " refusal=" << r.refusal << " runtime=" << r.execution.message << '\n';
            CHECK(r.admitted && r.execution.reason == StopReason::None);
            CHECK(r.vm_executions == 1 && r.legacy_executions == 0);
            CHECK(output == expected);
        };
        execute("x := 1; fn(getx()){return x}; fn(makecounter()){n:=10;return ()=>{n+=1;return n}}; counter:=makecounter()", "", 1);
        execute("print(x)", "1\n", 2);
        execute("x += 4", "", 3);
        execute("print(getx())", "5\n", 4);
        execute("print(counter())", "11\n", 5);
        const auto count = session.binding_count();
        const auto calls = session.executions();
        const auto closure = session.value("counter");
        std::string output;
        auto refused = session.execute("leaked:=99;print(\"FORBIDDEN\");bytes([65])", "unit-6", output);
        CHECK(!refused.admitted && !refused.refusal.empty());
        CHECK(refused.vm_executions == 0 && refused.legacy_executions == 0 && output.empty());
        CHECK(!session.contains("leaked") && session.binding_count() == count);
        CHECK(session.executions() == calls && session.refusals() == 1);
        CHECK(session.value("counter").heap() == closure.heap());
        execute("print(getx());print(counter());print(x)", "5\n12\n5\n", 7);
        // Unit 8 closes once; an externally held closure owns its immutable
        // code until its final reference is dropped.
        if (!closure.is(Kind::Function)) return rt_test::finish("CP19 real eight-unit session");
        std::weak_ptr<const Program> code = closure.heap_as<FunctionObject>()->program_owner;
        CHECK(!code.expired());
        session.close(); session.close();
        CHECK(!code.expired());
    }
    {
        VmSession session;
        std::string output;
        auto r = session.execute("x:=1;saved:=error(\"seed\");fn(fail0()){\n throw error(\"cross\")} ", "definitions", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        r = session.execute("try{fail0()}catch(e){saved=e}", "catcher", output);
        if (!r.admitted || r.execution.reason != StopReason::None)
            std::cerr << "catcher refusal=" << r.refusal << " message=" << r.execution.message << "\n";
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        auto error = session.value("saved");
        CHECK(error.is(Kind::Error));
        if (error.is(Kind::Error)) {
            CHECK(error.heap_as<ErrorObject>()->source == "definitions");
            CHECK(error.heap_as<ErrorObject>()->line == 2);
        }
        output.clear();
        r = session.execute("x+=1;print(\"ONCE\");throw error(\"stop\");unreached:=0", "runtime-error", output);
        CHECK(r.admitted && r.execution.reason == StopReason::Error && r.vm_executions == 1);
        CHECK(output == "ONCE\n" && session.value("x").as_int() == 2);
        CHECK(!session.contains("unreached"));
        output.clear();
        r = session.execute("unreached:=9;print(unreached)", "later-declaration", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && output == "9\n");
        output.clear();
        r = session.execute("throw saved", "rethrow", output);
        CHECK(r.admitted && r.execution.reason == StopReason::Error);
        CHECK(r.execution.origin_source == "definitions" && r.execution.origin_line == 2);
        CHECK(session.value("saved").heap() == error.heap());
        output.clear();
        r = session.execute("a:=[1,2]", "array", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        auto copy = session.value("a");
        r = session.execute("a.push(3)", "mutate", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        CHECK(array_size(copy) == 2 && array_size(session.value("a")) == 3);
    }
    {
        VmSession session;
        std::string output;
        auto r = session.execute("f:=()=>ghost;throw error(\"stop\");ghost:=0", "partial-runtime", output);
        CHECK(r.admitted && r.execution.reason == StopReason::Error);
        CHECK(session.contains("f") && !session.contains("ghost"));
        r = session.evaluate("f()", "missing-global", output);
        CHECK(r.admitted && r.execution.reason == StopReason::Error);
        CHECK(r.execution.message.find("ghost") != std::string::npos);
        r = session.execute("ghost:=9", "later-global", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        r = session.evaluate("f()", "retained-closure", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 9);
    }
    {
        VmSession session;
        std::string output;
        auto r = session.execute("fn(inc(v)){return v+1}", "definition", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        for (int i = 0; i < 200; ++i) {
            r = session.evaluate("inc(8)", "call", output);
            CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 9);
        }
        CHECK(session.retained_code_units() == 1);
        CHECK(session.code_slots() <= 4);
    }
    {
        VmSession session;
        std::string output;
        auto r = session.execute("fn(makec()){n:=10;return v=>{n+=v;return n}};c:=makec()", "closure-definition", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        const auto closure = session.value("c");
        r = session.evaluate("c(2)", "first-later-argument", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 12);
        CHECK(session.value("c").heap() == closure.heap());
        r = session.execute("leaked:=0;print(\"FORBIDDEN\");c(\"bad\")", "widening", output);
        CHECK(!r.admitted && r.refusal == "session:capture-contract" && output.empty());
        CHECK(!session.contains("leaked") && session.value("c").heap() == closure.heap());
        r = session.evaluate("c(3)", "after-refusal", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 15);
        CHECK(session.value("c").heap() == closure.heap());
    }
    {
        VmSession session;
        std::string output;
        auto r = session.execute("fn(g0()){return 1};f:=g0", "old-definition", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        auto alias = session.value("f");
        std::weak_ptr<const Program> old = alias.heap_as<FunctionObject>()->program_owner;
        const auto identity = alias.heap_as<FunctionObject>()->session_identity;
        r = session.execute("fn(g0()){return 2}", "new-definition", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        CHECK(session.value("g0").heap() == alias.heap());
        CHECK(alias.heap_as<FunctionObject>()->session_identity == identity && identity != 0);
        CHECK(old.expired());
        r = session.evaluate("f()", "alias-after-redefinition", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 2);
        r = session.execute("fn(g0()){return 3};bytes([65])", "refused-redefinition", output);
        CHECK(!r.admitted && output.empty());
        r = session.evaluate("f()", "alias-after-refusal", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 2);
    }
    {
        VmSession session;
        std::string output;
        auto r = session.execute("a:=[0,0];tick:=0;x:=1", "assignment-state", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        auto snapshot = session.value("a");
        r = session.evaluate("(a[1]=(tick+=1))", "index-assignment", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 1);
        CHECK(session.value("tick").as_int() == 1 && array_items(session.value("a"))[1].as_int() == 1);
        CHECK(array_items(snapshot)[1].as_int() == 0);
        r = session.evaluate("1+(x=7)", "nested-assignment", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 8);
        r = session.evaluate("false && (x=8)", "skipped-assignment", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && session.value("x").as_int() == 7);
        r = session.evaluate("(x=8)+a[9]", "runtime-assignment-error", output);
        CHECK(r.admitted && r.execution.reason == StopReason::Error && session.value("x").as_int() == 8);
        r = session.evaluate("(x=9)+bytes([65])", "refused-assignment", output);
        CHECK(!r.admitted && session.value("x").as_int() == 8 && output.empty());
    }
    {
        const std::string source = "leaked:=5;bytes([65]);unknown(1)";
        Ast ast(source); const auto parsed = parse_script(ast);
        CHECK(parsed.ok);
        SessionCompileContext original;
        const auto map = SourceMap::single("inventory-source", source);
        const auto full = session_inventory_json(ast, parsed.root, parsed.ok, original, map, false);
        CHECK(full.find("\"inventory_complete\":true") != std::string::npos);
        CHECK(full.find("builtin-bytes:bytes") != std::string::npos && full.find("callable:unknown") != std::string::npos);
        CHECK(full.find("session:redeclared") == std::string::npos && original.symbols.empty());
        const auto bounded = session_inventory_json(ast, parsed.root, parsed.ok, original, map, false, 1);
        CHECK(bounded.find("\"inventory_complete\":false") != std::string::npos);
        CHECK(bounded.find("round-limit") != std::string::npos && bounded.find("callable:unknown") == std::string::npos);
    }
    CHECK(live_heap_bytes() == before);
    return rt_test::finish("CP19 real eight-unit session");
}
