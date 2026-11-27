#include "rt/compiler_blocks.h"
#include "rt/module_data.h"
#include "Json.h"

#include <algorithm>

// rt-lint: cold-file compile-time block register ranges, once per unit

#include <vector>

namespace nift::rt::detail {

// CP17 unwind release: block register ranges (compiler_expr.h).
UnitCompiler::BlockRegs UnitCompiler::block_begin(NodeId block) {
    BlockRegs regs;
    regs.mark = fb_->top();
    regs.outer_high = fb_->reset_high();
    regs.clear = block_holds_handles(block);
    return regs;
}

void UnitCompiler::block_end(BlockRegs& regs) {
    regs.high = fb_->high();
    fb_->merge_high(regs.outer_high);
}

void UnitCompiler::block_clear(const BlockRegs& regs) {
    if (!regs.clear) return;
    for (std::uint16_t r = regs.mark; r < regs.high; ++r) fb_->emit(OpCode::LoadNull, r);
}

bool UnitCompiler::block_holds_handles(NodeId block) const {
    if (block == kNoNode) return false;
    std::vector<NodeId> stack{block};
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        if (kinds(id) & kKindHandle) return true;
        const Node& n = ast_.at(id);
        if (n.kind == NodeKind::Lambda || n.kind == NodeKind::FnDecl) continue;  // their own frames
        for (NodeId child : {n.a, n.b, n.c})
            if (child != kNoNode) stack.push_back(child);
        const NodeId* list = ast_.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k)
            if (list[k] != kNoNode) stack.push_back(list[k]);
    }
    return false;
}

bool UnitCompiler::coalesce(const Node& node, std::uint16_t dst) {
    if (!infix_left_ok(node)) return false;
    if (!expr_to(node.a, dst)) return false;
    const std::size_t done = fb_->emit(OpCode::JmpNotNull, dst);
    if (!expr_to(node.b, dst)) return false;
    fb_->patch(done, fb_->here());
    return builder_ok();
}

int UnitCompiler::literal_ordering_error(const Node& expression, const Node& diagnostic, std::uint16_t dst) {
    if (session_ || in_function_ || in_lambda_ || loop_reachable() ||
        expression.kind != NodeKind::Binary) return -1;
    switch (expression.op) {
        case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge: break;
        default: return -1;
    }
    const auto literal = [&](NodeId id) {
        const Node& node = ast_.at(id);
        if (node.kind != NodeKind::Array && node.kind != NodeKind::Object) return false;
        const auto source = text(node);
        json::Document document;
        std::string error;
        return source.find("$[") == std::string_view::npos &&
            nift_json::parse(std::string(source), document, error);
    };
    if (!literal(expression.a) || !literal(expression.b)) return -1;
    const auto mark = fb_->top();
    std::uint16_t left = 0, right = 0;
    if (!expr_any(expression.a, left) || !expr_any(expression.b, right) ||
        (diagnostic.kind == NodeKind::If && !temp(dst))) return 0;
    const bool ok = legacy_fatal(diagnostic, diagnostic.kind == NodeKind::If
        ? "@if ordering comparisons require two numbers or two strings of the same type"
        : "ordering comparisons require two numbers or two strings", dst);
    fb_->release_to(mark);
    return ok ? 1 : 0;
}

KindSet UnitCompiler::guarded_kind(NodeId id) const {
    if (id != kNoNode && id == guarded_conversion_.first) return guarded_conversion_.second;
    if (id != kNoNode && guarded_conversion_.first != kNoNode && ast_.at(id).kind == NodeKind::Ident) {
        const Binding* binding = find_binding(text(ast_.at(id)));
        if (binding && binding->decl_node == guarded_conversion_.first) return guarded_conversion_.second;
    }
    if (kinds_.of(id) == (kKindObject | kKindNull) &&
        std::find(guarded_object_reads_.begin(), guarded_object_reads_.end(), id) != guarded_object_reads_.end())
        return kKindObject;
    return 0;
}

