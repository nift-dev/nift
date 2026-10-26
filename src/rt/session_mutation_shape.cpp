#include "rt/session_mutation_shape.h"
#include "rt/literal.h"
#include "rt/string.h"
#include <algorithm>
// rt-lint: cold-file persistent nested collection effects, never runtime traversal
namespace nift::rt {
namespace {
using namespace infer_detail;
TyRef update(const Ast& ast, const std::vector<NodeId>& path, std::size_t at,
             const TyRef& type, const TyRef& value) {
    if (!type || type->any) return type;
    auto out = std::make_shared<Ty>(*type);
    if (at == path.size()) {
        if (type->kinds & kKindArray) {
            out->tuple = false; out->items.clear();
            out->elem = unite(type->elem, value);
        } else if (type->kinds & kKindObject) {
            for (auto& field : out->fields) field.second = unite(field.second, value);
            if (out->elem) out->elem = unite(out->elem, value);
        }
        return out;
    }
    const auto& step = ast.at(path[at]);
    if (step.kind == NodeKind::Index && (type->kinds & kKindArray)) {
        const auto& key = ast.at(step.b);
        const auto position = key.kind == NodeKind::Number ? literal_position(ast.text(key)) : -1;
        if (type->tuple) {
            for (std::size_t k = 0; k < out->items.size(); ++k)
                if (position < 0 || static_cast<std::size_t>(position) == k) {
                    out->items[k] = unite(out->items[k], update(ast, path, at + 1, out->items[k], value));
                    out->elem = unite(out->elem, out->items[k]);
                }
        } else out->elem = unite(type->elem, update(ast, path, at + 1, type->elem, value));
    }
    if (type->kinds & kKindObject) {
        std::string name;
        bool known = step.kind == NodeKind::Member;
        if (known) name = ast.text(ast.at(step.b));
        else if (ast.at(step.b).kind == NodeKind::String && !ast.at(step.b).count) {
            Value decoded;
            if (decode_string_literal(ast.text(ast.at(step.b)), decoded)) {
                name = string_view_of(decoded); known = true;
            }
        }
        for (auto& field : out->fields) if (!known || field.first == name)
            field.second = unite(field.second, update(ast, path, at + 1, field.second, value));
        if (out->elem) out->elem = unite(out->elem, update(ast, path, at + 1, out->elem, value));
    }
    return out;
}
} // namespace
NodeId session_mutation_root(const Ast& ast, NodeId receiver) {
    while (receiver != kNoNode) {
        const auto& node = ast.at(receiver);
        if (node.kind == NodeKind::Ident) return receiver;
        if (node.kind != NodeKind::Index && node.kind != NodeKind::Member) return kNoNode;
        receiver = node.a;
    }
    return kNoNode;
}
infer_detail::TyRef session_mutation_shape(const Ast& ast, NodeId receiver,
        const infer_detail::TyRef& root, const infer_detail::TyRef& element) {
    std::vector<NodeId> path;
    while (receiver != kNoNode && ast.at(receiver).kind != NodeKind::Ident) {
        path.push_back(receiver); receiver = ast.at(receiver).a;
    }
    std::reverse(path.begin(), path.end());
    return update(ast, path, 0, root, element);
}
} // namespace nift::rt
