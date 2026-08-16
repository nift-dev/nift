#include <minify/Minify.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
std::uint64_t next(std::uint64_t& state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

std::string mutate(std::string value, std::uint64_t& state) {
    static const std::string bytes = " <>/{}[]()'\"`\\;:,.+-*$@!?=\n\r\t09azAZ&%#_";
    const unsigned edits = 1 + static_cast<unsigned>(next(state) % 8);
    for (unsigned edit = 0; edit < edits; ++edit) {
        const unsigned operation = static_cast<unsigned>(next(state) % 4);
        const std::size_t position = value.empty() ? 0 : static_cast<std::size_t>(next(state) % (value.size() + 1));
        if (operation == 0 && value.size() < 4096) {
            value.insert(position, 1, bytes[static_cast<std::size_t>(next(state) % bytes.size())]);
        } else if (operation == 1 && !value.empty()) {
            value.erase(position == value.size() ? position - 1 : position, 1);
        } else if (operation == 2 && !value.empty()) {
            value[position == value.size() ? position - 1 : position] =
                bytes[static_cast<std::size_t>(next(state) % bytes.size())];
        } else if (!value.empty() && value.size() < 4096) {
            const std::size_t start = static_cast<std::size_t>(next(state) % value.size());
            const std::size_t length = std::min<std::size_t>(value.size() - start, 1 + next(state) % 16);
            value.insert(position, value.substr(start, length));
        }
    }
    return value;
}
}

int main(int argc, char** argv) {
    std::size_t cases_per_format = 10000;
    if (argc == 2) cases_per_format = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
    if (cases_per_format == 0) return 2;

    const std::vector<std::pair<minify::Format, std::string>> seeds = {
        {minify::Format::Html, "<!doctype html><p class=\"x\">a <b>b</b></p><script>const r=/[<>]/;</script>"},
        {minify::Format::Css, "@layer x { .a { width: calc(100% - 2rem); --x: 1  2; } }"},
        {minify::Format::JavaScript, "function f(){return\n{x:1}} const r=/https?:\\/\\//; console.log(r);"},
        {minify::Format::Jsx, "const x=<Comp<Map<string,number>> value={a?.b ?? /[<>]/.test(s)} />;"},
        {minify::Format::Json, "{\"items\":[1,true,null,{\"text\":\"a  b\"}]}"},
        {minify::Format::Xml, "<?xml version=\"1.0\"?><r><![CDATA[a < b]]><x> a  b </x></r>"},
        {minify::Format::Svg, "<svg xmlns=\"http://www.w3.org/2000/svg\"><text>a  b</text><path d=\"M 0 0\"/></svg>"}
    };

    std::uint64_t state = 0x9e3779b97f4a7c15ULL;
    std::size_t completed = 0;
    for (const auto& seed : seeds) {
        for (std::size_t index = 0; index < cases_per_format; ++index) {
            const std::string input = mutate(seed.second, state);
            std::string output, error;
            if (minify::run(seed.first, input, output, error)) {
                std::string second, second_error;
                if (!minify::run(seed.first, output, second, second_error)) {
                    std::cerr << "successful output rejected on second pass at case " << completed
                              << ": " << second_error << "\ninput:\n" << input
                              << "\nfirst output:\n" << output << '\n';
                    return 1;
                }
            }
            ++completed;
        }
    }
    std::cout << "Minify++ deterministic fuzz smoke passed (" << completed << " cases)\n";
}
