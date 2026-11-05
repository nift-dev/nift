#include "rt/natives/format_methods.h"
#include "rt/dispatch_defect.h"  // B4

#include "rt/shared_state.h"

#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/error.h"
#include "rt/natives/support.h"
#include "rt/number.h"
#include "rt/string.h"

#include <cstring>

namespace nift::rt::natives {

namespace {

Status internal(std::string& error, const char* what) {
    error = what;
    return Status::Internal;
}

// Legacy (Jsonic++) JSON string escaping: `"`, `\`, \b \f \n \r \t by name,
// other bytes below 0x20 as \u00XX (lower-case hex), every other byte raw
// (invalid UTF-8 included).
void append_json_string(std::string_view text, std::string& out) {
    static constexpr char hex[] = "0123456789abcdef";
    out += '"';
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += hex[(c >> 4) & 0x0f];
                    out += hex[c & 0x0f];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

constexpr int kMaxDepth = 128;  // legacy: "value serialization depth exceeded" beyond

// Error presentation shares CP13B's canonical field/cause encoding. Its
// pretty indentation is relative to the containing aggregate, and the depth
// budget includes each Error object's scalar fields and cause.
bool serialize_error(const Value& value, bool pretty, int depth, std::string& out, std::string& error) {
    const Value* cause = &value;
    int levels = 0;
    while (cause->is(Kind::Error)) {
        if (depth + ++levels > kMaxDepth) { error = "value serialization depth exceeded"; return false; }
        cause = &cause->heap_as<ErrorObject>()->cause;
    }
    const std::string text = error_to_json(value, pretty);
    for (char c : text) {
        out += c;
        if (pretty && c == '\n') out.append(static_cast<std::size_t>(depth * 2), ' ');
    }
    return true;
}

// Legacy serialize_value: compact ("," and ":") or pretty (two-space indent,
// one element per line, ": " after keys; empty containers stay "[]"/"{}").
// Recursion is bounded by kMaxDepth + 1 levels.
bool serialize_collection(const Value& value, bool pretty, int depth, std::string& out, std::string& error,
                          bool& bad_kind);
bool serialize_struct(const Value& value, bool pretty, int depth, std::string& out, std::string& error,
                      bool& bad_kind);
bool serialize_at(const Value& value, bool pretty, int depth, std::string& out, std::string& error, bool& bad_kind) {
    if (depth > kMaxDepth) { error = "value serialization depth exceeded"; return false; }
    switch (value.kind()) {
        case Kind::Null: case Kind::Bool: case Kind::Int: case Kind::Double: case Kind::Decimal:
            append_scalar(value, out);
            return true;
        case Kind::String: append_json_string(string_view_of(value), out); return true;
        case Kind::Bytes: error = "bytes values are not serializable"; return false;
        case Kind::Error: return serialize_error(value, pretty, depth, out, error);
        case Kind::Function: error = "opaque runtime value is not serializable"; return false;
        // B0 (docs/runtime-2/checkpoints/b0-safety.md): a shared handle (mutex,
        // channel, atomic, task) or a future reaches here at run time, through
        // any container: an ordinary program error, the same as a function.
#if defined(NIFT_SAFETY_MUTANT) && NIFT_SAFETY_MUTANT == 1
        case Kind::Handle: case Kind::Future: bad_kind = true; error = "stringify argument kind"; return false;  // S1
#else
        case Kind::Handle: case Kind::Future: error = "opaque runtime value is not serializable"; return false;
#endif
        case Kind::Array: case Kind::Object: break;
        case Kind::Collection: return serialize_collection(value, pretty, depth, out, error, bad_kind);
        case Kind::Struct: return serialize_struct(value, pretty, depth, out, error, bad_kind);
        // Cell: never a first-class value; it cannot reach the serializer.
        default: bad_kind = true; error = "stringify argument kind"; return false;
    }
    const bool array = value.is(Kind::Array);
    const std::size_t count = array ? array_size(value) : object_size(value);
    out += array ? '[' : '{';
    if (pretty && count != 0) out += '\n';
    for (std::size_t i = 0; i < count; ++i) {
        if (pretty) out.append(static_cast<std::size_t>((depth + 1) * 2), ' ');
        const Value* item = nullptr;
        if (array) {
            item = &array_items(value)[i];
        } else {
            const auto& entry = object_entries(value)[i];
            append_json_string(atom_text(entry.first), out);
            out += pretty ? ": " : ":";
            item = &entry.second;
        }
        if (!serialize_at(*item, pretty, depth + 1, out, error, bad_kind)) return false;
        if (i + 1 < count) out += ',';
        if (pretty) out += '\n';
    }
    if (pretty && count != 0) out.append(static_cast<std::size_t>(depth * 2), ' ');
    out += array ? ']' : '}';
    return true;
}

// D-063: legacy serialize_value's collection form (ParserExpression.cpp):
// `map([[k,v],...])`, `set([...])`, `stack([...])` (bottom first); pretty puts
// one entry per line, `[k, v]` with keys and values at the next depth.
bool serialize_collection(const Value& value, bool pretty, int depth, std::string& out, std::string& error,
                          bool& bad_kind) {
    const CollectionKind kind = collection_kind(value);
    const auto& items = collection_items(value);
    out += collection_kind_name(kind);
    out += "([";
    const std::size_t step = kind == CollectionKind::Map ? 2 : 1;
    const std::size_t count = items.size() / step;
    if (pretty && count != 0) out += '\n';
    for (std::size_t i = 0; i < count; ++i) {
        if (pretty) out.append(static_cast<std::size_t>((depth + 1) * 2), ' ');
        if (kind == CollectionKind::Map) {
            out += '[';
            if (!serialize_at(items[2 * i], pretty, depth + 1, out, error, bad_kind)) return false;
            out += pretty ? ", " : ",";
            if (!serialize_at(items[2 * i + 1], pretty, depth + 1, out, error, bad_kind)) return false;
            out += ']';
        } else if (!serialize_at(items[i], pretty, depth + 1, out, error, bad_kind)) {
            return false;
        }
        if (i + 1 < count) out += ',';
        if (pretty) out += '\n';
    }
    if (pretty && count != 0) out.append(static_cast<std::size_t>(depth * 2), ' ');
    out += "])";
    return true;
}

// B3 (b3-structs.md): the instance field declared at position `i` of its
// descriptor: field i itself (the construction order; asserted in checked
// builds, aggregate_store.h), else the field of that name. Null when the
// instance has no such field. Fields the descriptor does not declare are
// never reached (fail closed).
const Value* declared_field(const StructObject& instance, std::size_t i) {
    const std::string& name = instance.type->fields[i];
    if (i < instance.fields.size() && atom_text(instance.fields[i].first) == name)
        return &instance.fields[i].second;
    for (const auto& field : instance.fields)
        if (atom_text(field.first) == name) return &field.second;  // the by-name fallback
    return nullptr;
}

// B3: legacy serialize_value's struct form (ParserExpression.cpp): the type
// name, then each PUBLIC field of the instance's own descriptor in
// declaration order, `name:value` (the name raw, not quoted). Pretty puts one
// field per line at the next depth, `name: value`, each line ending with ","
// (the last too, legacy's form), and closes at the parent's indentation; no
// public field gives `Name{}`. Private fields are never written: privacy is
// the descriptor's, so it holds through every alias, copy and transfer.
// Observational: reads only, no mutation. It runs no user code by
// construction (owner ruling): the serializer takes no Context, VM or
// callable, and a descriptor carries no methods, so a struct's own
// stringify/prettify/highlight is reachable only by an explicit call
// (compiler_structs.cpp; b3_structs.py checks this structure).
bool serialize_struct(const Value& value, bool pretty, int depth, std::string& out, std::string& error,
                      bool& bad_kind) {
    const auto& instance = *value.heap_as<StructObject>();
    const StructType& type = *instance.type;
    out += type.name;
    out += '{';
    bool first = true;
#if defined(NIFT_B3_MUTANT) && NIFT_B3_MUTANT == 7
    // D7: the instance's own fields, unpaired with the descriptor (an undeclared
    // or unpaired field is written; privacy only by position).
    for (std::size_t i = 0; i < instance.fields.size(); ++i) {
        if (i < type.size() && type.is_private(i) && atom_text(instance.fields[i].first) == type.fields[i]) continue;
        const Value* field = &instance.fields[i].second;
        const std::string name(atom_text(instance.fields[i].first));
#else
    for (std::size_t i = 0; i < type.size(); ++i) {
#if !(defined(NIFT_B3_MUTANT) && NIFT_B3_MUTANT == 1)
        if (type.is_private(i)) continue;  // D1 renders private fields
#endif
        const Value* field = declared_field(instance, i);
        if (field == nullptr) continue;
        const std::string& name = type.fields[i];
#endif
        if (pretty) {
            if (first) out += '\n';
            out.append(static_cast<std::size_t>((depth + 1) * 2), ' ');
        } else if (!first) {
            out += ',';
        }
        out += name;
        out += pretty ? ": " : ":";
        if (!serialize_at(*field, pretty, depth + 1, out, error, bad_kind)) return false;
#if defined(NIFT_B3_MUTANT) && NIFT_B3_MUTANT == 6
        if (pretty) out += '\n';  // D6: no comma after a pretty field line
#else
        if (pretty) out += ",\n";
#endif
        first = false;
    }
    if (pretty && !first) out.append(static_cast<std::size_t>(depth * 2), ' ');
    out += '}';
    return true;
}

Status present(const Value* args, std::uint32_t argc, Value& out, std::string& error, std::string_view name,
               bool pretty) {
    if (argc == 0) return internal(error, "presentation receiver");
    if (!no_arguments(argc, name, error)) return Status::Error;
    std::string text;
    bool bad_kind = false;
    if (!serialize(args[0], pretty, text, error, bad_kind)) return bad_kind ? Status::Internal : Status::Error;
    out = make_string(static_cast<std::string&&>(text));
    return Status::Ok;
}

enum class Escape : std::uint8_t { Html, Attr, Url };

Status escaped(const Value* args, std::uint32_t argc, Value& out, std::string& error, std::string_view name,
               Escape mode) {
    if (argc != 1) return fail(error, std::string(name) + ": expected one value");
    const Value& v = args[0];
    std::string text;
    if (v.is(Kind::String) || v.is_number() || v.is(Kind::Bool) || v.is_null()) append_scalar(v, text);
    else return fail(error, std::string(name) + ": value must be a string or scalar");
    std::string result;
    result.reserve(text.size() + 8);
    static constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char c : text) {
        if (mode == Escape::Url) {
            const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                               c == '_' || c == '.' || c == '~';
            if (plain) { result += static_cast<char>(c); continue; }
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 0xF];
            continue;
        }
        if (c == '&') result += "&amp;";
        else if (c == '<') result += "&lt;";
        else if (c == '>') result += "&gt;";
        else if (mode == Escape::Attr && c == '"') result += "&quot;";
        else if (mode == Escape::Attr && c == '\'') result += "&#39;";
        else result += static_cast<char>(c);
    }
    out = make_string(static_cast<std::string&&>(result));
    return Status::Ok;
}

// is_<name>(v): legacy compares public_type(v) with the predicate's category.
Status predicate(const Value* args, std::uint32_t argc, Value& out, std::string& error, std::string_view name,
                 bool (*test)(const char* type)) {
    if (argc != 1) return fail(error, std::string(name) + ": expected one value");
    const char* type = public_type_name(args[0]);
    if (!type) return internal(error, "predicate argument kind");
    out = Value::boolean(test(type));
    return Status::Ok;
}

bool same(const char* a, const char* b) { return std::strcmp(a, b) == 0; }  // rt-lint: allow-string-compare cold

} // namespace

bool serialize(const Value& value, bool pretty, std::string& out, std::string& error, bool& internal_kind) {
    internal_kind = false;
    return serialize_at(value, pretty, 0, out, error, internal_kind);
}

const char* public_type_name(const Value& value) {
    switch (value.kind()) {
        case Kind::Null: return "null";
        case Kind::Bool: return "bool";
        case Kind::Int: case Kind::Double: case Kind::Decimal: return number_is_integer(value) ? "int" : "float";
        case Kind::String: return "string";
        case Kind::Bytes: return "bytes";
        case Kind::Array: return "array";
        case Kind::Object: return "object";
        case Kind::Function: return "function";
        case Kind::Struct: return "struct";
        case Kind::Error: return "error";
        case Kind::Handle: return shared_handle_type_name(value);
        case Kind::Collection: return "collection";  // legacy type() of every collection kind
        default: return nullptr;  // host handles keep their CP17 refusal
    }
}

const char* unrenderable_text(const Value& value) {
    switch (value.kind()) {
        case Kind::Function: case Kind::Handle: case Kind::Future: return "opaque runtime value is not serializable";
        case Kind::Collection: return "map, set and stack values cannot be rendered as text";
        case Kind::Struct: return "struct instances cannot be rendered as text";
        case Kind::Array: return "cannot render a JSON array as text; select an element first";
        case Kind::Object: return "cannot render a JSON object as text; select a member first";
        case Kind::Error: return "Error values cannot be rendered as text";
        case Kind::Bytes: return "bytes values cannot be rendered as text; decode as UTF-8 first";
        default: return nullptr;
    }
}

// The interpolation lowering (compiler_interp.cpp) binds to_string from the
// scalar part of the operand's kinds (scalar_only); a value of a kind it set
// aside arrives here at run time (`for (v : [1, mutex()]) { "$[v]" }`, B0 F1).
Status scalar_to_string(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
#if defined(NIFT_SAFETY_MUTANT) && NIFT_SAFETY_MUTANT == 6
    if (argc == 0 || !(args[0].is_number() || args[0].is(Kind::Bool) || args[0].is_null()))
        return internal(error, "to_string receiver");  // S6
#else
    if (argc == 0) return internal(error, "to_string receiver");  // the compiler always passes the receiver
    if (!(args[0].is_number() || args[0].is(Kind::Bool) || args[0].is_null())) {
        const char* text = unrenderable_text(args[0]);
        if (!text) return internal(error, "to_string receiver");  // a string: the lowering appends it directly
        note_dispatch_defect();  // B4: flagged sites never get here (b4-dispatch.md 4)
        return fail(error, text);
    }
#endif
    if (!no_arguments(argc, "to_string", error)) return Status::Error;
    std::string text;
    append_scalar(args[0], text);
    out = make_string(static_cast<std::string&&>(text));
    return Status::Ok;
}

Status fmt_stringify(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return present(args, argc, out, error, "stringify", false);
}

Status fmt_prettify(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return present(args, argc, out, error, "prettify", true);
}

// highlight() changes only the REPL's terminal display; as an expression it is
// the compact serialization (legacy complete_presentation ignores the flag).
Status fmt_highlight(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return present(args, argc, out, error, "highlight", false);
}

Status fn_html_escape(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return escaped(args, argc, out, error, "html_escape", Escape::Html);
}

Status fn_attr_escape(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return escaped(args, argc, out, error, "attr_escape", Escape::Attr);
}

Status fn_url_encode(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return escaped(args, argc, out, error, "url_encode", Escape::Url);
}

Status fn_type(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc != 1) return fail(error, "type: expected one value");
    const char* type = public_type_name(args[0]);
    if (!type) return internal(error, "type argument kind");
    out = make_string(type);
    return Status::Ok;
}

