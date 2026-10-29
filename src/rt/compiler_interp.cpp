// Lowering of an interpolated string literal (CP15B Stage A).
//
// The parser represents `"a$[expr]b"` as a String node whose children are
// StringPart nodes (literal text) and expression nodes. Evaluation is
// left-to-right and exactly once: each expression is compiled into its own
// register, converted to text with the CP14 to_string native, and appended to
// a single result register. If an expression fails, later expressions have not
// run and earlier side effects are already observable, matching legacy.
//
// Only expressions whose local scalar proof succeeds are admitted. The unit
// kind inference does not visit String children (infer*.cpp belongs to CP12B),
// so interpolation_kind() proves the common scalar forms from bindings,
// literals, operators, native result rules and simple user-function returns.
// Aggregates use interpolation:non-scalar; unproven or mixed families use
// interpolation:unknown-type (a tracked coverage gap, not unsupported syntax).
#include "rt/compiler_interp.h"

#include "rt/compiler_expr.h"

#include "rt/builtin_bind.h"
#include "rt/infer_types.h"
#include "rt/native.h"
#include "rt/string.h"

#include <charconv>
#include <cstdint>

namespace nift::rt::detail {

namespace {

KindSet native_result_kinds(const natives::BuiltinSpec& spec) {
    if (spec.rule != natives::ResultRule::Fixed) return kKindUnknown;
    const KindSet result = static_cast<KindSet>(spec.result);
    if (result == static_cast<KindSet>(natives::kAny)) return kKindUnknown;
    return result;
}

KindSet scalar_only(KindSet kinds) {
    if (kinds & kKindUnknown) return kKindUnknown;
    if (kinds & kKindAggregate) return kKindAggregate;
    if (!(kinds & kKindScalar)) return kKindUnknown;
    return static_cast<KindSet>(kinds & kKindScalar);
}

bool literal_scalar(const Ast& ast, NodeId id) {
    const NodeKind kind = ast.at(id).kind;
    if (kind == NodeKind::String) return ast.at(id).count == 0;  // no nested interpolation effects
    return kind == NodeKind::Number || kind == NodeKind::True || kind == NodeKind::False ||
           kind == NodeKind::Null;
}

// `$[[1,2][0]]`: the index expression is refused by the compiler because the
// unit inference does not visit String children. If the array and position are
// literals and every item is a side-effect-free scalar literal, evaluate the
// item directly; this preserves evaluation (nothing else in the array can have
// an effect).
bool literal_index_item(const Ast& ast, NodeId id, NodeId& item) {
    const Node& index = ast.at(id);
    if (index.kind != NodeKind::Index) return false;
    const Node& array = ast.at(index.a);
    const Node& position = ast.at(index.b);
    if (array.kind != NodeKind::Array || position.kind != NodeKind::Number) return false;
    std::int64_t at = 0;
    const std::string_view digits = ast.text(position);
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), at);
    if (parsed.ec != std::errc() || parsed.ptr != digits.data() + digits.size() || at < 0 ||
        static_cast<std::uint64_t>(at) >= array.count)
        return false;
    const NodeId* items = ast.list(array);
    for (std::uint32_t k = 0; k < array.count; ++k)
        if (!literal_scalar(ast, items[k])) return false;
    item = items[static_cast<std::size_t>(at)];
    return true;
}

} // namespace

void UnitCompiler::collect_returns(NodeId id, std::vector<NodeId>& out) const {
    if (id == kNoNode) return;
    const Node& node = ast_.at(id);
    if (node.kind == NodeKind::Return) { out.push_back(id); return; }
    // A nested callable's returns belong to that callable.
    if (node.kind == NodeKind::FnDecl || node.kind == NodeKind::Lambda) return;
    collect_returns(node.a, out);
    collect_returns(node.b, out);
    collect_returns(node.c, out);
    const NodeId* list = ast_.list(node);
    for (std::uint32_t k = 0; k < node.count; ++k) collect_returns(list[k], out);
}

KindSet UnitCompiler::interpolation_function_kind(std::size_t f,
                                                       std::unordered_set<std::size_t>& active) const {
    if (!active.insert(f).second) return kKindUnknown;
    const Node& fn = ast_.at(plan_.functions[f]);
    std::vector<NodeId> returns;
    collect_returns(fn.b, returns);
    KindSet result = 0;
    for (NodeId id : returns) {
        const Node& ret = ast_.at(id);
        const KindSet kind = (ret.flags & kFlagBare) || ret.a == kNoNode
                                      ? kKindNull
#if defined(NIFT_INTERP_MUTANT) && NIFT_INTERP_MUTANT == 4
                                      : interpolation_kind(ret.a, active);
#else
                                      : interpolation_literal_kind(ret.a);
#endif
        result |= kind;
    }
    if (returns.empty() || infer_detail::block_falls_through(ast_, fn.b)) result |= kKindNull;
    active.erase(f);
    if (result & kKindUnknown) return kKindUnknown;
    return result == 0 ? kKindUnknown : result;
}

