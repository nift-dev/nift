#include "json.h"

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::size_t current_rss_kib() {
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string key;
    while (status >> key) {
        if (key == "VmRSS:") {
            std::size_t kib = 0;
            std::string unit;
            status >> kib >> unit;
            return kib;
        }
        status.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
#endif
    return 0;
}

void must_parse(const std::string& source, json::Document& out) {
    std::string error;
    const bool ok = json::Document::parse(source, out, error);
    if (!ok) {
        std::cerr << "unexpected parse failure: " << error << "\n";
        std::abort();
    }
}

void must_reject(const std::string& source) {
    json::Document out;
    std::string error;
    if (json::Document::parse(source, out, error) || error.empty()) {
        std::cerr << "unexpected acceptance: " << source.substr(0, 120) << "\n";
        std::abort();
    }
}

std::string make_large_document() {
    std::string source = R"({"name":"large","items":[)";
    for (int i = 0; i < 2048; ++i) {
        if (i) source += ',';
        source += R"({"id":)" + std::to_string(i) +
                  R"(,"enabled":true,"label":"item-)" + std::to_string(i) +
                  R"(-\\n\\u263A","values":[0,1,2,3,4,5,6,7]})";
    }
    source += R"(],"tail":"done"})";
    return source;
}

std::string make_deep_document(int depth) {
    std::string source;
    source.reserve(static_cast<std::size_t>(depth) * 2 + 1);
    source.append(static_cast<std::size_t>(depth), '[');
    source += '0';
    source.append(static_cast<std::size_t>(depth), ']');
    return source;
}

std::string make_stream_document(int count) {
    std::string source = R"({"meta":{"version":1},"tracked":[)";
    for (int i = 0; i < count; ++i) {
        if (i) source += ',';
        source += R"({"name":"entry-)" + std::to_string(i) +
                  R"(","output":"public/)" + std::to_string(i) + R"(.html"})";
    }
    source += R"(],"after":[true,false,null]})";
    return source;
}

void valid_parse_roundtrip_pressure(const std::string& large, const std::string& deep) {
    const std::vector<std::string> samples = {
        "null", "true", "false", "0", "-123.5e2", R"("text\\n\\u263A")",
        R"([])", R"({})", R"([1,true,null,"x",{"a":[2,3]}])",
        R"({"nested":{"object":{"array":[1,2,3]}}})"
    };

    for (const auto& source : samples) {
        json::Document value;
        must_parse(source, value);
        const std::string dumped = value.dump();
        json::Document reparsed;
        must_parse(dumped, reparsed);
        assert(!dumped.empty());
    }

    json::Document value;
    must_parse(large, value);
    assert(value.is_object());
    assert(value["items"].array.size() == 2048);
    const std::string dumped = value.dump(0);
    json::Document reparsed;
    must_parse(dumped, reparsed);
    assert(reparsed["tail"].string == "done");

    must_parse(deep, value);
    assert(value.is_array());
}

void malformed_cleanup_pressure() {
    const std::vector<std::string> invalid = {
        "", " ", "[", "{", "true false", "+1", ".1", "01", "-01", "1.", "1e", "1e+", "--1",
        "[1,]", "[1,,2]", "[1 2]", R"({"a":})", R"({"a":1 "b":2})",
        R"({"a":1,})", R"({"a":1,"a":2})", R"({a:1})", R"("\x")",
        R"("\uD800")", R"("\uDC00")", R"("\uD800\u0041")",
        std::string("\"raw\nnewline\"")
    };
    for (const auto& source : invalid) must_reject(source);

    // Force failures after substantial partial allocation, not only at byte 0.
    std::string partial_array = "[";
    for (int i = 0; i < 1024; ++i) {
        if (i) partial_array += ',';
        partial_array += R"({"id":)" + std::to_string(i) + R"(,"payload":"xxxxxxxxxxxxxxxx"})";
    }
    partial_array += ",]";
    must_reject(partial_array);

    std::string partial_object = "{";
    for (int i = 0; i < 512; ++i) {
        if (i) partial_object += ',';
        partial_object += "\"key" + std::to_string(i) + "\":[1,2,3,4]";
    }
    partial_object += R"(,"broken":[1,2,})";
    must_reject(partial_object);
}

