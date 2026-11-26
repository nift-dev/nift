#pragma once
// Syntactic queries over a module's AST used by the module graph
// (rt/module_graph.h): declared names, the generic node walk, `return`
// statements outside functions and `export(<not a name>)` statements.
#include "rt/ast.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nift::rt {

// The name a Declare/FnDecl/StructDecl/EnumDecl statement declares, or empty.
std::string_view declared_name(const Ast& ast, const Node& st);

// `path` is strictly below `root` (lexically).
bool path_inside(const std::filesystem::path& root, const std::filesystem::path& path);

// Calls `visit(id, in_function)` for every node below `root`, in no
// particular order; `in_function` is true inside FnDecl and Lambda bodies.
template <typename Visit>
void walk(const Ast& ast, NodeId root, Visit visit) {
    std::vector<std::pair<NodeId, bool>> stack{{root, false}};
    while (!stack.empty()) {
        const auto [id, in_fn] = stack.back();
        stack.pop_back();
        if (id == kNoNode) continue;
        const Node& n = ast.at(id);
        visit(id, in_fn);
        const bool below = in_fn || n.kind == NodeKind::FnDecl || n.kind == NodeKind::Lambda;
        for (NodeId child : {n.a, n.b, n.c}) stack.push_back({child, below});
        const NodeId* list = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back({list[k], below});
    }
}

// The smallest offset of a `return` (valued if `valued`, else any) outside
// functions below `id`; UINT32_MAX when there is none.
std::uint32_t first_return(const Ast& ast, NodeId id, bool valued);

// `export(<not a name>)`, which the parser keeps as an Opaque statement.
bool export_without_name(const Ast& ast, const Node& st);

// Names an export may collide with in the importer: its top-level
// bindings, and functions, structs and enums declared anywhere (legacy's
// global tables). Declared after the import too (conservative).
std::vector<std::string_view> importer_names(const Ast& ast, NodeId root);

// Identifier nodes that provably belong to a parameter or to a plain
// declaration in a function's block. Used only by the link visibility check.
std::vector<bool> module_local_uses(const Ast& ast, NodeId root, const std::vector<NodeId>& uses);

// An import's plain string-literal argument (empty included: a package name).
bool literal_argument(const Ast& ast, const Node& st, std::string& out);

// The first import or export (in text order) that is not a top-level
// statement: "import-nested" / "export-nested" at `at`, or nullptr.
const char* first_misplaced(const Ast& ast, NodeId root, std::uint32_t& at);

// Legacy's load-time validation of a module (D-022 #3, rt/prevalidate.h;
// NIFT_PREVALIDATE=off skips it, as legacy): false with the first failure.
bool module_prevalidates(const std::string& source, std::uint32_t& offset, std::string& message);

// An export named like one of `names` (the entry's invocation bindings)
// into the entry (`importer` 0).
bool invocation_export(std::size_t importer, std::string_view exported, const std::vector<std::string_view>& names);

} // namespace nift::rt
