#include "rt/module_scan.h"

#include "rt/literal.h"
#include "rt/prevalidate.h"
#include "rt/string.h"

#include <algorithm>
#include <cstdlib>

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

bool literal_argument(const Ast& ast, const Node& st, std::string& out) {
    if (st.a == kNoNode) return false;
    const Node& arg = ast.at(st.a);
    Value value;
    if (arg.kind != NodeKind::String || arg.count != 0) return false;
    if (!decode_string_literal(ast.text(arg), value)) return false;
    out = std::string(string_view_of(value));
    return true;  // empty: a package name (legacy: invalid package name)
}

const char* first_misplaced(const Ast& ast, NodeId root, std::uint32_t& at) {
    const Node& program = ast.at(root);
    const NodeId* items = ast.list(program);
    const NodeId* last = items + program.count;
    const auto top = [&](NodeId id) { return std::find(items, last, id) != last; };
    at = UINT32_MAX;
    const char* reason = nullptr;
    walk(ast, root, [&](NodeId id, bool) {
        const Node& n = ast.at(id);
        const char* r = n.kind == NodeKind::Import && !top(id)   ? "import-nested"
                        : n.kind == NodeKind::Export && !top(id) ? "export-nested"
                                                                 : nullptr;
        if (r && n.begin < at) { at = n.begin; reason = r; }
    });
    return reason;
}

bool module_prevalidates(const std::string& source, std::uint32_t& offset, std::string& message) {
    const char* mode = std::getenv("NIFT_PREVALIDATE");
#if defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 17  // test-only (packages/mutants.sh)
    mode = "off";
#endif
    if (mode && std::string_view(mode) == "off") return true;
    const Prevalidation verdict = prevalidate(source);
    offset = verdict.offset;
    message = verdict.message;
    return verdict.ok;
}

bool invocation_export(std::size_t importer, std::string_view exported, const std::vector<std::string_view>& names) {
    return importer == 0 && std::find(names.begin(), names.end(), exported) != names.end();
}

} // namespace nift::rt
