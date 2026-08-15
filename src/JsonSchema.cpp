#include "JsonSchema.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace jsonschema {
namespace {

std::string type_name(const json::Document& value) {
    switch (value.type) {
        case json::Type::Null: return "null";
        case json::Type::Boolean: return "boolean";
        case json::Type::Number: return "number";
        case json::Type::String: return "string";
        case json::Type::Array: return "array";
        case json::Type::Object: return "object";
    }
    return "unknown";
}

bool json_equal(const json::Document& a, const json::Document& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case json::Type::Null: return true;
        case json::Type::Boolean: return a.boolean == b.boolean;
        case json::Type::Number: return a.num == b.num;
        case json::Type::String: return a.string == b.string;
        case json::Type::Array:
            if (a.array.size() != b.array.size()) return false;
            for (std::size_t i = 0; i < a.array.size(); ++i)
                if (!json_equal(a.array[i], b.array[i])) return false;
            return true;
        case json::Type::Object:
            if (a.object.size() != b.object.size()) return false;
            for (const auto& [key, value] : a.object) {
                if (!b.has(key) || !json_equal(value, b[key])) return false;
            }
            return true;
    }
    return false;
}

bool non_negative_integer(const json::Document& value, std::size_t& out) {
    if (!value.is_number() || value.num < 0.0 || std::floor(value.num) != value.num ||
        value.num > static_cast<double>(std::numeric_limits<std::size_t>::max())) return false;
    out = static_cast<std::size_t>(value.num);
    return static_cast<double>(out) == value.num;
}

std::string pointer_unescape(const std::string& token, bool& ok) {
    std::string out;
    out.reserve(token.size());
    ok = true;
    for (std::size_t i = 0; i < token.size(); ++i) {
        if (token[i] != '~') { out.push_back(token[i]); continue; }
        if (i + 1 >= token.size()) { ok = false; return {}; }
        const char next = token[++i];
        if (next == '0') out.push_back('~');
        else if (next == '1') out.push_back('/');
        else { ok = false; return {}; }
    }
    return out;
}

