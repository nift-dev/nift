#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
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

    // The generated output location of the current page, used by @pathto to
    // compute relative paths (and by the 404 rule for root-absolute paths).
    // Without it, @pathto has no path context and errors.
    void set_current_output(std::filesystem::path output) { current_output_ = std::move(output); }

    // set_title and set("title", ...) write the same per-render title slot:
    // both override an Engine "title" default; the most recent write wins.
    void set_title(std::string title);

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
    std::filesystem::path current_output_;
    std::unordered_map<std::string, Value> bindings_;
};

} // namespace nift
