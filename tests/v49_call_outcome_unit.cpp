#include "Ast.h"
#include <cassert>
#include <string>

// An unsupported outcome may consume its arguments. A secondary handler must
// still receive the original values; without a secondary handler ownership can
// pass directly to the outcome handler.
int main() {
    using nift::RuntimeValue;
    using Outcome = nift::detail::EvalOutcome<RuntimeValue>;
    for (bool method : {false, true}) {
        for (bool retry : {false, true}) {
            auto parsed = nift::ast::parse_expression(method ? "a.test([1,2])" : "test([1,2])");
            assert(parsed.supported);
            nift::ast::Context context;
            context.resolve = [](const std::string&, RuntimeValue& out, std::string&) {
                out = RuntimeValue::make_array(); return true;
            };
            context.resolve_ref = [](const std::string&, std::shared_ptr<const RuntimeValue>& out, std::string&) {
                out = std::make_shared<const RuntimeValue>(RuntimeValue::make_array()); return true;
            };
            int outcomes = 0, retries = 0;
            auto primary = [&](std::vector<RuntimeValue>&& args) {
                ++outcomes;
                assert(args.size() == 1 && args[0].array.size() == 2);
                if (!retry) return Outcome::value(std::move(args[0]));
                args[0].array.clear(); args.clear();
                return Outcome::unsupported();
            };
            auto secondary = [&](std::vector<RuntimeValue>&& args, RuntimeValue& out) {
                ++retries;
                assert(args.size() == 1 && args[0].array.size() == 2);
                out = std::move(args[0]); return true;
            };
            if (method) {
                context.native_method_outcome = [&](const RuntimeValue&, const std::string&, std::vector<RuntimeValue>&& args) { return primary(std::move(args)); };
                if (retry) context.native_method = [&](const RuntimeValue&, const std::string&, std::vector<RuntimeValue>&& args, RuntimeValue& out, std::string&) { return secondary(std::move(args), out); };
            } else {
                context.call_outcome = [&](const std::string&, std::vector<RuntimeValue>&& args) { return primary(std::move(args)); };
                if (retry) context.call = [&](const std::string&, std::vector<RuntimeValue>&& args, RuntimeValue& out, std::string&) { return secondary(std::move(args), out); };
            }
            RuntimeValue out; std::string error;
            assert(nift::ast::evaluate(*parsed.expr, context, out, error));
            assert(error.empty() && out.array.size() == 2 && out.array[0].num == 1 && out.array[1].num == 2);
            assert(outcomes == 1 && retries == (retry ? 1 : 0));
        }
    }
}
