#pragma once
#include <filesystem>
#include <set>
#include <vector>
#include <string>
#include <utility>
#include <vector>
#include <set>
#include <optional>
#include <map>

struct Config {
    std::string content_dir = "content/";
    std::string content_ext = ".html";
    std::string output_dir = "public/";
    std::string output_ext = ".html";
    std::string default_template = "templates/template.html";
    std::string incremental_mode = "modified";
    std::set<std::string> minify_exts;
    std::map<std::string, std::string> contracts;
    int build_threads = -1;
};

struct TrackedInfo {
    std::string name;
    std::string title;
    std::string template_path;
    std::string content_ext;
    std::string output_ext;
    std::optional<bool> minify;
};

struct BuildError {
    std::string tracked_name;
    std::filesystem::path source_file;
    std::size_t line = 0;
    std::string message;
    std::size_t column = 0;
    std::string source_line;

    BuildError() = default;
    BuildError(std::string tracked, std::filesystem::path source, std::size_t source_line_number,
               std::string error_message, std::size_t source_column = 0, std::string source_text = {})
        : tracked_name(std::move(tracked)), source_file(std::move(source)), line(source_line_number),
          message(std::move(error_message)), column(source_column), source_line(std::move(source_text)) {}
};

struct RenderResult {
    bool ok = true;
    bool content_used = false;
    std::size_t content_count = 0;
    std::string output;
    BuildError error;
    std::set<std::string> dependencies;
    std::set<std::string> reqs;
};

struct WatchExtension {
    std::string content_ext;
    std::string template_path;
    std::string output_ext;
};

struct WatchDirectory {
    std::string path;
    std::vector<WatchExtension> extensions;
};
