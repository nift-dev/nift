#include "rt/compiler_lambda_helpers.h"

namespace nift::rt::detail {

bool UnitCompiler::lambda_references_this(NodeId body) const {
    std::vector<std::pair<NodeId, int>> stack{{body, 0}};
    while (!stack.empty()) {
        const auto [id, depth] = stack.back();
        stack.pop_back();
        if (id == kNoNode) continue;
        const Node& n = ast_.at(id);
        if (n.kind == NodeKind::FnDecl) continue;
        int nested = depth;
        if (n.kind == NodeKind::Lambda) ++nested;
        if (n.kind == NodeKind::Ident && nested > 0 && text(n) == "this")  // rt-lint: allow-string-compare
            return true;
        for (NodeId child : {n.a, n.b, n.c}) stack.push_back({child, nested});
        const NodeId* list = ast_.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back({list[k], nested});
    }
    return false;
}

bool UnitCompiler::lambda_upvalues(const LambdaTask& task) {
    for (std::uint16_t k = 0; k < task.upvalue_names.size(); ++k) {
        Binding binding;
        binding.upvalue = true;
        binding.upvalue_index = k;
        binding.decl_node = task.upvalue_decls[k];
        binding.loop_binding = task.upvalue_loop[k];
        binding.readonly = task.upvalue_readonly[k];
        binding.const_array = task.upvalue_const_array[k];
        binding.typed = true;
        binding.decl_kinds = static_cast<KindSet>(kKindAll | kKindUnknown);
        if (binding.loop_binding) {
            std::uint16_t cell = 0;
            if (!temp(cell)) return false;
            fb_->emit(OpCode::GetUpvalueCell, cell, k);
            fb_->emit(OpCode::CopyCellLocation, cell, cell);
            binding.upvalue = false;
            binding.cell = true;
            binding.reg = cell;
        }
        if (!scopes_.declare(task.upvalue_names[k], binding)) return false;
    }
    return true;
}

bool UnitCompiler::lambda_locals_free(const Node& lambda) {
    // Deeper lambdas have their own check.
    {
        std::vector<NodeId> stack{lambda.b};
        std::vector<std::pair<std::string_view, NodeId>> declared;
        while (!stack.empty()) {
            const NodeId id = stack.back();
            stack.pop_back();
            if (id == kNoNode) continue;
            const Node& n = ast_.at(id);
            if (n.kind == NodeKind::Lambda || n.kind == NodeKind::FnDecl) continue;
            if (n.kind == NodeKind::Declare && n.a != kNoNode && ast_.at(n.a).kind == NodeKind::Ident)
                declared.emplace_back(text(ast_.at(n.a)), n.a);
            if (n.kind == NodeKind::Try && n.b != kNoNode)
                declared.emplace_back(text(ast_.at(n.b)), n.b);
            if (n.kind == NodeKind::For) {
                const Node& h = ast_.at(n.a);
                const NodeId* names = ast_.list(h);
                for (std::uint32_t k = 0; k < h.count; ++k) declared.emplace_back(text(ast_.at(names[k])), names[k]);
            }
            for (NodeId child : {n.a, n.b, n.c}) stack.push_back(child);
            const NodeId* list = ast_.list(n);
            for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back(list[k]);
        }
        const Node* duplicate = lambda_entry_duplicate(lambda);
        for (const auto& d : declared)
            if ((!duplicate || d.second != node_id(*duplicate)) && find_binding(d.first) != nullptr)
                return fail(ast_.at(d.second), "static-error:redeclared");
    }
    return true;
}

} // namespace nift::rt::detail
