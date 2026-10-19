#pragma once
// Globals in the kind inference (CP12b, rt/globals.h): the one shared slot
// of a top-level binding that functions read or write as a free name.
#include "rt/globals.h"
#include "rt/infer_types.h"

#include <cstddef>
#include <set>
#include <string_view>
#include <vector>

namespace nift::rt::infer_detail {

// The persistent slot (in `vars`) of the binding declared at node `at`,
// created on first use; `persist` maps node ids to slots.
std::size_t persistent_slot(std::vector<std::size_t>& persist, std::vector<TyRef>& vars, std::size_t nodes, NodeId at);

class GlobalSlots {
public:
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    explicit GlobalSlots(const GlobalPlan* plan) : plan_(plan) {}
    // The slot (in `vars`, indexed by declaration node in `persist`) of free
    // name `name` of function `f` when it resolves to a global; kNone otherwise.
    std::size_t find(int f, std::string_view name, std::vector<std::size_t>& persist, std::vector<TyRef>& vars,
                     std::size_t nodes);
    // Whether `var` (an element of `vars`) is a global's shared slot.
    bool shared(const std::vector<TyRef>& vars, const TyRef* var) const {
        return !slots_.empty() && var >= vars.data() && var < vars.data() + vars.size() &&
               slots_.count(static_cast<std::size_t>(var - vars.data())) != 0;
    }

private:
    const GlobalPlan* plan_;
    std::set<std::size_t> slots_;
};

} // namespace nift::rt::infer_detail
