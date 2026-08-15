#include <minify/Minify.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

bool write_file(const fs::path& path, const std::string& value) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << value;
    return static_cast<bool>(out);
}

fs::path minified_path(const fs::path& input) {
    const std::string ext = input.extension().string();
    fs::path output = input.parent_path() / input.stem();
    output += ".min" + ext;
    return output;
}

void help() {
    std::cout
        << "minify - conservative multi-format minifier\n\n"
        << "Usage: minify [--in-place|-i] <files...>\n\n"
        << "By default foo.js is written to foo.min.js.\n"
        << "Use --in-place (or -i) to overwrite the source file.\n";
}
}

int main(int argc, char** argv) {
    bool in_place = false;
    std::vector<fs::path> files;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { help(); return 0; }
        if (arg == "--version" || arg == "-v") {
            std::cout << "Minify++ 1.1.0\n";
            return 0;
        }
        if (arg == "--in-place" || arg == "-i") { in_place = true; continue; }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "minify: unknown option '" << arg << "'\n";
            return 2;
        }
        files.emplace_back(arg);
    }
    if (files.empty()) { help(); return 1; }

    bool failed = false;
    for (const auto& input : files) {
        std::error_code ec;
        if (!fs::is_regular_file(input, ec)) {
            std::cerr << "minify: cannot read '" << input.string() << "'\n";
            failed = true; continue;
        }
        minify::Format format;
        if (!minify::format_for_extension(input.extension().string(), format)) {
            std::cerr << "minify: unsupported extension '" << input.extension().string() << "'\n";
            failed = true; continue;
        }
        const std::string source = read_file(input);
        std::string output, error;
        if (!minify::run(format, source, output, error)) {
            std::cerr << "minify: " << input.string() << ": " << error << "\n";
            failed = true; continue;
        }
        const fs::path destination = in_place ? input : minified_path(input);
        if (!write_file(destination, output)) {
            std::cerr << "minify: cannot write '" << destination.string() << "'\n";
            failed = true; continue;
        }
        std::cout << input.string() << " -> " << destination.string() << "\n";
    }
    return failed ? 1 : 0;
}
