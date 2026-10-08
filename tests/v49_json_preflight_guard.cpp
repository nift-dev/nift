#include "RuntimeJson.h"
#include <cassert>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const int n = std::stoi(argv[1]);
    auto value = nift::RuntimeValue::make_array();
    for (int i = 0; i < n; ++i) value.array.emplace_back(i);
    for (int i = 0; i < 48; ++i) {
        auto parent = nift::RuntimeValue::make_array();
        parent.array.push_back(std::move(value));
        value = std::move(parent);
    }
    json::Document document;
    std::string error;
    assert(nift::runtime_to_json(value, document, error));
    const auto* leaf = &document;
    for (int i = 0; i < 48; ++i) { assert(leaf->is_array() && leaf->array.size() == 1); leaf = &leaf->array[0]; }
    assert(leaf->array.size() == static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) assert(leaf->array[i].num == i);
    std::cout << n << '\n';
}
