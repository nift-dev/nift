#include "rt/session_invocation.h"
#include "rt/aggregate.h"
#include "rt/cell.h"
#include "rt/session_shape.h"
#include "rt/string.h"
#include <stdexcept>
// rt-lint: cold-file immutable command invocation bindings, no source evaluation
namespace nift::rt {
void VmSession::initialize_invocation(const std::string& command, const std::vector<std::string>& arguments) {
    if (closed_ || executions_ || refusals_ || !namespace_.symbols.empty())
        throw std::logic_error("invocation must precede session admission");
    auto strings = infer_detail::kinds_ty(kKindString);
    auto array_type = std::make_shared<infer_detail::Ty>();
    array_type->kinds = kKindArray; array_type->elem = strings;
    auto args = make_array(arguments.size());
    for (const auto& argument : arguments) array_push(args, make_string(argument));
    SessionCompileContext pending;
    std::vector<Value> globals;
    auto install = [&](std::string name, Value value, infer_detail::TyRef shape) {
        SessionSymbol symbol;
        symbol.name = std::move(name); symbol.slot = static_cast<std::uint16_t>(globals.size());
        symbol.kinds = symbol.contract = shape->kinds; symbol.structure = std::move(shape);
        symbol.readonly = true; symbol.const_array = value.is(Kind::Array);
        const auto type = binding_tag(value);
        globals.push_back(make_cell(std::move(value), type)); globals.push_back(Value::integer(type));
        pending.symbols.push_back(std::move(symbol));
    };
    install("cmd", make_string(command), strings);
    install("args", std::move(args), array_type);
    pending.slots = static_cast<std::uint16_t>(globals.size());
    globals_.swap(globals); namespace_ = std::move(pending);
}
} // namespace nift::rt
