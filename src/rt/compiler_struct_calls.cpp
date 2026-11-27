#include "rt/compiler_struct_calls.h"

#include "rt/alias.h"
#include "rt/module_link.h"
#include "rt/structs.h"
#include "rt/builtin_bind.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace nift::rt::detail {
// A fresh result assembled from values cannot forward or mutate a formal.
static bool method_returns_value_object(const Ast& ast, const Node& body) {
    if (body.count != 1) return false;
    const Node& stmt = ast.at(ast.list(body)[0]);
    if (stmt.kind != NodeKind::Return || stmt.a == kNoNode) return false;
    const Node& object = ast.at(stmt.a);
    if (object.kind != NodeKind::Object) return false;
    const auto* props = ast.list(object);
    return std::all_of(props, props + object.count, [&](NodeId id) {
        const auto& value = ast.at(ast.at(id).b);
        return value.kind == NodeKind::Ident || value.kind == NodeKind::Number || value.kind == NodeKind::True ||
               value.kind == NodeKind::False || value.kind == NodeKind::Null ||
               (value.kind == NodeKind::String && value.count == 0);
    });
}
// Legacy splits these boolean chains at || before negating each
// method result. Match the same short-circuit even when a method has effects.
bool UnitCompiler::boolean_method_condition(NodeId id) const {
    if (!plan_.is_method(function_) || !plan_.linked || plan_.linked->locate(ast_.at(id).begin).module == 0)
        return false;
    const auto& node = ast_.at(id);
    if (node.kind == NodeKind::Logical && (node.op == Op::Or || node.op == Op::And))
        return boolean_method_condition(node.a) && boolean_method_condition(node.b);
    if (node.kind == NodeKind::Binary && kinds(id) == kKindBool) return true;
    if (node.kind == NodeKind::Call) {  // a plain boolean method on the right of ||
        const auto& method = ast_.at(node.a);
        return method.kind == NodeKind::Member &&
               (kinds(id) == kKindBool || boolean_method_name(text(ast_.at(method.b))));
    }
    if (node.kind != NodeKind::Unary || node.op != Op::Not) return false;
    const auto& call = ast_.at(node.a);
    const bool boolean_call = call.kind == NodeKind::Call && ast_.at(call.a).kind == NodeKind::Member &&
                              boolean_method_name(text(ast_.at(ast_.at(call.a).b)));
    if (kinds(node.a) != kKindBool && !boolean_call) return false;
    // The comparison is explicitly grouped under !. Its normal admission
    // and operand checks still run when the logical RHS is emitted.
    const auto spelling = text(node);
    if (call.kind == NodeKind::Binary && call.op == Op::Gt &&
        spelling.substr(0, 2) == "!(" && spelling.back() == ')' &&  // rt-lint: allow-string-compare compile time
        ast_.at(call.b).kind == NodeKind::Number && text(ast_.at(call.b)) == "0")  // rt-lint: allow-string-compare
        return true;
    // A boolean field/name under `!` in the same split chain.
    if (call.kind == NodeKind::Member || call.kind == NodeKind::Ident) return true;
    if (call.kind != NodeKind::Call) return false;
    return ast_.at(call.a).kind == NodeKind::Member;
}