// The first statement records the built-in type of a parameter. With both
// bindings stable, a guarded conversion can use the corresponding native.
std::pair<NodeId, KindSet> UnitCompiler::guarded_conversion(const Node& node) const {
    if (session_ || in_lambda_ || !in_function_ || plan_.coroutines || !imported_module_data(plan_, node))
        return {kNoNode, 0};
    const auto& test = ast_.at(node.a);
    const auto& yes = ast_.at(node.b);
    const auto label = [&](NodeId id, std::string_view spelling) -> NodeId {
        const auto& n = ast_.at(id);
        if (n.kind != NodeKind::Binary || n.op != Op::Eq || ast_.at(n.a).kind != NodeKind::Ident ||
            ast_.at(n.b).kind != NodeKind::String || text(ast_.at(n.b)) != spelling) return kNoNode;
        return n.a;
    };
    NodeId lhs = label(node.a, R"("string")");
    const bool string_test = lhs != kNoNode;
    if (!string_test) {
        if (test.kind != NodeKind::Logical || test.op != Op::Or || yes.count != 1) return {kNoNode, 0};
        lhs = label(test.a, R"("int")");
        const NodeId rhs = label(test.b, R"("float")");
        if (lhs == kNoNode || rhs == kNoNode || text(ast_.at(lhs)) != text(ast_.at(rhs))) return {kNoNode, 0};
    } else if (yes.count != 1 && yes.count != 2) {
        return {kNoNode, 0};
    }
    const auto* alias = find_binding(text(ast_.at(lhs)));
    const auto& body = ast_.at(ast_.at(plan_.functions[function_]).b);
    if (!alias || alias->cell || alias->global || !body.count) return {kNoNode, 0};
    const auto& declaration = ast_.at(ast_.list(body)[0]);
    if (declaration.kind != NodeKind::Declare || declaration.a != alias->decl_node) return {kNoNode, 0};
    const auto& type_call = ast_.at(declaration.b);
    if (type_call.kind != NodeKind::Call || type_call.count != 1) return {kNoNode, 0};
    const auto& type = ast_.at(type_call.a);
    if (type.kind != NodeKind::Ident || text(type) != "type" || find_binding("type") ||
        plan_.index.count("type") || plan_.struct_index.count("type")) return {kNoNode, 0};
    const int owner = current_struct();
    if (owner >= 0 && plan_.structs[static_cast<std::size_t>(owner)].method("type")) return {kNoNode, 0};
    const auto& input = ast_.at(ast_.list(type_call)[0]);
    if (input.kind != NodeKind::Ident) return {kNoNode, 0};
    const auto* parameter = find_binding(text(input));
    if (!parameter || parameter->cell || parameter->global || parameter->decl_node == kNoNode ||
        ast_.at(parameter->decl_node).kind != NodeKind::Param ||
        assigned_.count(text(input)) || assigned_.count(text(ast_.at(lhs)))) return {kNoNode, 0};
    for (NodeId id = 0; id < ast_.size(); ++id) {
        const auto& candidate = ast_.at(id);
        if (candidate.kind == NodeKind::Command && candidate.begin >= body.begin && candidate.end <= body.end)
            return {kNoNode, 0};
    }
    const auto* statements = ast_.list(yes);
    if (yes.count == 2) {
        const auto& early = ast_.at(statements[0]);
        if (early.kind != NodeKind::If || early.c != kNoNode) return {kNoNode, 0};
        const auto& alternative = ast_.at(early.b);
        if (alternative.count != 1) return {kNoNode, 0};
        const auto& exit = ast_.at(ast_.list(alternative)[0]);
        if (exit.kind != NodeKind::Return || exit.a == kNoNode ||
            ast_.at(exit.a).kind != NodeKind::Null) return {kNoNode, 0};
    }
    const auto& ret = ast_.at(statements[yes.count - 1]);
    if (ret.kind != NodeKind::Return || ret.a == kNoNode) return {kNoNode, 0};
    const auto plain_string = [&](NodeId id) {
        const auto& n = ast_.at(id);
        return n.kind == NodeKind::String && n.count == 0;
    };
    NodeId value = ret.a;
    if (string_test) {
        // Allow a conversion surrounded by literal prefixes and suffixes.
        const auto& suffix = ast_.at(value);
        if (suffix.kind != NodeKind::Binary || suffix.op != Op::Add ||
            !plain_string(suffix.b)) return {kNoNode, 0};
        const auto& prefix = ast_.at(suffix.a);
        if (prefix.kind != NodeKind::Binary || prefix.op != Op::Add ||
            !plain_string(prefix.a)) return {kNoNode, 0};
        value = prefix.b;
    }
    const auto& conversion = ast_.at(value);
    if (conversion.kind != NodeKind::Call || conversion.count != (string_test ? 2u : 0u)) return {kNoNode, 0};
    const auto& method = ast_.at(conversion.a);
    if (method.kind != NodeKind::Member ||
        text(ast_.at(method.b)) != (string_test ? "replace" : "to_string")) return {kNoNode, 0};
    const auto& receiver = ast_.at(method.a);
    if (receiver.kind != NodeKind::Ident || text(receiver) != text(input)) return {kNoNode, 0};
    if (string_test && (!plain_string(ast_.list(conversion)[0]) ||
                        !plain_string(ast_.list(conversion)[1]))) return {kNoNode, 0};
    return {method.a, string_test ? kKindString : kKindNumber};
}