const json::Document* resolve_local_ref(const json::Document& root,
                                        const std::string& ref,
                                        std::string& error) {
    if (ref == "#") return &root;
    if (ref.rfind("#/", 0) != 0) {
        error = "only local JSON Schema $ref values beginning with '#/' are supported (got '" + ref + "')";
        return nullptr;
    }
    const json::Document* current = &root;
    std::size_t start = 2;
    while (start <= ref.size()) {
        const auto slash = ref.find('/', start);
        const std::string encoded = ref.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        bool ok = false;
        const std::string token = pointer_unescape(encoded, ok);
        if (!ok) {
            error = "invalid JSON Pointer escape in $ref '" + ref + "'";
            return nullptr;
        }
        if (current->is_object()) {
            if (!current->has(token)) {
                error = "$ref '" + ref + "' does not resolve (missing member '" + token + "')";
                return nullptr;
            }
            current = &(*current)[token];
        } else if (current->is_array()) {
            if (token.empty() || !std::all_of(token.begin(), token.end(), [](char c) {
                    return std::isdigit(static_cast<unsigned char>(c));
                })) {
                error = "$ref '" + ref + "' uses a non-numeric array index '" + token + "'";
                return nullptr;
            }
            std::size_t index = 0;
            try { index = static_cast<std::size_t>(std::stoull(token)); }
            catch (...) { error = "$ref array index is out of range in '" + ref + "'"; return nullptr; }
            if (index >= current->array.size()) {
                error = "$ref array index is out of range in '" + ref + "'";
                return nullptr;
            }
            current = &(*current)[index];
        } else {
            error = "$ref '" + ref + "' traverses through a non-container value";
            return nullptr;
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return current;
}

std::size_t utf8_codepoint_count(const std::string& value) {
    std::size_t count = 0;
    for (unsigned char c : value) {
        // UTF-8 continuation bytes begin with 10xxxxxx. Counting every other
        // byte gives the Unicode scalar/code-point count for valid UTF-8.
        if ((c & 0xc0u) != 0x80u) ++count;
    }
    return count;
}

bool matches_type(const json::Document& value, const std::string& type) {
    if (type == "null") return value.is_null();
    if (type == "boolean") return value.is_bool();
    if (type == "number") return value.is_number();
    if (type == "integer") return value.is_number() && std::floor(value.num) == value.num;
    if (type == "string") return value.is_string();
    if (type == "array") return value.is_array();
    if (type == "object") return value.is_object();
    return false;
}

bool valid_type_name(const std::string& type) {
    static const std::unordered_set<std::string> names = {
        "null", "boolean", "number", "integer", "string", "array", "object"
    };
    return names.count(type) != 0;
}

struct Validator {
    const json::Document& root_schema;
    static constexpr std::size_t max_depth = 256;

    bool fail(std::string& error, const std::string& instance_path, const std::string& message) const {
        error = "at " + instance_path + ": " + message;
        return false;
    }

    bool validate_schema_shape(const json::Document& schema,
                               const std::string& schema_path,
                               std::string& error,
                               std::size_t depth) const {
        if (depth > max_depth) {
            error = "JSON Schema nesting is too deep near " + schema_path;
            return false;
        }
        if (schema.is_bool()) return true;
        if (!schema.is_object()) {
            error = "JSON Schema at " + schema_path + " must be an object or boolean";
            return false;
        }

        static const std::unordered_set<std::string> supported = {
            "$schema", "$comment", "title", "description", "default", "examples",
            "$defs", "$ref", "type", "enum", "const",
            "properties", "required", "additionalProperties", "minProperties", "maxProperties",
            "items", "minItems", "maxItems", "uniqueItems", "contains", "minContains", "maxContains",
            "minLength", "maxLength", "pattern",
            "minimum", "maximum", "exclusiveMinimum", "exclusiveMaximum", "multipleOf",
            "allOf", "anyOf", "oneOf", "not"
        };
        for (const auto& [key, ignored] : schema.object) {
            (void)ignored;
            if (!supported.count(key)) {
                error = "unsupported JSON Schema keyword '" + key + "' at " + schema_path;
                return false;
            }
        }

        if (schema.has("type")) {
            const auto& t = schema["type"];
            if (t.is_string()) {
                if (!valid_type_name(t.string)) { error = "unknown JSON Schema type '" + t.string + "' at " + schema_path; return false; }
            } else if (t.is_array()) {
                if (t.array.empty()) { error = "type array cannot be empty at " + schema_path; return false; }
                std::unordered_set<std::string> seen;
                for (const auto& item : t.array) {
                    if (!item.is_string() || !valid_type_name(item.string)) {
                        error = "type array must contain supported type names at " + schema_path;
                        return false;
                    }
                    if (!seen.insert(item.string).second) {
                        error = "type array contains duplicate type '" + item.string + "' at " + schema_path;
                        return false;
                    }
                }
            } else { error = "type must be a string or array of strings at " + schema_path; return false; }
        }

        if (schema.has("required")) {
            const auto& req = schema["required"];
            if (!req.is_array()) { error = "required must be an array at " + schema_path; return false; }
            std::unordered_set<std::string> seen;
            for (const auto& item : req.array) {
                if (!item.is_string()) { error = "required must contain only strings at " + schema_path; return false; }
                if (!seen.insert(item.string).second) { error = "required contains duplicate member '" + item.string + "' at " + schema_path; return false; }
            }
        }

        auto check_non_negative = [&](const char* key) {
            if (!schema.has(key)) return true;
            std::size_t ignored = 0;
            if (!non_negative_integer(schema[key], ignored)) {
                error = std::string(key) + " must be a non-negative integer at " + schema_path;
                return false;
            }
            return true;
        };
        for (const char* key : {"minProperties", "maxProperties", "minItems", "maxItems", "minContains", "maxContains", "minLength", "maxLength"})
            if (!check_non_negative(key)) return false;

        for (const char* key : {"minimum", "maximum", "exclusiveMinimum", "exclusiveMaximum", "multipleOf"}) {
            if (schema.has(key) && !schema[key].is_number()) {
                error = std::string(key) + " must be a number at " + schema_path;
                return false;
            }
        }
        if (schema.has("multipleOf") && schema["multipleOf"].num <= 0.0) {
            error = "multipleOf must be greater than zero at " + schema_path;
            return false;
        }
        if (schema.has("pattern")) {
            if (!schema["pattern"].is_string()) { error = "pattern must be a string at " + schema_path; return false; }
            try { std::regex compiled(schema["pattern"].string); (void)compiled; }
            catch (const std::regex_error&) { error = "pattern is not a valid regular expression at " + schema_path; return false; }
        }
        if (schema.has("uniqueItems") && !schema["uniqueItems"].is_bool()) {
            error = "uniqueItems must be boolean at " + schema_path;
            return false;
        }
        if (schema.has("additionalProperties") && !schema["additionalProperties"].is_bool() && !schema["additionalProperties"].is_object()) {
            error = "additionalProperties must be a boolean or schema object at " + schema_path;
            return false;
        }
        if (schema.has("properties") && !schema["properties"].is_object()) {
            error = "properties must be an object at " + schema_path;
            return false;
        }
        if (schema.has("$defs") && !schema["$defs"].is_object()) {
            error = "$defs must be an object at " + schema_path;
            return false;
        }
        if (schema.has("$ref") && !schema["$ref"].is_string()) {
            error = "$ref must be a string at " + schema_path;
            return false;
        }
        if (schema.has("enum")) {
            if (!schema["enum"].is_array() || schema["enum"].array.empty()) {
                error = "enum must be a non-empty array at " + schema_path;
                return false;
            }
        }
        for (const char* key : {"allOf", "anyOf", "oneOf"}) {
            if (!schema.has(key)) continue;
            const auto& list = schema[key];
            if (!list.is_array() || list.array.empty()) {
                error = std::string(key) + " must be a non-empty array of schemas at " + schema_path;
                return false;
            }
            for (std::size_t i = 0; i < list.array.size(); ++i)
                if (!validate_schema_shape(list.array[i], schema_path + "/" + key + "/" + std::to_string(i), error, depth + 1)) return false;
        }
        for (const char* key : {"items", "contains", "not"}) {
            if (schema.has(key) && !validate_schema_shape(schema[key], schema_path + "/" + key, error, depth + 1)) return false;
        }
        if (schema.has("additionalProperties") && schema["additionalProperties"].is_object() &&
            !validate_schema_shape(schema["additionalProperties"], schema_path + "/additionalProperties", error, depth + 1)) return false;
        if (schema.has("properties")) {
            for (const auto& [key, child] : schema["properties"].object)
                if (!validate_schema_shape(child, schema_path + "/properties/" + key, error, depth + 1)) return false;
        }
        if (schema.has("$defs")) {
            for (const auto& [key, child] : schema["$defs"].object)
                if (!validate_schema_shape(child, schema_path + "/$defs/" + key, error, depth + 1)) return false;
        }
        return true;
    }

    bool apply(const json::Document& instance,
               const json::Document& schema,
               const std::string& instance_path,
               std::string& error,
               std::size_t depth) const {
        if (depth > max_depth) return fail(error, instance_path, "JSON Schema validation exceeded maximum nesting depth");
        if (schema.is_bool()) {
            if (schema.boolean) return true;
            return fail(error, instance_path, "value is rejected by a false schema");
        }
        if (!schema.is_object()) return fail(error, instance_path, "internal schema is not an object or boolean");

        if (schema.has("$ref")) {
            std::string ref_error;
            const auto* target = resolve_local_ref(root_schema, schema["$ref"].string, ref_error);
            if (!target) { error = ref_error; return false; }
            if (!apply(instance, *target, instance_path, error, depth + 1)) return false;
        }

        if (schema.has("type")) {
            bool match = false;
            const auto& t = schema["type"];
            if (t.is_string()) match = matches_type(instance, t.string);
            else for (const auto& item : t.array) if (matches_type(instance, item.string)) { match = true; break; }
            if (!match) {
                std::string expected;
                if (t.is_string()) expected = t.string;
                else {
                    for (std::size_t i = 0; i < t.array.size(); ++i) {
                        if (i) expected += " or ";
                        expected += t.array[i].string;
                    }
                }
                return fail(error, instance_path, "expected " + expected + ", received " + type_name(instance));
            }
        }

        if (schema.has("const") && !json_equal(instance, schema["const"]))
            return fail(error, instance_path, "value does not match const");
        if (schema.has("enum")) {
            bool found = false;
            for (const auto& candidate : schema["enum"].array) if (json_equal(instance, candidate)) { found = true; break; }
            if (!found) return fail(error, instance_path, "value is not one of the allowed enum values");
        }

        if (instance.is_object()) {
            if (schema.has("minProperties")) {
                const std::size_t min = static_cast<std::size_t>(schema["minProperties"].num);
                if (instance.object.size() < min) return fail(error, instance_path, "object has fewer than " + std::to_string(min) + " properties");
            }
            if (schema.has("maxProperties")) {
                const std::size_t max = static_cast<std::size_t>(schema["maxProperties"].num);
                if (instance.object.size() > max) return fail(error, instance_path, "object has more than " + std::to_string(max) + " properties");
            }
            if (schema.has("required")) {
                for (const auto& required : schema["required"].array)
                    if (!instance.has(required.string))
                        return fail(error, instance_path, "required property '" + required.string + "' is missing");
            }
            if (schema.has("properties")) {
                const auto& properties = schema["properties"];
                for (const auto& [key, child_schema] : properties.object) {
                    if (instance.has(key) && !apply(instance[key], child_schema, instance_path + "." + key, error, depth + 1)) return false;
                }
            }
            if (schema.has("additionalProperties")) {
                const auto& additional = schema["additionalProperties"];
                for (const auto& [key, value] : instance.object) {
                    const bool declared = schema.has("properties") && schema["properties"].has(key);
                    if (declared) continue;
                    if (additional.is_bool()) {
                        if (!additional.boolean) return fail(error, instance_path + "." + key, "additional property is not allowed");
                    } else if (!apply(value, additional, instance_path + "." + key, error, depth + 1)) return false;
                }
            }
        }

        if (instance.is_array()) {
            if (schema.has("minItems")) {
                const std::size_t min = static_cast<std::size_t>(schema["minItems"].num);
                if (instance.array.size() < min) return fail(error, instance_path, "array has fewer than " + std::to_string(min) + " items");
            }
            if (schema.has("maxItems")) {
                const std::size_t max = static_cast<std::size_t>(schema["maxItems"].num);
                if (instance.array.size() > max) return fail(error, instance_path, "array has more than " + std::to_string(max) + " items");
            }
            if (schema.has("uniqueItems") && schema["uniqueItems"].boolean) {
                for (std::size_t i = 0; i < instance.array.size(); ++i)
                    for (std::size_t j = i + 1; j < instance.array.size(); ++j)
                        if (json_equal(instance.array[i], instance.array[j]))
                            return fail(error, instance_path + "[" + std::to_string(j) + "]", "array items must be unique");
            }
            if (schema.has("items")) {
                for (std::size_t i = 0; i < instance.array.size(); ++i)
                    if (!apply(instance.array[i], schema["items"], instance_path + "[" + std::to_string(i) + "]", error, depth + 1)) return false;
            }
            if (schema.has("contains")) {
                std::size_t matches = 0;
                for (const auto& item : instance.array) {
                    std::string ignored;
                    if (apply(item, schema["contains"], instance_path, ignored, depth + 1)) ++matches;
                }
                const std::size_t min = schema.has("minContains") ? static_cast<std::size_t>(schema["minContains"].num) : 1;
                const std::size_t max = schema.has("maxContains") ? static_cast<std::size_t>(schema["maxContains"].num) : std::numeric_limits<std::size_t>::max();
                if (matches < min || matches > max)
                    return fail(error, instance_path, "array contains " + std::to_string(matches) + " matching items; expected between " + std::to_string(min) + " and " + (max == std::numeric_limits<std::size_t>::max() ? std::string("unbounded") : std::to_string(max)));
            }
        }

        if (instance.is_string()) {
            const std::size_t string_length = utf8_codepoint_count(instance.string);
            if (schema.has("minLength") && string_length < static_cast<std::size_t>(schema["minLength"].num))
                return fail(error, instance_path, "string is shorter than minLength");
            if (schema.has("maxLength") && string_length > static_cast<std::size_t>(schema["maxLength"].num))
                return fail(error, instance_path, "string is longer than maxLength");
            if (schema.has("pattern")) {
                try {
                    if (!std::regex_search(instance.string, std::regex(schema["pattern"].string)))
                        return fail(error, instance_path, "string does not match required pattern");
                } catch (const std::regex_error&) {
                    error = "invalid regex pattern in schema";
                    return false;
                }
            }
        }

        if (instance.is_number()) {
            if (schema.has("minimum") && instance.num < schema["minimum"].num) return fail(error, instance_path, "number is less than minimum");
            if (schema.has("maximum") && instance.num > schema["maximum"].num) return fail(error, instance_path, "number is greater than maximum");
            if (schema.has("exclusiveMinimum") && instance.num <= schema["exclusiveMinimum"].num) return fail(error, instance_path, "number is not greater than exclusiveMinimum");
            if (schema.has("exclusiveMaximum") && instance.num >= schema["exclusiveMaximum"].num) return fail(error, instance_path, "number is not less than exclusiveMaximum");
            if (schema.has("multipleOf")) {
                const double divisor = schema["multipleOf"].num;
                const double quotient = instance.num / divisor;
                const double nearest = std::round(quotient);
                if (std::fabs(quotient - nearest) > 1e-10 * std::max(1.0, std::fabs(quotient)))
                    return fail(error, instance_path, "number is not a multipleOf " + schema["multipleOf"].dump(0));
            }
        }

        auto count_matches = [&](const json::Document& list) {
            std::size_t count = 0;
            for (const auto& child : list.array) {
                std::string ignored;
                if (apply(instance, child, instance_path, ignored, depth + 1)) ++count;
            }
            return count;
        };
        if (schema.has("allOf")) {
            for (const auto& child : schema["allOf"].array)
                if (!apply(instance, child, instance_path, error, depth + 1)) return false;
        }
        if (schema.has("anyOf") && count_matches(schema["anyOf"]) == 0)
            return fail(error, instance_path, "value does not match any schema in anyOf");
        if (schema.has("oneOf") && count_matches(schema["oneOf"]) != 1)
            return fail(error, instance_path, "value must match exactly one schema in oneOf");
        if (schema.has("not")) {
            std::string ignored;
            if (apply(instance, schema["not"], instance_path, ignored, depth + 1))
                return fail(error, instance_path, "value matches schema forbidden by not");
        }
        return true;
    }
};

} // namespace

bool validate(const json::Document& instance,
              const json::Document& schema,
              std::string& error) {
    error.clear();
    Validator validator{schema};
    if (!validator.validate_schema_shape(schema, "#", error, 0)) return false;
    return validator.apply(instance, schema, "$", error, 0);
}

} // namespace jsonschema
