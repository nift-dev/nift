#include "rt/session_invocation.h"
#include "rt/aggregate.h"
#include "rt/cell.h"
#include "rt/mutation.h"
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
    build::Array arg_builder(arguments.size());
    for (const auto& argument : arguments) arg_builder.push(make_string(argument));
#if defined(NIFT_ARGSCMD_MUTANT) && NIFT_ARGSCMD_MUTANT == 2  // test-only: a wrong value (args_cmd/mutants.sh)
    if (!arguments.empty()) arg_builder.drop_last();
#endif
    auto args = std::move(arg_builder).publish();
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
    // B1: `args` is immutable through every holder (scripts, sessions and
    // modules all install this value); copy()/deepcopy() give mutable copies.
#if !(defined(NIFT_ARGSCMD_MUTANT) && NIFT_ARGSCMD_MUTANT == 5) && !(defined(NIFT_B5_MUTANT) && NIFT_B5_MUTANT == 7)
    // (test-only controls: args_cmd mutant 5, B5 R-B7: args without kImmutable)
    mutation::make_immutable(args);
#endif
    install("args", std::move(args), array_type);
    return out;
}

void locate_invocation_shadows(std::vector<InvocationBinding>& bindings, const Ast& ast, NodeId root,
                               const std::function<bool(std::uint32_t)>& in_entry) {
    const Node& program = ast.at(root);
    const NodeId* items = ast.list(program);
    for (auto& binding : bindings) {
        binding.shadow = kNoNode;
        for (std::uint32_t k = 0; k < program.count && binding.shadow == kNoNode; ++k) {
            const Node& st = ast.at(items[k]);
            if (st.kind == NodeKind::Declare && st.a != kNoNode && ast.at(st.a).kind == NodeKind::Ident &&
                ast.text(ast.at(st.a)) == binding.symbol.name &&
                in_entry(st.begin))
                binding.shadow = st.a;
        }
    }
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
        // B5 (U6): the injected array is new and held by this binding only: private
        if (binding.symbol.structure && (binding.symbol.structure->kinds & (kKindArray | kKindObject)))
            binding.symbol.heap_contents = binding.symbol.structure;
        pending.symbols.push_back(std::move(binding.symbol));
    }
    pending.slots = static_cast<std::uint16_t>(globals.size());
    globals_.swap(globals); namespace_ = std::move(pending);
}
} // namespace nift::rt
