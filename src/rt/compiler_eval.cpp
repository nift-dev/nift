#include "rt/compiler_eval.h"
#include "rt/collection.h"
#include "rt/parser.h"
#include "rt/unit_check.h"

// rt-lint: cold-file expression-unit structural adaptation
namespace nift::rt {
void normalize_eval_callbacks(Ast& ast, NodeId root) {
    std::vector<NodeId> pending{root};
    while (!pending.empty()) {
        const auto id = pending.back(); pending.pop_back();
        if (id == kNoNode) continue;
        const Node node = ast.at(id);
        if (node.kind == NodeKind::Call && ast.at(node.a).kind == NodeKind::Member) {
            const Node callee = ast.at(node.a);
            const int method = collection_method(ast.text(ast.at(callee.b)));
            if (ast.at(callee.a).kind == NodeKind::Array && method >= static_cast<int>(CollectionMethod::Map)) {
                const auto* list = ast.list(node);
                std::vector<NodeId> args(list, list + node.count);
                const bool sort_by = method == static_cast<int>(CollectionMethod::SortBy);
                for (std::size_t k = 0; k < args.size(); k += sort_by && args.size() > 1 ? 2 : 1) {
                    if (!sort_by && k > 0) break;  // reduce's accumulator is not a callback
                    const Node argument = ast.at(args[k]);
                    if (argument.kind != NodeKind::String || argument.end < argument.begin + 2) continue;
                    const auto text = ast.text(argument);
                    if (text.find('\\') != std::string_view::npos || text.find("$[") != std::string_view::npos)
                        continue;
                    const auto parsed = parse_expression(ast, argument.begin + 1, argument.end - 1);
                    if (first_opaque_node(ast, parsed) == kNoNode) args[k] = parsed;
                }
                ast.set_list(id, args);
            }
        }
        const Node updated = ast.at(id);
        for (auto child : {updated.a, updated.b, updated.c}) pending.push_back(child);
        for (std::uint32_t k = 0; k < updated.count; ++k) pending.push_back(ast.list(updated)[k]);
    }
}

NodeId make_eval_program(Ast& ast, NodeId expression) {
    const auto begin = ast.at(expression).begin, end = ast.at(expression).end;
    const auto kind = ast.at(expression).kind;
    const bool assignment = kind == NodeKind::Declare;
    const auto value = assignment ? ast.at(expression).a : expression;
    const NodeId returned = ast.add(NodeKind::Return, begin, end);
    ast.at(returned).a = value;
    const NodeId root = ast.add(NodeKind::Program, begin, end);
    ast.set_list(root, assignment ? std::vector<NodeId>{expression, returned}
                                  : std::vector<NodeId>{returned});
    return root;
}
CompileResult compile_expression_unit(Ast& ast, NodeId expression) {
    // No source prefix, evaluation replay or second expression evaluator.
    return compile_eval_program(ast, make_eval_program(ast, expression));
}
} // namespace nift::rt
