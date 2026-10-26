#include "rt/compiler_session_order.h"
#include "rt/compiler.h"
#include "rt/alias.h"
#include <algorithm>
// rt-lint: cold-file textual argument types for phase-dependent registry calls
namespace nift::rt::detail {
bool UnitCompiler::session_order_call_types(const Node& node, NodeId callee, std::uint16_t base, bool& phased) {
    phased = false;
    const auto targets = kinds_.callable_targets.find(callee);
    if (targets != kinds_.callable_targets.end()) for (const auto target : targets->second) {
        if (ast_.at(target).kind != NodeKind::FnDecl) continue;
        const auto name = std::string(text(ast_.at(ast_.at(target).a)));
        phased = phased || session_->phased_functions.count(name);
        for (const auto& alias : session_->named_templates) phased = phased || alias.second == name;
    }
    if (phased) {
        for (std::uint32_t k = 0; k < node.count; ++k) {
            const auto argument = ast_.list(node)[k];
            for (const auto target : targets->second) {
                const auto found = std::find(plan_.functions.begin(), plan_.functions.end(), target);
                if (found == plan_.functions.end()) continue;
                const auto f = static_cast<std::size_t>(found - plan_.functions.begin());
                if (f < plan_.escaping.size() && k < plan_.escaping[f].size() && plan_.escaping[f][k] &&
                    is_place_argument(ast_, argument)) return fail(ast_.at(argument), "alias-argument");
                if (k < observable_[f].size() && observable_[f][k] && loop_reachable() &&
                    type_mismatch_possible(text_type_at(argument), kinds(argument), scope_index()))
                    return fail(ast_.at(argument), "context:parameter-type");
            }
            const auto destination = static_cast<std::uint16_t>(base + node.count + k);
            while (fb_->top() <= destination) { std::uint16_t reg = 0; if (!temp(reg)) return false; }
            std::uint16_t operand = 0;
            if (!text_type_of(text_type_at(argument), ast_.at(argument), operand)) return false;
            if (operand & kConstantBit) fb_->emit(OpCode::LoadK, destination, operand & ~kConstantBit);
            else if (operand != destination) fb_->emit(OpCode::Move, destination, operand);
        }
        return true;
    }
    return true;
}
} // namespace nift::rt::detail
