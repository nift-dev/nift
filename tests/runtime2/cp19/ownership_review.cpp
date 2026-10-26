// CP19: expression-result ownership outlives VM, compiler, AST and source.
#include "rt/aggregate.h"
#include "rt/compiler.h"
#include "rt/error.h"
#include "rt/legacy_bridge.h"
#include "rt/parser.h"
#include "rt/source_map.h"
#include "rt/string.h"
#include "rt/unit_check.h"
#include "rt/vm.h"
#include "rt/script_runner.h"
#include "rt/compare.h"
#include <cstdlib>
#include "rt_test.h"

#include <string>

using namespace nift::rt;

namespace {
Value evaluate(std::string source) {
    Ast ast(source);
    const NodeId expression = parse_expression(ast, 0, static_cast<std::uint32_t>(source.size()));
    CHECK(first_opaque_node(ast, expression) == kNoNode);
    auto compiled = compile_expression_unit(ast, expression);
    CHECK(compiled.program != nullptr);
    if (!compiled.program) return Value();
    const SourceMap map = SourceMap::single("<eval>", source);
    Value captured;
    VmOptions options;
    options.source_map = &map;
    options.return_value = &captured;
    Vm vm(*compiled.program, options);
    std::string output;
    const auto result = vm.run(output);
    CHECK(result.reason == StopReason::None);
    CHECK(result.returned && result.return_text.empty());
    CHECK(output.empty());
    return captured;
}

void check_owned_result() {
    Value array = evaluate("[\"owned\", {\"key\":[1,2,3]}]");
    CHECK(array.is(Kind::Array) && array_size(array) == 2);
    CHECK(string_view_of(array_items(array)[0]) == "owned");
    nift::RuntimeValue legacy;
    std::string error;
    CHECK(to_legacy(array, legacy, error));
    CHECK(legacy.array[1].object[0].second.array.size() == 3);
    Value copy = array;
    array = Value();
    CHECK(string_view_of(array_items(copy)[0]) == "owned");
    // Repeated construction/destruction must not borrow a prior VM's storage.
    for (int i = 0; i < 200; ++i) {
        auto next = evaluate("{\"x\":[\"repeat\",1]}");
        CHECK(to_legacy(next, legacy, error));
        CHECK(legacy.object[0].second.array[0].string == "repeat");
    }
}

void check_error_origin() {
    auto error = evaluate("\n  error(\"owned-origin\")");
    CHECK(error.is(Kind::Error));
    nift::RuntimeValue legacy;
    std::string message;
    CHECK(to_legacy(error, legacy, message));
    CHECK(legacy.is_error());
    Value source, line, column;
    CHECK(error_member(error, "source", source));
    CHECK(error_member(error, "line", line));
    CHECK(error_member(error, "column", column));
    CHECK(string_view_of(source).empty() && line.as_int() == 0 && column.as_int() == 0);
}

void check_script_return_unchanged() {
    const std::string source = "return [1,2]";
    Ast ast(source);
    auto parsed = parse_script(ast);
    CHECK(parsed.ok);
    auto compiled = compile_unit(ast, parsed.root);
    CHECK(!compiled.program && compiled.reason == "type:script-return");
    const std::string scalar = "return 7";
    Ast scalar_ast(scalar);
    auto scalar_parsed = parse_script(scalar_ast);
    compiled = compile_unit(scalar_ast, scalar_parsed.root);
    CHECK(compiled.program != nullptr);
    Vm vm(*compiled.program);
    std::string output;
    auto result = vm.run(output);
    CHECK(result.reason == StopReason::None && result.returned && result.return_text == "7");
}

void independent_ownership() {
    Value a=evaluate("[1,2]");Value saved=a;array_push(a,Value::integer(3));
    CHECK(array_size(saved)==2 && array_size(a)==3);
    Value object=evaluate("{\"a\":[1,2]}");Value other=object;
    Value child=*object_get(object,atom("a"));array_set(child,0,Value::integer(99));
    object_set(object,atom("a"),child);
    CHECK(equal(array_items(*object_get(other,atom("a")))[0],Value::integer(1)));
    CHECK(array_items(*object_get(object,atom("a")))[0].as_int()==99);
    saved=Value();a=Value();other=Value();
    CHECK(array_size(child)==2 && array_items(child)[0].as_int()==99);
    ::setenv("NIFT_VM","require",1);::unsetenv("NIFT_VM_STATS");
    nift::RuntimeValue legacy,retained;std::string error;int exit=999;
    for(int i=0;i<40;++i) {
        std::string source="["+std::to_string(i)+",{\"x\":[1,2]}]";
        error.clear();exit=999;
        CHECK(eval_expression_on_vm(source,legacy,error,exit));
        CHECK(exit==0 && error.empty());
        CHECK(legacy.array[0].num==i && legacy.array[1].object[0].second.array.size()==2);
        if(i==0)retained=legacy;
    }
    CHECK(retained.array[0].num==0);
}
} // namespace

int main() {
    independent_ownership();
    check_owned_result();
    check_error_origin();
    check_script_return_unchanged();
    return rt_test::finish("CP19 expression ownership");
}
