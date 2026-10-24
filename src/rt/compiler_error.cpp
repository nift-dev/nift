#include "rt/compiler_error.h"

#include "rt/atom.h"

#include "rt/resolver.h"

// rt-lint: cold-file compile-time Error construction and member lowering, once per unit

namespace nift::rt::detail {

bool UnitCompiler::error_method_call(const Node& node, std::uint16_t dst, std::string_view name, NodeId receiver) {
    const bool stringify = name == "stringify";  // rt-lint: allow-string-compare CP13B method
    const bool prettify = name == "prettify";    // rt-lint: allow-string-compare CP13B method
    const bool highlight = name == "highlight";  // rt-lint: allow-string-compare CP13B method
    if (!((stringify || prettify || highlight) && node.count == 0 && (kinds(receiver) & kKindError) &&
          (kinds(receiver) & ~(kKindError | kKindNull)) == 0))
        return false;
    const std::uint16_t mark = fb_->top();
    std::uint16_t reg = 0;
    if (!reg_any(receiver, reg)) return false;
    fb_->set_offset(site(node));
    const std::size_t at = fb_->emit(OpCode::ErrorJson, dst, reg, prettify ? 1 : 0);
    fb_->note(at, std::string(text(node)));
    fb_->release_to(mark);
    return true;
}

bool UnitCompiler::error_member_access(const Node& node, std::uint16_t dst, NodeId receiver) {
    if (!((kinds(receiver) & kKindError) && (kinds(receiver) & ~(kKindError | kKindNull)) == 0)) return false;
    const std::string_view member = text(ast_.at(node.b));
    const bool message = member == "message";   // rt-lint: allow-string-compare
    const bool code = member == "code";         // rt-lint: allow-string-compare
    const bool category = member == "category"; // rt-lint: allow-string-compare
    const bool source = member == "source";     // rt-lint: allow-string-compare
    const bool line = member == "line";         // rt-lint: allow-string-compare
    const bool column = member == "column";     // rt-lint: allow-string-compare
    const bool cause = member == "cause";       // rt-lint: allow-string-compare
    if (!(message || code || category || source || line || column || cause))
        return fail(node, "method:" + std::string(member));
    const std::uint16_t mark = fb_->top();
    std::uint16_t reg = 0;
    if (!reg_any(receiver, reg)) return false;
    const Atom key = atom(member);
    if (key.id > 0xffff) return fail(node, "limit:atoms");
    fb_->set_offset(site(node));
    fb_->emit(OpCode::GetMember, dst, reg, static_cast<std::uint16_t>(key.id));
    fb_->release_to(mark);
    return true;
}

bool UnitCompiler::error_new(const Node& node, std::uint16_t dst) {
    if (!plain_postfix(node) || !no_trailing_comma(node)) return false;
    const bool in_place = dst + 1u == fb_->top();
    const std::uint16_t base = in_place ? dst : fb_->top();
    const NodeId* args = ast_.list(node);
    for (std::uint32_t k = 0; k < node.count; ++k) {
        if (ast_.at(args[k]).kind == NodeKind::Spread) return fail(ast_.at(args[k]), "spread");
        std::uint16_t reg = base + k;
        if (!(in_place && k == 0) && !temp(reg)) return false;
        if (!expr_to(args[k], reg)) return false;
    }
    std::uint16_t slot = base;
    if (node.count == 0 && !in_place && !temp(slot)) return false;
    fb_->set_offset(error_call_offset_ == kNoNode ? site(node) : error_call_offset_);
    fb_->emit(OpCode::ErrorNew, base, base, static_cast<std::uint16_t>(node.count));
    if (dst != base) fb_->emit(OpCode::Move, dst, base);
    fb_->release_to(in_place ? static_cast<std::uint16_t>(dst + 1) : base);
    return builder_ok();
}

} // namespace nift::rt::detail
