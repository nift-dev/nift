#include "rt/compiler_stmt_expr.h"
#include "rt/compiler.h"
// rt-lint: cold-file statement-expression side effects and presentation lowering
namespace nift::rt::detail {
bool UnitCompiler::expression_statement(const Node& node) {
    const Node& expr = ast_.at(node.a);
    if (expr.kind == NodeKind::Call && ast_.at(expr.a).kind == NodeKind::Member) {
        const Node& callee = ast_.at(expr.a);
        if (text(ast_.at(callee.b)) == "push") {  // rt-lint: allow-string-compare compile time
            const Node& receiver = ast_.at(callee.a);
            if (!plain_postfix(expr) || !plain_postfix(callee) || !no_trailing_comma(expr)) return false;
            std::uint16_t reg = 0, value = 0;
            const Binding* holder = receiver.kind == NodeKind::Ident ? find_binding(text(receiver)) : nullptr;
            const bool global = holder != nullptr && holder->global;  // CP12b: taken, pushed, put back
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
            if(holder&&(holder->const_array||holder->selector||holder->loop_binding)) {
                std::uint16_t result=0;
                return temp(result)&&array_mutation(expr,result,0);
            }
            const NodeId arg = ast_.list(expr)[0];
            // records20k coverage (stage 4): legacy pushes an aggregate taken
            // from a place as a location reference (refused: no_alias); a
            // fresh value (call result, literal, operator result) is a value
            // in both engines.
            if (!no_alias(arg, ast_.at(arg)) ||
                (!(kinds(callee.a) & kKindUnknown) && kinds_.of(callee.a) != 0 &&
                 !kinds_within(callee.a, kKindArray, receiver, "type:push")))
                return false;
            if (!expr_any(arg, value)) return false;
            if (indirect && !global && held.selector_root_only && !held.loop_binding)
                fb_->emit(OpCode::CellTake, reg, held.reg);
            if (global && !global_take(held, reg, text(receiver))) return false;
            fb_->set_offset(expr.begin);
            const std::size_t at = fb_->emit(OpCode::Push, reg, value);
            fb_->note(at, std::string(text(expr)));
            if (global) global_put(held, reg);
            else if (indirect) {
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
