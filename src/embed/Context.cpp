#include "nift/context.h"

#include "Json.h"
#include "ValueInternal.h"

#include <string>
#include <string_view>
#include <utility>

namespace nift {

void Context::set_title(std::string title) {
    title_ = title;
    bindings_["title"] = Value(std::move(title));
}

bool Context::set(std::string name, Value value) {
    if (!detail::valid_binding_identifier(name) || detail::structural_builtin_name(name)) return false;
    bindings_[std::move(name)] = std::move(value);
    return true;
}
bool Context::set(std::string name, std::string value) {
    return set(std::move(name), Value(std::move(value)));
}
bool Context::set(std::string name, int value) {
    return set(std::move(name), Value(value));
}
bool Context::set(std::string name, bool value) {
    return set(std::move(name), Value(value));
}
bool Context::set_json(std::string name, std::string_view json_text) {
    if (!detail::valid_binding_identifier(name) || detail::structural_builtin_name(name)) return false;
    Value value;
    std::string error;
    if (!json::Document::parse(std::string(json_text), ValueAccess::doc(value), error)) return false;
    bindings_[std::move(name)] = std::move(value);
    return true;
}

} // namespace nift
