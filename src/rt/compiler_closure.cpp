#include "rt/compiler_closure.h"

#include "rt/compiler_capture.h"

#include "rt/native.h"

#include <algorithm>
#include <unordered_set>

// rt-lint: cold-file compile-time closure lowering, once per unit

namespace nift::rt::detail {

void UnitCompiler::prepare_captures() {
    prepare_capture_bindings(ast_, root_, plan_, top_level_decl_ids_, captured_bindings_);
    declared_names_.clear();
    std::vector<NodeId> stack{root_};
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        if (id == kNoNode) continue;
        const Node& n = ast_.at(id);
        if (n.kind == NodeKind::Declare && n.a != kNoNode) {
            const Node& target = ast_.at(n.a);
            if (target.kind == NodeKind::Ident) declared_names_.insert(text(target));
            for (std::uint32_t k = 0; target.kind == NodeKind::Pattern && k < target.count; ++k)
                declared_names_.insert(text(ast_.at(ast_.list(target)[k])));
        } else if (n.kind == NodeKind::Param || (n.kind == NodeKind::Try && n.b != kNoNode)) {
            declared_names_.insert(text(ast_.at(n.kind == NodeKind::Param ? id : n.b)));
        } else if (n.kind == NodeKind::ForBinding) {
            for (std::uint32_t k = 0; k < n.count; ++k) declared_names_.insert(text(ast_.at(ast_.list(n)[k])));
        }
        for (NodeId child : {n.a, n.b, n.c}) stack.push_back(child);
        const NodeId* list = ast_.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back(list[k]);
    }
}

const Binding* UnitCompiler::find_lambda_binding(std::string_view name) const {
    if (!in_lambda_) return find_binding(name);
    if (const Binding* local = scopes_.find(name)) return local;
    const auto it = top_level_bindings_.find(name);
    return it == top_level_bindings_.end() ? nullptr : &it->second;
}

bool UnitCompiler::read_binding(const Binding& b, std::uint16_t& reg) {
    if (b.upvalue) {
        if (!temp(reg)) return false;
        fb_->emit(OpCode::GetUpvalue, reg, b.upvalue_index);
        return true;
    }
    if (b.cell) {
        if (!temp(reg)) return false;
        fb_->emit(OpCode::CellLoad, reg, b.reg);
        return true;
    }
    if (b.global) return global_read(b.reg, reg);
    reg = b.reg;
    return true;
}

bool UnitCompiler::store_binding(const Binding& b, std::uint16_t value, std::uint16_t text_type) {
    if (b.upvalue) {
        fb_->emit(OpCode::SetUpvalue, b.upvalue_index, value, text_type);
        return true;
    }
    if (b.cell) {
        fb_->emit(OpCode::CellStore, b.reg, value, text_type);
        return true;
    }
    if (b.global) {
        global_put(b, value);
        return true;
    }
    if (b.reg != value) fb_->emit(OpCode::Move, b.reg, value);
    return true;
}

void UnitCompiler::collect_lambda_names(NodeId id, std::vector<std::string_view>& out) const {
    std::vector<NodeId> stack{id};
    while (!stack.empty()) {
        const NodeId current = stack.back();
        stack.pop_back();
        if (current == kNoNode) continue;
        const Node& n = ast_.at(current);
        if (n.kind == NodeKind::Ident) {
            const std::string_view name = text(n);
            if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
        }
        if (n.kind == NodeKind::FnDecl) continue;  // a separate (unsupported here) callable
        for (NodeId child : {n.a, n.b, n.c}) stack.push_back(child);
        const NodeId* list = ast_.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back(list[k]);
    }
}