// Admit reads only in the immediate return. A call operand may precede a
// member read, so accept only literals, bare reads, and direct member reads.
std::vector<NodeId> UnitCompiler::guarded_return_reads(const Node& node) const {
    if (session_ || in_lambda_ || !in_function_ || plan_.coroutines || !imported_module_data(plan_, node)) return {};
    const auto& test = ast_.at(node.a);
    if (test.kind != NodeKind::Binary || test.op != Op::Ne || ast_.at(test.b).kind != NodeKind::Null) return {};
    const auto& name = ast_.at(test.a);
    if (name.kind != NodeKind::Ident || kinds(test.a) != (kKindObject | kKindNull)) return {};
    const auto* binding = find_binding(text(name));
    if (!binding || binding->global || binding->cell) return {};
    const auto& body = ast_.at(node.b);
    if (body.count != 1) return {};
    const auto& ret = ast_.at(ast_.list(body)[0]);
    if (ret.kind != NodeKind::Return || ret.a == kNoNode) return {};
    const auto& expression = ast_.at(ret.a);
    std::vector<NodeId> reads;
    const auto check = [&](NodeId id) {
        const auto& arg = ast_.at(id);
        if (arg.kind == NodeKind::Member) {
            const auto& receiver = ast_.at(arg.a);
            if (receiver.kind != NodeKind::Ident) return false;
            if (text(receiver) == text(name)) reads.push_back(arg.a);
            return true;
        }
        return arg.kind == NodeKind::Ident || arg.kind == NodeKind::Number || arg.kind == NodeKind::True ||
               arg.kind == NodeKind::False || arg.kind == NodeKind::Null ||
               (arg.kind == NodeKind::String && arg.count == 0);
    };
    if (expression.kind != NodeKind::Call) return check(ret.a) ? reads : std::vector<NodeId>{};
    const auto& callee = ast_.at(expression.a);
    if (callee.kind != NodeKind::Ident &&
        (callee.kind != NodeKind::Member || ast_.at(callee.a).kind != NodeKind::Ident)) return {};
    for (std::uint32_t k = 0; k < expression.count; ++k)
        if (!check(ast_.list(expression)[k])) return {};
    return reads;
}

KindSet type_label_kind(std::string_view spelling);

// `type(x) != "<label>"` whose body exits: after the if, x has that label.
bool UnitCompiler::type_call_guard(const Node& condition, NodeId& source, KindSet& kind) const {
    if (session_ || in_lambda_ || !in_function_ || plan_.coroutines) return false;
    // `type(x) != "<label>" || …` with an exiting body: after the if, that
    // disjunct is false, so x has the label.
    if (condition.kind == NodeKind::Logical && condition.op == Op::Or)
        return type_call_guard(ast_.at(condition.a), source, kind) ||
               type_call_guard(ast_.at(condition.b), source, kind);
    if (condition.kind != NodeKind::Binary || condition.op != Op::Ne) return false;
    const Node& call = ast_.at(condition.a);
    const Node& label = ast_.at(condition.b);
    if (call.kind != NodeKind::Call || call.count != 1 || label.kind != NodeKind::String) return false;
    const Node& callee = ast_.at(call.a);
    if (callee.kind != NodeKind::Ident || text(callee) != "type" || find_binding("type") ||
        plan_.index.count("type") || plan_.struct_index.count("type")) return false;
    const KindSet narrowed = type_label_kind(text(label));
    if (narrowed == 0) return false;
    const Node& input = ast_.at(ast_.list(call)[0]);
    if (input.kind != NodeKind::Ident) return false;
    const Binding* binding = find_binding(text(input));
    if (!binding || binding->cell || binding->global || binding->decl_node == kNoNode ||
        assigned_.count(text(input))) return false;
    source = binding->decl_node;
    kind = narrowed;
    return true;
}

