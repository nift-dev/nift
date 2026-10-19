#pragma once
// Test support for the CP14 built-in natives: legacy-visible rendering of
// results (`type(r)` and `[r].stringify()` as legacy prints them), Nift
// source literals for rt values, and exact heap-footprint walks.
#include "RuntimeValue.h"
#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/heap_account.h"
#include "rt/legacy_bridge.h"
#include "rt/natives/builtin.h"
#include "rt/number.h"
#include "rt/string.h"

#include <charconv>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace builtins_test {

using nift::rt::Kind;
using nift::rt::Value;

// Legacy public_type for the kinds natives produce.
inline std::string public_type(const Value& v) {
    switch (v.kind()) {
        case Kind::Null: return "null";
        case Kind::Bool: return "bool";
        case Kind::Int: case Kind::Double: case Kind::Decimal:
            return nift::rt::number_is_integer(v) ? "int" : "float";
        case Kind::String: return "string";
        case Kind::Array: return "array";
        case Kind::Object: return "object";
        default: return "other";
    }
}

// Legacy serialize_value (stringify) for plain values: scalars and strings
// through RuntimeValue::dump, arrays and objects with "," and ":".
inline bool json(const Value& v, std::string& out) {
    switch (v.kind()) {
        case Kind::Array: {
            out += '[';
            bool first = true;
            for (const Value& item : nift::rt::array_items(v)) {
                if (!first) out += ',';
                first = false;
                if (!json(item, out)) return false;
            }
            out += ']';
            return true;
        }
        case Kind::Object: {
            out += '{';
            bool first = true;
            for (const auto& entry : nift::rt::object_entries(v)) {
                if (!first) out += ',';
                first = false;
                out += nift::RuntimeValue(std::string(nift::rt::atom_text(entry.first))).dump(0);
                out += ':';
                if (!json(entry.second, out)) return false;
            }
            out += '}';
            return true;
        }
        default: {
            nift::RuntimeValue legacy;
            std::string error;
            if (!nift::rt::to_legacy(v, legacy, error)) return false;
            out += legacy.dump(0);
            return true;
        }
    }
}

// What the legacy probe program prints for a successful case.
inline std::string shown(const Value& v) {
    std::string text = "ok " + public_type(v) + " | [";
    json(v, text);
    return text + "]";
}

// A Nift source literal for `v` (strings: `"` and `\` escaped, \n and \t as
// escapes, every other byte raw; callers avoid `$` and 0x1f).
inline std::string literal(const Value& v) {
    switch (v.kind()) {
        case Kind::Null: return "null";
        case Kind::Bool: return v.as_bool() ? "true" : "false";
        case Kind::Int:
            // -2^63 is not a Nift integer literal (unary minus of 2^63); legacy's
            // JSON object literal holds it exactly.
            if (v.as_int() == INT64_MIN) return "{\"v\": -9223372036854775808}.v";
            return std::to_string(v.as_int());
        case Kind::Double: return nift::RuntimeValue(v.as_double()).dump(0);
        case Kind::Decimal:
            // An exact spelling survives only through legacy's JSON object
            // literal path (a bare literal like 0.0 or 1e-400 is re-read).
            return "{\"v\": " + std::string(nift::rt::decimal_spelling(v)) + "}.v";
        case Kind::Bytes: {
            std::string out = "bytes([";
            bool first = true;
            for (std::uint8_t b : nift::rt::bytes_of(v)) {
                if (!first) out += ", ";
                first = false;
                out += std::to_string(b);
            }
            return out + "])";
        }
        case Kind::String: {
            std::string out = "\"";
            for (char c : nift::rt::string_view_of(v)) {
                if (c == '"' || c == '\\') { out += '\\'; out += c; }
                else if (c == '\n') out += "\\n";
                else if (c == '\t') out += "\\t";
                else out += c;
            }
            return out + "\"";
        }
        case Kind::Array: {
            std::string out = "[";
            bool first = true;
            for (const Value& item : nift::rt::array_items(v)) {
                if (!first) out += ", ";
                first = false;
                out += literal(item);
            }
            return out + "]";
        }
        case Kind::Object: {
            std::string out = "{";
            bool first = true;
            for (const auto& entry : nift::rt::object_entries(v)) {
                if (!first) out += ", ";
                first = false;
                out += literal(nift::rt::make_string(nift::rt::atom_text(entry.first))) + ": " + literal(entry.second);
            }
            return out + "}";
        }
        default: return "null";
    }
}

// Sum of the recorded footprints of every distinct heap object reachable
// from `roots` (the exact live-heap contribution of those values).
inline std::size_t footprint(const std::vector<const Value*>& roots) {
    std::unordered_set<const nift::rt::HeapObject*> seen;
    std::vector<const Value*> work(roots.begin(), roots.end());
    std::size_t total = 0;
    while (!work.empty()) {
        const Value* v = work.back();
        work.pop_back();
        if (!v->is_heap() || !seen.insert(v->heap()).second) continue;
        total += nift::rt::heap_footprint(v->heap());
        if (v->is(Kind::Array))
            for (const Value& item : nift::rt::array_items(*v)) work.push_back(&item);
        if (v->is(Kind::Object))
            for (const auto& entry : nift::rt::object_entries(*v)) work.push_back(&entry.second);
    }
    return total;
}

} // namespace builtins_test