bool UnitCompiler::lambda_value(const Node& node, std::uint16_t dst) {
    if (node.flags & kFlagAsync) return fail(node, "fn-flags");
    if (node.b == kNoNode) return fail(node, "lambda-shape");
    const NodeId* params = ast_.list(node);
    for (std::uint32_t k = 0; k < node.count; ++k) {
        const Node& param = ast_.at(params[k]);
        if (param.kind != NodeKind::Param) return fail(param, "lambda-param");
        if (param.flags & kFlagVariadic) return fail(param, "lambda-variadic");
        if (reserved_name(text(param))) return fail(param, "param-name-reserved");
    }
    std::vector<std::string_view> names;
    collect_lambda_names(node.b, names);
    // Lambda-local names shadow enclosing bindings; do not over-capture them.
    std::unordered_set<std::string_view> locals;
    for (std::uint32_t k = 0; k < node.count; ++k) locals.insert(text(ast_.at(params[k])));
    std::vector<NodeId> stack{node.b};
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        if (id == kNoNode) continue;
        const Node& n = ast_.at(id);
        if (n.kind == NodeKind::Declare && n.a != kNoNode && ast_.at(n.a).kind == NodeKind::Ident)
            locals.insert(text(ast_.at(n.a)));
        if (n.kind == NodeKind::FnDecl) continue;
        for (NodeId child : {n.a, n.b, n.c}) stack.push_back(child);
        const NodeId* list = ast_.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back(list[k]);
    }
    std::vector<Capture> captures;
    std::vector<std::string_view> upvalue_names;
    for (std::string_view name : names) {
        if (locals.count(name)) continue;
        const Binding* binding = find_binding(name);
        if (binding == nullptr) continue;  // a global/function or a body-local name
        if (binding->global) continue;
        if (!in_function_ && top_level_bindings_.count(name)) {
            const Binding& global = top_level_bindings_.at(name);
            if (binding->reg == global.reg) continue;  // the direct program-level global, not a shadow
        }
        if (binding->cell) {
            captures.push_back({Capture::Source::LocalCell, binding->reg});
        } else if (binding->upvalue) {
            captures.push_back({Capture::Source::OuterUpvalue, binding->upvalue_index});
        } else {
            return fail(node, "lambda-capture");
        }
        upvalue_names.push_back(name);
    }
    if (program_ == nullptr || program_->protos.size() >= 0xffff) return fail(node, "limit:lambdas");
    const auto slot = static_cast<std::uint16_t>(program_->protos.size());
    program_->protos.push_back(nullptr);
    lambda_tasks_.push_back(LambdaTask{node_id(node), slot, std::move(upvalue_names), std::move(captures)});
    const LambdaTask& task = lambda_tasks_.back();
    const std::uint16_t mark = fb_->top();
    std::uint16_t base = 0;
    if (!temp(base)) return false;
    for (std::uint16_t k = 0; k < task.captures.size(); ++k) {
        const Capture& capture = task.captures[k];
        std::uint16_t reg = 0;
        if (!temp(reg)) return false;
        if (capture.source == Capture::Source::LocalCell)
            fb_->emit(OpCode::Move, reg, capture.index);
        else
            fb_->emit(OpCode::GetUpvalueCell, reg, capture.index);
    }
    const bool expression_body = (node.flags & kFlagBlockBody) == 0;
    const std::uint8_t saved_flags = fb_->flags();
    if (expression_body) fb_->set_flags(static_cast<std::uint8_t>(fb_->flags() | kInstrLambdaExprBody));
    fb_->emit(OpCode::Closure, base, slot, static_cast<std::uint16_t>(task.captures.size()));
    fb_->set_flags(saved_flags);
    if (dst != base) fb_->emit(OpCode::Move, dst, base);
    fb_->release_to(mark);
    return builder_ok();
}