// A conjunction `… && type(x) == "<label>" && …`: the true branch and the
// rest of the conjunction see x as that label.
bool UnitCompiler::type_call_positive_guard(const Node& condition, NodeId& source, KindSet& kind) const {
    if (session_ || in_lambda_ || !in_function_ || plan_.coroutines) return false;
    const Node& n = condition;
    if (n.kind == NodeKind::Logical && n.op == Op::And)
        return type_call_positive_guard(ast_.at(n.a), source, kind) ||
               type_call_positive_guard(ast_.at(n.b), source, kind);
    if (n.kind != NodeKind::Binary || n.op != Op::Eq) return false;
    NodeId call_id = n.a, label_id = n.b;
    if (ast_.at(call_id).kind != NodeKind::Call) std::swap(call_id, label_id);
    const Node& call = ast_.at(call_id);
    const Node& label = ast_.at(label_id);
    if (call.kind != NodeKind::Call || call.count != 1 || label.kind != NodeKind::String) return false;
    const Node& callee = ast_.at(call.a);
    if (callee.kind != NodeKind::Ident || text(callee) != "type" || find_binding("type") ||
        plan_.index.count("type") || plan_.struct_index.count("type")) return false;
    const KindSet narrowed = type_label_kind(text(label));
    if (narrowed == 0) return false;
    const Node& input = ast_.at(ast_.list(call)[0]);
    if (input.kind != NodeKind::Ident) return false;
    const Binding* binding = find_binding(text(input));
    if (!binding || binding->cell || binding->global || binding->decl_node == kNoNode ||
        assigned_.count(text(input))) return false;
    source = binding->decl_node;
    kind = narrowed;
    return true;
}

bool UnitCompiler::block_exits(const Node& body) const {
    if (body.kind == NodeKind::Return || body.kind == NodeKind::Throw ||
        body.kind == NodeKind::Break || body.kind == NodeKind::Continue)
        return true;
    if (body.count == 0) return false;
    const Node& last = ast_.at(ast_.list(body)[body.count - 1]);
    return last.kind == NodeKind::Return || last.kind == NodeKind::Throw ||
           last.kind == NodeKind::Break || last.kind == NodeKind::Continue;
}

// `x != null` / `null != x`: the true branch sees a non-null value. The
// remaining kinds are unknown when the only proved kind was null.
bool UnitCompiler::null_test_guard(const Node& condition, NodeId& source, KindSet& kind) const {
    if (session_ || in_lambda_ || !in_function_ || plan_.coroutines) return false;
    if (condition.kind != NodeKind::Binary || condition.op != Op::Ne) return false;
    NodeId ident = kNoNode;
    if (ast_.at(condition.b).kind == NodeKind::Null && ast_.at(condition.a).kind == NodeKind::Ident)
        ident = condition.a;
    else if (ast_.at(condition.a).kind == NodeKind::Null && ast_.at(condition.b).kind == NodeKind::Ident)
        ident = condition.b;
    if (ident == kNoNode) return false;
    const Binding* binding = find_binding(text(ast_.at(ident)));
    if (!binding || binding->cell || binding->global || binding->decl_node == kNoNode ||
        assigned_.count(text(ast_.at(ident)))) return false;
    const KindSet raw = kinds_.of(ident);
    if (raw & kKindUnknown) return false;  // still unproved: keep the generic dispatch
    const KindSet remaining = static_cast<KindSet>(raw & ~kKindNull);
    if (remaining == 0) return false;  // only null was proved: no useful narrowing
    source = binding->decl_node;
    kind = remaining;
    return true;
}

KindSet type_label_kind(std::string_view spelling) {
    if (spelling == R"("null")") return kKindNull;
    if (spelling == R"("bool")") return kKindBool;
    if (spelling == R"("int")") return kKindInt;
    if (spelling == R"("float")") return kKindFrac;
    if (spelling == R"("number")") return kKindNumber;
    if (spelling == R"("string")") return kKindString;
    if (spelling == R"("array")") return kKindArray;
    if (spelling == R"("object")") return kKindObject;
    if (spelling == R"("bytes")") return kKindBytes;
    return 0;
}

