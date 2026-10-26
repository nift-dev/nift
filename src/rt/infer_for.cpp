#include "rt/infer_for.h"
#include "rt/session_mutation_shape.h"
// rt-lint: cold-file shared for-loop binding and persistent alias type effects
namespace nift::rt::infer_detail {
void Inference::for_statement(const Node& n) {
    const Node& header = ast_.at(n.a);
    const NodeId iterable = header.a;
    const TyRef source = expr(iterable);
    const bool pair = (header.flags & kFlagPairBinding) != 0;
    const bool array = (source->kinds & kKindArray) != 0;
    const bool object = (source->kinds & kKindObject) != 0;
    // The compiler refuses an unknown/union source before executing;
    // these loops either raise at their header (known non-iterable or
    // the wrong form) or are compiled as a normal array/object loop.
    const bool normal = !(source->kinds & kKindUnknown) && !(array && object) &&
                        ((pair && object) || (!pair && array));
    TyRef value = kinds_ty(kKindScalar);
    if (normal) {
        value = pair ? element_of(source, nullptr, true) : element_of(source, nullptr, false);
        if (!value || value->kinds == 0) value = kinds_ty(kKindUnknown);
        result_.elements[iterable] |= value->kinds;
    }
    const NodeId source_root = session_ ? session_mutation_root(ast_, iterable) : kNoNode;
    const auto* source_var = source_root == kNoNode ? nullptr : find(ast_.text(ast_.at(source_root)));
    const auto source_slot = source_var ? static_cast<std::size_t>(source_var - vars_.data()) :
        static_cast<std::size_t>(-1);
    const NodeId* names = ast_.list(header);
    scopes_.emplace_back();
    if (pair && header.count >= 2) {
        declare(ast_.text(ast_.at(names[0])), names[0], kinds_ty(kKindString));
        declare(ast_.text(ast_.at(names[1])), names[1], value);
    } else if (!pair && header.count >= 1) {
        declare(ast_.text(ast_.at(names[0])), names[0], value);
    }
    if (session_ && normal && source_slot != static_cast<std::size_t>(-1) &&
        (pair ? header.count >= 2 : header.count >= 1) && (value->kinds & kKindAggregate)) {
        const auto value_name = pair ? names[1] : names[0];
        const auto* alias = find(ast_.text(ast_.at(value_name)));
        const auto alias_slot = static_cast<std::size_t>(alias - vars_.data());
        if (alias_slot != source_slot) session_for_aliases_[alias_slot] = {source_slot, iterable};
    }
    auto loop = std::make_shared<Ty>();
    loop->kinds = kKindObject;
    loop->fields.emplace("index", kinds_ty(kKindInt));
    loop->fields.emplace("index0", kinds_ty(kKindInt));
    loop->fields.emplace("first", kinds_ty(kKindBool));
    loop->fields.emplace("last", kinds_ty(kKindBool));
    loop->fields.emplace("length", kinds_ty(kKindInt));
    declare("loop", header.a, loop);
    if (normal) statements(ast_.at(n.b));
    scopes_.pop_back();
}
} // namespace nift::rt::infer_detail
