#pragma once
// Source-level linking of a unit's static imports (CP12 phase B, modules;
// D-037, D-041, D-046). Where the module graph (rt/module_graph.h) admits a
// unit, and its scopes make it exact, the program the VM compiles is the
// entry script with every `import(...)` statement replaced by the imported
// module's own (linked) text and every `export(...)` blanked: one unit, so
// every analysis (globals, kinds, D-018 contexts, aliasing) applies across
// modules unchanged. It is exact because:
//   - each module is imported once (InitPolicy::RefuseReinitialisation:
//     legacy runs it once too) and initialises in place, at its import;
//   - an exported binding is the module's binding itself (D-046 #2);
//   - names stay apart: the names declared outside functions, and the
//     function names, of different units are disjoint, and a unit uses
//     another unit's such name only where it imports it (legacy isolates
//     module scopes; module functions resolve names lexically, D-046 #4);
//   - a module has no top-level `return` (legacy ends only the module).
// Otherwise the unit is refused with a reason (legacy runs it).
// Positions in the linked text map back to (module, offset) for
// diagnostics: errors raised while a module initialises carry legacy's
// `import: ` prefix once per active import level.
#include "rt/ast.h"
#include "rt/module_graph.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nift::rt {

struct LinkSegment {
    std::size_t begin = 0;       // [begin, end) in the linked text
    std::size_t end = 0;
    std::size_t module = 0;      // index into graph.modules (0: the entry)
    std::size_t source_begin = 0;  // offset of `begin` in that module's own text
};

struct LinkedUnit {
    bool linked = false;  // the entry imports modules (otherwise nothing changes)
    bool ok = true;
    std::string reason;
    std::size_t where = 0;     // module of the refused construct
    std::uint32_t offset = 0;  // its offset in that module's text
    ModuleGraph graph;
    std::string text;          // the linked program (when linked && ok)
    std::vector<LinkSegment> segments;
    std::vector<int> depth;    // per module: import nesting (entry 0)
    std::vector<std::string_view> module_names;  // declared outside functions in imported modules

    struct Location {
        std::size_t module = 0;
        std::size_t offset = 0;
    };
    // The module position of a linked-text offset.
    Location locate(std::size_t offset) const;
};

// `entry_source` is the text `entry` was parsed from.
LinkedUnit link_unit(const Ast& entry, NodeId root, std::string_view entry_source,
                     const std::filesystem::path& entry_path);

} // namespace nift::rt