// `t == "bytes"` where `t := type(x)` and both are stable: the true branch
// sees x as that kind. This is legacy's type-dispatch idiom.
bool UnitCompiler::type_test_guard(const Node& condition, NodeId& source, KindSet& kind) const {
    if (session_ || in_lambda_ || !in_function_ || plan_.coroutines) return false;
    if (condition.kind != NodeKind::Binary || condition.op != Op::Eq) return false;
    const Node& label = ast_.at(condition.b);
    const Node& name = ast_.at(condition.a);
    if (label.kind != NodeKind::String || name.kind != NodeKind::Ident) return false;
    const KindSet narrowed = type_label_kind(text(label));
    if (narrowed == 0) return false;
    const Binding* alias = find_binding(text(name));
    if (!alias || alias->cell || alias->global || alias->decl_node == kNoNode ||
        assigned_.count(text(name))) return false;
    const Node* initializer = nullptr;
    for (NodeId id = 0; id < ast_.size(); ++id) {
        const Node& n = ast_.at(id);
        if (n.kind == NodeKind::Declare && n.a == alias->decl_node) { initializer = &ast_.at(n.b); break; }
    }
    if (!initializer || initializer->kind != NodeKind::Call || initializer->count != 1) return false;
    const Node& test = ast_.at(initializer->a);
    if (test.kind != NodeKind::Ident || text(test) != "type" || find_binding("type") ||
        plan_.index.count("type") || plan_.struct_index.count("type")) return false;
    const Node& input = ast_.at(ast_.list(*initializer)[0]);
    if (input.kind != NodeKind::Ident) return false;
    const Binding* target = find_binding(text(input));
    if (!target || target->cell || target->global || target->decl_node == kNoNode ||
        assigned_.count(text(input))) return false;
    source = target->decl_node;
    kind = narrowed;
    return true;
}

bool UnitCompiler::if_chain(const Node& node) {
    std::vector<std::size_t> to_else;
    const int rejected = literal_ordering_error(ast_.at(node.a), node, 0);
    bool jumped = rejected > 0;
    if (rejected < 0) {
        const bool canonical = !loop_reachable();
        const OrderScope order_scope(order_ctx_, {node.a, canonical ? node.begin : 0, canonical ? 1 : 0});
        jumped = false_jump(node.a, to_else);
    }
    if (rejected == 0 || !jumped) return false;
    const Node& body = ast_.at(node.b);
    scopes_.push();
    BlockRegs body_regs = block_begin(node.b);
    auto saved_reads = std::move(guarded_object_reads_);
    guarded_object_reads_ = guarded_return_reads(node);
    const auto saved_conversion = guarded_conversion_;
    NodeId guarded_source = kNoNode;
    KindSet guarded_type = 0;
    std::pair<NodeId, KindSet> branch_guard{kNoNode, 0};
    if (type_test_guard(ast_.at(node.a), guarded_source, guarded_type) ||
        null_test_guard(ast_.at(node.a), guarded_source, guarded_type) ||
        type_call_positive_guard(ast_.at(node.a), guarded_source, guarded_type))
        branch_guard = {guarded_source, guarded_type};
    guarded_conversion_ = branch_guard.first != kNoNode ? branch_guard : guarded_conversion(node);
    const bool ok = statements(body);
    guarded_conversion_ = saved_conversion;
    guarded_object_reads_ = std::move(saved_reads);
    block_end(body_regs);
    scopes_.pop();
    if (!ok) return false;
    block_clear(body_regs);
    // `if (type(x) != "<label>") { return … }`: the statements after the if
    // see x as that label (the true branch left the block).
    if (node.c == kNoNode && block_exits(body)) {
        NodeId guard_source = kNoNode;
        KindSet guard_kind = 0;
        if (type_call_guard(ast_.at(node.a), guard_source, guard_kind))
            guarded_conversion_ = {guard_source, guard_kind};
    }
    if (node.c == kNoNode) {
        for (std::size_t at : to_else) fb_->patch(at, fb_->here());
        return builder_ok();
    }
    const std::size_t to_end = fb_->emit(OpCode::Jmp);
    for (std::size_t at : to_else) fb_->patch(at, fb_->here());
    const Node& other = ast_.at(node.c);
    bool other_ok = false;
    if (other.kind == NodeKind::If) {
        other_ok = if_chain(other);
        if (other_ok && loop_region() && !statement_preparable(other))
            other_ok = fail(other, "prepared-unpreparable");
    } else {
        scopes_.push();
        const std::uint16_t mark = fb_->top();
        BlockRegs else_regs = block_begin(node.c);
        other_ok = statements(other);
        block_end(else_regs);
        if (other_ok) block_clear(else_regs);
        fb_->release_to(mark);
        scopes_.pop();
    }
    fb_->patch(to_end, fb_->here());
    return other_ok && builder_ok();
}

} // namespace nift::rt::detail
