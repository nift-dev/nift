#include "rt/session_location.h"
#include "rt/compiler.h"
#include "rt/aggregate.h"
#include "rt/cell.h"
#include "rt/string.h"
#include <unordered_set>
// rt-lint: cold-file stable storage identities; never reads a selector cache or selected value
namespace nift::rt {
namespace {
std::uintptr_t location(const Value& value, SessionCaptureLocation& out,
                        std::unordered_set<const CellObject*>& active, unsigned depth) {
    const auto* cell = cell_of(value);
    if (!cell || depth > 64 || !active.insert(cell).second) return 0;
    auto key = reinterpret_cast<std::uintptr_t>(cell);
    if (cell->binding_type == kSourceSlot) {
        const auto parent = location(cell->value, out, active, depth + 1);
        if (!parent) key = 0;
        else out.edges.push_back({key, parent, false, {}});
    } else if (cell->binding_type == kSelectorCell) {
        if (!cell->value.is(Kind::Array) || array_size(cell->value) != 8) key = 0;
        else {
            const auto& descriptor = array_items(cell->value);
            const auto* local = cell_of(descriptor[7]);
            const auto parent = location(descriptor[0], out, active, depth + 1);
            if (!local || !parent || !(descriptor[1].is(Kind::Int) || descriptor[1].is(Kind::String))) key = 0;
            else {
                key = reinterpret_cast<std::uintptr_t>(local);
                out.edges.push_back({key, parent, descriptor[1].is(Kind::Int),
                    descriptor[1].is(Kind::String) ? std::string(string_view_of(descriptor[1])) : std::string{},
                    descriptor[1].is(Kind::String)});
            }
        }
    } else if (cell->binding_type < 0) key = 0;
    active.erase(cell);
    return key;
}
}
SessionCaptureLocation session_capture_location(const Value& value) {
    SessionCaptureLocation out;
    std::unordered_set<const CellObject*> active;
    out.key = location(value, out, active, 0);
    return out;
}
void clear_session_locations(SessionCompileContext& context) {
    context.global_locations.clear(); context.capture_locations.clear(); context.capture_initializers.clear();
    context.call_result_seeds.clear(); context.reserved_rebindings.clear(); context.unit_begin = 0;
}
void seed_session_locations(SessionCompileContext& context, const std::vector<Value>& globals) {
    context.global_locations.clear();
    for (const auto& symbol : context.symbols)
        if (symbol.defined && symbol.slot < globals.size())
            context.global_locations.emplace(symbol.name, session_capture_location(globals[symbol.slot]));
}

}
