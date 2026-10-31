#include "rt/module_scan.h"

#include <algorithm>

// rt-lint: cold-file compile-time analysis, once per module

namespace nift::rt {

std::string_view declared_name(const Ast& ast, const Node& st) {
    const bool decl = st.kind == NodeKind::Declare || st.kind == NodeKind::FnDecl ||
                      st.kind == NodeKind::StructDecl || st.kind == NodeKind::EnumDecl;
    if (!decl || st.a == kNoNode || ast.at(st.a).kind != NodeKind::Ident) return {};
    return ast.text(ast.at(st.a));
}

bool path_inside(const std::filesystem::path& root, const std::filesystem::path& path) {
    const std::filesystem::path rel = path.lexically_relative(root);
    return !rel.empty() && *rel.begin() != "..";
}

std::uint32_t first_return(const Ast& ast, NodeId id, bool valued) {
    std::uint32_t at = UINT32_MAX;
    walk(ast, id, [&](NodeId n, bool in_fn) {
        const Node& r = ast.at(n);
        if (in_fn || r.kind != NodeKind::Return) return;
        if (valued && ((r.flags & kFlagBare) || r.a == kNoNode)) return;
        at = std::min(at, r.begin);
    });
    return at;
}

bool export_without_name(const Ast& ast, const Node& st) {
    if (st.kind != NodeKind::Opaque) return false;
    const std::string_view text = ast.text(st);
    if (text.rfind("export", 0) != 0) return false;
    std::size_t p = 6;
    while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
    return p < text.size() && text[p] == '(';
}

std::vector<std::string_view> importer_names(const Ast& ast, NodeId root) {
    std::vector<std::string_view> names;
    const auto has = [&](std::string_view name) { return std::find(names.begin(), names.end(), name) != names.end(); };
    const Node& program = ast.at(root);
    const NodeId* items = ast.list(program);
    for (std::uint32_t k = 0; k < program.count; ++k)
        if (const std::string_view name = declared_name(ast, ast.at(items[k])); !name.empty()) names.push_back(name);
    walk(ast, root, [&](NodeId id, bool) {
        const Node& n = ast.at(id);
        if (n.kind == NodeKind::Declare || (n.flags & kFlagMethod)) return;
        if (const std::string_view name = declared_name(ast, n); !name.empty() && !has(name)) names.push_back(name);
    });
    return names;
}

} // namespace nift::rt
