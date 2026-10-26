#include "rt/session_shape.h"

// rt-lint: cold-file bounded immutable type-graph contract adaptation
namespace nift::rt {
namespace {
infer_detail::TyRef global_shape(const infer_detail::TyRef& type, bool number_binding, bool contents) {
    if (!type) return {};
    auto out = std::make_shared<infer_detail::Ty>(*type);
    // Int bindings cannot later hold a double. Double bindings and untyped
    // aggregate elements can hold either numeric kind. Widen before initial
    // compilation, never after a reader has been published.
    if ((number_binding || contents || (out->kinds & kKindFrac)) && (out->kinds & kKindNumber))
        out->kinds |= kKindNumber;
    out->tuple = false; out->items.clear();
    out->elem = global_shape(type->elem, false, true);
    if ((out->kinds & kKindArray) && out->elem && out->elem->kinds == 0)
        out->elem = infer_detail::any_ty();
    for (auto& field : out->fields) field.second = global_shape(field.second, false, true);
    return out;
}
} // namespace
infer_detail::TyRef session_global_shape(const infer_detail::TyRef& type, bool number_binding) {
    return global_shape(type, number_binding, false);
}
infer_detail::TyRef session_shape(const infer_detail::TyRef& type) {
    if (!type) return {};
    auto out = std::make_shared<infer_detail::Ty>(*type);
    out->callables.clear(); out->struct_id = -1;
    out->elem = session_shape(type->elem);
    for (auto& item : out->items) item = session_shape(item);
    for (auto& field : out->fields) field.second = session_shape(field.second);
    return out;
}
bool session_shape_within(const infer_detail::TyRef& value, const infer_detail::TyRef& bound) {
    if (!value || !bound || bound->any) return true;
    if (value->any || (value->kinds & ~bound->kinds)) return false;
    if ((value->kinds & kKindArray) && !session_shape_within(value->elem, bound->elem)) return false;
    if (value->kinds & kKindObject) for (const auto& field : value->fields) {
        const auto allowed = bound->fields.find(field.first);
        if (allowed == bound->fields.end() || !session_shape_within(field.second, allowed->second)) return false;
    }
    return true;
}
} // namespace nift::rt
