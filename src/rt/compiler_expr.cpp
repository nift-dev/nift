#include "rt/compiler_expr.h"

#include "rt/alias.h"
#include "rt/atom.h"
#include "rt/literal.h"
#include "rt/native.h"

namespace nift::rt::detail {

namespace {

bool is_place(NodeKind kind) { return kind == NodeKind::Ident || kind == NodeKind::Member || kind == NodeKind::Index; }

OpCode binary_opcode(Op op) {
    switch (op) {
        case Op::Add: return OpCode::Add;
        case Op::Sub: return OpCode::Sub;
        case Op::Mul: return OpCode::Mul;
        case Op::Div: return OpCode::Div;
        case Op::Mod: return OpCode::Mod;
        case Op::Eq: return OpCode::Eq;
        case Op::Ne: return OpCode::Ne;
        case Op::Lt: return OpCode::Lt;
        case Op::Le: return OpCode::Le;
        case Op::Gt: return OpCode::Gt;
        case Op::Ge: return OpCode::Ge;
        default: return OpCode::Nop;
    }
}

OpCode fused_not_jump(Op op) {
    switch (op) {
        case Op::Lt: return OpCode::JmpNotLt;
        case Op::Le: return OpCode::JmpNotLe;
        case Op::Gt: return OpCode::JmpNotGt;
        case Op::Ge: return OpCode::JmpNotGe;
        case Op::Eq: return OpCode::JmpNotEq;
        case Op::Ne: return OpCode::JmpNotNe;
        default: return OpCode::Nop;
    }
}


} // namespace

bool UnitCompiler::temp(std::uint16_t& reg) { return fb_->alloc(reg) || builder_ok(); }

bool UnitCompiler::local_register(const Node& node, std::uint16_t& reg) {
    const Binding* binding = scopes_.find(text(node));
    if (!binding) {
        if (plan_.index.count(text(node))) return fail(node, "fn-as-value");
        return fail(node, in_function_ ? "free-name" : "unresolved-name");
    }
    reg = binding->reg;
    return true;
}

bool UnitCompiler::has_call(NodeId id) const {
    if (id == kNoNode) return false;
    const Node& n = ast_.at(id);
    if (n.kind == NodeKind::Call) return true;
    if (has_call(n.a) || has_call(n.b) || has_call(n.c)) return true;
    const NodeId* list = ast_.list(n);
    for (std::uint32_t k = 0; k < n.count; ++k)
        if (has_call(list[k])) return true;
    return false;
}

// Literal (or negated numeric literal) value of `node`.
bool UnitCompiler::constant_of(const Node& node, Value& value, bool& is_constant) {
    is_constant = true;
    switch (node.kind) {
        case NodeKind::Number:
            if (!decode_number_literal(text(node), value)) return fail(node, "number-literal");
            return true;
        case NodeKind::String:
            if (node.count != 0) return fail(node, "interpolation");
            if (!decode_string_literal(text(node), value)) return fail(node, "string-literal");
            return true;
        case NodeKind::True: value = Value::boolean(true); return true;
        case NodeKind::False: value = Value::boolean(false); return true;
        case NodeKind::Null: value = Value::null(); return true;
        case NodeKind::Unary: {
            const Node& operand = ast_.at(node.a);
            if (node.op != Op::Neg || operand.kind != NodeKind::Number) break;
            if (!decode_number_literal(text(operand), value)) return fail(operand, "number-literal");
            // -0 stays a run-time negation (the VM bails on integer -0).
            if (value.is(Kind::Int) && value.as_int() != 0) { value = Value::integer(-value.as_int()); return true; }
            if (value.is(Kind::Double) && value.as_double() != 0) {
                value = Value::number(-value.as_double());
                return true;
            }
            break;
        }
        default: break;
    }
    is_constant = false;
    return true;
}

bool UnitCompiler::expr_any(NodeId id, std::uint16_t& operand) {
    const Node& node = ast_.at(id);
    if (node.kind == NodeKind::Ident) return local_register(node, operand);
    Value value;
    bool is_constant = false;
    if (!constant_of(node, value, is_constant)) return false;
    if (is_constant) return fb_->constant(value, operand) || builder_ok();
    return temp(operand) && expr_to(id, operand);
}

bool UnitCompiler::reg_any(NodeId id, std::uint16_t& reg) {
    const Node& node = ast_.at(id);
    if (node.kind == NodeKind::Ident) return local_register(node, reg);
    return temp(reg) && expr_to(id, reg);
}

bool UnitCompiler::expr_to(NodeId id, std::uint16_t dst) {
    const Node& node = ast_.at(id);
    // Legacy limits expression nesting at run time (about 90 levels).
    struct Depth { int& d; explicit Depth(int& x) : d(++x) {} ~Depth() { --d; } } depth(depth_);
    if (depth_ > kMaxExpressionNesting) return fail(node, "expression-nesting");
    fb_->set_offset(node.begin);
    Value value;
    bool is_constant = false;
    if (!constant_of(node, value, is_constant)) return false;
    if (is_constant) {
        if (value.is(Kind::Int) && value.as_int() >= -32768 && value.as_int() <= 32767) {
            fb_->emit(OpCode::LoadI, dst, jump_operand(static_cast<int>(value.as_int())));
        } else if (value.is_null()) {
            fb_->emit(OpCode::LoadNull, dst);
        } else if (value.is(Kind::Bool)) {
            fb_->emit(OpCode::LoadBool, dst, value.as_bool() ? 1 : 0);
        } else {
            std::uint16_t k = 0;
            if (!fb_->constant(value, k)) return builder_ok();
            fb_->emit(OpCode::LoadK, dst, static_cast<std::uint16_t>(k & ~kConstantBit));
        }
        return true;
    }
    switch (node.kind) {
        case NodeKind::Ident: {
            std::uint16_t reg = 0;
            if (!local_register(node, reg)) return false;
            if (reg != dst) fb_->emit(OpCode::Move, dst, reg);
            return true;
        }
        case NodeKind::Binary: return binary(node, dst);
        case NodeKind::Logical: return logical(node, dst);
        case NodeKind::Coalesce: return coalesce(node, dst);
        case NodeKind::Ternary: return ternary(node, dst);
        case NodeKind::Unary: return unary(node, dst);
        case NodeKind::Call: return call(node, dst);
        case NodeKind::Member: return member(node, dst);
        case NodeKind::Index: return index(node, dst);
        case NodeKind::Array: return array_literal(node, dst);
        case NodeKind::Object: return object_literal(node, dst);
        default: return fail(node, std::string("expression:") + node_kind_name(node.kind));
    }
}

bool UnitCompiler::binary(const Node& node, std::uint16_t dst) {
    const OpCode op = binary_opcode(node.op);
    if (op == OpCode::Nop) return fail(node, "binary-operator");
    if (!infix_left_ok(node) || !comparison_chain_ok(node)) return false;
    const std::uint16_t mark = fb_->top();
    std::uint16_t left = 0, right = 0;
    if (!expr_any(node.a, left) || !expr_any(node.b, right)) return false;
    fb_->set_offset(node.begin);
    fb_->emit(op, dst, left, right);
    fb_->release_to(mark);
    return true;
}

// `&&` / `||` produce a bool (legacy converts the deciding operand).
bool UnitCompiler::logical(const Node& node, std::uint16_t dst) {
    if (node.op != Op::And && node.op != Op::Or) return fail(node, "logical-operator");
    if (!infix_left_ok(node)) return false;
    if (!expr_to(node.a, dst)) return false;
    const bool is_and = node.op == Op::And;
    const std::size_t decided = fb_->emit(is_and ? OpCode::JmpFalse : OpCode::JmpTrue, dst);
    if (!expr_to(node.b, dst)) return false;
    fb_->emit(OpCode::Truth, dst, dst);
    const std::size_t done = fb_->emit(OpCode::Jmp);
    fb_->patch(decided, fb_->here());
    fb_->emit(OpCode::LoadBool, dst, is_and ? 0 : 1);
    fb_->patch(done, fb_->here());
    return builder_ok();
}

bool UnitCompiler::coalesce(const Node& node, std::uint16_t dst) {
    if (!infix_left_ok(node)) return false;
    if (!expr_to(node.a, dst)) return false;
    const std::size_t done = fb_->emit(OpCode::JmpNotNull, dst);
    if (!expr_to(node.b, dst)) return false;
    fb_->patch(done, fb_->here());
    return builder_ok();
}

bool UnitCompiler::ternary(const Node& node, std::uint16_t dst) {
    if (!infix_left_ok(node)) return false;
    std::vector<std::size_t> to_else;
    if (!false_jump(node.a, to_else) || !expr_to(node.b, dst)) return false;
    const std::size_t done = fb_->emit(OpCode::Jmp);
    for (std::size_t at : to_else) fb_->patch(at, fb_->here());
    if (!expr_to(node.c, dst)) return false;
    fb_->patch(done, fb_->here());
    return builder_ok();
}

bool UnitCompiler::unary(const Node& node, std::uint16_t dst) {
    const std::uint16_t mark = fb_->top();
    std::uint16_t operand = 0;
    if (!reg_any(node.a, operand)) return false;
    fb_->set_offset(node.begin);
    switch (node.op) {
        case Op::Not: fb_->emit(OpCode::Not, dst, operand); break;
        case Op::Neg: fb_->emit(OpCode::Neg, dst, operand); break;
        case Op::Pos: fb_->emit(OpCode::Pos, dst, operand); break;
        default: return fail(node, "unary-operator");
    }
    fb_->release_to(mark);
    return true;
}

bool UnitCompiler::call(const Node& node, std::uint16_t dst) {
    const Node& callee = ast_.at(node.a);
    if (callee.kind == NodeKind::Member) return method_call(node, dst);
    if (callee.kind != NodeKind::Ident) return fail(callee, "callee");
    if (!plain_postfix(node) || !no_trailing_comma(node)) return false;
    const std::string_view name = text(callee);
    if (scopes_.find(name)) return fail(callee, "call-binding");
    const auto fn = plan_.index.find(name);
    NativeInfo native{};
    const bool is_native = fn == plan_.index.end() && lookup_native(name, native);
    if (fn == plan_.index.end() && !is_native) return fail(callee, "callable:" + std::string(name));
    const std::uint16_t arity = is_native ? native.arity : ast_.at(plan_.functions[fn->second - 1]).count;
    // When `dst` is the newest register nothing above it is live, so the call
    // window (arguments, then the callee frame) starts at `dst` and the result
    // lands in place; otherwise it starts at the first free register.
    const bool in_place = dst + 1u == fb_->top();
    const std::uint16_t base = in_place ? dst : fb_->top();
    const NodeId* args = ast_.list(node);
    for (std::uint32_t k = 0; k < node.count; ++k) {
        std::uint16_t reg = base;
        if (ast_.at(args[k]).kind == NodeKind::Spread) return fail(ast_.at(args[k]), "spread");
        // A place argument is a legacy location reference (review D6).
        if (!is_native && is_place_argument(ast_, args[k]) && fn->second - 1u < plan_.escaping.size() &&
            k < plan_.escaping[fn->second - 1u].size() && plan_.escaping[fn->second - 1u][k])
            return fail(ast_.at(args[k]), "alias-argument");
        if (!(in_place && k == 0) && !temp(reg)) return false;
        if (!expr_to(args[k], reg)) return false;
        if (!is_native && !param_check(args[k], reg)) return false;
    }
    fb_->set_offset(node.begin);
    if (node.count != arity) return fail(node, "static-error:arity");
    if (is_native) {
        fb_->emit(OpCode::CallNative, base, static_cast<std::uint16_t>(native.id),
                  static_cast<std::uint16_t>(node.count));
    } else {
        fb_->emit(OpCode::Call, base, fn->second, static_cast<std::uint16_t>(node.count));
    }
    std::uint16_t slot = base;
    if (node.count == 0 && !in_place && !temp(slot)) return false;  // the result slot exists even without arguments
    if (dst != base) fb_->emit(OpCode::Move, dst, base);
    fb_->release_to(in_place ? static_cast<std::uint16_t>(dst + 1) : base);
    return builder_ok();
}

bool UnitCompiler::method_call(const Node& node, std::uint16_t dst) {
    const Node& callee = ast_.at(node.a);
    if (!plain_postfix(node) || !plain_postfix(callee) || !no_trailing_comma(node)) return false;
    const std::string_view name = text(ast_.at(callee.b));
    if (name != "length" && name != "size")  // rt-lint: allow-string-compare compile time
        return fail(callee, "method:" + std::string(name));
    if (node.count != 0) return fail(node, "method-arity");
    const std::uint16_t mark = fb_->top();
    std::uint16_t receiver = 0;
    if (!reg_any(callee.a, receiver)) return false;
    fb_->set_offset(node.begin);
    fb_->emit(OpCode::Length, dst, receiver, name == "size" ? 1 : 0);  // rt-lint: allow-string-compare compile time
    fb_->release_to(mark);
    return true;
}

bool UnitCompiler::member(const Node& node, std::uint16_t dst) {
    if (!plain_postfix(node)) return false;
    const std::uint16_t mark = fb_->top();
    std::uint16_t object = 0;
    if (!reg_any(node.a, object)) return false;
    const Atom key = atom(text(ast_.at(node.b)));
    if (key.id > 0xffff) return fail(node, "limit:atoms");
    fb_->set_offset(node.begin);
    fb_->emit(OpCode::GetMember, dst, object, static_cast<std::uint16_t>(key.id));
    fb_->release_to(mark);
    return true;
}

bool UnitCompiler::index(const Node& node, std::uint16_t dst) {
    if (!plain_postfix(node)) return false;
    const std::uint16_t mark = fb_->top();
    std::uint16_t object = 0, key = 0;
    if (!reg_any(node.a, object) || !expr_any(node.b, key)) return false;
    fb_->set_offset(node.begin);
    fb_->emit(OpCode::GetIndex, dst, object, key);
    fb_->release_to(mark);
    return true;
}

bool UnitCompiler::array_literal(const Node& node, std::uint16_t dst) {
    if (!no_trailing_comma(node)) return false;
    const std::uint16_t base = fb_->top();
    const NodeId* items = ast_.list(node);
    for (std::uint32_t k = 0; k < node.count; ++k) {
        std::uint16_t reg = 0;
        if (ast_.at(items[k]).kind == NodeKind::Spread) return fail(ast_.at(items[k]), "spread");
        if (!temp(reg) || !expr_to(items[k], reg)) return false;
        if (is_place(ast_.at(items[k]).kind)) fb_->emit(OpCode::NoAlias, reg);
    }
    fb_->set_offset(node.begin);
    fb_->emit(OpCode::NewArray, dst, base, static_cast<std::uint16_t>(node.count));
    fb_->release_to(base);
    return true;
}

bool UnitCompiler::object_literal(const Node& node, std::uint16_t dst) {
    if (!no_trailing_comma(node)) return false;
    const std::uint16_t mark = fb_->top();
    std::uint16_t object = 0;
    if (!temp(object)) return false;
    fb_->emit(OpCode::NewObject, object);
    std::vector<std::uint32_t> seen;
    const NodeId* props = ast_.list(node);
    for (std::uint32_t k = 0; k < node.count; ++k) {
        const Node& prop = ast_.at(props[k]);
        Value key_text;
        if (!decode_string_literal(text(ast_.at(prop.a)), key_text)) return fail(prop, "object-key");
        const Atom key = atom(text(ast_.at(prop.a)).substr(1, text(ast_.at(prop.a)).size() - 2));
        if (key.id > 0xffff) return fail(prop, "limit:atoms");
        for (std::uint32_t s : seen)
            if (s == key.id) return fail(prop, "object-duplicate-key");
        seen.push_back(key.id);
        std::uint16_t value = 0;
        if (!expr_any(prop.b, value)) return false;
        fb_->set_offset(prop.begin);
        if (is_place(ast_.at(prop.b).kind)) fb_->emit(OpCode::NoAlias, value);
        fb_->emit(OpCode::ObjectSet, object, static_cast<std::uint16_t>(key.id), value);
    }
    fb_->emit(OpCode::Move, dst, object);
    fb_->release_to(mark);
    return true;
}

bool UnitCompiler::false_jump(NodeId id, std::vector<std::size_t>& jumps) {
    const Node& node = ast_.at(id);
    if ((node.kind == NodeKind::Logical || node.kind == NodeKind::Binary) && !infix_left_ok(node)) return false;
    if (node.kind == NodeKind::Logical && node.op == Op::And)
        return false_jump(node.a, jumps) && false_jump(node.b, jumps);
    const std::uint16_t mark = fb_->top();
    const OpCode fused = node.kind == NodeKind::Binary ? fused_not_jump(node.op) : OpCode::Nop;
    if (fused != OpCode::Nop) {
        if (!comparison_chain_ok(node)) return false;
        std::uint16_t left = 0, right = 0;
        if (!expr_any(node.a, left) || !expr_any(node.b, right)) return false;
        fb_->set_offset(node.begin);
        jumps.push_back(fb_->emit(fused, left, right));
    } else {
        std::uint16_t value = 0;
        if (!temp(value) || !expr_to(id, value)) return false;
        jumps.push_back(fb_->emit(OpCode::JmpFalse, value));
    }
    fb_->release_to(mark);
    return true;
}

} // namespace nift::rt::detail
