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

const Node* UnitCompiler::indexed_identity_argument(const Node& call, std::uint16_t& function) const {
    if (session_ || plan_.coroutines || (module_names_ && !module_names_->empty()) ||
        call.kind != NodeKind::Call || call.count != 1) return nullptr;
    const Node& head = ast_.at(call.a);
    if (head.kind != NodeKind::Ident || kinds_.bindings.count(call.a)) return nullptr;
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
    const Node& path = ast_.at(ast_.list(call)[0]);
    if (path.kind != NodeKind::Index) return nullptr;
    const auto key = ast_.at(path.b).kind;
    if ((key != NodeKind::Ident && key != NodeKind::Number) || ast_.at(path.a).kind != NodeKind::Ident ||
        kinds(path.a) != kKindArray || kinds(path.b) == 0 || (kinds(path.b) & ~kKindNumber) ||
        kinds(node_id(path)) != kKindArray) return nullptr;
    function = found->second;
    return &path;
}

bool UnitCompiler::stable_indexed_root(NodeId binding) const {
    // The retained binding can change, but every possible element remains an
    // array. No other call may borrow the root or replace one of its slots.
    const auto array_of_arrays = [&](NodeId id) {
        const Node& literal = ast_.at(id);
        if (literal.kind != NodeKind::Array) return false;
        const NodeId* items = ast_.list(literal);
        for (std::uint32_t k = 0; k < literal.count; ++k)
            if (ast_.at(items[k]).kind != NodeKind::Array) return false;
        return true;
    };
    const auto is_identity_path = [&](const Node* outer, const Node& path) {
        std::uint16_t function = 0;
        return outer && indexed_identity_argument(*outer, function) == &path;
    };
    struct Visit { NodeId id, parent, outer; };
    std::vector<Visit> pending{{root_, kNoNode, kNoNode}};
    bool declared = false;
    while (!pending.empty()) {
        const auto item = pending.back();
        pending.pop_back();
        if (item.id == kNoNode) continue;
        const Node& n = ast_.at(item.id);
        if (n.kind == NodeKind::Command || n.kind == NodeKind::Opaque || n.kind == NodeKind::TemplateStmt)
            return false;
        if (n.kind == NodeKind::Declare && n.a == binding) {
            if (!array_of_arrays(n.b)) return false;
            declared = true;
        }
        const auto resolved = kinds_.bindings.find(item.id);
        if (item.id != binding && n.kind == NodeKind::Ident &&
            resolved != kinds_.bindings.end() && resolved->second == binding) {
            if (item.parent == kNoNode) return false;
            const Node& parent = ast_.at(item.parent);
            const Node* outer = item.outer == kNoNode ? nullptr : &ast_.at(item.outer);
            if (parent.kind == NodeKind::Assign && parent.a == item.id && array_of_arrays(parent.b)) {
                // Rebinding the root preserves the element kind.
            } else if (parent.kind == NodeKind::Index && parent.a == item.id &&
                       (is_identity_path(outer, parent) ||
                        (outer && outer->kind == NodeKind::Assign && outer->a == item.parent &&
                         ast_.at(outer->b).kind == NodeKind::Array))) {
                // An admitted location or a checked replacement of a slot.
            } else if (parent.kind == NodeKind::Declare && parent.b == item.id &&
                       observed_array_alias(parent.a, false)) {
                // The saved array may be displayed after the root is rebound.
            } else if (parent.kind == NodeKind::Member && parent.a == item.id && outer &&
                       outer->kind == NodeKind::Call && outer->a == item.parent) {
                const auto method = text(ast_.at(parent.b));
                if (method != "clear" && method != "remove" && method != "stringify" &&
                    !(method == "push" && outer->count == 1 &&
                      ast_.at(ast_.list(*outer)[0]).kind == NodeKind::Array)) return false;
            } else {
                return false;
            }
        }
        for (NodeId child : {n.a, n.b, n.c}) pending.push_back({child, item.id, item.parent});
        for (std::uint32_t k = 0; k < n.count; ++k)
            pending.push_back({ast_.list(n)[k], item.id, item.parent});
    }
    return declared;
}

