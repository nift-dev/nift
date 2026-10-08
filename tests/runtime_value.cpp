#include "RuntimeJson.h"

#include <cassert>
#include <limits>
#include <string>

namespace {
nift::RuntimeValue exact_number(std::string spelling) {
    nift::RuntimeValue value;
    value.type = nift::RuntimeType::StrNumber;
    value.string = std::move(spelling);
    return value;
}
}

int main() {
    static_assert(static_cast<int>(nift::RuntimeType::Null) == 0);
    static_assert(static_cast<int>(nift::RuntimeType::Boolean) == 1);
    static_assert(static_cast<int>(nift::RuntimeType::Number) == 2);
    static_assert(static_cast<int>(nift::RuntimeType::StrNumber) == 3);
    static_assert(static_cast<int>(nift::RuntimeType::String) == 4);
    static_assert(static_cast<int>(nift::RuntimeType::Array) == 5);
    static_assert(static_cast<int>(nift::RuntimeType::Object) == 6);
    static_assert(static_cast<int>(nift::RuntimeType::Bytes) == 7);
    static_assert(static_cast<int>(nift::RuntimeType::Timer) == 8);
    static_assert(static_cast<int>(nift::RuntimeType::Error) == 9);

    assert(nift::runtime_fingerprint(nift::RuntimeValue(nullptr)) == "0:");
    assert(nift::runtime_fingerprint(nift::RuntimeValue(true)) == "1:1");
    assert(nift::runtime_fingerprint(nift::RuntimeValue(2)) == "2:+2e0");
    assert(nift::runtime_fingerprint(exact_number("2.00")) == "2:+2e0");
    assert(nift::runtime_fingerprint(nift::RuntimeValue("x")) == "4:1:x");
    assert(nift::runtime_fingerprint(nift::RuntimeValue::make_array()) == "5:0[]");
    assert(nift::runtime_fingerprint(nift::RuntimeValue::make_object()) == "6:0{}");
    assert(nift::runtime_fingerprint(nift::RuntimeValue(nift::RuntimeBytes{})) == "7:0:");
    assert(nift::runtime_fingerprint(nift::RuntimeValue::make_timer(4, 9)) == "8:4:9");

    assert(nift::runtime_valid_user_error_code("user.raised"));
    assert(nift::runtime_valid_user_error_code("user.owner.not_found"));
    assert(!nift::runtime_valid_user_error_code("io.open_failed"));
    assert(!nift::runtime_valid_user_error_code("user.Upper"));
    const auto cause = nift::RuntimeValue::make_error("root", "user.root");
    const auto runtime_error = nift::RuntimeValue::make_error(
        "outer", "user.owner.failed", cause, "script.f", 2, 3);
    assert(runtime_error.is_error());
    assert(nift::runtime_truthy(runtime_error));
    assert(nift::runtime_contains_error(runtime_error));
    nift::RuntimeValue member;
    assert(nift::runtime_error_member(runtime_error, "category", member));
    assert(member.string == "user");
    assert(nift::runtime_error_member(runtime_error, "cause", member));
    assert(nift::runtime_equal(member, cause));
    assert(nift::runtime_equal(runtime_error, runtime_error));
    json::Document expected_runtime_error;
    std::string expected_runtime_error_parse;
    assert(json::Document::parse(R"({"message":"outer","code":"user.owner.failed","category":"user","source":"script.f","line":2,"column":3,"cause":{"message":"root","code":"user.root","category":"user","source":"","line":0,"column":0,"cause":null}})",
                                 expected_runtime_error, expected_runtime_error_parse));
    assert(runtime_error.dump(0) == expected_runtime_error.dump(0));
    json::Document rejected_error;
    std::string rejected_error_message;
    assert(!nift::runtime_to_json(runtime_error, rejected_error, rejected_error_message));
    assert(rejected_error_message == "Error values are not JSON serializable");
    auto deepest = nift::RuntimeValue::make_error("0");
    for (int i = 1; i < 16; ++i)
        deepest = nift::RuntimeValue::make_error(std::to_string(i), "user.depth", deepest);
    assert(deepest.is_error());
    bool depth_rejected = false;
    try { (void)nift::RuntimeValue::make_error("16", "user.depth", deepest); }
    catch (const std::invalid_argument&) { depth_rejected = true; }
    assert(depth_rejected);

    json::Document parsed;
    std::string error;
    assert(json::Document::parse(
        R"({"exact":1234567890123456789,"nested":[null,true,2.5,"x"],"o":{"k":1}})",
        parsed, error));

    nift::RuntimeValue value = nift::runtime_from_json(parsed);
    assert(value.is_object());
    assert(value["exact"].type == nift::RuntimeType::StrNumber);
    assert(value["exact"].string == "1234567890123456789");
    assert(value["nested"].array.size() == 4);
    assert(value["nested"].array[0].is_null());
    assert(value["nested"].array[1].boolean);
    assert(value["nested"].array[2].num == 2.5);
    assert(value["nested"].array[3].string == "x");

    const json::Document round_trip = nift::runtime_to_json(value);
    assert(round_trip["exact"].type == json::Type::StrNumber);
    assert(round_trip.dump(0) == parsed.dump(0));

    json::Document error_shaped_json;
    assert(json::Document::parse(
        R"({"message":"x","code":"io.open_failed","category":"io","source":"forged.f","line":7,"column":9,"cause":null})",
        error_shaped_json, error));
    const auto error_shaped_value = nift::runtime_from_json(error_shaped_json);
    assert(error_shaped_value.type == nift::RuntimeType::Object);
    assert(nift::runtime_to_json(error_shaped_value).dump(0) == error_shaped_json.dump(0));

    nift::RuntimeValue copy = value;
    copy["nested"].array[3].string = "changed";
    assert(value["nested"].array[3].string == "x");
    assert(nift::runtime_fingerprint(copy) != nift::runtime_fingerprint(value));
    assert(nift::runtime_equal(value, nift::runtime_from_json(round_trip)));

    json::Document boundaries;
    assert(json::Document::parse(
        "[9007199254740992,9007199254740993,-9007199254740992,-9007199254740993]",
        boundaries, error));
    const auto numbers = nift::runtime_from_json(boundaries);
    assert(!nift::runtime_equal(numbers.array[0], numbers.array[1]));
    assert(nift::runtime_compare_numbers(numbers.array[0], numbers.array[1]) < 0);
    assert(nift::runtime_compare_numbers(numbers.array[2], numbers.array[3]) > 0);
    assert(nift::runtime_fingerprint(numbers.array[0]) != nift::runtime_fingerprint(numbers.array[1]));
    assert(nift::runtime_fingerprint(numbers.array[2]) != nift::runtime_fingerprint(numbers.array[3]));

    const auto huge = exact_number("123456789012345678901234567890");
    const auto huge_next = exact_number("123456789012345678901234567891");
    const auto negative_huge = exact_number("-123456789012345678901234567890");
    const auto negative_huge_next = exact_number("-123456789012345678901234567891");
    assert(nift::runtime_compare_numbers(huge, huge_next) < 0);
    assert(nift::runtime_compare_numbers(negative_huge, negative_huge_next) > 0);
    assert(!nift::runtime_numbers_equal(huge, huge_next));
    assert(nift::runtime_numeric_fingerprint(huge) !=
           nift::runtime_numeric_fingerprint(huge_next));

    const auto tiny = exact_number("1e-1000");
    const auto tiny_equivalent = exact_number("10.000e-1001");
    const auto tiny_next = exact_number("1.000000000000000000000000000001e-1000");
    assert(nift::runtime_numbers_equal(tiny, tiny_equivalent));
    assert(nift::runtime_numeric_fingerprint(tiny) ==
           nift::runtime_numeric_fingerprint(tiny_equivalent));
    assert(nift::runtime_compare_numbers(tiny, tiny_next) < 0);
    assert(nift::runtime_compare_numbers(tiny, nift::RuntimeValue(0.0)) > 0);
    assert(!nift::runtime_numbers_equal(tiny, nift::RuntimeValue(0.0)));
    assert(nift::runtime_numeric_fingerprint(tiny) !=
           nift::runtime_numeric_fingerprint(tiny_next));
    assert(nift::runtime_numeric_fingerprint(tiny) !=
           nift::runtime_numeric_fingerprint(nift::RuntimeValue(0.0)));

    const auto negative_tiny = exact_number("-100e-1002");
    assert(nift::runtime_numbers_equal(negative_tiny, exact_number("-1e-1000")));
    assert(nift::runtime_numeric_fingerprint(negative_tiny) ==
           nift::runtime_numeric_fingerprint(exact_number("-1e-1000")));

    const auto decimal = exact_number("1.23000e+5");
    const auto integer_spelling = exact_number("123000");
    const auto alternate_exponent = exact_number("0.123e6");
    assert(nift::runtime_numbers_equal(decimal, integer_spelling));
    assert(nift::runtime_numbers_equal(decimal, alternate_exponent));
    assert(nift::runtime_numeric_fingerprint(decimal) ==
           nift::runtime_numeric_fingerprint(integer_spelling));
    assert(nift::runtime_numbers_equal(nift::RuntimeValue(1.5), exact_number("15e-1")));
    assert(nift::runtime_numeric_fingerprint(nift::RuntimeValue(1.5)) ==
           nift::runtime_numeric_fingerprint(exact_number("15e-1")));

    const auto enormous_exponent = exact_number("1e999999999999999999999999999999");
    const auto enormous_exponent_equivalent = exact_number("10e999999999999999999999999999998");
    assert(nift::runtime_numbers_equal(enormous_exponent, enormous_exponent_equivalent));
    assert(nift::runtime_numeric_fingerprint(enormous_exponent) ==
           nift::runtime_numeric_fingerprint(enormous_exponent_equivalent));

    const nift::RuntimeValue nan(std::numeric_limits<double>::quiet_NaN());
    const nift::RuntimeValue another_nan(std::numeric_limits<double>::quiet_NaN());
    const nift::RuntimeValue positive_infinity(std::numeric_limits<double>::infinity());
    const nift::RuntimeValue negative_infinity(-std::numeric_limits<double>::infinity());
    assert(!nift::runtime_numbers_equal(nan, nan));
    assert(!nift::runtime_equal(nan, nan));
    assert(nift::runtime_fingerprint(nan) == nift::runtime_fingerprint(another_nan));
    assert(!nift::runtime_equal(nan, another_nan));
    assert(nift::runtime_numbers_equal(positive_infinity, positive_infinity));
    assert(nift::runtime_numbers_equal(negative_infinity, negative_infinity));
    assert(!nift::runtime_numbers_equal(positive_infinity, negative_infinity));
    assert(nift::runtime_compare_numbers(negative_infinity, huge) < 0);
    assert(nift::runtime_compare_numbers(positive_infinity, huge) > 0);
    assert(nift::runtime_compare_numbers(nan, nan) == 0);
    assert(nift::runtime_compare_numbers(huge, nan) < 0);
    assert(nift::runtime_compare_numbers(nan, huge) > 0);
    int relational = 0;
    assert(!nift::runtime_compare_numbers_relational(nan, nan, relational));
    assert(!nift::runtime_compare_numbers_relational(nan, huge, relational));
    assert(!nift::runtime_compare_numbers_relational(huge, nan, relational));
    assert(nift::runtime_compare_numbers_relational(negative_infinity, positive_infinity,
                                                     relational));
    assert(relational < 0);
    assert(nift::runtime_numeric_fingerprint(positive_infinity) == "+inf");
    assert(nift::runtime_numeric_fingerprint(negative_infinity) == "-inf");
    assert(nift::runtime_numeric_fingerprint(nan) == "nan");

    assert(!nift::runtime_number_is_zero(tiny));
    assert(nift::runtime_truthy(tiny));
    assert(!nift::runtime_number_is_integer(tiny));
    assert(nift::runtime_number_is_integer(decimal));
    assert(nift::runtime_number_is_integer(exact_number("-0e-999999999999999999")));
    std::size_t size = 0;
    assert(nift::runtime_number_to_size(exact_number("00012e1"), size) && size == 120);
    assert(!nift::runtime_number_to_size(tiny, size));
    assert(!nift::runtime_number_to_size(exact_number("1e999999999999"), size));
    assert(!nift::runtime_number_to_size(exact_number("-1"), size));
    std::int64_t signed_value = 0;
    assert(nift::runtime_number_to_i64(exact_number("9223372036854775807"), signed_value) &&
           signed_value == std::numeric_limits<std::int64_t>::max());
    assert(nift::runtime_number_to_i64(exact_number("-9223372036854775808"), signed_value) &&
           signed_value == std::numeric_limits<std::int64_t>::min());
    assert(!nift::runtime_number_to_i64(exact_number("9223372036854775808"), signed_value));
    assert(!nift::runtime_number_to_i64(tiny, signed_value));
    assert(nift::runtime_number_to_signed(exact_number("-128"), 8, signed_value) &&
           signed_value == -128);
    assert(nift::runtime_number_to_signed(exact_number("127"), 8, signed_value) &&
           signed_value == 127);
    assert(!nift::runtime_number_to_signed(exact_number("-129"), 8, signed_value));
    assert(!nift::runtime_number_to_signed(exact_number("128"), 8, signed_value));
    std::uint64_t unsigned_value = 0;
    assert(nift::runtime_number_to_unsigned(exact_number("18446744073709551615"), 64,
                                            unsigned_value) &&
           unsigned_value == std::numeric_limits<std::uint64_t>::max());
    assert(nift::runtime_number_to_unsigned(exact_number("255"), 8, unsigned_value) &&
           unsigned_value == 255);
    assert(!nift::runtime_number_to_unsigned(exact_number("256"), 8, unsigned_value));
    assert(!nift::runtime_number_to_unsigned(exact_number("-1"), 64, unsigned_value));
    assert(!nift::runtime_number_to_unsigned(tiny, 64, unsigned_value));
    json::Document integer_boundaries;
    assert(json::Document::parse(
        "[-9223372036854775808,18446744073709551615]", integer_boundaries, error));
    const auto runtime_integer_boundaries = nift::runtime_from_json(integer_boundaries);
    assert(nift::runtime_number_to_signed(runtime_integer_boundaries.array[0], 64, signed_value) &&
           signed_value == std::numeric_limits<std::int64_t>::min());
    assert(nift::runtime_number_to_unsigned(runtime_integer_boundaries.array[1], 64, unsigned_value) &&
           unsigned_value == std::numeric_limits<std::uint64_t>::max());
    const auto signed_runtime = nift::runtime_integer(std::numeric_limits<std::int64_t>::max());
    assert(signed_runtime.type == nift::RuntimeType::StrNumber);
    assert(signed_runtime.string == "9223372036854775807");
    assert(signed_runtime.num == static_cast<double>(std::numeric_limits<std::int64_t>::max()));
    const auto unsigned_runtime = nift::runtime_unsigned_integer(
        std::numeric_limits<std::uint64_t>::max());
    assert(unsigned_runtime.type == nift::RuntimeType::StrNumber);
    assert(unsigned_runtime.string == "18446744073709551615");
    assert(unsigned_runtime.num == static_cast<double>(std::numeric_limits<std::uint64_t>::max()));
    assert(nift::runtime_unsigned_integer(42).type == nift::RuntimeType::Number);
    const auto negated_tiny = nift::runtime_number_negate(tiny);
    assert(negated_tiny.type == nift::RuntimeType::StrNumber && negated_tiny.string == "-1e-1000");
    assert(nift::runtime_number_negate(negated_tiny).string == "1e-1000");

    nift::RuntimeValue duplicate_left = nift::RuntimeValue::make_object();
    duplicate_left.object.emplace_back("k", nift::RuntimeValue(1));
    duplicate_left.object.emplace_back("k", nift::RuntimeValue(2));
    nift::RuntimeValue duplicate_reordered = nift::RuntimeValue::make_object();
    duplicate_reordered.object.emplace_back("k", nift::RuntimeValue(2));
    duplicate_reordered.object.emplace_back("k", nift::RuntimeValue(1));
    nift::RuntimeValue duplicate_different = nift::RuntimeValue::make_object();
    duplicate_different.object.emplace_back("k", nift::RuntimeValue(1));
    duplicate_different.object.emplace_back("k", nift::RuntimeValue(1));
    assert(nift::runtime_equal(duplicate_left, duplicate_reordered));
    assert(!nift::runtime_equal(duplicate_left, duplicate_different));
    assert(nift::runtime_fingerprint(duplicate_left) ==
           nift::runtime_fingerprint(duplicate_reordered));

    const nift::RuntimeValue empty_bytes(nift::RuntimeBytes{});
    const nift::RuntimeValue binary(nift::RuntimeBytes{0x00, 0x7f, 0x80, 0xff});
    const nift::RuntimeValue binary_copy = binary;
    assert(empty_bytes.is_bytes() && !nift::runtime_truthy(empty_bytes));
    assert(!nift::runtime_fingerprint(empty_bytes).empty());
    assert(binary.is_bytes() && nift::runtime_truthy(binary));
    assert(binary.bytes == binary_copy.bytes);
    assert(nift::runtime_equal(binary, binary_copy));
    assert(!nift::runtime_equal(binary, nift::RuntimeValue(std::string("\0\x7f\x80\xff", 4))));
    nift::RuntimeValue byte_array = nift::RuntimeValue::make_array();
    byte_array.array = {nift::RuntimeValue(0), nift::RuntimeValue(127),
                        nift::RuntimeValue(128), nift::RuntimeValue(255)};
    assert(!nift::runtime_equal(binary, byte_array));
    assert(nift::runtime_fingerprint(binary) != nift::runtime_fingerprint(
        nift::RuntimeValue(std::string("\0\x7f\x80\xff", 4))));
    assert(nift::runtime_fingerprint(binary) != nift::runtime_fingerprint(
        nift::RuntimeValue(nift::RuntimeBytes{0x00, 0x7f, 0x80, 0xfe})));

    nift::RuntimeValue nested_bytes = nift::RuntimeValue::make_object();
    nested_bytes["payload"] = binary;
    assert(nift::runtime_equal(nested_bytes, nested_bytes));
    json::Document unchanged(true);
    std::string bytes_error;
    assert(!nift::runtime_to_json(binary, unchanged, bytes_error));
    assert(unchanged.is_bool() && unchanged.boolean);
    assert(bytes_error == "bytes values are not JSON serializable");
    assert(!nift::runtime_to_json(nested_bytes, unchanged, bytes_error));
    bool threw = false;
    try { (void)nift::runtime_to_json(binary); }
    catch (const std::runtime_error& e) {
        threw = std::string(e.what()) == "bytes values are not JSON serializable";
    }
    assert(threw);
    // Conversion rejects any nested timer before other unsupported values,
    // preserves caller output on failure, and keeps wide/deep mixed structure.
    auto timer_mixture = nift::RuntimeValue::make_array();
    timer_mixture.array = {binary, runtime_error, nift::RuntimeValue::make_timer(3, 4)};
    json::Document timer_unchanged(true);
    std::string timer_error;
    assert(!nift::runtime_to_json(timer_mixture, timer_unchanged, timer_error));
    assert(timer_error == "timer values are not JSON serializable");
    assert(timer_unchanged.is_bool() && timer_unchanged.boolean);
    auto wide = nift::RuntimeValue::make_object();
    for (int i = 0; i < 200; ++i) wide.object.emplace_back(std::to_string(i), value);
    auto deep = wide;
    for (int i = 0; i < 48; ++i) {
        auto parent = nift::RuntimeValue::make_array();
        parent.array.push_back(std::move(deep));
        deep = std::move(parent);
    }
    json::Document deep_json;
    std::string deep_error = "previous error";
    assert(nift::runtime_to_json(deep, deep_json, deep_error));
    assert(deep_error.empty());
    assert(nift::runtime_equal(deep, nift::runtime_from_json(deep_json)));
    auto nested_timer = timer_mixture;
    for (int i = 0; i < 48; ++i) {
        auto parent = nift::RuntimeValue::make_object();
        parent.object.emplace_back("child", std::move(nested_timer));
        nested_timer = std::move(parent);
    }
    assert(!nift::runtime_to_json(nested_timer, timer_unchanged, timer_error));
    assert(timer_error == "timer values are not JSON serializable");
    assert(timer_unchanged.is_bool() && timer_unchanged.boolean);

}
