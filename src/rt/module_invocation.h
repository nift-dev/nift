#pragma once
// The entry script's `args`/`cmd` in imported modules (D-NEXT-packages #4,
// checkpoints/weekend-packages.md §4.5, C1; supersedes D-062 #1). Resolution is
// by binding identity, never by spelling alone:
//   - a module that declares `args := …` (a plain top-level statement) has a
//     private binding: every mention of the name in that module is it, and the
//     linker renames it internally (rename_ids) so it cannot meet the entry's
//     binding or another module's; messages still say `args`;
//   - in any other module, a read inside a function is the entry's binding
//     (as for an entry function at the import's position), unless the function
//     may run while a module initialises;
//   - a read in module top-level code, or in a function some module's
//     top-level code may reach by name (fixed point over the names mentioned
//     in function bodies), is refused: import-scope:invocation-init;
//   - what cannot be proved is refused: import-scope:invocation-ambiguous (a
//     declaration not at top level, a struct/enum of that name, a read before
//     the private declaration, an export of the private binding).
#include "rt/ast.h"
#include "rt/module_graph.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nift::rt {

struct InvocationUnit {
    const Ast* ast = nullptr;
    NodeId root = kNoNode;
};

struct InvocationPlan {
    bool ok = true;
    std::string reason;
    std::size_t where = 0;
    std::uint32_t offset = 0;
    bool positioned = false;
    std::string message;  // a program error (invocation-export): legacy's text with its prefix
    bool note = false;  // invocation-init through a function: the initialising mention
    std::size_t note_where = 0;
    std::uint32_t note_offset = 0;
    // Per unit: the names it declares privately, and the Ident nodes to rename.
    std::vector<std::vector<std::string_view>> private_names;
    std::vector<std::vector<NodeId>> rename_ids;
};

// `units[0]` is the entry (never checked); `names`: the entry's invocation names.
InvocationPlan plan_invocation(const std::vector<InvocationUnit>& units, const ModuleGraph& graph,
                               const std::vector<std::string_view>& names);

} // namespace nift::rt
