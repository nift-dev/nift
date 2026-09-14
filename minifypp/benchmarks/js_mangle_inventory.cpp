#include <minify/Minify.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: js-mangle-inventory FILE...\n";
        return 2;
    }
    for (int argument = 1; argument < argc; ++argument) {
        std::ifstream input(argv[argument], std::ios::binary);
        if (!input) return 1;
        const std::string source{std::istreambuf_iterator<char>(input), {}};
        std::string report, error;
        if (!minify::javascript_mangle_report(source, report, error)) {
            std::cerr << argv[argument] << ": " << error << '\n';
            return 1;
        }
        std::cout << argv[argument] << '\t' << report << '\n';
    }
}
