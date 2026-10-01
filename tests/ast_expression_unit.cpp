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

    // CP1 baseline: prepared callable dispatch uses false + empty error for
    // Unsupported, while false + a message is an actual failure.
    {
        auto parsed = nift::ast::parse_expression("probe()");
        assert(parsed.supported);
        int legacy_calls = 0;
        context.call = [](const std::string&, std::vector<nift::RuntimeValue>&&,
                          nift::RuntimeValue&, std::string&) { return false; };
        context.legacy = [&](const std::string& source, nift::RuntimeValue& out,
                             std::string&) {
            ++legacy_calls;
            assert(source == "probe()");
            out = nift::RuntimeValue(19);
            return true;
        };
        nift::RuntimeValue out;
        std::string error;
        assert(nift::ast::evaluate(*parsed.expr, context, out, error));
        assert(out.is_number() && out.num == 19 && legacy_calls == 1);

        context.call = [](const std::string&, std::vector<nift::RuntimeValue>&&,
                          nift::RuntimeValue&, std::string& error) {
            error = "prepared callable failed";
            return false;
        };
        legacy_calls = 0;
        error.clear();
        assert(!nift::ast::evaluate(*parsed.expr, context, out, error));
        assert(error == "prepared callable failed" && legacy_calls == 0);
    }

    // Native methods currently have no failure/unsupported distinction: false
    // falls back even when the prepared method supplied an error. CP2 must make
    // any correction explicit rather than silently changing this baseline.
    {
        auto parsed = nift::ast::parse_expression("o.probe()");
        assert(parsed.supported);
        int legacy_calls = 0;
        context.native_method = [](const nift::RuntimeValue&, const std::string&,
                                   std::vector<nift::RuntimeValue>&&,
                                   nift::RuntimeValue&, std::string& error) {
            error = "prepared method failed";
            return false;
        };
        context.legacy = [&](const std::string& source, nift::RuntimeValue& out,
                             std::string& error) {
            ++legacy_calls;
            assert(source == "o.probe()");
            assert(error == "prepared method failed");
            error.clear();
            out = nift::RuntimeValue(23);
            return true;
        };
        nift::RuntimeValue out;
        std::string error;
        assert(nift::ast::evaluate(*parsed.expr, context, out, error));
        assert(out.is_number() && out.num == 23 && legacy_calls == 1);
    }

    for (const auto* source : {"x = y + 1", "x += y", "++x", "y--",
                               "if(x > y) { x += 1 }", "while(x < 10) { x++ }"}) {
        auto statement = nift::ast::parse_statement(source);
        if (!statement.supported) { std::cerr << "statement: " << source << '\n'; return 1; }
    }
    return 0;
}