bool UnitCompiler::compile_lambda_task(const LambdaTask& task, std::unique_ptr<Proto>& out) {
    auto saved_fb = std::move(fb_);
    const Scopes saved_scopes = scopes_;
    const std::vector<Loop> saved_loops = loops_;
    const std::unordered_set<std::string_view> saved_assigned = assigned_;
    const bool saved_in_function = in_function_;
    const bool saved_in_lambda = in_lambda_;
    const std::size_t saved_function = function_;
    const int saved_loop_depth = loop_depth_;
    const bool saved_textual = textual_statement_;
    const int saved_call_args = call_args_;
    const int saved_index_depth = index_depth_;
    const NodeId saved_path_arg = path_arg_;
    const NodeId saved_path_chain = path_chain_;
    const Node* saved_chain_top = chain_top_;
    const Node* saved_chain_next = chain_next_;
    const Node* saved_print_arg = print_arg_;
    const int saved_depth = depth_;
    const bool saved_probe = probe_;
    const std::string saved_reason = reason_;
    const std::uint32_t saved_offset = offset_;

    const Node& lambda = ast_.at(task.lambda);
    in_function_ = true;
    in_lambda_ = true;
    function_ = plan_.functions.size();
    loop_depth_ = 0;
    textual_statement_ = false;
    call_args_ = 0;
    index_depth_ = 0;
    path_arg_ = path_chain_ = kNoNode;
    chain_top_ = chain_next_ = nullptr;
    print_arg_ = nullptr;
    depth_ = 0;
    probe_ = false;
    reason_.clear();
    offset_ = 0;
    begin("lambda", lambda.b);
    for (std::uint16_t k = 0; k < task.upvalue_names.size(); ++k) {
        Binding binding;
        binding.upvalue = true;
        binding.upvalue_index = k;
        binding.typed = true;
        binding.decl_kinds = static_cast<KindSet>(kKindAll | kKindUnknown);
        if (!scopes_.declare(task.upvalue_names[k], binding)) return false;
    }
    scopes_.push();  // parameters and locals
    const NodeId* params = ast_.list(lambda);
    std::vector<std::uint16_t> regs(lambda.count);
    for (std::uint32_t k = 0; k < lambda.count; ++k)
        if (!fb_->alloc(regs[k])) return false;
    fb_->set_params(static_cast<std::uint16_t>(lambda.count));
    for (std::uint32_t k = 0; k < lambda.count; ++k) {
        const Node& param = ast_.at(params[k]);
        const std::string_view name = text(param);
        Binding binding;
        binding.reg = regs[k];
        binding.typed = true;
        binding.decl_kinds = kinds(params[k]);
        if (!fb_->alloc(binding.type_reg)) return false;
        std::uint16_t untyped = 0;
        if (!fb_->constant(Value::integer(-1), untyped)) return false;
        fb_->set_offset(param.begin);
        fb_->emit(OpCode::BindType, binding.type_reg, binding.reg, untyped);
        if (captured_binding(node_id(param))) {
            if (!fb_->alloc(binding.cell_reg)) return false;
            fb_->emit(OpCode::NewCell, binding.cell_reg, binding.reg, binding.type_reg);
            binding.cell = true;
            binding.reg = binding.cell_reg;
        }
        if (!scopes_.declare(name, binding)) return fail(param, "param-duplicate");
    }
    bool ok;
    if (lambda.flags & kFlagBlockBody) {
        ok = statements(ast_.at(lambda.b));
        if (ok) fb_->emit(OpCode::ReturnNull);
    } else {
        std::uint16_t value = 0;
        ok = temp(value) && expr_to(lambda.b, value);
        if (ok) fb_->emit(OpCode::Return, value);
    }
    if (ok) {
        fb_->drop_unread_types();
        ok = finish(out);
    }
    if (!ok) {
        // Keep the failure reason/offset from the lambda body; the unit is
        // already being refused, so state restoration is not needed.
        return false;
    }
    fb_ = std::move(saved_fb);
    scopes_ = saved_scopes;
    loops_ = saved_loops;
    assigned_ = saved_assigned;
    in_function_ = saved_in_function;
    in_lambda_ = saved_in_lambda;
    function_ = saved_function;
    loop_depth_ = saved_loop_depth;
    textual_statement_ = saved_textual;
    call_args_ = saved_call_args;
    index_depth_ = saved_index_depth;
    path_arg_ = saved_path_arg;
    path_chain_ = saved_path_chain;
    chain_top_ = saved_chain_top;
    chain_next_ = saved_chain_next;
    print_arg_ = saved_print_arg;
    depth_ = saved_depth;
    probe_ = saved_probe;
    reason_ = saved_reason;
    offset_ = saved_offset;
    return true;
}

bool UnitCompiler::call_value(const Node& node, NodeId callee_id, bool member, std::uint16_t dst) {
    const Node& callee = ast_.at(callee_id);
    const NodeId* args = ast_.list(node);
    const std::uint16_t mark = fb_->top();
    std::uint16_t callee_reg = 0;
    if (!temp(callee_reg) || !expr_to(callee_id, callee_reg)) return false;
    const std::uint16_t base = fb_->top();
    for (std::uint32_t k = 0; k < node.count; ++k) {
        if (ast_.at(args[k]).kind == NodeKind::Spread) return fail(ast_.at(args[k]), "spread");
        std::uint16_t reg = 0;
        if (!temp(reg) || !expr_to(args[k], reg)) return false;
    }
    if (node.count == 0) {
        std::uint16_t result = 0;
        if (!temp(result)) return false;
    }
    fb_->set_offset(site(node));
    const std::uint8_t saved = fb_->flags();
    if (member) fb_->set_flags(static_cast<std::uint8_t>(fb_->flags() | kInstrCallMember));
    const std::size_t at = fb_->emit(OpCode::CallValue, base, callee_reg, static_cast<std::uint16_t>(node.count));
    fb_->set_flags(saved);
    fb_->note(at, member ? std::string(text(node)) : std::string(text(callee)));
    if (dst != base) fb_->emit(OpCode::Move, dst, base);
    fb_->release_to(mark);
    return builder_ok();
}

} // namespace nift::rt::detail
