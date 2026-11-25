#include "rt/compiler_cells.h"

// rt-lint: cold-file binding-slot and selector access emission, once per unit

namespace nift::rt::detail {

bool UnitCompiler::binding_cell(const Binding& b, std::uint16_t& reg) {
    if (b.upvalue) {
        if (!temp(reg)) return false;
        fb_->emit(OpCode::GetUpvalueCell, reg, b.upvalue_index);
    } else if (b.global) {
        if (!(session_ ? global_read_value(b.reg, reg, b.session_name) : global_read(b.reg, reg))) return false;
    } else reg = b.reg;
    return true;
}

bool UnitCompiler::indexed_binding(const Binding& b, std::uint16_t& reg) {
    if (!(b.cell && b.loop_binding)) return read_binding(b, reg);
    if (!temp(reg)) return false;
    fb_->emit(OpCode::CellLoadCached, reg, b.reg);
    return true;
}

std::size_t UnitCompiler::emit_binding_read(OpCode op, std::uint16_t dst, std::uint16_t source) {
    const std::uint32_t saved_offset = fb_->offset();
    if (return_read_offset_ != kNoNode) fb_->set_offset(return_read_offset_);
    const std::size_t at = fb_->emit(op, dst, source);
    fb_->set_offset(saved_offset);
    if (!global_note_.empty()) fb_->note(at, std::string(global_note_));
    return at;
}

bool UnitCompiler::read_binding(const Binding& b, std::uint16_t& reg) {
    if (b.upvalue) {
        if (!temp(reg)) return false;
        emit_binding_read(OpCode::GetUpvalue, reg, b.upvalue_index);
    } else if (b.cell) {
        std::uint16_t cell = 0;
        if (!binding_cell(b, cell) || !temp(reg)) return false;
        emit_binding_read(OpCode::CellLoad, reg, cell);
    } else if (b.global) return global_read(b.reg, reg);
    else reg = b.reg;
    return true;
}

bool UnitCompiler::store_indexed_binding(const Binding& b, std::uint16_t value) {
    std::uint16_t untyped = 0;
    if (!fb_->constant(Value::integer(-1), untyped)) return false;
    if (b.cell && (b.loop_binding || b.location_source)) fb_->emit(OpCode::CellStoreCached, b.reg, value);
    else return store_binding(b, value, untyped);
    return true;
}

bool UnitCompiler::store_binding(const Binding& b, std::uint16_t value, std::uint16_t text_type) {
    if (b.upvalue) fb_->emit(OpCode::SetUpvalue, b.upvalue_index, value, text_type);
    else if (b.cell) {
        std::uint16_t cell = 0;
        if (!binding_cell(b, cell)) return false;
        fb_->emit(OpCode::CellStore, cell, value, text_type);
    } else if (b.global) global_put(b, value);
    else if (b.reg != value) fb_->emit(OpCode::Move, b.reg, value);
    return true;
}

const Node* UnitCompiler::indexed_identity_argument(const Node& call, std::uint16_t& function, bool forward) const {
    if (session_ || plan_.coroutines || (module_names_ && !module_names_->empty()) ||
        call.kind != NodeKind::Call || call.count > 1) return nullptr;
    const Node& head = ast_.at(call.a);
    if (head.kind != NodeKind::Ident) return nullptr;
    const auto local = kinds_.bindings.find(call.a);
    NodeId path_id = kNoNode;
    if (local != kinds_.bindings.end()) {
        if (forward && call.count != 1) return nullptr;
        const auto declaration = direct_indexed_lambdas_.find(local->second);
        if (declaration == direct_indexed_lambdas_.end()) return nullptr;
        const Node& lambda = ast_.at(declaration->second);
        if (lambda.kind != NodeKind::Lambda || lambda.flags & (kFlagBlockBody | kFlagAsync) ||
            lambda.count != call.count || lambda.b == kNoNode) return nullptr;
        if (call.count == 1) {
            const NodeId parameter = ast_.list(lambda)[0];
            const auto returned = kinds_.bindings.find(lambda.b);
            if (ast_.at(parameter).flags & kFlagVariadic || returned == kinds_.bindings.end() ||
                returned->second != parameter) return nullptr;
            path_id = ast_.list(call)[0];
        } else {
            const Node& body = ast_.at(lambda.b);
            if (body.kind != NodeKind::Index || ast_.at(body.b).kind != NodeKind::Number) return nullptr;
            path_id = lambda.b;
        }
        function = 0;
    } else {
        if (call.count != 1) return nullptr;
        const auto found = plan_.index.find(text(head));
        if (found == plan_.index.end()) return nullptr;
        const Node& fn = ast_.at(plan_.functions[found->second - 1]);
        if (fn.count != 1 || fn.flags & (kFlagAsync | kFlagMethod | kFlagDeferredBody) ||
            observable_[found->second - 1][0]) return nullptr;
        const Node& param = ast_.at(ast_.list(fn)[0]);
        const Node& body = ast_.at(fn.b);
        if ((param.flags & kFlagVariadic) || body.count != 1) return nullptr;
        const Node& ret = ast_.at(ast_.list(body)[0]);
        if (ret.kind != NodeKind::Return || ret.a == kNoNode ||
            ast_.at(ret.a).kind != NodeKind::Ident || text(ast_.at(ret.a)) != text(param)) return nullptr;
        path_id = ast_.list(call)[0];
        function = found->second;
    }
    const Node& path = ast_.at(path_id);
    const bool eligible = forward ? path.kind == NodeKind::Ident && bare_argument(call, 0)
                                  : indexed_aggregate_path(path);
    return eligible ? &path : nullptr;
}

bool UnitCompiler::indexed_aggregate_path(const Node& path) const {
    if (session_ || plan_.coroutines || (module_names_ && !module_names_->empty()) ||
        path.kind != NodeKind::Index) return false;
    const Node& key = ast_.at(path.b);
    const auto element = kinds(node_id(path));
    if (ast_.at(path.a).kind != NodeKind::Ident ||
        (element != kKindArray && element != kKindObject)) return false;
    if (kinds(path.a) == kKindObject)
        return kinds(path.b) == kKindString &&
               (key.kind != NodeKind::String || (key.count == 0 && key.a == kNoNode));
    return kinds(path.a) == kKindArray &&
           (key.kind == NodeKind::Ident || key.kind == NodeKind::Number) &&
           kinds(path.b) != 0 && !(kinds(path.b) & ~kKindNumber);
}

const Node* UnitCompiler::indexed_return(std::uint16_t function) const {
    const Node& fn = ast_.at(plan_.functions[function - 1]);
    if (fn.count || fn.flags & (kFlagAsync | kFlagMethod | kFlagDeferredBody)) return nullptr;
    const Node& body = ast_.at(fn.b);
    if (body.count != 2) return nullptr;
    const Node& decl = ast_.at(ast_.list(body)[0]);
    const Node& ret = ast_.at(ast_.list(body)[1]);
    if (decl.kind != NodeKind::Declare || decl.flags || ret.kind != NodeKind::Return ||
        ret.a == kNoNode) return nullptr;
    std::uint16_t identity = 0;
    const Node* path = indexed_identity_argument(ast_.at(ret.a), identity);
    if (!path) return nullptr;
    const auto root = kinds_.bindings.find(path->a);
    return root != kinds_.bindings.end() && root->second == decl.a &&
           stable_indexed_root(decl.a, *path) ? &ret : nullptr;
}

void UnitCompiler::prepare_indexed_roots() {
    if (session_ || plan_.coroutines || (module_names_ && !module_names_->empty())) return;
    // A local lambda value is known if its binding is only used as a direct
    // callee. Scan once so each location call can use its lexical identity.
    std::unordered_set<NodeId> callees;
    for (NodeId id = 0; id < ast_.size(); ++id) {
        const Node& node = ast_.at(id);
        if (node.kind == NodeKind::Call) callees.insert(node.a);
        if (node.kind == NodeKind::Declare && node.b != kNoNode && ast_.at(node.b).kind == NodeKind::Lambda)
            direct_indexed_lambdas_.emplace(node.a, node.b);
    }
    for (const auto& [use, binding] : kinds_.bindings)
        if (use != binding && !callees.count(use)) direct_indexed_lambdas_.erase(binding);
    for (NodeId id = 0; id < ast_.size(); ++id) {
        const Node& decl = ast_.at(id);
        const NodeId value = decl.kind == NodeKind::Declare && !decl.flags ? decl.b :
                             decl.kind == NodeKind::Return ? decl.a : kNoNode;
        if (value == kNoNode) continue;
        std::uint16_t function = 0;
        const Node& init = ast_.at(value);
        const bool direct = decl.kind == NodeKind::Declare && indexed_aggregate_path(init) &&
                            observed_array_alias(decl.a, true, true);
        const auto* path = direct ? &init : indexed_identity_argument(init, function);
        if (!path) continue;
        const auto root = kinds_.bindings.find(path->a);
        if (root != kinds_.bindings.end() && stable_indexed_root(root->second, *path, false, direct))
            selector_roots_.insert(root->second);
    }
}

bool UnitCompiler::emit_indexed_location(const Node& path, const Binding& root, std::uint16_t result,
                                         std::uint32_t offset) {
    std::uint16_t owner = 0, key = 0;
    if (!binding_cell(root, owner) || !temp(key) || !expr_to(path.b, key)) return false;
    fb_->set_offset(offset);
    fb_->note(fb_->emit(OpCode::NewIndexedCell, result, owner, key), std::string(text(path)));
    return builder_ok();
}

bool UnitCompiler::emit_indexed_identity(const Node& call_node, const Node& path,
                                         std::uint16_t function, std::uint16_t result) {
    const Binding* root = find_binding(text(ast_.at(path.a)));
    const auto source = kinds_.bindings.find(path.a);
    const Binding* callee = find_binding(text(ast_.at(call_node.a)));
    const auto callee_source = kinds_.bindings.find(call_node.a);
    if (!root || !root->location_source || root->global ||
        source == kinds_.bindings.end() || source->second != root->decl_node ||
        (function ? callee != nullptr :
         !callee || callee_source == kinds_.bindings.end() || callee_source->second != callee->decl_node) ||
        (call_node.count && !bare_argument(call_node, 0))) return fail(path, "path-in-call-argument");
    if (!plain_postfix(call_node) || !no_trailing_comma(call_node) || !operand_window(result, 1)) return false;
    std::uint16_t callable = 0;
    if (!function && !read_binding(*callee, callable)) return false;
    if (!call_node.count) {
        std::uint16_t scratch = 0;
        if (!temp(scratch)) return false;
        fb_->set_offset(call_node.begin);
        const auto at = fb_->emit(OpCode::CallValue, scratch, callable, 0);
        fb_->note(at, std::string(text(ast_.at(call_node.a))));
        fb_->mark_indexed_capture_call(fb_->note_index(at));
        fb_->release_to(scratch);
    }
    auto offset = site(path);
    if (function && kinds(path.a) == kKindObject) {
        // Legacy locates named-call arguments from the untrimmed initializer
        // start but measures them in the trimmed initializer.
        const auto source_text = ast_.source();
        auto start = site(call_node);
        while (start && (source_text[start - 1] == ' ' || source_text[start - 1] == '\t')) --start;
        const bool initializer = start >= 2 && source_text.substr(start - 2, 2) == ":=";
        offset -= initializer ? site(call_node) - start : 1;
    }
    if (!emit_indexed_location(path, *root, result, offset)) return false;
    fb_->set_offset(site(call_node));
    if (function) {
        fb_->emit(OpCode::Call, result, function, 1);
    } else if (call_node.count) {
        // Preserve the normal lambda call and pass the resolved value. The
        // proven identity body returns that value without changing the root.
        std::uint16_t argument = 0;
        if (!temp(argument)) return false;
        fb_->emit(OpCode::CellLoad, argument, result);
        fb_->emit(OpCode::CallValue, argument, callable, 1);
    }
    fb_->release_to(static_cast<std::uint16_t>(result + 1));
    return builder_ok();
}

bool UnitCompiler::indexed_call_value(const Node& node, std::uint16_t function, std::uint16_t dst) {
    if (node.count) return fail(node, "static-error:arity");
    std::uint16_t location = 0;
    if (!temp(location)) return false;
    fb_->set_offset(site(node));
    fb_->emit(OpCode::Call, location, function, 0);
    fb_->emit(OpCode::CellLoad, dst, location);
    fb_->release_to(location);
    return builder_ok();
}

int UnitCompiler::declare_indexed_alias(const Node& node) {
    if (in_function_ || in_lambda_ || loop_reachable() || captured_binding(node.a)) return 0;
    std::uint16_t function = 0;
    const Node& call_node = ast_.at(node.b);
    const bool direct = indexed_aggregate_path(call_node);
    std::uint16_t forward_function = 0;
    const Node* copy = call_node.kind == NodeKind::Ident ? &call_node
                                                        : indexed_identity_argument(call_node, forward_function, true);
    const Binding* previous = copy ? find_binding(text(*copy)) : nullptr;
    const bool forward = previous && previous->forward_indexed_alias;
    const bool forward_call = forward && copy != &call_node;
    if (direct && !observed_array_alias(node.a, true, true)) return 0;
    const auto* path = direct ? &call_node : indexed_identity_argument(call_node, function);
    const Binding* root = nullptr;
    bool stable_members = false;
    if (path) {
        root = find_binding(text(ast_.at(path->a)));
        const auto source = kinds_.bindings.find(path->a);
        if (!root || !root->location_source || root->global || source == kinds_.bindings.end() ||
            source->second != root->decl_node) return 0;
        const KindSet element = kinds(node_id(*path));
        stable_members = element == kKindObject && !direct &&
                         !observed_array_alias(node.a, true) &&
                         stable_indexed_root(root->decl_node, *path, true);
        // B5 Q3 direct extraction retains the value when the source binding
        // is reassigned. The location-return proof has a different contract.
        if (!stable_members && !stable_indexed_root(root->decl_node, *path, false, direct)) return 0;
    } else if (!forward) {
        if (call_node.kind != NodeKind::Call || call_node.count ||
            ast_.at(call_node.a).kind != NodeKind::Ident) return 0;
        const auto callee = text(ast_.at(call_node.a));
        const auto found = plan_.index.find(callee);
        if (find_binding(callee) || found == plan_.index.end() || !indexed_return(found->second)) return 0;
        function = found->second;
    }
    if (!observed_array_alias(node.a, true, direct || forward, stable_members)) {
        fail(ast_.at(node.b), "alias-argument");
        return -1;
    }
    if ((!forward || forward_call) && (!plain_postfix(call_node) || !no_trailing_comma(call_node))) return -1;
    const Node& target = ast_.at(node.a);
    const auto name = text(target);
    Binding binding;
    std::uint16_t result = 0;
    if (!new_binding(name, target, result, binding)) return -1;
    if (forward) {
        const auto resolved = kinds_.bindings.find(node_id(*copy));
        const Binding* callee = forward_call ? find_binding(text(ast_.at(call_node.a))) : nullptr;
        const auto callee_source = kinds_.bindings.find(call_node.a);
        if (resolved == kinds_.bindings.end() || resolved->second != previous->decl_node ||
            (forward_call && (forward_function ? callee != nullptr :
             !callee || callee_source == kinds_.bindings.end() || callee_source->second != callee->decl_node))) {
            fail(*copy, "alias-argument");
            return -1;
        }
        std::uint16_t source = 0, callable = 0, payload = result;
        if (callee && (!read_binding(*callee, callable) || !temp(payload))) return -1;
        if (!binding_cell(*previous, source)) return -1;
        fb_->set_offset(copy->begin);
        fb_->note(fb_->emit(OpCode::CellLoadAlias, payload, source), std::string(text(*copy)));
        if (forward_call) {
            if (!operand_window(payload, 1)) return -1;
            fb_->set_offset(site(call_node));
            fb_->emit(forward_function ? OpCode::Call : OpCode::CallValue, payload,
                      forward_function ? forward_function : callable, 1);
        }
        if (payload != result) fb_->emit(OpCode::Move, result, payload);
        fb_->emit(OpCode::Move, binding.type_reg, previous->type_reg);
        fb_->release_to(static_cast<std::uint16_t>(result + 1));
    } else if (direct) {
        if (!emit_indexed_location(*path, *root, result, site(*path))) return -1;
        fb_->release_to(static_cast<std::uint16_t>(result + 1));
    } else if (path) {
        if (!emit_indexed_identity(call_node, *path, function, result)) return -1;
    } else {
        fb_->set_offset(site(call_node));
        fb_->emit(OpCode::Call, result, function, 0);
    }

    if (!forward) {
        std::uint16_t selected = 0, untyped = 0;
        if (!temp(selected) || !fb_->constant(Value::integer(-1), untyped)) return -1;
        fb_->emit(OpCode::CellLoad, selected, result);
        fb_->emit(OpCode::BindType, binding.type_reg, selected, untyped);
        fb_->release_to(static_cast<std::uint16_t>(result + 1));
    }
    fb_->emit(OpCode::NewCell, result, result, binding.type_reg);
    binding.cell = binding.indexed_alias = true;
    binding.forward_indexed_alias = direct || forward;
    binding.decl_kinds = path ? kinds(node_id(*path)) :
                         forward ? previous->decl_kinds :
                         kinds(node.b) == kKindObject ? kKindObject : kKindArray;
    if (!scopes_.declare(name, binding)) {
        fail(node, "static-error:redeclared");
        return -1;
    }
    return builder_ok() ? 1 : -1;
}

} // namespace nift::rt::detail
