#include "rt/session.h"
#include "rt/session_inventory.h"
#include "rt/session_callable.h"
#include "rt/parser.h"
#include "rt/heap.h"
#include "rt/string.h"
#include "rt/aggregate.h"
#include "rt/heap_account.h"
#include "rt_test.h"
#include <iostream>
#include <filesystem>
using namespace nift::rt;
int main() {
    // Atom storage belongs to the thread (CP23); pre-intern fixture keys so
    // the exact final heap comparison measures owned runtime values.
    atom("left"); atom("right"); atom("f"); atom("Counter"); atom("Kept"); atom("v"); atom("First"); atom("Second");
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
        const auto* original_code = closure.heap_as<FunctionObject>()->code;
        const auto* original_owner = closure.heap_as<FunctionObject>()->program_owner.get();
        std::string output;
        auto refused = session.execute("leaked:=99;print(\"FORBIDDEN\");bytes([65])", "unit-6", output);
        CHECK(!refused.admitted && !refused.refusal.empty());
        CHECK(refused.vm_executions == 0 && refused.legacy_executions == 0 && output.empty());
        CHECK(!session.contains("leaked") && session.binding_count() == count);
        CHECK(session.executions() == calls && session.refusals() == 1);
        CHECK(session.value("counter").heap() == closure.heap());
        CHECK(closure.heap_as<FunctionObject>()->code == original_code);
        CHECK(closure.heap_as<FunctionObject>()->program_owner.get() == original_owner);
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
        auto r = session.execute("f:=()=>ghost[0];throw error(\"stop\");ghost:=[1];ghost.push(\"later\")", "reserved-array", output);
        CHECK(r.admitted && r.execution.reason == StopReason::Error && !session.contains("ghost"));
        r = session.execute("ghost:=[3]", "fill-reserved-array", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        r = session.execute("ghost.push(\"later\")", "preserved-array-bound", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && array_size(session.value("ghost")) == 2);
        r = session.evaluate("f()", "old-array-reader", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 3);
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
    {
        VmOptions options; options.heap_budget = 1;
        VmSession session(options); session.initialize_invocation("budget", {});
        std::string output;
        const auto result = session.execute("print(\"MUST-NOT-RUN\")", "budget-unit", output);
        CHECK(result.admitted && result.execution.reason == StopReason::Internal);
        CHECK(result.execution.message == "memory budget exceeded" && output.empty());
        CHECK(result.vm_executions == 1 && result.legacy_executions == 0 && session.binding_count() == 0);
    }
    {
        VmSession session;
        std::string output;
        const auto result = session.execute("f:=()=>1;o:={\"left\":f,\"right\":f};a:=[f,f]", "callable-facts", output);
        CHECK(result.admitted && result.execution.reason == StopReason::None);
        const auto object = session_callable_shape(session.value("o"));
        const auto array = session_callable_shape(session.value("a"));
        CHECK(object && object->fields.size() == 2 && array && array->element);
        const auto object_targets = session_callable_targets(object);
        const auto array_targets = session_callable_targets(array);
        CHECK(object_targets.size() == 1 && array_targets.size() == 1);
        CHECK(object_targets.front().identity == array_targets.front().identity);
        std::weak_ptr<const Program> owner = session.value("f").heap_as<FunctionObject>()->program_owner;
        session.close();
        CHECK(owner.expired());  // compiler facts never pin executable values or their Program
        CHECK(object_targets.front().definition->path == "callable-facts");
        CHECK(object_targets.front().definition->source->find("f:=()=>1") != std::string::npos);
    }
    {
        VmSession session;
        std::string output;
        auto r = session.execute("o:={\"f\":v=>v+1};saved:=error(\"seed\")", "object-definition", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        const auto object = session.value("o");
        const auto function = object_entries(object).front().second;
        const auto code = function.heap_as<FunctionObject>()->code;
        const auto owner = function.heap_as<FunctionObject>()->program_owner;
        r = session.execute("leaked:=9;print(\"FORBIDDEN\");o.f(bytes([65]))", "refused-object-call", output);
        CHECK(!r.admitted && !session.contains("leaked") && output.empty());
        CHECK(function.heap_as<FunctionObject>()->code == code && function.heap_as<FunctionObject>()->program_owner == owner);
        r = session.evaluate("o.f(4)", "specialized-object-call", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 5);
        CHECK(object_entries(session.value("o")).front().second.heap() == function.heap());
        r = session.execute("fn(makecb()){g:=x=>x+1;return x=>g(x)};c:=makecb()", "captured-definition", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        const auto facts = session_callable_shape(session.value("c"));
        CHECK(facts && facts->complete && session_callable_targets(facts).size() == 2);
        r = session.evaluate("c(7)", "captured-later-call", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 8);
    }
    {
        VmSession session;
        std::string output;
        auto r = session.execute("x:=1.5;fn(readv()){return x}", "numeric-definition", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        r = session.evaluate("(x=3)", "integer-in-double-binding", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 3);
        r = session.evaluate("readv()", "numeric-old-reader", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 3);
        r = session.execute("print(\"FORBIDDEN\");fn(caller()){x:=7;return readv()}", "dynamic-shadow", output);
        CHECK(!r.admitted && r.refusal == "session:dynamic-shadow" && output.empty());
        CHECK(!session.contains("caller") && session.value("x").as_int() == 3);
        r = session.execute("fn(localv()){x:=7;return x}", "unrelated-shadow", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        r = session.evaluate("localv()", "lexical-local", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 7);
    }
    {
        VmSession session;
        std::string output;
        auto r = session.execute("struct(Counter){v:=0;fn(add(n)){v+=n} fn(get()){return v} fn(fail()){throw error(\"original\")}}", "schema-source", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && session.contains("Counter"));
        r = session.execute("c:=Counter()", "instance-unit", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        const auto snapshot = session.value("c");
        r = session.execute("saved:=error(\"seed\");try{c.fail()}catch(e){saved=e}", "new-method-catcher", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        const auto saved = session.value("saved");
        CHECK(saved.is(Kind::Error) && saved.heap_as<ErrorObject>()->source == "schema-source");
        CHECK(saved.is(Kind::Error) && saved.heap_as<ErrorObject>()->message == "original");
        r = session.execute("c.add(3)", "method-unit", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && output.empty());
        CHECK(struct_get(snapshot, atom("v"))->as_int() == 0);
        r = session.evaluate("c.get()", "method-result", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 3);
        r = session.execute("struct(Ghost){v:=0};print(\"FORBIDDEN\");bytes([65])", "refused-schema", output);
        CHECK(!r.admitted && !session.contains("Ghost") && output.empty());
        r = session.execute("struct(Counter){v:=99}", "refused-schema-replacement", output);
        CHECK(!r.admitted && r.refusal == "session:struct-redeclared" && output.empty());
        r = session.evaluate("c.v", "after-refusals", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 3);
        r = session.execute("struct(Kept){v:=0};throw error(\"stop\")", "runtime-schema", output);
        CHECK(r.admitted && r.execution.reason == StopReason::Error && session.contains("Kept"));
        r = session.execute("k:=Kept()", "after-schema-error", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && session.value("k").is(Kind::Struct));
    }
    {
        VmSession session;
        std::string output;
        auto r = session.execute("f:=()=>1;oldf:=f;reader:=()=>f()", "lambda-original", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        const auto old = session.value("f"), reader = session.value("reader");
        const auto code = old.heap_as<FunctionObject>()->code;
        const auto owner = old.heap_as<FunctionObject>()->program_owner;
        const auto last_id = reader.heap_as<FunctionObject>()->session_identity;
        r = session.execute("f = ()=>9;print(\"FORBIDDEN\");bytes([65])", "refused-rebind", output);
        CHECK(!r.admitted && output.empty() && session.value("f").heap() == old.heap());
        CHECK(old.heap_as<FunctionObject>()->code == code && old.heap_as<FunctionObject>()->program_owner == owner);
        r = session.execute("f = ()=>2", "new-lambda", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        CHECK(session.value("f").heap() != old.heap() && session.value("oldf").heap() == old.heap());
        CHECK(session.value("f").heap_as<FunctionObject>()->session_identity == last_id + 1);
        r = session.evaluate("f()", "new-lambda-call", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 2);
        r = session.evaluate("oldf()", "old-lambda-call", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 1);
        r = session.evaluate("reader()", "cell-reader-call", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 2);
        CHECK(session.value("reader").heap() == reader.heap());
        r = session.execute("f = ()=>3;print(\"ONCE\");throw error(\"stop\")", "runtime-rebind", output);
        CHECK(r.admitted && r.execution.reason == StopReason::Error && output == "ONCE\n");
        output.clear();
        r = session.evaluate("reader()", "reader-after-runtime", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 3);
        r = session.execute("f = ()=>{throw error(\"new\")}", "new-error-definition", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        r = session.evaluate("f()", "new-error-call", output);
        CHECK(r.admitted && r.execution.reason == StopReason::Error && r.execution.error_origin);
        CHECK(r.execution.origin_source == "new-error-definition");
    }
    {
        VmSession session;
        std::string output;
        auto r = session.execute("struct(First){v:=1};struct(Second){v:=2};throw error(\"stop\");p:=First()", "reserved-instance", output);
        CHECK(r.admitted && r.execution.reason == StopReason::Error && !session.contains("p"));
        r = session.execute("print(\"FORBIDDEN\");p:=Second()", "incompatible-instance", output);
        CHECK(!r.admitted && r.refusal == "session:binding-struct-contract" && output.empty());
        CHECK(!session.contains("p"));
        r = session.execute("p:=First()", "compatible-instance", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        r = session.evaluate("p.v", "reserved-instance-read", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 1);
    }
    {
        struct State { VmSession* session = nullptr; SessionResult nested; std::string output, nested_output; } state;
        VmOptions options; options.flush_threshold = 1; options.flush_context = &state;
        options.flush = [](std::string& output, void* context) {
            auto& state = *static_cast<State*>(context);
            state.output += output; output.clear();
            state.nested = state.session->execute("leaked:=9", "reentrant-unit", state.nested_output);
        };
        VmSession session(options); state.session = &session;
        std::string output;
        const auto result = session.execute("x:=1;print(\"ONCE\");x+=1", "outer-unit", output);
        CHECK(result.admitted && result.execution.reason == StopReason::None && state.output == "ONCE\n");
        CHECK(state.nested.execution.reason == StopReason::Internal && !state.nested.admitted);
        CHECK(state.nested.vm_executions == 0 && state.nested.legacy_executions == 0 && state.nested_output.empty());
        CHECK(!session.contains("leaked") && session.value("x").as_int() == 2 && session.executions() == 1);
    }
    {
        struct State { VmSession* session = nullptr; std::string output; } state;
        VmOptions options; options.flush_threshold = 1; options.flush_context = &state;
        options.flush = [](std::string& output, void* context) {
            auto& state = *static_cast<State*>(context);
            state.output += output; output.clear(); state.session->close();
        };
        VmSession session(options); state.session = &session;
        std::string output;
        const auto result = session.execute("x:=1;print(\"ONCE\");x+=1;print(x)", "closing-unit", output);
        CHECK(result.admitted && result.execution.reason == StopReason::None && state.output == "ONCE\n2\n");
        CHECK(session.binding_count() == 0 && session.executions() == 1);
    }
    {
        VmSession session;
        std::string output;
        auto result = session.execute("saved:=error(\"seed\");x:=1", "render-state", output);
        CHECK(result.admitted && result.execution.reason == StopReason::None);
        const auto error = session.value("saved");
        result = session.execute("x=2;print(\"ONCE\");saved", "render-error", output, true);
        CHECK(result.admitted && result.execution.reason == StopReason::Error && !result.execution.catchable);
        CHECK(result.execution.statement_text.empty() && output == "ONCE\n" && session.value("x").as_int() == 2);
        CHECK(session.value("saved").heap() == error.heap());
        CHECK(error.heap_as<ErrorObject>()->source.empty() && error.heap_as<ErrorObject>()->message == "seed");
        output.clear();
        result = session.execute("print(\"A\");print(\"B\")", "render-success", output, true);
        CHECK(result.admitted && result.execution.reason == StopReason::None && output == "A\nB\n");
        CHECK(result.execution.statement_text == "nullnull");
    }
    {
        VmSession session;
        std::string output;
        auto r = session.execute("nested:=[[1],[2]];saved:=nested", "nested-state", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        const auto saved = session.value("saved");
        r = session.execute("print(\"FORBIDDEN\");for(row:nested){row.push(\"bad\")}", "nested-refusal", output);
        CHECK(!r.admitted && r.refusal == "session:binding-shape-contract");
        CHECK(r.vm_executions == 0 && r.legacy_executions == 0 && output.empty());
        CHECK(session.value("nested").heap() == saved.heap());
        r = session.execute("for(row:nested){row.push(2.5)}", "nested-numeric", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.vm_executions == 1);
        r = session.evaluate("nested.stringify()", "nested-result", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        CHECK(string_view_of(r.value) == "[[1,2.5],[2,2.5]]");
        r = session.evaluate("saved.stringify()", "nested-snapshot", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        CHECK(string_view_of(r.value) == "[[1],[2]]");
    }
    {
        VmSession session;
        std::string output;
        auto r = session.execute("a:=[[1]];f:=()=>a.stringify();old:=f;fn(g()){return 1}", "combined-state", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        const auto count = session.binding_count();
        const auto array = session.value("a"), function = session.value("f"), named = session.value("g");
        for (const char* rejected : {
            "fn(g()){return 2};leaked:=7;f = ()=>\"changed\";for(row:a){row.push(\"bad\")};print(\"FORBIDDEN\")",
            "fn(g()){return 2};leaked:=7;f = ()=>\"changed\";for(row:a){row.push(2)};print(\"FORBIDDEN\");bytes([65])"}) {
            const auto owner = function.heap_as<FunctionObject>()->program_owner;
            const auto* code = function.heap_as<FunctionObject>()->code;
            r = session.execute(rejected, "combined-refusal", output);
            CHECK(!r.admitted && r.vm_executions == 0 && r.legacy_executions == 0 && output.empty());
            CHECK(r.refusal == (std::string(rejected).find("bytes") != std::string::npos ?
                "builtin-bytes:bytes" : "session:binding-shape-contract"));
            CHECK(!session.contains("leaked") && session.binding_count() == count);
            CHECK(session.value("a").heap() == array.heap() && session.value("f").heap() == function.heap());
            CHECK(session.value("g").heap() == named.heap() && function.heap_as<FunctionObject>()->program_owner == owner &&
                function.heap_as<FunctionObject>()->code == code);
            r = session.execute("print(\"FORBIDDEN\");for(row:a){row.push(\"bad\")}", "bounds-still-original", output);
            CHECK(!r.admitted && r.refusal == "session:binding-shape-contract" && output.empty());
            r = session.evaluate("g()", "named-still-original", output);
            CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 1);
            r = session.evaluate("old()", "reader-still-original", output);
            CHECK(r.admitted && r.execution.reason == StopReason::None && string_view_of(r.value) == "[[1]]");
        }
        r = session.execute("fn(g()){return 3};f = ()=>\"live\";for(row:a){row.push(2.5)};"
            "print(\"ONCE\");throw error(\"stop\")", "combined-runtime", output);
        if (!r.admitted || r.execution.reason != StopReason::Error)
            std::cerr << "combined runtime refusal=" << r.refusal << " runtime=" << r.execution.message << '\n';
        CHECK(r.admitted && r.vm_executions == 1 && r.execution.reason == StopReason::Error && output == "ONCE\n");
        output.clear();
        r = session.evaluate("g()", "replacement-survives", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && r.value.as_int() == 3);
        r = session.evaluate("f()", "rebind-survives", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && string_view_of(r.value) == "live");
        CHECK(session.value("f").heap() != function.heap() && bool(function.heap_as<FunctionObject>()->program_owner));
        r = session.evaluate("old()", "old-reader-new-cell", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && string_view_of(r.value) == "[[1,2.5]]");
        r = session.execute("fn(makec()){caparr:=[[1]];return v=>{for(row:caparr){row.push(v)};return caparr.stringify()}};"
            "c:=makec();warm:=c(0)", "combined-capture", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None);
        // This conservative boundary is a CP19 gap, not successful replacement coverage.
        r = session.execute("fn(g()){return 4};print(\"FORBIDDEN\")", "replacement-with-loop-capture", output);
        CHECK(!r.admitted && r.refusal == "session:redefinition-callable-flow" && output.empty());
        r = session.execute("print(\"FORBIDDEN\");c(\"bad\")", "capture-still-original", output);
        CHECK(!r.admitted && r.refusal == "session:capture-contract" && output.empty());
        r = session.evaluate("c(2)", "capture-valid", output);
        CHECK(r.admitted && r.execution.reason == StopReason::None && string_view_of(r.value) == "[[1,0,2]]");
    }
    CHECK(live_heap_bytes() == before);
    return rt_test::finish("CP19 real eight-unit session");
}
