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
    out->historical_callable = out->untracked_callable = false;
    out->elem = session_shape(type->elem);
    for (auto& item : out->items) item = session_shape(item);
    for (auto& field : out->fields) field.second = session_shape(field.second);
    return out;
}
infer_detail::TyRef session_historical_shape(const infer_detail::TyRef& type) {
    if (!type) return {};
    auto out = std::make_shared<infer_detail::Ty>(*type);
    out->historical_callable = (out->kinds & kKindFunction) != 0;
    out->untracked_callable = false;
    out->elem = session_historical_shape(type->elem);
    for (auto& item : out->items) item = session_historical_shape(item);
    for (auto& field : out->fields) field.second = session_historical_shape(field.second);
    return out;
}
bool session_proved_shape(const infer_detail::TyRef& type) {
    if (!type) return false;
#if !defined(NIFT_SESSION_ADMISSION_MUTANT) || NIFT_SESSION_ADMISSION_MUTANT != 18
    if (type->any || (type->kinds & kKindUnknown)) return false;
#endif
    if (type->elem && !session_proved_shape(type->elem)) return false;
    for (const auto& item : type->items) if (!session_proved_shape(item)) return false;
    for (const auto& field : type->fields) if (!session_proved_shape(field.second)) return false;
    return true;  // bottom is an empty result set, never Unknown
}
infer_detail::TyRef session_current_shape(const infer_detail::TyRef& type) {
#if defined(NIFT_SESSION_ADMISSION_MUTANT) && (NIFT_SESSION_ADMISSION_MUTANT == 16 || \
    NIFT_SESSION_ADMISSION_MUTANT == 17)
    return type;
#endif
    if (!type) return {};
    auto elem = session_current_shape(type->elem);
    std::shared_ptr<infer_detail::Ty> out;
    const auto change = [&]() -> infer_detail::Ty& {
        if (!out) out = std::make_shared<infer_detail::Ty>(*type);
        return *out;
    };
    if ((type->kinds & kKindFunction) && !(type->kinds & kKindUnknown) && !type->any &&
        type->callables.empty() && !type->historical_callable &&
        !type->untracked_callable) change().untracked_callable = true;
    if (elem != type->elem) change().elem = std::move(elem);
    for (std::size_t k = 0; k < type->items.size(); ++k) {
        auto item = session_current_shape(type->items[k]);
        if (item != type->items[k]) change().items[k] = std::move(item);
    }
    for (const auto& field : type->fields) {
        auto value = session_current_shape(field.second);
        if (value != field.second) change().fields[field.first] = std::move(value);
    }
    return out ? infer_detail::TyRef(out) : type;
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
