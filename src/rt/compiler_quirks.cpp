#include "rt/compiler_quirks.h"

#include "rt/legacy_quirks.h"

// Legacy behaviours the compiler reproduces pending an owner decision (CP12):
// Q1 textual re-evaluation of prepared statements with large values, Q2
// textual re-resolution of a failing print(path), Q3 statement-error sites.
// Each is switched in rt/legacy_quirks.h; this file is all of the compiler
// side, so changing or removing one touches only its switch, this file and
// its marked VM slow path.

namespace nift::rt::detail {

// LEGACY QUIRK Q1 (which statements get textual re-evaluation code).
// A prepared declaration, assignment, compound assignment or increment whose
// value (or operand) legacy calls large is re-evaluated by legacy on its
// textual path (ParserTemplate.cpp, large_num): such statements are compiled
// twice, the textual code next to the prepared code (CP12; P6).
bool UnitCompiler::reevaluated(const Node& node, NodeId value) const {
    if (!kQuirkLargeReevaluation || !prepared()) return false;
    switch (node.kind) {
        case NodeKind::Declare: case NodeKind::Assign: return (kinds(value) & kKindNumber) != 0;
        case NodeKind::CompoundAssign: case NodeKind::IncDec: return (kinds(node.a) & kKindNumber) != 0;
        default: return false;
    }
}

// LEGACY QUIRK Q1/Q2: the statement's textual code, compiled as a cold block
// after `skip` (the jump over it on the normal path); laid out after the
// function body by FunctionBuilder::layout_cold.
bool UnitCompiler::textual_copy(std::size_t skip, const std::function<bool()>& textual_code) {
    const bool saved = textual_statement_;
    textual_statement_ = true;
    update_flags();
    const bool ok = textual_code();
    textual_statement_ = saved;
    update_flags();
    fb_->patch(skip, fb_->here());
    fb_->cold_block(skip, skip + 1, fb_->here());
    return ok && builder_ok();
}

// LEGACY QUIRK Q2.
bool UnitCompiler::print_statement(const Node& node, std::uint16_t scratch) {
    const Node& expr = ast_.at(node.a);
    // print(path) on the prepared path: when the location does not exist
    // legacy resolves the statement again on its textual path (its message,
    // its evaluation of the keys): that code is compiled next to it and the
    // lookup continues there (kInstrPathText, StopReason::Reeval).
    const Node& callee = ast_.at(expr.a);
    const bool path_print = prepared() && callee.kind == NodeKind::Ident && expr.count == 1 &&
                            (ast_.at(ast_.list(expr)[0]).kind == NodeKind::Index ||
                             ast_.at(ast_.list(expr)[0]).kind == NodeKind::Member);
    if (!path_print || !kQuirkPrintPathReresolve) return expr_to(node.a, scratch);
    const std::size_t first = fb_->here();
    print_statement_ = true;
    const bool ok = expr_to(node.a, scratch);
    print_statement_ = false;
    if (!ok) return false;
    std::size_t lookup = fb_->here();
    for (std::size_t i = first; i < fb_->here(); ++i)
        if (fb_->at(i).flags & kInstrPathText) lookup = i;
    if (lookup == fb_->here()) return expr_to(node.a, scratch);  // not a call of print
    const std::size_t done = fb_->emit(OpCode::Jmp);
    fb_->set_note_alt(fb_->note_index(lookup), fb_->here());
    return textual_copy(done, [&] { return expr_to(node.a, scratch); });
}

// LEGACY QUIRK Q3.
// The offset legacy reports for an error of a statement that has no failing
// expression (a binding type change, a compound operation, an element
// assignment) on the prepared path: the statement of the outermost loop body
// that contains it (legacy's prepared statement span). In a function legacy
// runs prepared, the caller's call (kInstrCallerSite).
std::uint32_t UnitCompiler::statement_site(const Node& node) const {
    if (kQuirkStatementErrorSites && prepared() && !textual_statement_ && variant_ == Variant::Textual &&
        loop_site_ != kNoSite)
        return loop_site_;
    return node.begin;
}

void UnitCompiler::caller_site(std::size_t at) {
    if (kQuirkStatementErrorSites && variant_ == Variant::Prepared && prepared())
        fb_->at(at).flags |= kInstrCallerSite;
}

} // namespace nift::rt::detail
