#include "rt/compiler_expr.h"
#include "rt/alias.h"
#include "rt/atom.h"
#include "rt/literal.h"
#include "rt/native.h"
#include "rt/natives/json_methods.h"
#include "rt/prepared_parse.h"
namespace nift::rt::detail {
namespace {
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


// Kinds an arithmetic operator accepts without a legacy type error (or a
// prepared-path rendering of aggregates in `+`).
bool arithmetic_kinds_ok(Op op, KindSet l, KindSet r) {
    if ((l | r) & kKindUnknown) return false;  // may be anything: refused (review D1); probe: caller
    const auto only = [](KindSet k, KindSet allowed) { return (k & ~allowed) == 0; };
    if (op == Op::Add) {
        if (only(l, kKindNumber) && only(r, kKindNumber)) return true;
        // Concatenation: one side is a string, the other a scalar.
        if (only(l, kKindScalar) && only(r, kKindScalar)) {
            for (KindSet a = 1; a <= kKindObject; a <<= 1)
                for (KindSet b = 1; b <= kKindObject; b <<= 1)
                    if ((l & a) && (r & b) && !((a | b) & kKindString) && !((a & kKindNumber) && (b & kKindNumber)))
                        return false;
            return true;
        }
        return false;
    }
    return only(l, kKindNumber) && only(r, kKindNumber);
}

bool ordering_kinds_ok(KindSet l, KindSet r) {
    if ((l | r) & kKindUnknown) return false;  // may be anything: refused (review D1)
    return ((l & ~kKindNumber) == 0 && (r & ~kKindNumber) == 0) || ((l & ~kKindString) == 0 && (r & ~kKindString) == 0);
}

} // namespace

bool UnitCompiler::temp(std::uint16_t& reg) { return fb_->alloc(reg) || builder_ok(); }

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
            if (node.count != 0) { is_constant = false; return true; }  // interpolation lowering
            if (!decode_string_literal(text(node), value)) return fail(node, "string-literal");
            return true;
        case NodeKind::True: value = Value::boolean(true); return true;
        case NodeKind::False: value = Value::boolean(false); return true;
        case NodeKind::Null: value = Value::null(); return true;
        case NodeKind::Unary: {
            const Node& operand = ast_.at(node.a);
            if (node.op != Op::Neg || operand.kind != NodeKind::Number) break;
            switch (decode_negative_number_literal(text(operand), value)) {
                case NegativeLiteral::Decoded: return true;
                case NegativeLiteral::Zero: break;  // -0 stays a run-time negation
                case NegativeLiteral::Invalid: return fail(operand, "number-literal");
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
        case NodeKind::Lambda: return lambda_value(node, dst);
        case NodeKind::Binary: return binary(node, dst);
        case NodeKind::Logical: return logical(node, dst);
        case NodeKind::Coalesce: return coalesce(node, dst);
        case NodeKind::Ternary: return ternary(node, dst);
        case NodeKind::Unary: return unary(node, dst);
        case NodeKind::Assign: case NodeKind::CompoundAssign: return assignment_expr(node, dst);
        case NodeKind::IncDec: return inc_dec_expr(node, dst);
        case NodeKind::Call: return call(node, dst);
        case NodeKind::Member: return member(node, dst);
        case NodeKind::Index: return index(node, dst);
        case NodeKind::Array: return array_literal(node, dst);
        case NodeKind::Object: return object_literal(node, dst);
        case NodeKind::String: return interpolation(node, dst);
        default: return fail(node, std::string("expression:") + node_kind_name(node.kind));
    }
}

bool UnitCompiler::binary(const Node& node, std::uint16_t dst) {
    const OpCode op = binary_opcode(node.op);
    if (op == OpCode::Nop) return fail(node, "binary-operator");
    if (!infix_left_ok(node) || !comparison_chain_ok(node)) return false;
    const KindSet l = kinds(node.a), r = kinds(node.b);
    bool numeric_guard = false;
    switch (op) {
        case OpCode::Add: case OpCode::Sub: case OpCode::Mul: case OpCode::Div: case OpCode::Mod:
            // Legacy's string + callable is a run-time error with a fixed
            // message; compile it and let concatenate report it (handles_lambda_string).
            if (op == OpCode::Add && ((l & kKindFunction) || (r & kKindFunction)) &&
                ((l & kKindString) || (r & kKindString)))
                break;
            if (!arithmetic_kinds_ok(node.op, l, r) && !unknown_defers(l | r)) {
                const auto widened = [](KindSet k) { return (k & kKindNumber) && (k & ~kKindNumber); };
                numeric_guard = session_ && op != OpCode::Add && !((l | r) & kKindUnknown) &&
                    (widened(l) || widened(r) || node.begin < session_->unit_begin);
                if (!numeric_guard) return fail(node, std::string("type:") + op_name(node.op));
            }
            break;
        case OpCode::Lt: case OpCode::Le: case OpCode::Gt: case OpCode::Ge:
            if (!ordering_kinds_ok(l, r) && !unknown_defers(l | r))
                return fail(node, std::string("type:") + op_name(node.op));
            break;
        default: break;
    }
    const std::uint16_t mark = fb_->top();
    std::uint16_t left = 0, right = 0;
    if (!expr_any(node.a, left) || !expr_any(node.b, right)) return false;
    fb_->set_offset(site(node));
#if !defined(NIFT_SESSION_ADMISSION_MUTANT) || NIFT_SESSION_ADMISSION_MUTANT != 9
    if (numeric_guard) fb_->emit(OpCode::SessionNumeric, 0, left, right);
#endif
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
    if (node.op != Op::Not && !kinds_within(node.a, kKindNumber, node, "type:unary")) return false;
    // Legacy's prepared negation of an integer 0 gives -0 (the canonical one
    // 0): refused in loop code where the operand may be an integer (D-018).
    if (node.op == Op::Neg && loop_reachable() && (kinds(node.a) & (kKindInt | kKindUnknown)))
        return fail(node, "context:negative-zero");
    const std::uint16_t mark = fb_->top();
    std::uint16_t operand = 0;
    if (!reg_any(node.a, operand)) return false;
    fb_->set_offset(site(node));
    switch (node.op) {
        case Op::Not: fb_->emit(OpCode::Not, dst, operand); break;
        case Op::Neg: fb_->emit(OpCode::Neg, dst, operand); break;
        case Op::Pos: fb_->emit(OpCode::Pos, dst, operand); break;
        default: return fail(node, "unary-operator");
    }
    fb_->release_to(mark);
    return true;
}

bool UnitCompiler::array_literal(const Node& node, std::uint16_t dst) {
    if (!no_trailing_comma(node)) return false;
    const std::uint16_t base = fb_->top();
    const NodeId* items = ast_.list(node);
#if defined(NIFT_SESSION_ADMISSION_MUTANT) && NIFT_SESSION_ADMISSION_MUTANT == 22
    for (std::uint32_t k = 0; k < node.count; ++k) {
        std::uint16_t reg = 0;
        if (!temp(reg) || !expr_to(items[k], reg)) return false;
    }
#else
    // Reserve contiguous payload; Cell reads need scratch registers between items.
    for (std::uint32_t k = 0; k < node.count; ++k) {
        std::uint16_t reg = 0;
        if (!temp(reg)) return false;
    }
    const auto scratch = fb_->top();
    for (std::uint32_t k = 0; k < node.count; ++k) {
        if (ast_.at(items[k]).kind == NodeKind::Spread) return fail(ast_.at(items[k]), "spread");
        if (!no_alias(items[k], ast_.at(items[k]))) return false;
        if (!expr_to(items[k], static_cast<std::uint16_t>(base + k))) return false;
        fb_->release_to(scratch);
    }
#endif
    fb_->set_offset(node.begin);
    fb_->emit(OpCode::NewArray, dst, base, static_cast<std::uint16_t>(node.count));
    fb_->release_to(base);
    return true;
}

// A JSON number spelling of an integral negative zero (`-0`, `-0e5`; not
// `-0.0`), outside strings. Legacy keeps it as -0 when it reads the literal
// as JSON, but in loop code its prepared parser, when it accepts the literal
// (no string-valued member, every number in range), reads it as Nift: 0.
// The CP12 #11 class; refused there (context:negative-zero).
static bool integral_negative_zero(std::string_view json) {
    bool quoted = false;
    for (std::size_t k = 0; k < json.size(); ++k) {
        const char c = json[k];
        if (quoted) {
            if (c == '\\') ++k;
            else if (c == '"') quoted = false;
        } else if (c == '"') {
            quoted = true;
        } else if (c == '-' && k + 1 < json.size() && json[k + 1] == '0' &&
                   (k + 2 >= json.size() || (json[k + 2] != '.' && (json[k + 2] < '0' || json[k + 2] > '9')))) {
            return true;
        }
    }
    return false;
}

bool UnitCompiler::object_literal(const Node& node, std::uint16_t dst) {
    // CP15 W9 (J-Q1, review R3): legacy reads a `{...}` literal whose text is
    // JSON with its strict JSON reader (exact number spellings, JSON escapes)
    // and evaluates any other as a Nift literal. The literal's text is parsed
    // here, at compile time, with the same reader (natives::json_parse): JSON
    // -> that constant value. Not JSON -> compiled below as a Nift literal,
    // which is legacy's fallback; the CP10/CP11 guards (opaque statements,
    // number-literal, string-literal, ...) refuse what they refused before.
    {
        Value parsed;
        std::string error;
        const natives::Context context;
        if (natives::json_parse(context, text(node), parsed, error) == natives::Status::Ok) {
            if (loop_reachable() && integral_negative_zero(text(node)) && legacy_prepared_parse_accepts(text(node)))
                return fail(node, "context:negative-zero");
            std::uint16_t k = 0;
            if (!fb_->constant(parsed, k)) return builder_ok();
            fb_->set_offset(node.begin);
            fb_->emit(OpCode::LoadK, dst, static_cast<std::uint16_t>(k & ~kConstantBit));
            return true;
        }
    }
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
        if (!no_alias(prop.b, prop)) return false;
        if (!expr_any(prop.b, value)) return false;
        fb_->set_offset(prop.begin);
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
        // The fused form takes the same kind check as the plain comparison
        // (found by vm_context_consistency.py: `if (p > 1)` with a string p).
        const bool ordering = node.op != Op::Eq && node.op != Op::Ne;
        if (ordering && !ordering_kinds_ok(kinds(node.a), kinds(node.b)) &&
            !unknown_defers(kinds(node.a) | kinds(node.b)))
            return fail(node, std::string("type:") + op_name(node.op));
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
