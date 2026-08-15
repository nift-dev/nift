#include "Json.h"
#include "JsonSchema.h"
#include <cstdlib>
#include <iostream>
#include <string>

static json::Document parse(const std::string& source) {
    json::Document result;
    std::string error;
    if (!json::Document::parse(source, result, error)) {
        std::cerr << "fixture parse failed: " << error << '\n';
        std::exit(1);
    }
    return result;
}

static void expect_valid(const std::string& instance, const std::string& schema) {
    const auto i = parse(instance);
    const auto s = parse(schema);
    std::string error;
    if (!jsonschema::validate(i, s, error)) {
        std::cerr << "expected valid but got: " << error << '\n';
        std::exit(1);
    }
}

static void expect_invalid(const std::string& instance, const std::string& schema,
                           const std::string& expected) {
    const auto i = parse(instance);
    const auto s = parse(schema);
    std::string error;
    if (jsonschema::validate(i, s, error)) {
        std::cerr << "expected invalid but validation passed\n";
        std::exit(1);
    }
    if (error.find(expected) == std::string::npos) {
        std::cerr << "expected error containing '" << expected << "', got: " << error << '\n';
        std::exit(1);
    }
}

int main() {
    expect_valid(R"({"name":"Nift","version":4})",
                 R"({"type":"object","required":["name"],"properties":{"name":{"type":"string"},"version":{"type":"integer","minimum":1}},"additionalProperties":false})");
    expect_invalid(R"({"name":"Nift","extra":true})",
                   R"({"type":"object","properties":{"name":{"type":"string"}},"additionalProperties":false})",
                   "additional property is not allowed");
    expect_invalid(R"({"name":2})",
                   R"({"type":"object","properties":{"name":{"type":"string"}}})",
                   "at $.name: expected string");
    expect_invalid(R"({})", R"({"type":"object","required":["name"]})",
                   "required property 'name' is missing");

    expect_valid(R"([1,2,3])", R"({"type":"array","items":{"type":"integer"},"minItems":2,"uniqueItems":true})");
    expect_invalid(R"([1,1])", R"({"type":"array","uniqueItems":true})", "array items must be unique");
    expect_invalid(R"([1,"two"])", R"({"type":"array","items":{"type":"number"}})", "at $[1]: expected number");
    expect_valid(R"([1,"two"])", R"({"type":"array","contains":{"type":"string"}})");
    expect_invalid(R"([1,2])", R"({"type":"array","contains":{"type":"string"}})", "matching items");

    expect_valid(R"("abc-123")", R"({"type":"string","minLength":3,"pattern":"^[a-z]+-[0-9]+$"})");
    expect_invalid(R"("ab")", R"({"type":"string","minLength":3})", "shorter than minLength");
    expect_valid("12", R"({"type":"number","minimum":10,"maximum":20,"multipleOf":2})");
    expect_invalid("11", R"({"type":"number","multipleOf":2})", "not a multipleOf");

    expect_valid(R"("draft")", R"({"enum":["draft","published"]})");
    expect_invalid(R"("other")", R"({"enum":["draft","published"]})", "allowed enum");
    expect_valid("5", R"({"const":5})");
    expect_invalid("6", R"({"const":5})", "does not match const");

    expect_valid(R"({"author":{"name":"Ada"}})",
                 R"({"$defs":{"person":{"type":"object","required":["name"],"properties":{"name":{"type":"string"}}}},"type":"object","properties":{"author":{"$ref":"#/$defs/person"}}})");
    expect_invalid(R"({"author":{}})",
                   R"({"$defs":{"person":{"type":"object","required":["name"]}},"properties":{"author":{"$ref":"#/$defs/person"}}})",
                   "required property 'name'");

    expect_valid("5", R"({"allOf":[{"type":"number"},{"minimum":1}]})");
    expect_valid(R"("x")", R"({"anyOf":[{"type":"number"},{"type":"string"}]})");
    expect_valid("5", R"({"oneOf":[{"type":"number"},{"type":"string"}]})");
    expect_invalid("5", R"({"not":{"type":"number"}})", "forbidden by not");

    expect_valid("5", "true");
    expect_invalid("5", "false", "false schema");
    expect_invalid(R"("x")", R"({"format":"email"})", "unsupported JSON Schema keyword 'format'");
    expect_invalid(R"("x")", R"({"$ref":"other.json"})", "only local JSON Schema $ref");

    // Shape validation and less-common supported vocabulary.
    expect_valid(R"({"a":1,"b":2})", R"({"type":"object","minProperties":2,"maxProperties":2})");
    expect_invalid(R"({"a":1})", R"({"type":"object","minProperties":2})", "fewer than 2 properties");
    expect_invalid(R"({"a":1,"b":2})", R"({"type":"object","maxProperties":1})", "more than 1 properties");
    expect_valid(R"({"a":1,"b":2})", R"({"type":"object","properties":{"a":{"type":"number"}},"additionalProperties":{"type":"number"}})");
    expect_invalid(R"({"a":1,"b":"x"})", R"({"type":"object","properties":{"a":{"type":"number"}},"additionalProperties":{"type":"number"}})", "at $.b: expected number");
    expect_valid(R"([1,2,3])", R"({"type":"array","minItems":3,"maxItems":3})");
    expect_invalid(R"([1,2,3])", R"({"type":"array","maxItems":2})", "more than 2 items");
    expect_valid(R"([1,2,"x"])", R"({"contains":{"type":"number"},"minContains":2,"maxContains":2})");
    expect_invalid(R"([1,2,3])", R"({"contains":{"type":"number"},"maxContains":2})", "matching items");
    expect_valid(R"("é")", R"({"minLength":1,"maxLength":1})");
    expect_invalid(R"("é")", R"({"maxLength":0})", "longer than maxLength");
    expect_valid("10", R"({"exclusiveMinimum":9,"exclusiveMaximum":11})");
    expect_invalid("9", R"({"exclusiveMinimum":9})", "not greater than exclusiveMinimum");
    expect_invalid("11", R"({"exclusiveMaximum":11})", "not less than exclusiveMaximum");
    expect_valid("null", R"({"type":["null","string"]})");
    expect_valid(R"("ok")", R"({"type":["null","string"]})");
    expect_invalid("3", R"({"type":["null","string"]})", "expected null or string");
    expect_invalid("1", R"({"type":[]})", "type array cannot be empty");
    expect_invalid("1", R"({"type":["number","number"]})", "duplicate type");
    expect_invalid("1", R"({"multipleOf":0})", "multipleOf must be greater than zero");
    expect_invalid(R"("x")", R"({"pattern":"["})", "pattern is not a valid regular expression");
    expect_invalid("1", R"({"required":"x"})", "required must be an array");
    expect_invalid("1", R"({"required":["x","x"]})", "required contains duplicate member");
    expect_invalid("1", R"({"properties":[]})", "properties must be an object");
    expect_invalid("1", R"({"$defs":[]})", "$defs must be an object");
    expect_invalid("1", R"({"$ref":3})", "$ref must be a string");
    expect_invalid("1", R"({"enum":[]})", "enum must be a non-empty array");
    expect_invalid("1", R"({"allOf":[]})", "allOf must be a non-empty array");
    expect_invalid("1", R"({"uniqueItems":"yes"})", "uniqueItems must be boolean");
    expect_invalid("1", R"({"minItems":-1})", "minItems must be a non-negative integer");
    expect_valid(R"({"a/b":{"~key":3}})",
                 R"({"properties":{"a/b":{"properties":{"~key":{"type":"number"}}}}})");
    expect_valid("3", R"({"$defs":{"a/b":{"type":"number"}},"$ref":"#/$defs/a~1b"})");
    expect_invalid("3", R"({"$ref":"#/$defs/missing","$defs":{}})", "does not resolve");
    expect_invalid("3", R"({"$ref":"#/~2bad"})", "invalid JSON Pointer escape");
    expect_valid(R"({"x":1})", R"({"properties":{"x":true}})");
    expect_invalid(R"({"x":1})", R"({"properties":{"x":false}})", "false schema");
    expect_valid(R"({"x":1})", R"({"additionalProperties":true})");
    expect_invalid(R"({"x":1})", R"({"additionalProperties":false})", "additional property is not allowed");
    expect_invalid("1", R"({"$ref":"#"})", "maximum nesting depth");;

    // Shape validation and type unions.
    expect_valid("null", R"({"type":["null","string"]})");
    expect_valid(R"("ok")", R"({"type":["null","string"]})");
    expect_invalid("1", R"({"type":["null","string"]})", "expected null or string");
    expect_invalid("1", R"({"type":[]})", "type array cannot be empty");
    expect_invalid("1", R"({"type":["number","number"]})", "duplicate type");
    expect_invalid("1", R"({"type":"wat"})", "unknown JSON Schema type");
    expect_invalid("1", R"({"required":"x"})", "required must be an array");
    expect_invalid("1", R"({"required":["x","x"]})", "duplicate member");
    expect_invalid("1", R"({"uniqueItems":1})", "uniqueItems must be boolean");
    expect_invalid("1", R"({"multipleOf":0})", "multipleOf must be greater than zero");
    expect_invalid("1", R"({"pattern":"["})", "pattern is not a valid regular expression");

    // Object constraints, including schema-valued additionalProperties.
    expect_valid(R"({"a":1})", R"({"minProperties":1,"maxProperties":1})");
    expect_invalid(R"({})", R"({"minProperties":1})", "fewer than 1 properties");
    expect_invalid(R"({"a":1,"b":2})", R"({"maxProperties":1})", "more than 1 properties");
    expect_valid(R"({"known":"x","extra":2})",
                 R"({"properties":{"known":{"type":"string"}},"additionalProperties":{"type":"number"}})");
    expect_invalid(R"({"known":"x","extra":"bad"})",
                   R"({"properties":{"known":{"type":"string"}},"additionalProperties":{"type":"number"}})",
                   "at $.extra: expected number");
    expect_invalid(R"({"x":1})", R"({"properties":{"x":false}})", "false schema");

    // Array contains bounds and booleans.
    expect_valid(R"([1,"x",2])", R"({"contains":{"type":"number"},"minContains":2,"maxContains":2})");
    expect_invalid(R"([1,"x",2,3])", R"({"contains":{"type":"number"},"maxContains":2})", "matching items");
    expect_valid(R"([])", R"({"items":false})");
    expect_invalid(R"([1])", R"({"items":false})", "false schema");

    // Unicode string lengths count code points rather than UTF-8 bytes.
    expect_valid(R"("é")", R"({"minLength":1,"maxLength":1})");
    expect_valid(R"("😀")", R"({"minLength":1,"maxLength":1})");
    expect_invalid(R"("é")", R"({"minLength":2})", "shorter than minLength");

    // Numeric exclusivity and integer semantics.
    expect_valid("3", R"({"type":"integer","exclusiveMinimum":2,"exclusiveMaximum":4})");
    expect_invalid("2", R"({"exclusiveMinimum":2})", "not greater than exclusiveMinimum");
    expect_invalid("4", R"({"exclusiveMaximum":4})", "not less than exclusiveMaximum");
    expect_invalid("3.5", R"({"type":"integer"})", "expected integer");

    // Composition semantics.
    expect_invalid("5", R"({"oneOf":[{"type":"number"},{"minimum":1}]})", "exactly one");
    expect_invalid("false", R"({"anyOf":[{"type":"number"},{"type":"string"}]})", "does not match any");
    expect_invalid("0", R"({"allOf":[{"type":"number"},{"minimum":1}]})", "less than minimum");

    // JSON Pointer escaping and invalid refs.
    expect_valid(R"("yes")", R"({"$defs":{"a/b":{"const":"yes"},"til~de":{"const":"ok"}},"$ref":"#/$defs/a~1b"})");
    expect_valid(R"("ok")", R"({"$defs":{"til~de":{"const":"ok"}},"$ref":"#/$defs/til~0de"})");
    expect_invalid("1", R"({"$ref":"#/$defs/missing","$defs":{}})", "does not resolve");
    expect_invalid("1", R"({"$ref":"#/$defs/a~2b","$defs":{}})", "invalid JSON Pointer escape");

    // Keywords with reference-resolution semantics must not be silently accepted
    // until Nift actually implements those semantics.
    expect_invalid("1", R"({"$id":"https://example.test/schema","type":"number"})",
                   "unsupported JSON Schema keyword '$id'");
    expect_invalid("1", R"({"$anchor":"thing","type":"number"})",
                   "unsupported JSON Schema keyword '$anchor'");


    std::cout << "JSON Schema smoke test passed\n";
}