KindSet UnitCompiler::interpolation_kind(NodeId id, std::unordered_set<std::size_t>& active) const {
    return interpolation_kind_impl(id, active, false);
}

KindSet UnitCompiler::interpolation_literal_kind(NodeId id) const {
    std::unordered_set<std::size_t> active;
    return interpolation_kind_impl(id, active, true);
}

KindSet UnitCompiler::interpolation_kind_impl(NodeId id, std::unordered_set<std::size_t>& active,
                                                   bool literal_only) const {
    if (id == kNoNode) return kKindUnknown;
    const Node& node = ast_.at(id);
    switch (node.kind) {
        case NodeKind::Number: return kKindNumber;
        case NodeKind::String: return kKindString;  // includes nested interpolation
        case NodeKind::True: case NodeKind::False: return kKindBool;
        case NodeKind::Null: return kKindNull;
        case NodeKind::Ident: {
            if (literal_only) return kKindUnknown;  // never use the caller's scope for a callee return
            const Binding* binding = scopes_.find(text(node));
            // CP25-R: a function's read of a top-level global is resolved as
            // every other read of it (find_binding: only the globals the
            // plan resolves for this function); its binding type is main's.
            if (!binding && !session_) {
                const Binding* global = find_binding(text(node));
                if (global != nullptr && global->global) binding = global;
            }
            if (!binding) return kKindUnknown;
            return scalar_only(binding->decl_kinds);
        }
        case NodeKind::Unary: {
            if (node.op == Op::Not) return kKindBool;
            const KindSet operand = interpolation_kind_impl(node.a, active, literal_only);
            if (operand == kKindNumber) return kKindNumber;
            return (operand & kKindAggregate) ? kKindAggregate : kKindUnknown;
        }
        case NodeKind::Binary: {
            const KindSet left = interpolation_kind_impl(node.a, active, literal_only);
            const KindSet right = interpolation_kind_impl(node.b, active, literal_only);
            if ((left | right) & kKindUnknown) return kKindUnknown;
            if ((left | right) & kKindAggregate) return kKindAggregate;
            switch (node.op) {
                case Op::Add:
                    if (left == kKindNumber && right == kKindNumber) return kKindNumber;
                    if ((left & kKindString) || (right & kKindString)) return kKindString;
                    return kKindUnknown;
                case Op::Sub: case Op::Mul: case Op::Div: case Op::Mod:
                    return (left == kKindNumber && right == kKindNumber) ? kKindNumber : kKindUnknown;
                case Op::Eq: case Op::Ne:
                    return kKindBool;
                case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge:
                    return ((left == kKindNumber && right == kKindNumber) ||
                            (left == kKindString && right == kKindString))
                               ? kKindBool
                               : kKindUnknown;
                default: return kKindUnknown;
            }
        }
        case NodeKind::Logical: return kKindBool;
        case NodeKind::Coalesce:
        case NodeKind::Ternary: {
            if (node.kind == NodeKind::Coalesce) {
                const KindSet left = interpolation_kind_impl(node.a, active, literal_only);
                const KindSet right = interpolation_kind_impl(node.b, active, literal_only);
                return scalar_only(static_cast<KindSet>(left | right));
            }
            const KindSet left = interpolation_kind_impl(node.b, active, literal_only);
            const KindSet right = interpolation_kind_impl(node.c, active, literal_only);
            return scalar_only(static_cast<KindSet>(left | right));
        }
        case NodeKind::Index: {
            if (literal_only) return kKindUnknown;
            // A literal array and literal position are common in nested
            // interpolation (`$[[1,2][0]]`); prove that element's kind.
            const Node& array = ast_.at(node.a);
            const Node& position = ast_.at(node.b);
            if (array.kind != NodeKind::Array || position.kind != NodeKind::Number) return kKindUnknown;
            std::int64_t at = 0;
            const std::string_view digits = text(position);
            const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), at);
            if (parsed.ec != std::errc() || parsed.ptr != digits.data() + digits.size() || at < 0 ||
                static_cast<std::uint64_t>(at) >= array.count)
                return kKindUnknown;
            return interpolation_kind_impl(ast_.list(array)[static_cast<std::size_t>(at)], active, literal_only);
        }
        case NodeKind::Array: case NodeKind::Object: return kKindAggregate;
        case NodeKind::Call: {
            if (literal_only) return kKindUnknown;
            const Node& callee = ast_.at(node.a);
            if (callee.kind == NodeKind::Ident) {
                const std::string_view name = text(callee);
                const auto fn = plan_.index.find(name);
                if (fn != plan_.index.end()) return interpolation_function_kind(fn->second - 1u, active);
                const natives::BuiltinSpec* spec = bind_function(name);
                if (spec) return native_result_kinds(*spec);
                return kKindUnknown;  // print, user callable value, unknown
            }
            if (callee.kind == NodeKind::Member) {
                const KindSet receiver = interpolation_kind(callee.a, active);
                if (receiver & kKindUnknown) return kKindUnknown;
                const natives::BuiltinSpec* spec = nullptr;
                const std::string_view name = text(ast_.at(callee.b));
                const MethodBinding binding = bind_method(receiver, name, spec);
                if (binding == MethodBinding::Bound && spec) return native_result_kinds(*spec);
                return kKindUnknown;
            }
            return kKindUnknown;
        }
        default: return kKindUnknown;
    }
}

