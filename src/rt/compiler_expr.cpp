// rt-lint: cold-file expression lowering runs at compile time; no runtime string comparisons here
#include "rt/compiler_expr.h"
#include "rt/compiler_binary.h"
#include "rt/alias.h"
#include "rt/atom.h"
#include "rt/literal.h"
#include "rt/native.h"
#include "rt/natives/json_methods.h"
#include "rt/prepared_parse.h"
#include "rt/module_link.h"
namespace nift::rt::detail {
namespace {
// `type(x) != "string"` (left of an OR) or `type(x) == "string"` (left of an
// AND): when it falls through, x is a string for the rest of the expression.
bool string_guard_ident(const Ast& ast, NodeId id, NodeId& ident, bool positive) {
    if (id == kNoNode) return false;
    const Node& n = ast.at(id);
    if (n.kind == NodeKind::Logical && n.op == (positive ? Op::And : Op::Or))
        return string_guard_ident(ast, n.a, ident, positive);
    if (n.kind != NodeKind::Binary || n.op != (positive ? Op::Eq : Op::Ne)) return false;
    const Node& call = ast.at(n.a);
    const Node& label = ast.at(n.b);
    if (call.kind != NodeKind::Call || call.count != 1 || label.kind != NodeKind::String ||
        ast.text(label) != R"("string")") return false;
    const Node& callee = ast.at(call.a);
    if (callee.kind != NodeKind::Ident || ast.text(callee) != "type") return false;
    const Node& arg = ast.at(ast.list(call)[0]);
    if (arg.kind != NodeKind::Ident) return false;
    ident = ast.list(call)[0];
    return true;
}


} // namespace

bool UnitCompiler::temp(std::uint16_t& reg) { return fb_->alloc(reg) || builder_ok(); }

bool UnitCompiler::expr_any(NodeId id, std::uint16_t& operand) {
    const Node& node = ast_.at(id);
    if (node.kind == NodeKind::Ident) {
        std::uint16_t source = 0;
        if (!local_register(node, source)) return false;
        if (!(kinds(id) & kKindAtomic)) { operand = source; return true; }
        return atomic_read(id, source, operand);
    }
    Value value;
    bool is_constant = false;
    if (!constant_of(node, value, is_constant)) return false;
    if (is_constant) return fb_->constant(value, operand) || builder_ok();
    return temp(operand) && expr_to(id, operand);
}

bool UnitCompiler::reg_any(NodeId id, std::uint16_t& reg) {
    const Node& node = ast_.at(id);
    if (node.kind == NodeKind::Ident) {
        std::uint16_t source = 0;
        if (!local_register(node, source)) return false;
        if (!(kinds(id) & kKindAtomic)) { reg = source; return true; }
        return atomic_read(id, source, reg);
    }
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
        case NodeKind::Ident: {  // a temporary the read needed (a global's) is freed: `[g, x]` (CP22 fix)
            std::uint16_t reg = 0, mark = fb_->top();
            if (!local_register(node, reg)) return false;
            if (reg != dst) { fb_->emit(OpCode::Move, dst, reg); if (reg >= mark) fb_->release_to(mark); }
            return true;
        }
        case NodeKind::Lambda: return lambda_value(node, dst);
        case NodeKind::Binary: return binary(node, dst);
        case NodeKind::Logical: return logical(node, dst);
        case NodeKind::Coalesce: return coalesce(node, dst);
        case NodeKind::Ternary: return ternary(node, dst);
        case NodeKind::Unary: return unary(node, dst);
        case NodeKind::Assign: case NodeKind::CompoundAssign:
            return !invocation_write(id) && assignment_expr(node, dst);
        case NodeKind::IncDec: return !invocation_write(id) && inc_dec_expr(node, dst);
        case NodeKind::Call:
#if !(defined(NIFT_C145_MUTANT) && NIFT_C145_MUTANT == 3)  // mutant 3: the write-family fallback dropped
            // C145 (D-102): write/write_line/write_val with a direct `$[` literal: legacy interpolates for an
            // ofstream and passes it raw otherwise, decided by the receiver at run time; refused before anything runs.
            if (node.flags & kFlagAmbiguousLiteralArg) return fail(node, "host-literal-arg");
#endif
            return call(node, dst);
        case NodeKind::Await: return await_expr(node, dst);  // CP22 (compiler_coro.cpp)
        case NodeKind::Member: return member(node, dst);
        case NodeKind::Index: return index(node, dst);
        case NodeKind::Array: return array_literal(node, dst);
        case NodeKind::Object: return object_literal(node, dst);
        case NodeKind::String: return interpolation(node, dst);
        default: return fail(node, std::string("expression:") + node_kind_name(node.kind));
    }
}


