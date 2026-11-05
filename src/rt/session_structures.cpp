#include "rt/session_structures.h"
#include "rt/compiler_session_scope.h"
// rt-lint: cold-file immutable session schema publication after whole-unit admission
namespace nift::rt {
void session_structures(const Ast& ast, NodeId root, const SessionText& text,
                        const std::string& path, SessionCompileContext& pending) {
    // CP12b admits only top-level schemas before any executable statement.
    // Their constant fields have no declaration-time evaluation or Host effects.
    const auto& body = ast.at(root);
    for (std::uint32_t k = 0; k < body.count; ++k) {
        const auto& node = ast.at(ast.list(body)[k]);
        if (node.kind != NodeKind::StructDecl || node.begin < text.unit_begin) continue;
        auto definition = std::make_shared<SessionDefinition>();
        definition->structure = true; definition->name = ast.text(ast.at(node.a));
        definition->source = text.pieces.back().source; definition->path = path;
        definition->begin = node.begin - text.unit_begin; definition->end = node.end - text.unit_begin;
        bool methods = false;
        for (std::uint32_t m = 0; m < node.count; ++m) {
            const auto& member = ast.at(ast.list(node)[m]);
            methods = methods || member.kind == NodeKind::FnDecl;
            // B3 (review H1): a presentation method, for every later statement.
            const std::string_view named = ast.text(ast.at(member.a));
            if (member.kind == NodeKind::FnDecl && member.count == 0 && !(member.flags & kFlagPrivate) &&
                (named == "stringify" || named == "prettify" || named == "highlight"))  // rt-lint: allow-string-compare
                pending.presentation_methods.emplace(named);
        }
        if (methods) for (std::uint32_t m = 0; m < node.count; ++m) {
            const auto& field = ast.at(ast.list(node)[m]);
            if (field.kind == NodeKind::StructField &&
                detail::session_method_uses_bare_field(ast, ast.list(body)[k], ast.text(ast.at(field.a))))
                definition->dynamic_fields.emplace_back(ast.text(ast.at(field.a)));
        }
        pending.structures.insert_or_assign(definition->name, std::move(definition));
    }
    // B3: a descriptor made by a struct's declaring statement belongs to the
    // schema version this statement publishes (compiler_structs.cpp).
    for (auto it = pending.struct_types.begin(); it != pending.struct_types.end();) {
        if (it->second.definition) { ++it; continue; }
        const auto published = pending.structures.find(it->first);
        if (published == pending.structures.end()) { it = pending.struct_types.erase(it); continue; }
        it->second.definition = published->second;
        ++it;
    }
}
} // namespace nift::rt