#define NIFT_RT_PREDICATE(fn, name, test)                                                                   \
    Status fn(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {     \
        return predicate(args, argc, out, error, name, [](const char* t) { return test; });                \
    }
NIFT_RT_PREDICATE(fn_is_null, "is_null", same(t, "null"))
NIFT_RT_PREDICATE(fn_is_bool, "is_bool", same(t, "bool"))
NIFT_RT_PREDICATE(fn_is_number, "is_number", same(t, "int") || same(t, "float"))
NIFT_RT_PREDICATE(fn_is_int, "is_int", same(t, "int"))
NIFT_RT_PREDICATE(fn_is_float, "is_float", same(t, "float"))
NIFT_RT_PREDICATE(fn_is_string, "is_string", same(t, "string"))
NIFT_RT_PREDICATE(fn_is_bytes, "is_bytes", same(t, "bytes"))
NIFT_RT_PREDICATE(fn_is_array, "is_array", same(t, "array"))
NIFT_RT_PREDICATE(fn_is_object, "is_object", same(t, "object"))
NIFT_RT_PREDICATE(fn_is_struct, "is_struct", same(t, "struct"))
NIFT_RT_PREDICATE(fn_is_function, "is_function", same(t, "function"))
NIFT_RT_PREDICATE(fn_is_collection, "is_collection", same(t, "collection"))
NIFT_RT_PREDICATE(fn_is_enum, "is_enum", same(t, "enum"))
#undef NIFT_RT_PREDICATE

} // namespace nift::rt::natives
