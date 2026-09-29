#include "nift/c_abi.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

nift_script_result* evaluate(nift_engine* engine, const std::string& expression) {
    nift_script_result* result = nullptr;
    CHECK(nift_engine_evaluate(engine, expression.data(), expression.size(), &result) == NIFT_OK);
    CHECK(result != nullptr);
    CHECK(nift_script_result_ok(result) == 1);
    return result;
}

void test_setters_and_lifetime() {
    nift_engine* engine = nift_engine_new();
    CHECK(engine != nullptr);
    std::vector<std::uint8_t> source{0, 65, 127, 128, 255};
    const std::vector<std::uint8_t> expected = source;
    CHECK(nift_engine_set_bytes(engine, "payload", 7, source.data(), source.size()) == NIFT_OK);
    std::fill(source.begin(), source.end(), 9);

    nift_script_result* result = evaluate(engine, "payload");
    nift_bytes view{};
    CHECK(nift_script_result_value_bytes(result, &view) == NIFT_OK);
    CHECK(view.length == expected.size());
    CHECK(std::memcmp(view.data, expected.data(), expected.size()) == 0);
    const std::uint8_t* first_data = view.data;
    CHECK(nift_script_result_value_bytes(result, nullptr) == NIFT_OK);
    CHECK(nift_script_result_value_bytes(result, &view) == NIFT_OK);
    CHECK(view.data == first_data);

    nift_string json{reinterpret_cast<const char*>(1), 1};
    CHECK(nift_script_result_value_json(result, &json) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(json.data == nullptr && json.length == 0);
    CHECK(nift_script_result_ok(result) == 1);

    nift_script_result* later = evaluate(engine, "bytes([9])");
    nift_engine_free(engine);
    CHECK(std::memcmp(view.data, expected.data(), expected.size()) == 0);
    nift_script_result_free(later);
    nift_script_result_free(result);

    engine = nift_engine_new();
    CHECK(nift_engine_set_bytes(engine, "empty", 5, nullptr, 0) == NIFT_OK);
    std::uint8_t ignored = 7;
    CHECK(nift_engine_set_bytes(engine, "empty2", 6, &ignored, 0) == NIFT_OK);
    result = evaluate(engine, "empty");
    view = {reinterpret_cast<const std::uint8_t*>(1), 1};
    CHECK(nift_script_result_value_bytes(result, &view) == NIFT_OK);
    CHECK(view.data == nullptr && view.length == 0);
    nift_script_result_free(result);

    std::vector<std::uint8_t> large(65536);
    for (std::size_t i = 0; i < large.size(); ++i) large[i] = static_cast<std::uint8_t>(i);
    CHECK(nift_engine_set_bytes(engine, "large", 5, large.data(), large.size()) == NIFT_OK);
    result = evaluate(engine, "large");
    CHECK(nift_script_result_value_bytes(result, &view) == NIFT_OK);
    CHECK(view.length == large.size() && view.data[0] == 0 && view.data[65535] == 255);
    nift_script_result_free(result);

    CHECK(nift_engine_set_bytes(nullptr, "x", 1, nullptr, 0) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(nift_engine_set_bytes(engine, "x", 1, nullptr, 1) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(nift_engine_set_bytes(engine, nullptr, 0, large.data(), 1) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(nift_engine_set_bytes(engine, "9bad", 4, large.data(), 1) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(nift_engine_set_bytes(engine, "name", 4, large.data(), 1) == NIFT_ERROR_INVALID_ARGUMENT);
    nift_engine_free(engine);
}

void test_context_setter() {
    nift_engine* engine = nift_engine_new();
    nift_context* context = nift_context_new();
    std::vector<std::uint8_t> bytes{0, 65, 127, 128, 255};
    CHECK(nift_context_set_bytes(context, "payload", 7, bytes.data(), bytes.size()) == NIFT_OK);
    std::fill(bytes.begin(), bytes.end(), 1);
    const std::string text =
        "@script { return type(payload) + \"|\" + payload.length() + \"|\" + payload[4] }";
    nift_render_result* result = nullptr;
    CHECK(nift_engine_render_text(engine, context, text.data(), text.size(), &result) == NIFT_OK);
    CHECK(result != nullptr && nift_render_result_ok(result) == 1);
    nift_string output{};
    CHECK(nift_render_result_output(result, &output) == NIFT_OK);
    const std::string rendered(output.data, output.length);
    CHECK(rendered == "bytes|5|255");
    nift_render_result_free(result);

    CHECK(nift_context_set_bytes(context, "empty", 5, nullptr, 0) == NIFT_OK);
    CHECK(nift_context_set_bytes(nullptr, "x", 1, nullptr, 0) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(nift_context_set_bytes(context, "x", 1, nullptr, 1) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(nift_context_set_bytes(context, nullptr, 0, bytes.data(), 1) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(nift_context_set_bytes(context, "loop", 4, bytes.data(), 1) == NIFT_ERROR_INVALID_ARGUMENT);
    nift_context_free(context);
    nift_engine_free(engine);
}

void test_result_accessors() {
    nift_engine* engine = nift_engine_new();
    nift_script_result* result = nullptr;
    const std::string script = "return bytes([0,255]);";
    CHECK(nift_engine_execute(engine, script.data(), script.size(), "cp21", 4,
                              nullptr, nullptr, 0, &result) == NIFT_OK);
    CHECK(result != nullptr && nift_script_result_ok(result) == 1);
    nift_bytes view{};
    CHECK(nift_script_result_value_bytes(result, &view) == NIFT_OK);
    CHECK(view.length == 2 && view.data[0] == 0 && view.data[1] == 255);
    std::atomic<bool> bytes_concurrent_ok{true};
    std::vector<std::thread> byte_readers;
    for (int i = 0; i < 8; ++i) {
        byte_readers.emplace_back([&] {
            for (int j = 0; j < 100; ++j) {
                nift_bytes current{};
                if (nift_script_result_value_bytes(result, &current) != NIFT_OK ||
                    current.length != 2 || current.data[0] != 0 || current.data[1] != 255) {
                    bytes_concurrent_ok = false;
                }
            }
        });
    }
    for (auto& reader : byte_readers) reader.join();
    CHECK(bytes_concurrent_ok.load());
    nift_script_result_free(result);

    result = evaluate(engine, "[bytes([1])]");
    nift_string json{reinterpret_cast<const char*>(1), 1};
    CHECK(nift_script_result_value_json(result, &json) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(json.data == nullptr && json.length == 0);
    view = {reinterpret_cast<const std::uint8_t*>(1), 1};
    CHECK(nift_script_result_value_bytes(result, &view) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(view.data == nullptr && view.length == 0);
    CHECK(nift_script_result_ok(result) == 1);
    nift_script_result_free(result);

    result = evaluate(engine, "[1,2]");
    const std::string expected_json = "[\n1,\n2\n]";
    std::atomic<bool> concurrent_ok{true};
    std::vector<std::thread> readers;
    for (int i = 0; i < 8; ++i) {
        readers.emplace_back([&] {
            for (int j = 0; j < 100; ++j) {
                nift_string current{};
                if (nift_script_result_value_json(result, &current) != NIFT_OK ||
                    std::string(current.data, current.length) != expected_json) {
                    concurrent_ok = false;
                }
            }
        });
    }
    for (auto& reader : readers) reader.join();
    CHECK(concurrent_ok.load());
    CHECK(nift_script_result_value_json(result, &json) == NIFT_OK);
    CHECK(std::string(json.data, json.length) == expected_json);
    const char* first_json = json.data;
    CHECK(nift_script_result_value_json(result, nullptr) == NIFT_OK);
    CHECK(nift_script_result_value_json(result, &json) == NIFT_OK && json.data == first_json);
    nift_script_result_free(result);

    result = nullptr;
    const std::string invalid = "missing_cp21_value";
    CHECK(nift_engine_evaluate(engine, invalid.data(), invalid.size(), &result) == NIFT_OK);
    CHECK(result != nullptr && nift_script_result_ok(result) == 0);
    json = {reinterpret_cast<const char*>(1), 1};
    view = {reinterpret_cast<const std::uint8_t*>(1), 1};
    CHECK(nift_script_result_value_json(result, &json) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(json.data == nullptr && json.length == 0);
    CHECK(nift_script_result_value_bytes(result, &view) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(view.data == nullptr && view.length == 0);
    nift_script_result_free(result);

    view = {reinterpret_cast<const std::uint8_t*>(1), 1};
    CHECK(nift_script_result_value_bytes(nullptr, &view) == NIFT_ERROR_INVALID_ARGUMENT);
    CHECK(view.data == nullptr && view.length == 0);
    nift_engine_free(engine);
}

}  // namespace

int main() {
    CHECK(std::string(nift_abi_version()) == "1.2");
    CHECK(nift_abi_version_major() == 1);
    CHECK(nift_abi_version_minor() == 2);
    test_setters_and_lifetime();
    test_context_setter();
    test_result_accessors();
    if (failures != 0) {
        std::fprintf(stderr, "CP21 bytes embedding failed: %d check(s)\n", failures);
        return 1;
    }
    std::puts("CP21 bytes embedding: PASS");
    return 0;
}
