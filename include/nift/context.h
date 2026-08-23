#pragma once

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <unordered_map>

#include "nift/value.h"

namespace nift {

class Engine;

namespace detail {

// A pre-supplied binding name must be a valid Nift identifier, exactly like
// the names @json accepts.
inline bool valid_binding_identifier(const std::string& name) {
    if (name.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) return false;
    return std::all_of(name.begin() + 1, name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    });
}

// Structural built-ins describe the render's own geometry and feed @pathto and
// dependency spelling; they must never be shadowed by host values.
inline bool structural_builtin_name(const std::string& name) {
    static const std::set<std::string> names = {
        "name", "content-path", "output-path", "template-path", "loop",
    };
    return names.count(name) != 0;
}

} // namespace detail

// Per-render state: the page identity and request-scoped value bindings that
// vary from request to request. Long-lived capabilities (loaders, root, default
// values) belong to Engine; bindings set here override Engine defaults for this
// render only.
class Context {
public:
    void set_page_name(std::string name) { page_name_ = std::move(name); }
    void set_title(std::string title) { title_ = std::move(title); }

    // Request-scoped value bindings, resolved before Engine defaults, @json
    // bindings, contracts and built-in metadata. Returns false if the name is
    // not a valid binding identifier or is a structural built-in (name,
    // content-path, output-path, template-path, loop).
    bool set(std::string name, Value value);
    bool set(std::string name, std::string value);
    bool set(std::string name, int value);
    bool set(std::string name, bool value);
    bool set_json(std::string name, std::string_view json_text);

private:
    friend class Engine;
    std::string page_name_;
    std::string title_;
    std::unordered_map<std::string, Value> bindings_;
};

inline bool Context::set(std::string name, Value value) {
    if (!detail::valid_binding_identifier(name) || detail::structural_builtin_name(name)) return false;
    bindings_[std::move(name)] = std::move(value);
    return true;
}
inline bool Context::set(std::string name, std::string value) {
    return set(std::move(name), Value(std::move(value)));
}
inline bool Context::set(std::string name, int value) {
    return set(std::move(name), Value(value));
}
inline bool Context::set(std::string name, bool value) {
    return set(std::move(name), Value(value));
}
inline bool Context::set_json(std::string name, std::string_view json_text) {
    if (!detail::valid_binding_identifier(name) || detail::structural_builtin_name(name)) return false;
    Value value;
    std::string error;
    if (!json::Document::parse(std::string(json_text), value.document(), error)) return false;
    bindings_[std::move(name)] = std::move(value);
    return true;
}

} // namespace nift
