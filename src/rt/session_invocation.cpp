#include "rt/session_invocation.h"
#include "rt/aggregate.h"
#include "rt/cell.h"
#include "rt/session_shape.h"
#include "rt/string.h"
#include <stdexcept>
// rt-lint: cold-file immutable command invocation bindings, no source evaluation
namespace nift::rt {
std::vector<InvocationBinding> invocation_bindings(const std::string& command,
                                                   const std::vector<std::string>& arguments) {
    auto strings = infer_detail::kinds_ty(kKindString);
    auto array_type = std::make_shared<infer_detail::Ty>();
    array_type->kinds = kKindArray; array_type->elem = strings;
    auto args = make_array(arguments.size());
    for (const auto& argument : arguments) array_push(args, make_string(argument));
    std::vector<InvocationBinding> out;
    auto install = [&](std::string name, Value value, infer_detail::TyRef shape) {
        InvocationBinding binding;
        SessionSymbol& symbol = binding.symbol;
        symbol.name = std::move(name); symbol.slot = static_cast<std::uint16_t>(2 * out.size());
        symbol.kinds = symbol.contract = shape->kinds; symbol.structure = std::move(shape);
        symbol.readonly = true; symbol.const_array = value.is(Kind::Array); symbol.invocation = true;
        binding.type = binding_tag(value);
        binding.value = std::move(value);
        out.push_back(std::move(binding));
    };
    install("cmd", make_string(command), strings);
    install("args", std::move(args), array_type);
    return out;
}

std::vector<std::string_view> invocation_names(const std::vector<InvocationBinding>* invocation) {
    std::vector<std::string_view> names;
    if (invocation) for (const auto& binding : *invocation) names.push_back(binding.symbol.name);
    return names;
}

void VmSession::initialize_invocation(const std::string& command, const std::vector<std::string>& arguments) {
    if (closed_ || executions_ || refusals_ || !namespace_.symbols.empty())
        throw std::logic_error("invocation must precede session admission");
    SessionCompileContext pending;
    std::vector<Value> globals;
    for (auto& binding : invocation_bindings(command, arguments)) {
        globals.push_back(make_cell(std::move(binding.value), binding.type));
        globals.push_back(Value::integer(binding.type));
        pending.symbols.push_back(std::move(binding.symbol));
    }
    pending.slots = static_cast<std::uint16_t>(globals.size());
    globals_.swap(globals); namespace_ = std::move(pending);
}
} // namespace nift::rt
