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

bool UnitCompiler::guarded_object(NodeId id) const {
    return std::find(guarded_object_reads_.begin(), guarded_object_reads_.end(), id) != guarded_object_reads_.end();
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

bool UnitCompiler::if_chain(const Node& node) {
    std::vector<std::size_t> to_else;
    const int rejected = literal_ordering_error(ast_.at(node.a), node, 0);
    if (rejected == 0 || (rejected < 0 && !false_jump(node.a, to_else))) return false;
    const Node& body = ast_.at(node.b);
    scopes_.push();
    BlockRegs body_regs = block_begin(node.b);
    auto saved_reads = std::move(guarded_object_reads_);
    guarded_object_reads_ = guarded_return_reads(node);
    const bool ok = statements(body);
    guarded_object_reads_ = std::move(saved_reads);
    block_end(body_regs);
    scopes_.pop();
    if (!ok) return false;
    block_clear(body_regs);
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
