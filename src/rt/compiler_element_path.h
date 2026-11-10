#pragma once
// Checkpoint 1, the assignment family (combined-b3-b5.md section 94): element stores and compound
// updates through a path of up to three levels (`g[i][j] = v`, `o["k"] = v`, `a[i] op= v`); internal
// to the compiler.
#include "rt/compiler_expr.h"

#include <vector>

namespace nift::rt::detail {

struct ElementPath {
    // True when `target` (an Index) is not the plain array store `name[key]` that UnitCompiler::assign
    // compiles itself: a nested target, or a receiver proved to be an object.
    static bool form(const UnitCompiler& c, const Node& target);
    // `target = value` (owner 16:35, option C; H-A): the receiver, then the value, then the keys and
    // the intermediate containers, then the checked commit. `result` as UnitCompiler::assign.
    static bool store(UnitCompiler& c, const Node& node, const std::uint16_t* result);
    // `target op= value` (D2): the receiver, the keys, the intermediate containers and the element,
    // then the value, then the operation and the commit into the container read first.
    static bool compound(UnitCompiler& c, const Node& node);

private:
    struct Root {
        std::uint16_t reg = 0;  // the receiver, retained when the value can rebind its name
        Binding held;
        bool global = false, indirect = false;
    };
    static bool levels(UnitCompiler& c, const Node& target, std::vector<const Node*>& out);
    static bool key_ok(const UnitCompiler& c, const Node& level);
    static bool kinds_ok(UnitCompiler& c, const Node& level, const Node& target);
    static bool root(UnitCompiler& c, const Node& object, const Node& target, NodeId rhs, Root& out);
    static bool write_back(UnitCompiler& c, const Root& root, NodeId rhs);
};

} // namespace nift::rt::detail
