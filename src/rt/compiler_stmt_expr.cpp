#include "rt/compiler_stmt_expr.h"
#include "rt/compiler.h"
// rt-lint: cold-file statement-expression side effects and presentation lowering
namespace nift::rt::detail {
bool UnitCompiler::expression_statement(const Node& node) {
    const Node& expr = ast_.at(node.a);
    if (expr.kind == NodeKind::Call && ast_.at(expr.a).kind == NodeKind::Member) {
        const Node& callee = ast_.at(expr.a);
        // D-063: a stack's push is a keyed mutator (compiler_keyed.cpp).
        if (text(ast_.at(callee.b)) == "push" &&  // rt-lint: allow-string-compare compile time
            !(kinds(callee.a) != 0 && (kinds(callee.a) & ~kKindCollection) == 0)) {
            const Node& receiver = ast_.at(callee.a);
            if (!plain_postfix(expr) || !plain_postfix(callee) || !no_trailing_comma(expr)) return false;
            std::uint16_t reg = 0, value = 0;
            const Binding* holder = receiver.kind == NodeKind::Ident ? find_binding(text(receiver)) : nullptr;
            const bool global = holder != nullptr && holder->global;  // B2: read, pushed in place
            const bool indirect = holder != nullptr && (holder->cell || holder->upvalue);
            const Binding held = (global || indirect) ? *holder : Binding();
            if (receiver.kind != NodeKind::Ident ||
                (!global && !(holder && holder->cell ? indexed_binding(*holder, reg) :
                              local_register(receiver, reg))))
                return fail(receiver, "push-receiver");
            if (expr.count != 1) {
                // The legacy receiver diagnostic and prepared-function view
                // are unproved here; retain their original static refusal.
                if(kinds(callee.a)!=kKindArray||loop_reachable())
                    return fail(expr,"static-error:push-arity");
                std::uint16_t result=0;
                return temp(result)&&collection_failure(expr,result,"push: expected one argument");
            }
            if(holder&&(holder->const_array||holder->loop_binding)) {
                std::uint16_t result=0;
                return temp(result)&&array_mutation(expr,result,0);
            }
            const NodeId arg = ast_.list(expr)[0];
            // B5 (D-NEXT-refsem-b): the pushed value is shared, whatever it is
            // written as (a place, a call result, a literal).
            // Keep the reached type error of a closure result after the
            // factory worklist refines it from unknown to numeric.
            const KindSet receiver_kinds = kinds(callee.a);
            if (!session_ && !in_function_ && !loop_reachable() && holder && !holder->cell &&
                !holder->global && !holder->upvalue && !assigned_.count(text(receiver)) &&
                receiver_kinds != 0 && (receiver_kinds & ~kKindNumber) == 0) {
                const Node* origin = nullptr;
                bool has_command = false;
                for (NodeId id = 0; id < ast_.size(); ++id) {
                    const Node& candidate = ast_.at(id);
                    has_command |= candidate.kind == NodeKind::Command;
                    if (candidate.kind == NodeKind::Declare && candidate.a == holder->decl_node) origin = &candidate;
                }
                if (origin && !has_command) {
                    const Node& declaration = *origin;
                    const Node& init = ast_.at(declaration.b);
                    if (init.kind == NodeKind::Call && kinds(init.a) == kKindFunction) {
                        const auto targets = kinds_.callable_targets.find(init.a);
                        const auto unknown = kinds_.callable_unknown.find(init.a);
                        bool closures = targets != kinds_.callable_targets.end() && !targets->second.empty() &&
                            unknown != kinds_.callable_unknown.end() && !unknown->second;
                        if (closures) for (NodeId target : targets->second)
                            closures &= ast_.at(target).kind == NodeKind::Lambda;
                        if (closures && unobservable_argument(arg)) {
                            std::uint16_t scratch = 0;
                            return temp(scratch) && legacy_fatal(expr,
                                "unknown value or malformed expression: " + std::string(text(expr)), scratch);
                        }
                    }
                }
            }
            if (!(kinds(callee.a) & kKindUnknown) && kinds_.of(callee.a) != 0 &&
                !kinds_within(callee.a, kKindArray, receiver, "type:push"))
                return false;
#if !(defined(NIFT_B5_MUTANT) && NIFT_B5_MUTANT == 13)
            // H-A (combined-b3-b5.md): the receiver is read before the operand and retained
            // in a register (a counted reference); a receiver in its binding's own register is
            // copied first when the operand can rebind it. The array stays in its holder, Push
            // writes it in place and checks the cycle itself (stage 2.4): no write-back.
            if (global && !(held.cell ? read_binding(held, reg) : global_read_value(held.reg, reg, text(receiver))))
                return false;
            if (!retain_receiver(reg, !global && !indirect, arg) || !expr_any(arg, value)) return false;
#else
            if (!expr_any(arg, value)) return false;
            // M13 (H-A): re-read after the operand, as before the H-A repair
            if (indirect && !global && held.selector_root_only && !held.loop_binding)
                fb_->emit(OpCode::CellLoad, reg, held.reg);
            if (global && !(held.cell ? read_binding(held, reg) : global_read_value(held.reg, reg, text(receiver))))
                return false;
#endif
            fb_->set_offset(expr.begin);
            const std::size_t at = fb_->emit(OpCode::Push, reg, value);
            fb_->note(at, std::string(text(expr)));
            // H-A: no write-back when the operand can rebind the binding.
            if (indirect && !global && (held.loop_binding || held.location_source) && !may_rebind(arg)) {
                std::uint16_t untyped = 0;
                if (!fb_->constant(Value::integer(-1), untyped)) return builder_ok();
                if (!store_indexed_binding(held, reg)) return false;
            }
            return true;
        }
    }
    // Legacy renders other expression statements as template text (an
    // aggregate value is an error there); only calls are compiled. A call
    // whose value may be an Error is rendered by legacy's template path
    // (`Error values cannot be rendered as text in $[...]`): refuse it.
    if (session_ && session_->inspect_tail && !in_function_) {
        // D-063: legacy shows nothing for a statement that mutated at its
        // top level (last_expression_mutation_), here the value would be
        // shown; and a collection value has no session form here (below).
        if (!loop_region() && unit_makes_collections() && keyed_mutator_call(expr))
            return fail(expr, "session:collection-mutator");
        if (!loop_region()) return session_render(node_id(node));
        std::uint16_t scratch = 0;
        return temp(scratch) && expr_to(node.a, scratch);  // prepared loop statements discard values
    }
    if (expr.kind != NodeKind::Call && !(expr.kind == NodeKind::Await && plan_.coroutines))
        return fail(expr, "expression-statement");
    if (kinds(node.a) == kKindError) return fail(expr, "error-result-expression");
    std::uint16_t scratch = 0;
    return temp(scratch) && expr_to(node.a, scratch);
}

} // namespace nift::rt::detail
