#include "rt/compiler_deferred.h"
#include "rt/legacy_names.h"
#include "rt/name_scan.h"
#include "rt/native_qualifier.h"

// rt-lint: cold-file compile-time dead-code admission (slot 3)

// Slot 3 (owner 2026-11-11 05:41, D-068 #1 and #3; dead-code-admission.md 5.2): a call to a name that is no
// function, binding or built-in raises legacy's `undefined callable: <name>` when it is reached, instead of
// refusing the whole program. `if (false) { nope() }` / `print("success")` prints success (D-067 #11).
namespace nift::rt::detail {
namespace {

// Legacy evaluates no argument before raising `undefined callable` (5.2 order table), so the arguments are not
// compiled. They must therefore hold nothing a compile could refuse statically (an S construct needs a
// statement, so no lambda or block), nothing that suspends, and no spread.
bool inert_arguments(const Ast& ast, const Node& call) {
    std::vector<NodeId> stack(ast.list(call), ast.list(call) + call.count);
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        if (id == kNoNode) continue;
        const Node& n = ast.at(id);
        switch (n.kind) {
        case NodeKind::Number: case NodeKind::String: case NodeKind::StringPart: case NodeKind::True:
        case NodeKind::False: case NodeKind::Null: case NodeKind::Ident: case NodeKind::Unary:
        case NodeKind::Binary: case NodeKind::Logical: case NodeKind::Coalesce: case NodeKind::Ternary:
        case NodeKind::Call: case NodeKind::Member: case NodeKind::SafeMember: case NodeKind::Index:
        case NodeKind::SafeIndex: case NodeKind::Array: case NodeKind::Object: case NodeKind::Property:
            break;
        default:
            return false;  // Lambda, Spread, Await, Opaque, statements
        }
        const NodeId* list = ast.list(n);
        for (NodeId child : {n.a, n.b, n.c}) stack.push_back(child);
        for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back(list[k]);
    }
    return true;
}

} // namespace

bool UnitCompiler::deferred_call(const Node& node, std::string_view name, std::uint16_t dst) {
#if defined(NIFT_DEADCODE_MUTANT) && NIFT_DEADCODE_MUTANT == 1  // test-only: no deferral (today's refusal)
    return false;
#endif
    // L6: `native::<name>` outside the seven. The prevalidator reports it before a script runs; elsewhere (a
    // template, NIFT_PREVALIDATE=off) it is raised when reached, legacy's error and position. No later line or
    // scope can bind a qualified name, so sessions and dynamic scope do not matter.
    if (!native_qualified(name).empty()) {
        fb_->set_offset(site(node));
        fb_->mark_deferred_raise(
            fb_->note(fb_->emit(OpCode::Undefined, dst, 1), std::string(name) + native_not_a_function_suffix()));
        fb_->emit(OpCode::LoadNull, dst);
        return true;
    }
    // Sessions never defer (5.2 "Sessions", D-068 #4): a later line can bind any name. This covers every unit
    // the session compiler builds, the `nift -i` start-up file included (5.7a condition 1).
#if !(defined(NIFT_DEADCODE_MUTANT) && NIFT_DEADCODE_MUTANT == 2)  // test-only: sessions defer like scripts
    if (session_ != nullptr) return false;
#endif
    // A name legacy may know (a built-in, template function, reserved binding: any word in a string literal of
    // the legacy sources, legacy_names.py) may not be an undefined callable there: legacy may run it or raise its
    // own error, so it keeps the refusal (the L-defer rows among them). Broad on purpose: missing a built-in would
    // raise where legacy runs; an extra word only costs coverage.
#if !(defined(NIFT_DEADCODE_MUTANT) && NIFT_DEADCODE_MUTANT == 7)  // test-only: legacy-known names deferred
    // C120 records two free-call false positives in these textual tables:
    // to_string is a receiver method and unknown is diagnostic text.
    // Closed top-level scripts can use the ordinary unbound-name proof.
    const bool known_table_false_positive =
        !in_function_ && !in_lambda_ && !loop_reachable() && (!module_names_ || module_names_->empty()) &&
        (name == "to_string" || name == "unknown");
    if (!known_table_false_positive && (legacy_special_name(name) || legacy_known_word(name))) return false;
#endif
#if !(defined(NIFT_DEADCODE_MUTANT) && NIFT_DEADCODE_MUTANT == 3)  // test-only: name certainty skipped
    auto known = deferred_.unbound.find(name);
    if (known == deferred_.unbound.end())
        known = deferred_.unbound.emplace(name, name_unbound(ast_.source(), name)).first;
    if (!known->second) return false;  // dynamic scope could supply it (1.3): keep the refusal
#endif
#if !(defined(NIFT_DEADCODE_MUTANT) && NIFT_DEADCODE_MUTANT == 8)  // test-only: any argument deferred
    if (!inert_arguments(ast_, node)) return false;
#else
    static_cast<void>(&inert_arguments);
#endif
    deferred_.sites.emplace_back("callable:" + std::string(name), node.begin);
    // Legacy's report (5.2): never catchable, after the output of every earlier statement. Inside a return
    // value legacy reports the return statement with a `return: ` prefix; each calling frame whose own return
    // value made the call adds one more (Vm::error_result, Note::deferred_raise).
    const bool in_return = (fb_->flags() & kInstrReturnCall) != 0;
    // In loop-evaluator code legacy reports a return value's error at the call, unprefixed, and its callers'
    // prefixes there are not established: keep the refusal for a return value in such code.
#if !(defined(NIFT_DEADCODE_MUTANT) && NIFT_DEADCODE_MUTANT == 9)  // test-only: loop-code return values deferred
    if (in_return && loop_reachable()) return false;
#endif
    fb_->set_offset(in_return ? deferred_.return_statement : site(node));
    std::string message = std::string(in_return ? "return: " : "") + "undefined callable: " + std::string(name);
#if defined(NIFT_DEADCODE_MUTANT) && NIFT_DEADCODE_MUTANT == 5  // test-only: arguments evaluated before the raise
    for (std::uint32_t k = 0; k < node.count; ++k) {
        std::uint16_t reg = 0;
        if (!temp(reg) || !expr_to(ast_.list(node)[k], reg)) return false;
    }
    fb_->set_offset(in_return ? deferred_.return_statement : site(node));
#endif
#if defined(NIFT_DEADCODE_MUTANT) && NIFT_DEADCODE_MUTANT == 4  // test-only: the deferred op raises nothing
    static_cast<void>(message);
#else
    fb_->mark_deferred_raise(fb_->note(fb_->emit(OpCode::Undefined, dst, 1), std::move(message)));
#endif
    fb_->emit(OpCode::LoadNull, dst);
    return true;
}

} // namespace nift::rt::detail