// A method call: the instance, then the arguments, in a fresh window.
bool UnitCompiler::struct_call(const Node& node, int s, const StructMethodInfo& m, NodeId receiver,
                               int receiver_reg, std::uint16_t dst) {
    (void)s;
    bool is_this = receiver == kNoNode && receiver_reg < 0;
    if (receiver != kNoNode) struct_of_receiver(receiver, is_this);
    if (m.priv && !is_this) return fail(node, "struct-private");
    if (!plain_postfix(node) || !no_trailing_comma(node)) return false;
    const bool numeric_return = session_ && (fb_->flags() & kInstrReturnCall);
    const std::size_t f = m.function;
    const Node& fn = ast_.at(plan_.functions[f]);
    const bool variadic = variadic_method(ast_, fn);
    if (variadic && session_) return fail(node, "fn-param");
    const std::uint32_t fixed = fn.count - (variadic ? 1u : 0u);
    if (node.count < fixed || (!variadic && node.count != fixed)) return fail(node, "static-error:arity");
    const NodeId* args = ast_.list(node);
    const std::uint16_t base = fb_->top();
    if (!operand_window(base, fn.count + 1)) return false;
    if (receiver_reg >= 0) fb_->emit(OpCode::Move, base, static_cast<std::uint16_t>(receiver_reg));
    else if (!receiver_value(receiver, base)) return false;
    ++call_args_;
    const NodeId saved_path = path_arg_, saved_chain = path_chain_;
    const bool value_only = !session_ && plan_.linked && plan_.linked->locate(node.begin).module > 0 &&
                            method_returns_value_object(ast_, ast_.at(fn.b));
    const auto packed_mark = fb_->top();
    const auto append_spread = [&](std::uint16_t array, std::uint16_t source) {
        const auto mark = fb_->top();
        std::uint16_t index, count, element;
        if (!temp(index) || !temp(count) || !temp(element)) return false;
        fb_->emit(OpCode::LoadI, index, 0);
        fb_->emit(OpCode::Length, count, source, 0);
        const auto empty = fb_->emit(OpCode::JmpNotLt, index, count);
        const auto append = fb_->here();
        fb_->emit(OpCode::ArrayElemValue, element, source, index);
        fb_->emit(OpCode::Push, array, element);
        fb_->patch(fb_->emit(OpCode::JmpIncLt, index, count), append);
        fb_->patch(empty, fb_->here());
        fb_->emit(OpCode::LoadNull, element);
        fb_->release_to(mark);
        return builder_ok();
    };
    bool ok = true;
    std::vector<std::optional<std::uint16_t>> spread_values;
    if (variadic) {
        spread_values.resize(node.count - fixed);
        path_arg_ = path_chain_ = kNoNode;
        // Legacy takes spread snapshots before evaluating any ordinary operand.
        for (std::uint32_t k = fixed; ok && k < node.count; ++k) {
            const Node& operand = ast_.at(args[k]);
            if (operand.kind != NodeKind::Spread) continue;
            if (receiver_reg >= 0 || kinds(operand.a) != kKindArray) {
                ok = fail(operand, "spread");
                break;
            }
            std::uint16_t snapshot;
            if (!temp(snapshot)) { ok = false; break; }
            spread_values[k - fixed] = snapshot;
            const auto mark = fb_->top();
            std::uint16_t source;
            if (!temp(source) || !window_value(operand.a, source)) { ok = false; break; }
            fb_->set_offset(site(operand));
            fb_->emit(OpCode::NewArray, snapshot, snapshot, 0);
            ok = append_spread(snapshot, source);
            fb_->emit(OpCode::LoadNull, source);
            fb_->release_to(mark);
        }
    }
    for (std::uint32_t k = 0; ok && k < fixed; ++k) {
        // A bare scalar path is passed by value (legacy does not write a
        // method parameter back to the path); argument_path checks the kinds.
        const NodeKind arg_kind = ast_.at(args[k]).kind;
        const bool path_kind = arg_kind == NodeKind::Ident || arg_kind == NodeKind::Member ||
                               arg_kind == NodeKind::Index;
        // A non-escaping parameter only reads its argument, so a path may be
        // read as an ordinary value (shared aggregates included). An escaping
        // parameter keeps the scalar-only location rule (or alias-argument).
        const bool escapes = f < plan_.escaping.size() && k < plan_.escaping[f].size() &&
                             plan_.escaping[f][k];
        path_arg_ = escapes && index_depth_ == 0 && path_kind && bare_argument(node, k) ? args[k] : kNoNode;
        path_chain_ = kNoNode;
        if (ast_.at(args[k]).kind == NodeKind::Spread) ok = fail(ast_.at(args[k]), "spread");
        else if (is_place_argument(ast_, args[k]) && f < plan_.escaping.size() && k < plan_.escaping[f].size() &&
                 plan_.escaping[f][k])
            ok = fail(ast_.at(args[k]), "alias-argument");
        else ok = window_value(args[k], static_cast<std::uint16_t>(base + k + 1));
    }
    if (ok && variadic) {
        path_arg_ = path_chain_ = kNoNode;
        const auto rest = static_cast<std::uint16_t>(base + fixed + 1);
        fb_->set_offset(site(node));
        fb_->emit(OpCode::NewArray, rest, rest, 0);
        for (std::uint32_t k = fixed; ok && k < node.count; ++k) {
            const auto snapshot = spread_values[k - fixed];
            fb_->set_offset(site(ast_.at(args[k])));
            if (snapshot) {
                ok = append_spread(rest, *snapshot);
                fb_->emit(OpCode::LoadNull, *snapshot);
                continue;
            }
            const auto mark = fb_->top();
            std::uint16_t value;
            if (!temp(value) || !window_value(args[k], value)) { ok = false; break; }
            fb_->set_offset(site(ast_.at(args[k])));
            fb_->emit(OpCode::Push, rest, value);
            fb_->emit(OpCode::LoadNull, value);
            fb_->release_to(mark);
        }
    }
    fb_->release_to(packed_mark);
    path_arg_ = saved_path;
    path_chain_ = saved_chain;
    --call_args_;
    if (!ok) return false;
    if (loop_region() || loop_type_error_ambiguous())
        for (std::uint32_t k = 0; k < fixed; ++k)
            if (observable_[f][k] && type_mismatch_possible(text_type_at(args[k]), kinds(args[k]), scope_index()))
                return fail(ast_.at(args[k]), "context:parameter-type");
    if (!pass_param_types(f, fn.count, args, static_cast<std::uint16_t>(base + 1))) return false;
    fb_->set_offset(site(node));
    const std::size_t at = fb_->emit(OpCode::Call, base, static_cast<std::uint16_t>(f + 1),
                                     static_cast<std::uint16_t>(fn.count + 1));
    fb_->note(at, std::string(m.name), false, false, numeric_return);
    if (dst != base) fb_->emit(OpCode::Move, dst, base);
    fb_->release_to(dst >= base ? static_cast<std::uint16_t>(dst + 1) : base);
    return builder_ok();
}

} // namespace nift::rt::detail
