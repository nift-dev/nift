#include "rt/session_activation.h"
#include "rt/aggregate.h"
#include "rt/cell.h"
#include <unordered_set>
// rt-lint: cold-file equivalent code activation, no ordinary call-entry lookup
namespace nift::rt {
std::vector<std::pair<std::uint64_t, const Proto*>> VmSession::activation_targets(const Program& program) const {
    std::vector<std::pair<std::uint64_t, const Proto*>> targets;
    for (const auto& symbol : namespace_.symbols)
        for (const auto& callable : session_callable_targets(symbol.callable_shape))
            for (const auto& proto : program.protos)
                if (callable.definition->lambda ? proto->session_definition == callable.definition :
                    proto->name == callable.definition->name)
                    targets.emplace_back(callable.identity, proto.get());
    return targets;
}
void VmSession::activate_code(const std::vector<std::pair<std::uint64_t, const Proto*>>& targets,
                              const std::shared_ptr<Program>& owned) {
    std::vector<Value> pending;
    for (const auto& cell : globals_) if (is_cell(cell)) pending.push_back(cell_load(cell));
    std::unordered_set<const HeapObject*> seen;
    while (!pending.empty()) {
        const auto value = std::move(pending.back()); pending.pop_back();
        if (!may_contain_identity(value) || !seen.insert(value.heap()).second) continue;
        if (value.is(Kind::Function)) {
            auto* function = value.heap_as<FunctionObject>();
            const auto* original = static_cast<const Proto*>(function->code);
            for (const auto& target : targets) if (target.first == function->session_identity &&
                target.second->session_definition == original->session_definition) {
                function->code = target.second; function->program_owner = owned; break;
            }
            for (const auto& capture : function->upvalues) {
                const auto* cell = is_cell(capture) ? cell_of(capture) : nullptr;
                if (cell && cell->binding_type >= 0) pending.push_back(cell->value);
            }
        } else if (value.is(Kind::Array)) {
            for (const auto& item : array_items(value)) pending.push_back(item);
        } else if (value.is(Kind::Object)) {
            for (const auto& item : object_entries(value)) pending.push_back(item.second);
        }
    }
}
} // namespace nift::rt
