#include "../src/Ast.h"

#include <cassert>
#include <iostream>
#include <limits>
#include <unordered_map>

int main() {
    std::unordered_map<std::string, nift::RuntimeValue> vars{
        {"x", nift::RuntimeValue(7.0)}, {"y", nift::RuntimeValue(3.0)}};
    nift::RuntimeValue arr = nift::RuntimeValue::make_array();
    arr.array.emplace_back(9.0);
    vars["a"] = arr;
    nift::RuntimeValue obj = nift::RuntimeValue::make_object();
    obj["v"] = nift::RuntimeValue(11.0);
    vars["o"] = obj;
    nift::RuntimeValue tiny;
    tiny.type = nift::RuntimeType::StrNumber;
    tiny.string = "1e-1000";
    vars["tiny"] = tiny;
    vars["huge"] = nift::RuntimeValue(std::numeric_limits<double>::max());

    nift::ast::Context context;
    context.resolve = [&](const std::string& name, nift::RuntimeValue& out, std::string& error) {
        auto found = vars.find(name);
        if (found == vars.end()) { error = "missing " + name; return false; }
        out = found->second;
        return true;
    };
    context.render = [](const nift::RuntimeValue& value) {
        return value.is_string() ? value.string
             : value.is_number() ? value.dump(0) : std::string();
    };

    for (const auto* source : {"42", "x", "(x + y) * 2", "x + y * 2", "-x + 10",
                               "x > y && y == 3", "x < y || y <= 3", "!(x < y)",
                               "a[0] + o.v"}) {
        auto parsed = nift::ast::parse_expression(source);
        if (!parsed.supported) { std::cerr << source << ": " << parsed.error << '\n'; return 1; }
        nift::RuntimeValue out;
        std::string error;
        if (!nift::ast::evaluate(*parsed.expr, context, out, error)) {
            std::cerr << error << '\n';
            return 1;
        }
    }

    auto evaluate = [&](const std::string& source) {
        auto parsed = nift::ast::parse_expression(source);
        assert(parsed.supported);
        nift::RuntimeValue out;
        std::string error;
        assert(nift::ast::evaluate(*parsed.expr, context, out, error));
        return out;
    };
    assert(evaluate("!tiny").is_bool() && !evaluate("!tiny").boolean);
    assert(evaluate("-tiny").string == "-1e-1000");
    assert(evaluate("+tiny").string == "1e-1000");
    assert(evaluate("1e-1000").type == nift::RuntimeType::StrNumber);

    auto overflow = nift::ast::parse_expression("huge * 2");
    assert(overflow.supported);
    nift::RuntimeValue overflow_result;
    std::string overflow_error;
    assert(!nift::ast::evaluate(*overflow.expr, context, overflow_result, overflow_error));
    assert(overflow_error == "arithmetic result is not finite");

    for (const auto* source : {"x = y + 1", "x += y", "++x", "y--",
                               "if(x > y) { x += 1 }", "while(x < 10) { x++ }"}) {
        auto statement = nift::ast::parse_statement(source);
        if (!statement.supported) { std::cerr << "statement: " << source << '\n'; return 1; }
    }
    return 0;
}
