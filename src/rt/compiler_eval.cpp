#include "rt/compiler_eval.h"
#include "rt/collection.h"
#include "rt/parser.h"
#include "rt/unit_check.h"
#include "rt/infer.h"
#include "rt/globals.h"

// rt-lint: cold-file expression-unit structural adaptation
namespace nift::rt {
void normalize_eval_callbacks(Ast& ast, NodeId root, const UnitKinds* kinds) {
    std::vector<NodeId> pending{root};
    while (!pending.empty()) {
        const auto id = pending.back(); pending.pop_back();
        if (id == kNoNode) continue;
        const Node node = ast.at(id);
        if (node.kind == NodeKind::Call && ast.at(node.a).kind == NodeKind::Member) {
            const Node callee = ast.at(node.a);
            const int method = collection_method(ast.text(ast.at(callee.b)));
            if ((ast.at(callee.a).kind == NodeKind::Array || (kinds && kinds->of(callee.a) == kKindArray)) &&
                method >= static_cast<int>(CollectionMethod::Map)) {
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

void normalize_session_callbacks(Ast& ast, NodeId root, const SessionCompileContext& context) {
    bool quoted = false;
    std::vector<NodeId> pending{root};
    while (!pending.empty() && !quoted) {
        const auto id = pending.back(); pending.pop_back();
        if (id == kNoNode) continue;
        const auto& node = ast.at(id);
        if (node.kind == NodeKind::Call && ast.at(node.a).kind == NodeKind::Member && node.count) {
            const auto& member = ast.at(node.a);
            if (collection_method(ast.text(ast.at(member.b))) >= static_cast<int>(CollectionMethod::Map))
                for (std::uint32_t k = 0; k < node.count; ++k)
                    quoted = quoted || ast.at(ast.list(node)[k]).kind == NodeKind::String;
        }
        for (auto child : {node.a, node.b, node.c}) pending.push_back(child);
        for (std::uint32_t k = 0; k < node.count; ++k) pending.push_back(ast.list(node)[k]);
    }
    if (!quoted) return;  // ordinary commands pay no second inference pass
    const auto plan = plan_unit(ast, root, &context.phased_functions);
    if (!plan.ok) return;
    const auto globals = plan_globals(ast, root, plan);
    const auto kinds = infer_unit_kinds(ast, root, plan, 0, &globals, &context);
    if (kinds.converged) normalize_eval_callbacks(ast, root, &kinds);
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