void mutation_copy_move_pressure() {
    json::Document root;
    root["title"] = "lifetime";
    root["count"] = 1;
    root["enabled"] = true;
    root["items"] = json::Document::make_array();
    for (int i = 0; i < 256; ++i) {
        json::Document item = json::Document::make_object();
        item["id"] = i;
        item["name"] = std::string("item-") + std::to_string(i);
        item["values"] = json::Document::make_array();
        for (int j = 0; j < 8; ++j) item["values"].push_back(j);
        root["items"].push_back(item);
    }

    json::Document copied = root;
    json::Document moved = std::move(copied);
    json::Document assigned;
    assigned = moved;
    json::Document move_assigned;
    move_assigned = std::move(assigned);
    assert(move_assigned["items"].array.size() == 256);

    const std::string dumped = move_assigned.dump(0);
    json::Document reparsed;
    must_parse(dumped, reparsed);
    assert(reparsed["title"].string == "lifetime");

    // Repeatedly change the same owning value between major representation types.
    json::Document changing;
    changing = nullptr;
    changing = json::Document::make_object();
    changing["payload"] = std::string(32768, 'x');
    changing = json::Document::make_array();
    for (int i = 0; i < 128; ++i) changing.push_back(std::string(256, 'y'));
    changing = "small";
    assert(changing.is_string());
}

void streaming_pressure(const std::string& stream) {
    std::string error;
    int count = 0;
    bool ok = json::Document::for_each_array_item(
        stream, "tracked",
        [&](json::Document&& item) {
            assert(item.is_object());
            assert(item["name"].is_string());
            ++count;
            return true;
        }, error);
    assert(ok);
    assert(count == 2048);

    count = 0;
    error.clear();
    ok = json::Document::for_each_array_item(
        stream, "tracked",
        [&](json::Document&& item) {
            assert(item["output"].is_string());
            ++count;
            return count < 17;
        }, error);
    assert(!ok);
    assert(count == 17);
    assert(!error.empty());

    // Fail after many successfully streamed items, exercising parser/callback cleanup.
    std::string malformed = stream;
    const std::string marker = "],\"after\":";
    const std::size_t close = malformed.find(marker);
    assert(close != std::string::npos);
    malformed.replace(close, 1, ",BROKEN]");
    count = 0;
    error.clear();
    ok = json::Document::for_each_array_item(
        malformed, "tracked",
        [&](json::Document&&) { ++count; return true; }, error);
    assert(!ok);
    assert(count == 2048);
    assert(!error.empty());
}

} // namespace

int main(int argc, char** argv) {
    int iterations = 200;
    if (argc == 3 && std::string(argv[1]) == "--iterations") {
        iterations = std::stoi(argv[2]);
    } else if (argc != 1) {
        std::cerr << "usage: " << argv[0] << " [--iterations N]\n";
        return 2;
    }
    if (iterations < 10) {
        std::cerr << "iterations must be >= 10\n";
        return 2;
    }

    const std::string large = make_large_document();
    const std::string deep = make_deep_document(128);
    const std::string stream = make_stream_document(2048);

    const int warmup = std::max(5, iterations / 10);
    std::size_t rss_after_warmup = 0;
    std::size_t rss_midpoint = 0;

    for (int i = 0; i < iterations; ++i) {
        valid_parse_roundtrip_pressure(large, deep);
        malformed_cleanup_pressure();
        mutation_copy_move_pressure();
        streaming_pressure(stream);

        if (i + 1 == warmup) rss_after_warmup = current_rss_kib();
        if (i + 1 == iterations / 2) rss_midpoint = current_rss_kib();
    }

    const std::size_t rss_final = current_rss_kib();
    std::cout << "Jsonic++ lifetime corpus passed: iterations=" << iterations
              << " warmup=" << warmup
              << " rss_after_warmup_kib=" << rss_after_warmup
              << " rss_midpoint_kib=" << rss_midpoint
              << " rss_final_kib=" << rss_final << "\n";
    std::cout << "RSS is observational evidence only; sanitizer findings remain the lifetime failure oracle.\n";
    return 0;
}
