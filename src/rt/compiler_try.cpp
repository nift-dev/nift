#include "rt/compiler_expr.h"

#include "rt/resolver.h"

// rt-lint: cold-file compile-time try/catch/throw lowering, once per unit

namespace nift::rt::detail {

bool UnitCompiler::try_statement(const Node& node) {
    if (node.a == kNoNode || node.b == kNoNode || node.c == kNoNode)
        return fail(node, "statement:Try");
    const std::uint16_t mark = fb_->top();
    std::uint16_t error_reg = 0;
    if (!temp(error_reg)) return false;
    const std::uint16_t body_mark = fb_->top();
    const std::size_t at = fb_->emit(OpCode::TryBegin, error_reg, body_mark, 0);
    try_scopes_.push_back(loop_depth_);
    scopes_.push();
    const std::uint16_t protect_mark = fb_->top();
    const bool body_ok = statements(ast_.at(node.a));
    fb_->release_to(protect_mark);
    scopes_.pop();
    try_scopes_.pop_back();
    if (!body_ok) return false;
    fb_->emit(OpCode::TryEnd);
    const std::size_t over = fb_->emit(OpCode::Jmp);
    if (!fb_->patch(at, fb_->here())) return builder_ok();

    scopes_.push();
    const Node& name = ast_.at(node.b);
    Binding binding;
    binding.reg = error_reg;
    binding.decl_node = node_id(name);
    binding.typed = true;
    binding.decl_kinds = kKindError;
    binding.readonly = true;
    if (captured_binding(node_id(name))) {
        std::uint16_t type_reg = 0, cell_reg = 0;
        if (!fb_->alloc(type_reg) || !fb_->alloc(cell_reg)) return builder_ok();
        fb_->emit(OpCode::LoadI, type_reg, 9);  // legacy binding tag for error
        fb_->emit(OpCode::NewCell, cell_reg, error_reg, type_reg);
        binding.cell = true;
        binding.cell_reg = cell_reg;
        binding.reg = cell_reg;
        binding.type_reg = type_reg;
    }
    if (!scopes_.declare(text(name), binding)) return fail(name, "static-error:redeclared");
    const std::uint16_t catch_mark = fb_->top();
    const bool catch_ok = statements(ast_.at(node.c));
    fb_->release_to(catch_mark);
    scopes_.pop();
    if (!catch_ok) return false;
#if defined(NIFT_EXC_MUTANT) && NIFT_EXC_MUTANT == 3
    if (scopes_.find(text(name)) == nullptr && !scopes_.declare(text(name), binding))
        return fail(name, "static-error:redeclared");
#endif
    if (!fb_->patch(over, fb_->here())) return builder_ok();
    fb_->release_to(mark);
    return builder_ok();
}

bool UnitCompiler::throw_statement(const Node& node) {
    if (node.a == kNoNode) return fail(node, "throw-expression");
    const Node& operand = ast_.at(node.a);
    const bool direct_error = operand.kind == NodeKind::Call && ast_.at(operand.a).kind == NodeKind::Ident &&
                              text(ast_.at(operand.a)) == "error";  // rt-lint: allow-string-compare compile time
    const std::uint32_t saved = error_call_offset_;
    if (direct_error) error_call_offset_ = node.begin;
    std::uint16_t value = 0;
    const bool ok = temp(value) && expr_to(node.a, value);
    error_call_offset_ = saved;
    if (!ok) return false;
    fb_->set_offset(node.begin);
    fb_->emit(OpCode::Throw, value);
    fb_->release_to(value);
    return builder_ok();
}

} // namespace nift::rt::detail
