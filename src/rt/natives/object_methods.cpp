#include "rt/natives/object_methods.h"

#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/natives/support.h"
#include "rt/string.h"

#include <algorithm>
#include <vector>

namespace nift::rt::natives {

namespace {

bool receiver(const Value* args, std::uint32_t argc) { return argc > 0 && args[0].is(Kind::Object); }

Status internal(std::string& error, const char* what) {
    error = what;
    return Status::Internal;
}

std::string named(std::string_view name, const char* message) { return std::string(name) + message; }

enum class Listing : std::uint8_t { Keys, Values, Entries };

Status listing(const Value* args, std::uint32_t argc, Value& out, std::string& error, std::string_view name,
               Listing what) {
    if (!receiver(args, argc)) return internal(error, "listing receiver");
    if (!no_arguments(argc, name, error)) return Status::Error;
    const auto& entries = object_entries(args[0]);
    ArrayBuilder result(entries.size());
    const Atom key_atom = atom("key"), value_atom = atom("value");
    for (const auto& entry : entries) {
        if (what == Listing::Keys) { result.push(make_string(atom_text(entry.first))); continue; }
        if (what == Listing::Values) { result.push(entry.second); continue; }
        Value pair = make_object();
        object_set(pair, key_atom, make_string(atom_text(entry.first)));
        object_set(pair, value_atom, entry.second);
        result.push(static_cast<Value&&>(pair));
    }
    out = result.finish();
    return Status::Ok;
}

// Legacy merge_deep's recursive merge, at most 64 nested levels (the depth
// bounds the C++ recursion too).
bool merge_deep(const Value& left, const Value& right, Value& result, int level, std::string& error) {
    if (level > 64) { error = "merge_deep: maximum merge depth exceeded"; return false; }
    if (!(left.is(Kind::Object) && right.is(Kind::Object))) { result = right; return true; }
    Value merged = left;
    unshare(merged);  // before any reference into `right` is taken: `right` may share `left`'s object
    for (const auto& entry : object_entries(right)) {
        const Value* existing = object_get(merged, entry.first);
        if (existing && existing->is(Kind::Object) && entry.second.is(Kind::Object)) {
            const Value inner = *existing;  // copied out: object_set below invalidates `existing`
            Value child;
            if (!merge_deep(inner, entry.second, child, level + 1, error)) return false;
            object_set(merged, entry.first, static_cast<Value&&>(child));
        } else {
            object_set(merged, entry.first, entry.second);
        }
    }
    result = static_cast<Value&&>(merged);
    return true;
}

// pick / omit key list: legacy "<m>: expected one array of keys", "<m>: keys
// must be an array", "<m>: keys must be strings"; duplicates dropped, order
// kept.
Status key_patterns(const Value* args, std::uint32_t argc, std::string_view name, std::vector<std::string_view>& out,
                    std::string& error) {
    if (argc != 2) return fail(error, named(name, ": expected one array of keys"));
    if (!args[1].is(Kind::Array)) return fail(error, named(name, ": keys must be an array"));
    for (const Value& key : array_items(args[1])) {
        if (!key.is(Kind::String)) return fail(error, named(name, ": keys must be strings"));
        const std::string_view text = string_view_of(key);
        if (std::find(out.begin(), out.end(), text) == out.end()) out.push_back(text);
    }
    return Status::Ok;
}

bool key_matches(std::string_view pattern, std::string_view key) {
    return glob_magic(pattern) ? glob_match(pattern, key) : pattern == key;
}

} // namespace

bool glob_magic(std::string_view pattern) {
    bool escaped = false;
    for (char c : pattern) {
        if (escaped) { escaped = false; continue; }
        if (c == '\\') { escaped = true; continue; }
        if (c == '*' || c == '?') return true;
    }
    return false;
}

bool glob_match(std::string_view pattern, std::string_view name) {
    // A verbatim port, including legacy's handling of a backslash that does
    // not escape the next name byte (it then falls through to backtracking).
    if (!name.empty() && name[0] == '.' && (pattern.empty() || pattern[0] != '.')) return false;
    std::size_t p = 0, n = 0, star = std::string_view::npos, mark = 0;
    while (n < name.size()) {
        if (p < pattern.size() && pattern[p] == '\\') {
            if (p + 1 < pattern.size()) {
                ++p;
                if (pattern[p] == name[n]) { ++p; ++n; continue; }
            }
        } else if (p < pattern.size() && pattern[p] == '?') {
            ++p; ++n; continue;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++; mark = n; continue;
        } else if (p < pattern.size() && pattern[p] == name[n]) {
            ++p; ++n; continue;
        }
        if (star != std::string_view::npos) { p = star + 1; n = ++mark; continue; }
        return false;
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

Status obj_size(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (!receiver(args, argc)) return internal(error, "size receiver");
    if (!no_arguments(argc, "size", error)) return Status::Error;
    out = count_value(object_size(args[0]));
    return Status::Ok;
}

Status obj_empty(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (!receiver(args, argc)) return internal(error, "empty receiver");
    if (!no_arguments(argc, "empty", error)) return Status::Error;
    out = Value::boolean(object_size(args[0]) == 0);
    return Status::Ok;
}

Status obj_keys(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return listing(args, argc, out, error, "keys", Listing::Keys);
}

Status obj_values(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return listing(args, argc, out, error, "values", Listing::Values);
}

Status obj_entries(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return listing(args, argc, out, error, "entries", Listing::Entries);
}

Status obj_has(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (!receiver(args, argc)) return internal(error, "has receiver");
    if (argc != 2) return fail(error, "has: expected key");
    if (!args[1].is(Kind::String)) return fail(error, "has: key must be a string");
    Atom key;
    out = Value::boolean(AtomTable::current().find(string_view_of(args[1]), key) && object_get(args[0], key));
    return Status::Ok;
}

Status obj_get(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (!receiver(args, argc)) return internal(error, "get receiver");
    if (argc < 2 || argc > 3) return fail(error, "get: expected key and optional default");
    if (!args[1].is(Kind::String)) return fail(error, "get: key must be a string");
    Atom key;
    const Value* found = AtomTable::current().find(string_view_of(args[1]), key) ? object_get(args[0], key) : nullptr;
    // Legacy evaluates the default only when the key is missing; the compiler
    // admits a default argument only when evaluating it early cannot be
    // observed (design record, wiring rule W4).
    out = found ? *found : (argc == 3 ? args[2] : Value());
    return Status::Ok;
}

Status obj_merge(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (!receiver(args, argc)) return internal(error, "merge receiver");
    if (argc != 2) return fail(error, "merge: expected one object");
    if (!args[1].is(Kind::Object)) return fail(error, "merge: argument must be an object");
    Value result = args[0];
    unshare(result);  // first: `args[1]` may share the receiver's object (o.merge(o))
    for (const auto& entry : object_entries(args[1])) object_set(result, entry.first, entry.second);
    out = static_cast<Value&&>(result);
    return Status::Ok;
}

Status obj_merge_deep(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (!receiver(args, argc)) return internal(error, "merge_deep receiver");
    if (argc != 2) return fail(error, "merge_deep: expected one object");
    if (!args[1].is(Kind::Object)) return fail(error, "merge_deep: argument must be an object");
    Value result;
    if (!merge_deep(args[0], args[1], result, 0, error)) return Status::Error;
    out = static_cast<Value&&>(result);
    return Status::Ok;
}

Status obj_pick(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (!receiver(args, argc)) return internal(error, "pick receiver");
    std::vector<std::string_view> wanted;
    if (key_patterns(args, argc, "pick", wanted, error) != Status::Ok) return Status::Error;
    Value result = make_object();
    const auto& entries = object_entries(args[0]);
    // In pattern order; each key at most once (the first pattern that matches it).
    for (std::string_view pattern : wanted)
        for (const auto& entry : entries)
            if (!object_get(result, entry.first) && key_matches(pattern, atom_text(entry.first)))
                object_set(result, entry.first, entry.second);
    out = static_cast<Value&&>(result);
    return Status::Ok;
}

Status obj_omit(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (!receiver(args, argc)) return internal(error, "omit receiver");
    std::vector<std::string_view> wanted;
    if (key_patterns(args, argc, "omit", wanted, error) != Status::Ok) return Status::Error;
    Value result = make_object();
    for (const auto& entry : object_entries(args[0])) {
        bool drop = false;
        for (std::string_view pattern : wanted)
            if (key_matches(pattern, atom_text(entry.first))) { drop = true; break; }
        if (!drop) object_set(result, entry.first, entry.second);
    }
    out = static_cast<Value&&>(result);
    return Status::Ok;
}

} // namespace nift::rt::natives