bool UnitCompiler::observed_array_alias(NodeId binding, bool rebind) const {
    for (NodeId id = 0; id < ast_.size(); ++id) {
        const Node& node = ast_.at(id);
        if (node.kind == NodeKind::Command || node.kind == NodeKind::Opaque ||
            node.kind == NodeKind::TemplateStmt) return false;
    }
    std::unordered_set<NodeId> reads;
    for (NodeId id = 0; id < ast_.size(); ++id) {
        const Node& call = ast_.at(id);
        if (rebind && call.kind == NodeKind::Assign) reads.insert(call.a);
        if (call.kind != NodeKind::Call || call.count != 0) continue;
        const Node& member = ast_.at(call.a);
        if (member.kind == NodeKind::Member && text(ast_.at(member.b)) == "stringify")
            reads.insert(member.a);
    }
    for (const auto& [use, declaration] : kinds_.bindings)
        if (declaration == binding && use != binding && !reads.count(use)) return false;
    return true;
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
           stable_indexed_root(decl.a) ? &ret : nullptr;
}

void UnitCompiler::prepare_indexed_roots() {
    for (NodeId id = 0; id < ast_.size(); ++id) {
        const Node& decl = ast_.at(id);
        const NodeId value = decl.kind == NodeKind::Declare && !decl.flags ? decl.b :
                             decl.kind == NodeKind::Return ? decl.a : kNoNode;
        if (value == kNoNode) continue;
        std::uint16_t function = 0;
        const auto* path = indexed_identity_argument(ast_.at(value), function);
        if (!path) continue;
        const auto root = kinds_.bindings.find(path->a);
        if (root != kinds_.bindings.end() && stable_indexed_root(root->second))
            selector_roots_.insert(root->second);
    }
}

bool UnitCompiler::emit_indexed_identity(const Node& call_node, const Node& path,
                                         std::uint16_t function, std::uint16_t result) {
    const Binding* root = find_binding(text(ast_.at(path.a)));
    if (!root || !root->location_source || root->global ||
        find_binding(text(ast_.at(call_node.a))) || !bare_argument(call_node, 0))
        return fail(path, "path-in-call-argument");
    if (!plain_postfix(call_node) || !no_trailing_comma(call_node) || !operand_window(result, 1)) return false;
    std::uint16_t owner = 0, key = 0;
    if (!binding_cell(*root, owner) || !temp(key) || !expr_to(path.b, key)) return false;
    fb_->set_offset(site(path));
    fb_->note(fb_->emit(OpCode::NewIndexedCell, result, owner, key), std::string(text(path)));
    fb_->set_offset(site(call_node));
    fb_->emit(OpCode::Call, result, function, 1);
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
    const auto* path = indexed_identity_argument(call_node, function);
    if (path) {
        const Binding* root = find_binding(text(ast_.at(path->a)));
        if (!root || !root->location_source || root->global ||
            !stable_indexed_root(root->decl_node)) return 0;
    } else {
        if (call_node.kind != NodeKind::Call || call_node.count ||
            ast_.at(call_node.a).kind != NodeKind::Ident) return 0;
        const auto callee = text(ast_.at(call_node.a));
        const auto found = plan_.index.find(callee);
        if (find_binding(callee) || found == plan_.index.end() || !indexed_return(found->second)) return 0;
        function = found->second;
    }
    if (!observed_array_alias(node.a, true)) {
        fail(ast_.at(node.b), "alias-argument");
        return -1;
    }
    if (!plain_postfix(call_node) || !no_trailing_comma(call_node)) return -1;
    const Node& target = ast_.at(node.a);
    const auto name = text(target);
    Binding binding;
    std::uint16_t result = 0;
    if (!new_binding(name, target, result, binding)) return -1;
    if (path) {
        if (!emit_indexed_identity(call_node, *path, function, result)) return -1;
    } else {
        fb_->set_offset(site(call_node));
        fb_->emit(OpCode::Call, result, function, 0);
    }

    std::uint16_t selected = 0, untyped = 0;
    if (!temp(selected) || !fb_->constant(Value::integer(-1), untyped)) return -1;
    fb_->emit(OpCode::CellLoad, selected, result);
    fb_->emit(OpCode::BindType, binding.type_reg, selected, untyped);
    fb_->release_to(static_cast<std::uint16_t>(result + 1));
    fb_->emit(OpCode::NewCell, result, result, binding.type_reg);
    binding.cell = binding.indexed_alias = true;
    binding.decl_kinds = kKindArray;
    if (!scopes_.declare(name, binding)) {
        fail(node, "static-error:redeclared");
        return -1;
    }
    return builder_ok() ? 1 : -1;
}

} // namespace nift::rt::detail
