#include "rt/compiler_closure.h"
#include "rt/session_shape.h"

#include "rt/compiler_capture.h"

#include "rt/native.h"
#include "rt/compiler.h"

#include <algorithm>
#include <unordered_set>

// rt-lint: cold-file compile-time closure lowering, once per unit

namespace nift::rt::detail {

void UnitCompiler::prepare_captures() {
    prepare_capture_bindings(ast_, root_, plan_, kinds_, top_level_decl_ids_, captured_bindings_, selector_roots_);
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
        if (n.kind == NodeKind::Member || n.kind == NodeKind::SafeMember) {
            stack.push_back(n.a);
            continue;
        }
        if (n.kind == NodeKind::Property) {
            stack.push_back(n.b);
            continue;
        }
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
    const bool expression_body = (node.flags & kFlagBlockBody) == 0;
    std::vector<Capture> captures;
    std::vector<std::string_view> upvalue_names;
    std::vector<NodeId> upvalue_decls;
    std::vector<bool> upvalue_loop;
    std::vector<bool> upvalue_readonly;
    std::vector<bool> upvalue_const_array;
    for (std::string_view name : names) {
        const Binding* binding = find_binding(name);
        if (binding == nullptr) continue;  // a global/function or a body-local name
        if (binding->global) continue;
        // CP12b methods use locations, not a first-class struct receiver.
        // A captured struct cell has no proved receiver-lifetime lowering;
        // do not admit it through the new persistent session route.
        if (session_ && binding->struct_id >= 0) return fail(node, "session:captured-struct");
        // The lexical capture pass is scope-aware: an inner declaration does
        // not hide an outer binding used outside its block, and member keys
        // are not variable references.
        if (binding->decl_node == kNoNode || !captured_binding(binding->decl_node)) continue;
        if (!in_function_ && top_level_bindings_.count(name)) {
            const Binding& global = top_level_bindings_.at(name);
            if (binding->reg == global.reg) continue;  // the direct program-level global, not a shadow
        }
        if (binding->cell) captures.push_back({Capture::Source::LocalCell, binding->reg, *binding});
        else if (binding->upvalue)
            captures.push_back({Capture::Source::OuterUpvalue, binding->upvalue_index, *binding});
        else return fail(node, "lambda-capture");
        upvalue_names.push_back(name);
        upvalue_decls.push_back(binding->decl_node);
        upvalue_loop.push_back(binding->loop_binding);
        upvalue_readonly.push_back(binding->readonly);
        upvalue_const_array.push_back(binding->const_array);
    }
    if (session_ && !session_lambda_captures(node_id(node), node, upvalue_names, upvalue_loop,
                                            upvalue_readonly, upvalue_const_array)) return false;
    if (program_ == nullptr || program_->protos.size() >= 0xffff) return fail(node, "limit:lambdas");
    const auto slot = static_cast<std::uint16_t>(program_->protos.size());
    program_->protos.push_back(nullptr);
    lambda_tasks_.push_back(LambdaTask{node_id(node), slot, std::move(upvalue_names), std::move(upvalue_decls),
                                       std::move(upvalue_loop), std::move(upvalue_readonly),
                                       std::move(upvalue_const_array),
                                       std::move(captures)});
    const LambdaTask& task = lambda_tasks_.back();
    const std::uint16_t mark = fb_->top();
    std::uint16_t base = 0;
    if (!temp(base)) return false;
    // Closure reads its captured cells from R[base+1 .. base+c]; reserve those
    // contiguous registers before scratch registers used by live refreshes.
    std::vector<std::uint16_t> capture_regs(task.captures.size());
    for (std::uint16_t k = 0; k < task.captures.size(); ++k)
        if (!temp(capture_regs[k])) return false;
    for (std::uint16_t k = 0; k < task.captures.size(); ++k) {
        const Capture& capture = task.captures[k];
        const std::uint16_t reg = capture_regs[k];
        const Binding& binding = capture.binding;
        if (binding.selector) {
            // A copied legacy descriptor keeps its source location until this
            // particular binding is reassigned. Its cached iteration value is
            // deliberately not the captured location.
#if defined(NIFT_FOR_MUTANT) && NIFT_FOR_MUTANT == 3
            fb_->emit(OpCode::Move, reg, binding.reg);
#else
            const std::size_t assigned = fb_->emit(OpCode::JmpFalse, binding.lazy_active);
            fb_->emit(OpCode::CopyCellLocation, reg, binding.selector_reg);
            const std::size_t done = fb_->emit(OpCode::Jmp);
            fb_->patch(assigned, fb_->here());
            fb_->emit(OpCode::CopyCellLocation, reg, binding.reg);
            fb_->patch(done, fb_->here());
#endif
        } else if (capture.source == Capture::Source::LocalCell) {
            fb_->emit(binding.loop_binding ? OpCode::CopyCellLocation : OpCode::Move, reg, capture.index);
        } else {
            fb_->emit(OpCode::GetUpvalueCell, reg, capture.index);
            if (binding.loop_binding) fb_->emit(OpCode::CopyCellLocation, reg, reg);
        }
    }
    const std::uint8_t saved_flags = fb_->flags();
    if (expression_body) {
        std::uint8_t flags = static_cast<std::uint8_t>(saved_flags | kInstrLambdaExprBody);
        // Legacy's textual return constructs this literal from an unmapped
        // string. A nested lambda in an expression body retains its source view.
        if (saved_flags & kInstrReturnCall) flags |= kInstrLambdaReturnOrigin;
        fb_->set_flags(flags);
    }
    fb_->emit(OpCode::Closure, base, slot, static_cast<std::uint16_t>(task.captures.size()));
    fb_->set_flags(saved_flags);
    if (dst != base) fb_->emit(OpCode::Move, dst, base);
    fb_->release_to(mark);
    return builder_ok();
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

bool UnitCompiler::compile_lambda_task(const LambdaTask& task, std::unique_ptr<Proto>& out, bool collection_view) {
    auto saved_fb = std::move(fb_);
    const Scopes saved_scopes = scopes_;
    const std::vector<Loop> saved_loops = loops_;
    const std::vector<int> saved_try_scopes = try_scopes_;
    const std::uint32_t saved_error_call_offset = error_call_offset_;
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
    if (!(lambda.flags & kFlagBlockBody)) fb_->session_returns |= kinds(lambda.b);
    // Incoming arguments occupy the first registers, before descriptor locals.
    std::vector<std::uint16_t> regs(lambda.count);
    for (std::uint32_t k = 0; k < lambda.count; ++k)
        if (!fb_->alloc(regs[k])) return false;
    fb_->set_params(static_cast<std::uint16_t>(lambda.count));
    if (!lambda_upvalues(task)) return false;
    // Legacy rejects a nested lambda that declares a name already visible
    // in its enclosing scope (the declaration would shadow a captured/outer
    // binding). Only this lambda's own declarations count; a deeper lambda's
    // declaration is checked when that lambda is compiled.
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
        for (const auto& d : declared)
            if (find_binding(d.first) != nullptr) return fail(ast_.at(d.second), "static-error:redeclared");
    }
    scopes_.push();  // parameters and locals
    const NodeId* params = ast_.list(lambda);

    for (std::uint32_t k = 0; k < lambda.count; ++k) {
        const Node& param = ast_.at(params[k]);
        const std::string_view name = text(param);
        Binding binding;
        binding.reg = regs[k];
        binding.decl_node = node_id(param);
        binding.typed = true;
        binding.decl_kinds = kinds(params[k]);
        if (!fb_->alloc(binding.type_reg)) return false;
        std::uint16_t untyped = 0;
        if (!fb_->constant(Value::integer(-1), untyped)) return false;
        fb_->set_offset(param.begin);
        fb_->emit(OpCode::BindType, binding.type_reg, binding.reg, untyped);
        if (captured_binding(node_id(param)) || selector_roots_.count(node_id(param))) {
            binding.selector_root_only = !captured_binding(node_id(param));
            binding.location_source = selector_roots_.count(binding.decl_node);
            if (!fb_->alloc(binding.cell_reg)) return false;
            const auto op = binding.location_source ? OpCode::NewSourceSlot : OpCode::NewCell;
            fb_->emit(op, binding.cell_reg, binding.reg, binding.type_reg);
            if (binding.location_source) fb_->emit(OpCode::LoadNull, binding.reg);
            binding.cell = true;
            binding.reg = binding.cell_reg;
        }
        if (!scopes_.declare(name, binding)) return fail(param, "param-duplicate");
    }
    bool ok = compile_lambda_body(lambda, params, collection_view);
    if (ok) {
        fb_->drop_unread_types();
        ok = finish(out);
        if (ok) {
            out->collection_expression_view = collection_view;
            if (session_ && !session_lambda_definition(task, *out)) return false;
            if (session_) for (std::uint32_t k = 0; k < lambda.count; ++k) {
                out->session_params.push_back(kinds(params[k]));
                const auto shape = kinds_.session_shapes.find(params[k]);
                out->session_parameter_shapes.push_back(shape == kinds_.session_shapes.end() ?
                    infer_detail::kinds_ty(kinds(params[k])) : session_shape(shape->second));
                out->session_param_types.push_back(0xffff);
            }
        }
    }
    if (!ok) {
        // Keep the failure reason/offset from the lambda body; the unit is
        // already being refused, so state restoration is not needed.
        return false;
    }
    fb_ = std::move(saved_fb);
    scopes_ = saved_scopes;
    loops_ = saved_loops;
    try_scopes_ = saved_try_scopes;
    error_call_offset_ = saved_error_call_offset;
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
    if (!operand_window(base, node.count)) return false;
    for (std::uint32_t k = 0; k < node.count; ++k) {
        if (ast_.at(args[k]).kind == NodeKind::Spread) return fail(ast_.at(args[k]), "spread");
        if (!window_value(args[k], static_cast<std::uint16_t>(base + k))) return false;
    }
    if (session_ && !session_call_types(node, callee_id, base)) return false;
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
