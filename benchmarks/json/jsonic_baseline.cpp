#include "../../src/Json.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: jsonic_baseline FILE\n"; return 2; }
    using clock = std::chrono::steady_clock;
    const auto io0 = clock::now();
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) { std::cerr << "open failed\n"; return 2; }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto io1 = clock::now();
    json::Document document;
    std::string error;
    const auto parse0 = clock::now();
    if (!nift_json::parse(text, document, error)) { std::cerr << error << '\n'; return 1; }
    const auto parse1 = clock::now();
    double total = 0.0;
    const auto traverse0 = clock::now();
    if (document.is_array()) for (const auto& item : document.array) if (item.is_object() && item.has("v")) total += item["v"].num;
    const auto traverse1 = clock::now();
    const auto us = [](auto a, auto b) { return std::chrono::duration_cast<std::chrono::microseconds>(b-a).count(); };
    std::cout << "bytes=" << text.size() << " values=" << (document.is_array() ? document.array.size() : 0)
              << " io_us=" << us(io0, io1) << " parse_us=" << us(parse0, parse1)
              << " traverse_us=" << us(traverse0, traverse1) << " sum=" << total << '\n';
}
