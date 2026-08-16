#include <minify/Minify.h>

#include <filesystem>
#include <fstream>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {
bool read_file(const fs::path& path, std::string& value) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream out;
    out << in.rdbuf();
    if (in.bad()) return false;
    value = out.str();
    return true;
}

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(fs::path path) : path_(std::move(path)) {}
    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
};

bool make_temporary_directory(const fs::path& destination,
                              fs::path& temporary,
                              std::error_code& error) {
    const fs::path parent = destination.has_parent_path() ? destination.parent_path() : fs::path(".");
    const std::string stem = "." + destination.filename().string() + ".minify-tmp-";
    const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        temporary = parent / (stem + std::to_string(seed) + "-" + std::to_string(attempt));
        error.clear();
        if (fs::create_directory(temporary, error)) return true;
        if (error && error != std::errc::file_exists) return false;
    }
    error = std::make_error_code(std::errc::file_exists);
    return false;
}

bool write_file(const fs::path& path, const std::string& value, std::string& error) {
    std::error_code ec;
    const fs::file_status destination_status = fs::symlink_status(path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        error = ec.message();
        return false;
    }
    const bool destination_exists = !ec && fs::exists(destination_status);
    if (destination_exists && fs::is_symlink(destination_status)) {
        error = "refusing to replace a symbolic link";
        return false;
    }
    if (destination_exists && !fs::is_regular_file(destination_status)) {
        error = "destination is not a regular file";
        return false;
    }

    fs::path temporary_path;
    if (!make_temporary_directory(path, temporary_path, ec)) {
        error = "cannot create temporary directory: " + ec.message();
        return false;
    }
    TemporaryDirectory temporary(temporary_path);
    const fs::path candidate = temporary.path() / "output";
    {
        std::ofstream out(candidate, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot create temporary output";
            return false;
        }
        out.write(value.data(), static_cast<std::streamsize>(value.size()));
        out.close();
        if (!out) {
            error = "failed while writing temporary output";
            return false;
        }
    }

    if (destination_exists) {
        fs::permissions(candidate, destination_status.permissions(), ec);
        if (ec) {
            error = "cannot preserve destination permissions: " + ec.message();
            return false;
        }
    }

    fs::rename(candidate, path, ec);
    if (!ec) return true;

    // Standard filesystem rename does not replace an existing file on every
    // supported platform. Fall back to a recoverable backup/restore sequence.
    if (!destination_exists) {
        error = ec.message();
        return false;
    }
    const fs::path previous = temporary.path() / "previous";
    ec.clear();
    fs::rename(path, previous, ec);
    if (ec) {
        error = "cannot prepare destination replacement: " + ec.message();
        return false;
    }
    fs::rename(candidate, path, ec);
    if (!ec) return true;

    const std::string commit_error = ec.message();
    std::error_code restore_error;
    fs::rename(previous, path, restore_error);
    error = "cannot commit temporary output: " + commit_error;
    if (restore_error) error += "; cannot restore previous file: " + restore_error.message();
    return false;
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
        std::string source;
        if (!read_file(input, source)) {
            std::cerr << "minify: cannot read '" << input.string() << "'\n";
            failed = true; continue;
        }
        std::string output, error;
        if (!minify::run(format, source, output, error)) {
            std::cerr << "minify: " << input.string() << ": " << error << "\n";
            failed = true; continue;
        }
        const fs::path destination = in_place ? input : minified_path(input);
        std::string write_error;
        if (!write_file(destination, output, write_error)) {
            std::cerr << "minify: cannot write '" << destination.string() << "': " << write_error << "\n";
            failed = true; continue;
        }
        std::cout << input.string() << " -> " << destination.string() << "\n";
    }
    return failed ? 1 : 0;
}