bool UnitCompiler::interpolation(const Node& node, std::uint16_t dst) {
    if (node.count == 0) return fail(node, "interpolation:internal");
    const std::uint16_t mark = fb_->top();
    std::uint16_t result = 0;
    if (!temp(result)) return false;

    std::uint16_t empty = 0;
    if (!fb_->constant(make_string(""), empty)) return builder_ok();
    fb_->emit(OpCode::LoadK, result, static_cast<std::uint16_t>(empty & ~kConstantBit));

    const NodeId* parts = ast_.list(node);
    for (std::uint32_t k = 0; k < node.count; ++k) {
        NodeId part_id = parts[k];
        const Node& part = ast_.at(part_id);
        if (part.kind == NodeKind::StringPart) {
            std::uint16_t constant = 0;
            if (!fb_->constant(make_string(ast_.text(part)), constant)) return builder_ok();
            fb_->set_offset(part.begin);
            fb_->emit(OpCode::AddAssign, result, constant);
            continue;
        }

        NodeId resolved = kNoNode;
        if (literal_index_item(ast_, part_id, resolved)) part_id = resolved;

        std::unordered_set<std::size_t> active;
        const KindSet kinds_here = interpolation_kind(part_id, active);
        if (kinds_here & kKindHandle) return fail(part, "host-handle-flow");
        if (kinds_here & kKindUnknown) return fail(part, "interpolation:unknown-type");
        if (kinds_here & kKindAggregate) return fail(part, "interpolation:non-scalar");
        const KindSet scalar = scalar_only(kinds_here);
        if (scalar == kKindUnknown) return fail(part, "interpolation:unknown-type");
        const bool number = (scalar & ~kKindNumber) == 0;
        const bool string = scalar == kKindString;
        const bool boolean = scalar == kKindBool;
        const bool null = scalar == kKindNull;
        if (!number && !string && !boolean && !null) return fail(part, "interpolation:unknown-type");

        const natives::BuiltinSpec* spec = nullptr;
        const MethodBinding binding = bind_method(scalar, "to_string", spec);
        if (binding != MethodBinding::Bound && binding != MethodBinding::Dead)
            return fail(part, "interpolation:unknown-type");

        std::uint16_t piece = 0;
        if (!temp(piece)) return false;
        if (!expr_to(part_id, piece)) return false;
#if defined(NIFT_INTERP_MUTANT) && NIFT_INTERP_MUTANT == 3
        {
            // negative control: evaluate the embedded expression twice
            std::uint16_t again = 0;
            if (!temp(again) || !expr_to(part_id, again)) return false;
            fb_->release_to(static_cast<std::uint16_t>(piece + 1u));
        }
#endif
#if defined(NIFT_INTERP_MUTANT) && NIFT_INTERP_MUTANT == 1
        if (kinds_here & kKindFrac) {
            // negative control: render every double as 0
            std::uint16_t wrong = 0;
            if (!fb_->constant(make_string("0"), wrong)) return builder_ok();
            fb_->emit(OpCode::LoadK, piece, static_cast<std::uint16_t>(wrong & ~kConstantBit));
        }
#endif

        if (!string) {
            fb_->set_offset(ast_.at(part_id).begin);
            const std::size_t at = fb_->emit(
                OpCode::CallNative, piece,
                static_cast<std::uint16_t>(kFirstBuiltin + static_cast<std::uint16_t>(spec->id)), 1);
            fb_->note(at, "to_string");
        }
        fb_->set_offset(ast_.at(part_id).begin);
        fb_->emit(OpCode::AddAssign, result, piece);
        fb_->release_to(static_cast<std::uint16_t>(result + 1u));
    }

    if (dst != result) fb_->emit(OpCode::Move, dst, result);
    fb_->release_to(mark);
    return builder_ok();
}

} // namespace nift::rt::detail