bool UnitCompiler::logical(const Node& node, std::uint16_t dst) {
    if (node.op != Op::And && node.op != Op::Or) return fail(node, "logical-operator");
    if (!infix_left_ok(node)) return false;
    if (!expr_to(node.a, dst)) return false;
    const bool is_and = node.op == Op::And;
    const std::size_t decided = fb_->emit(is_and ? OpCode::JmpFalse : OpCode::JmpTrue, dst);
    const int guarded = is_and ? guarded_string_contains(node, dst) : -1;
    if (guarded == 0) return false;
    if (guarded < 0) {
        NodeId guard_ident = kNoNode;
        const auto saved_conversion = guarded_conversions_.size();
        NodeId typed_source = kNoNode;
        KindSet typed_kind = 0;
        if (is_and && type_call_positive_guard(ast_.at(node.a), typed_source, typed_kind)) {
            guarded_conversions_.emplace_back(typed_source, typed_kind);
        } else if (string_guard_ident(ast_, node.a, guard_ident, is_and)) {
            const Binding* guarded = find_binding(text(ast_.at(guard_ident)));
            // The binding must be stable: no assignment anywhere in the body.
            if (guarded && guarded->decl_node != kNoNode && !guarded->cell && !guarded->global &&
                !assigned_.count(text(ast_.at(guard_ident))))
                guarded_conversions_.emplace_back(guarded->decl_node, kKindString);
        }
        const bool rhs_ok = expr_to(node.b, dst);
        guarded_conversions_.resize(saved_conversion);
        if (!rhs_ok) return false;
    }
    fb_->emit(OpCode::Truth, dst, dst);
    const std::size_t done = fb_->emit(OpCode::Jmp);
    fb_->patch(decided, fb_->here());
    fb_->emit(OpCode::LoadBool, dst, is_and ? 0 : 1);
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
    const bool nested = numeric_diagnostic_tree(node);
    if (!nested && node.op != Op::Not && !kinds_within(node.a, kKindNumber, node, "type:unary")) return false;
    // Prepared negation of zero differs from the canonical path (D-018).
    if (node.op == Op::Neg && loop_reachable() && (kinds(node.a) & (kKindInt | kKindUnknown)))
        return fail(node, "context:negative-zero");
    const std::uint16_t mark = fb_->top();
    std::uint16_t operand = 0;
    if (!reg_any(node.a, operand)) return false;
    if (nested && node.op != Op::Not && !kinds_within(node.a, kKindNumber, node, "type:unary")) return false;
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
    // Reserve contiguous payload; Cell reads need scratch registers between items.
    for (std::uint32_t k = 0; k < node.count; ++k) {
        std::uint16_t reg = 0;
        if (!temp(reg)) return false;
    }
    const auto scratch = fb_->top();
    for (std::uint32_t k = 0; k < node.count; ++k) {
        if (ast_.at(items[k]).kind == NodeKind::Spread) return fail(ast_.at(items[k]), "spread");
#if defined(NIFT_SESSION_ADMISSION_MUTANT) && NIFT_SESSION_ADMISSION_MUTANT == 22
        // M22: a gap in the payload; the former reserve-only defect is masked
        // by CP22's accepted Ident scratch release and is not a detection.
        if (!expr_to(items[k], static_cast<std::uint16_t>(base + 2u * k))) return false;
#else
        if (!expr_to(items[k], static_cast<std::uint16_t>(base + k))) return false;
#endif
        fb_->release_to(scratch);
    }
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
            // B2 (b2-refrep.md section 17, review H1). Invariant: a compiled
            // constant is a description, not a runtime instance. Arrays and
            // objects are references, so an aggregate constant is a template
            // and every evaluation loads a new deep copy (budget reserved
            // first): the outer value and every nested array or object in it
            // are fresh (recursive freshness), never the shared pool object.
            const bool aggregate = parsed.is(Kind::Array) || parsed.is(Kind::Object);
            fb_->emit(aggregate ? OpCode::LoadKFresh : OpCode::LoadK, dst,
                      static_cast<std::uint16_t>(k & ~kConstantBit));
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
    if (node.kind == NodeKind::Logical && node.op == Op::And) {
        if (!false_jump(node.a, jumps)) return false;
        const auto saved = guarded_conversions_.size();
        NodeId subject = kNoNode;
        KindSet narrowed = 0;
        if (plan_.linked && plan_.linked->locate(node.begin).module > 0 &&
            type_call_positive_guard(ast_.at(node.a), subject, narrowed))
            guarded_conversions_.emplace_back(subject, narrowed);
        const bool ok = false_jump(node.b, jumps);
        guarded_conversions_.resize(saved);
        return ok;
    }
    const std::uint16_t mark = fb_->top();
    const OpCode fused = node.kind == NodeKind::Binary ? fused_not_jump(node.op) : OpCode::Nop;
    if (fused != OpCode::Nop) {
        if (!comparison_chain_ok(node)) return false;
        // The fused form takes the same kind check as the plain comparison
        // (found by vm_context_consistency.py: `if (p > 1)` with a string p).
        const bool ordering = node.op != Op::Eq && node.op != Op::Ne;
        const bool order_check = ordering && !ordering_kinds_ok(kinds(node.a), kinds(node.b)) &&
            !unknown_defers(kinds(node.a) | kinds(node.b));
        if (order_check && !order_guarded(node)) return fail(node, op_type_reason(node.op));
        std::uint16_t left = 0, right = 0;
#if defined(NIFT_D2_MUTANT) && NIFT_D2_MUTANT == 2
        if (!expr_any(node.a, left) || !expr_any(node.b, right)) return false;
#else
        if (!expr_any(node.a, left) || !retain_receiver(left, left < mark, node.b) || !expr_any(node.b, right))
            return false;  // D2 family A (fused comparison)
#endif
        if (order_check && !order_guard(node, left, right)) return false;
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
