#include "CLI.h"
#include "Console.h"
#include "FileSystem.h"
#include "JsonFile.h"
#include <minify/Minify.h>
#include <map>
#include "ProjectInfo.h"
#include "WatchList.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <conio.h>
#include <io.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {
constexpr const char* version_text = "Nift v4.0.0";
constexpr auto build_auto_poll_interval = std::chrono::milliseconds(200);
constexpr const char* build_auto_log_path = ".nift/build-auto.log";



class ScopedStreamCapture {
public:
    ScopedStreamCapture()
        : cout_buffer_(std::cout.rdbuf(buffer_.rdbuf())),
          cerr_buffer_(std::cerr.rdbuf(buffer_.rdbuf())) {}

    ~ScopedStreamCapture() {
        std::cout.rdbuf(cout_buffer_);
        std::cerr.rdbuf(cerr_buffer_);
    }

    std::string str() const { return buffer_.str(); }

private:
    std::ostringstream buffer_;
    std::streambuf* cout_buffer_;
    std::streambuf* cerr_buffer_;
};

class BuildAutoQuitKey {
public:
    BuildAutoQuitKey() {
#if defined(_WIN32)
        interactive_ = _isatty(_fileno(stdin)) != 0;
#else
        interactive_ = ::isatty(STDIN_FILENO) != 0;
        if (!interactive_) return;

        if (::tcgetattr(STDIN_FILENO, &original_) != 0) {
            interactive_ = false;
            return;
        }

        termios raw = original_;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (::tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
            interactive_ = false;
#endif
    }

    ~BuildAutoQuitKey() {
#if !defined(_WIN32)
        if (interactive_)
            ::tcsetattr(STDIN_FILENO, TCSANOW, &original_);
#endif
    }

    bool interactive() const { return interactive_; }

    bool pressed() const {
        if (!interactive_) return false;
#if defined(_WIN32)
        if (!_kbhit()) return false;
        const int key = _getch();
        return key == 'q' || key == 'Q';
#else
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(STDIN_FILENO, &read_set);
        timeval timeout{0, 0};
        const int ready = ::select(STDIN_FILENO + 1, &read_set, nullptr, nullptr, &timeout);
        if (ready <= 0 || !FD_ISSET(STDIN_FILENO, &read_set)) return false;

        char key = 0;
        const ssize_t bytes = ::read(STDIN_FILENO, &key, 1);
        return bytes == 1 && (key == 'q' || key == 'Q');
#endif
    }

private:
    bool interactive_ = false;
#if !defined(_WIN32)
    termios original_{};
#endif
};

bool write_if_changed(const fs::path& path, const std::string& contents) {
    if (filesystem::file_exists(path) && filesystem::read_file(path) == contents)
        return true;
    return filesystem::write_file(path, contents);
}

class CommandTimer {
public:
    explicit CommandTimer(bool enabled = true) : enabled_(enabled), started_(std::chrono::steady_clock::now()) {}

    ~CommandTimer() {
        if (!enabled_) return;
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
        std::lock_guard<std::mutex> lock(console::output_mutex);
        std::cout << console::dim("time taken: " + format_seconds(elapsed) + " seconds") << '\n';
    }

private:
    static std::string format_seconds(double seconds) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(6) << seconds;
        std::string value = out.str();
        while (value.size() > 2 && value.back() == '0') value.pop_back();
        if (!value.empty() && value.back() == '.') value.push_back('0');
        return value;
    }

    bool enabled_;
    std::chrono::steady_clock::time_point started_;
};

std::string json_quoted(const std::string& value) {
    json::Document document(value);
    return document.dump();
}

void print_json_value(const json::Document& value, int depth = 0) {
    const std::string indent(static_cast<std::size_t>(depth * 2), ' ');
    const std::string child_indent(static_cast<std::size_t>((depth + 1) * 2), ' ');

    switch (value.type) {
        case json::Type::Null:
            std::cout << console::json_literal("null");
            break;
        case json::Type::Boolean:
            std::cout << console::json_literal(value.boolean ? "true" : "false");
            break;
        case json::Type::Number: {
            std::string number = value.dump();
            std::cout << console::json_number(number);
            break;
        }
        case json::Type::String:
            std::cout << console::json_string(json_quoted(value.string));
            break;
        case json::Type::Array:
            if (value.array.empty()) { std::cout << "[]"; break; }
            std::cout << "[\n";
            for (std::size_t i = 0; i < value.array.size(); ++i) {
                std::cout << child_indent;
                print_json_value(value.array[i], depth + 1);
                if (i + 1 != value.array.size()) std::cout << ',';
                std::cout << '\n';
            }
            std::cout << indent << ']';
            break;
        case json::Type::Object:
            if (value.object.empty()) { std::cout << "{}"; break; }
            std::cout << "{\n";
            for (auto it = value.object.begin(); it != value.object.end(); ) {
                std::cout << child_indent << console::json_key(json_quoted(it->first)) << ": ";
                print_json_value(it->second, depth + 1);
                if (++it != value.object.end()) std::cout << ',';
                std::cout << '\n';
            }
            std::cout << indent << '}';
            break;
    }
}

void print_json_document(const json::Document& document, const std::string& heading = {}, const std::string& explanation = {}) {
    if (console::stdout_is_tty() && !heading.empty()) {
        std::cout << console::heading(heading) << '\n';
        if (!explanation.empty()) std::cout << console::dim(explanation) << "\n\n";
        else std::cout << '\n';
    }
    print_json_value(document);
    std::cout << '\n';
}

json::Document tracked_entry_json(const ProjectInfo& project, const TrackedInfo& info) {
    json::Document entry = json::Document::make_object();
    entry["name"] = info.name;
    entry["title"] = info.title;
    entry["content-path"] = project.relative(project.content_path(info));
    entry["output-path"] = project.relative(project.output_path(info));
    entry["template-path"] = info.template_path;
    entry["content-ext"] = info.content_ext.empty() ? project.config.content_ext : info.content_ext;
    entry["output-ext"] = info.output_ext.empty() ? project.config.output_ext : info.output_ext;
    std::string output_extension = info.output_ext.empty() ? project.config.output_ext : info.output_ext;
    std::transform(output_extension.begin(), output_extension.end(), output_extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    entry["minify"] = info.minify.has_value()
        ? *info.minify
        : project.config.minify_exts.count(output_extension) != 0;
    return entry;
}

json::Document watch_json(const WatchList& watch) {
    json::Document result = json::Document::make_object();
    result["watched"] = json::Document::make_array();
    for (const auto& directory : watch.directories) {
        json::Document item = json::Document::make_object();
        item["directory"] = directory.path;
        item["extensions"] = json::Document::make_array();
        for (const auto& extension : directory.extensions) {
            json::Document ext = json::Document::make_object();
            ext["content-ext"] = extension.content_ext;
            ext["template"] = extension.template_path;
            ext["output-ext"] = extension.output_ext;
            item["extensions"].push_back(ext);
        }
        result["watched"].push_back(item);
    }
    return result;
}

void print_about() {
    std::cout
        << console::heading("Nift") << " ⚡\n"
        << console::dim("Fast, lightweight website generation in C++") << "\n\n"
        << "Nift tracks content, templates and dependencies, then rebuilds only what\n"
        << "needs rebuilding. Use it for simple sites, documentation, generated assets,\n"
        << "or as a small build layer alongside the frontend/backend tools you prefer.\n\n"
        << console::dim(version_text) << '\n'
        << "Website: " << console::path("https://nift.dev") << '\n'
        << "License: MIT\n";
}

void print_commands() {
    auto row = [](const std::string& command, const std::string& usage, const std::string& description) {
        std::cout << "  " << console::good(command);
        if (!usage.empty()) std::cout << ' ' << usage;
        const std::size_t visible = 2 + command.size() + (usage.empty() ? 0 : 1 + usage.size());
        std::cout << std::string(visible < 37 ? 37 - visible : 2, ' ') << console::dim(description) << '\n';
    };

    std::cout << console::heading("Nift commands") << "\n\n";

    std::cout << console::dim("Build") << '\n';
    row("build(-updated)", "[options]", "Build files that need updating");
    row("build-all", "[options]", "Build every tracked file");
    row("build-names", "[options] <names>", "Build selected tracked names");
    row("build-auto", "[options]", "Continuously build changed files");

    std::cout << '\n' << console::dim("Project") << '\n';
    row("track", "<name> [title] [template]", "Track a new file");
    row("untrack", "<names...>", "Stop tracking without deleting content");
    row("rm", "<names...>", "Remove tracking, content and output");
    row("cp / mv", "<source> <destination>", "Copy or move a tracked file");
    row("watch / unwatch", "<directory>", "Manage watched directories");

    std::cout << '\n' << console::dim("Inspect") << '\n';
    row("info", "[names...]", "Show tracked file information");
    row("info-all", "", "Show all tracked information");
    row("status", "[-p]", "Show pages that need rebuilding and why");
    row("info-watching", "", "Show watched directories");

    std::cout << '\n' << console::dim("General") << '\n';
    row("init", "[.ext]", "Create a Nift project");
    row("minify", "[-i|--in-place] <files...>", "Minify to *.min.ext by default; -i overwrites sources");
    row("about", "", "About Nift and where to learn more");
    row("version", "", "Show version information");
    row("commands", "", "Show this command reference");
}

bool valid_options(int argc, char** argv, int start) {
    for (int i = start; i < argc; ++i) {
        const std::string value = argv[i];
        if (!value.empty() && value[0] == '-' && value != "-p" && value != "-n" && value != "-s") return false;
    }
    return true;
}

bool has_option(int argc, char** argv, int start, const std::string& option) {
    for (int i = start; i < argc; ++i)
        if (argv[i] == option) return true;
    return false;
}

bool options_only(int argc, char** argv, int start) {
    if (!valid_options(argc, argv, start)) return false;
    for (int i = start; i < argc; ++i) {
        const std::string value = argv[i];
        if (value.empty() || value[0] != '-') return false;
    }
    return true;
}

fs::path user_dependencies_path(const ProjectInfo& project, const TrackedInfo& info) {
    fs::path path = project.content_path(info);
    path.replace_extension(".deps.json");
    return path;
}

void remove_page_build_state(const ProjectInfo& project, const TrackedInfo& info, bool remove_content_files) {
    std::error_code error;
    if (remove_content_files) {
        fs::permissions(project.content_path(info), fs::perms::owner_write, fs::perm_options::add, error);
        error.clear();
        fs::remove(project.content_path(info), error);
        error.clear();
        fs::remove(user_dependencies_path(project, info), error);
    }
    error.clear();
    fs::remove(project.output_path(info), error);
    error.clear();
    fs::remove(project.info_path(info), error);
    error.clear();
    fs::remove(filesystem::hash_file_path(project.root, project.content_path(info)), error);
    error.clear();
    fs::remove(filesystem::hash_file_path(project.root, user_dependencies_path(project, info)), error);
}

bool initialise_project(const std::string& extension) {
    if (fs::exists(".nift")) {
        console::error("cannot initialise project");
        std::cerr << "  this directory is already a Nift project\n";
        return false;
    }

    if (!filesystem::valid_extension(extension)) {
        console::error("init extension must begin with '.' and cannot contain path separators");
        return false;
    }

    fs::create_directories(".nift");
    fs::create_directories("content/assets/css");
    fs::create_directories("content/assets/js");
    fs::create_directories("templates");
    fs::create_directories("public");

    json::Document config = json::Document::make_object();
    config["config"] = json::Document::make_object();
    config["config"]["content-dir"] = "content/";
    config["config"]["content-ext"] = extension;
    config["config"]["output-dir"] = "public/";
    config["config"]["output-ext"] = extension;
    config["config"]["default-template"] = "templates/template.html";
    config["config"]["build-threads"] = -1;
    config["config"]["incremental-mode"] = "modified";
    config["config"]["minify-exts"] = json::Document::make_array();
    if (!save_json_file(".nift/config.json", config)) return false;

    json::Document tracked = json::Document::make_object();
    tracked["tracked"] = json::Document::make_array();
    auto add = [&](const std::string& name, const std::string& title, const std::string& templ, const std::string& ce = "", const std::string& oe = "") {
        json::Document value = json::Document::make_object();
        value["name"] = name; value["title"] = title; value["template"] = templ;
        if (!ce.empty()) value["content-ext"] = ce;
        if (!oe.empty()) value["output-ext"] = oe;
        tracked["tracked"].push_back(value);
    };
    add("/", "index", "templates/template.html");
    add("assets/css/style", "style", "templates/template.css", ".css", ".css");
    add("assets/js/script", "script", "templates/template.js", ".js", ".js");
    if (!save_json_file(".nift/tracked.json", tracked)) return false;

    filesystem::write_file("content/index" + extension, "");
    filesystem::write_file("content/assets/css/style.css", "");
    filesystem::write_file("content/assets/js/script.js", "");
    filesystem::write_file("templates/head.html", "<meta charset=\"utf-8\">\n");
    filesystem::write_file("templates/template.html", "<!doctype html>\n<html lang=\"en\">\n\t<head>\n\t\t@input(\"templates/head.html\")\n\t</head>\n\t<body>\n\t\t@content\n\t</body>\n</html>\n");
    filesystem::write_file("templates/template.css", "@content\n");
    filesystem::write_file("templates/template.js", "@content\n");

    ProjectInfo project;
    return project.open() && project.build_all(true) == 0;
}
}

int run_cli(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";
    if (command.empty()) { print_commands(); return 0; }

    if (command == "about") {
        print_about();
        return 0;
    }
    if (command == "version" || command == "--version" || command == "-v") {
        std::cout << version_text << '\n';
        return 0;
    }
    if (command == "commands" || command == "cmds" || command == "--help" || command == "-h") {
        print_commands();
        return 0;
    }
    if (command == "init" || command == "init-html") {
        if ((command == "init" && argc > 3) || (command == "init-html" && argc > 2)) {
            console::error(command + " received too many arguments");
            return 1;
        }
        const std::string extension = command == "init-html" ? ".html" : (argc > 2 ? argv[2] : ".html");
        return initialise_project(extension) ? 0 : 1;
    }

    if (command == "minify") {
        bool in_place = false;
        std::vector<fs::path> files;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "-i" || arg == "--in-place") {
                in_place = true;
                continue;
            }
            if (!arg.empty() && arg[0] == '-') {
                console::error("unknown minify option '" + arg + "'");
                std::cerr << "  supported option: -i, --in-place\n";
                return 1;
            }
            files.emplace_back(arg);
        }
        if (files.empty()) {
            console::error("minify requires at least one file");
            return 1;
        }
        bool failed = false;
        std::size_t minified_count = 0;
        for (const auto& path : files) {
            if (!filesystem::file_exists(path)) {
                console::error("cannot minify '" + path.string() + "': file does not exist or is not a regular file");
                failed = true;
                continue;
            }
            minify::Format format;
            if (!minify::format_for_extension(path.extension().string(), format)) {
                console::error("cannot minify '" + path.string() + "': unsupported extension " + path.extension().string());
                failed = true;
                continue;
            }
            const std::string source = filesystem::read_file(path);
            std::string output, error;
            if (!minify::run(format, source, output, error)) {
                console::error("cannot minify '" + path.string() + "'" +
                               (error.empty() ? std::string() : ": " + error));
                failed = true;
                continue;
            }
            fs::path destination = path;
            if (!in_place) {
                destination = path.parent_path() / path.stem();
                destination += ".min" + path.extension().string();
            }
            if (!filesystem::write_file(destination, output)) {
                console::error("cannot minify '" + path.string() + "': failed to write '" + destination.string() + "'");
                failed = true;
                continue;
            }
            std::cout << console::good("✓") << ' ' << path.string();
            if (!in_place) std::cout << " -> " << destination.string();
            std::cout << '\n';
            ++minified_count;
        }
        if (minified_count > 1)
            std::cout << console::dim(std::to_string(minified_count) + " files minified") << '\n';
        return failed ? 1 : 0;
    }

    const std::set<std::string> project_commands = {
        "build-all", "build-updated", "build", "build-names", "build-auto",
        "track", "untrack", "rm", "del", "cp", "copy", "mv", "move",
        "info", "info-all", "info-names", "info-tracking", "status",
        "watch", "unwatch", "info-watching"
    };
    if (!project_commands.count(command)) {
        console::error("unknown command '" + command + "'");
        std::cerr << "  run 'nift commands' to list available commands\n";
        return 1;
    }

    const bool timed_command = command == "build-all" || command == "build-updated" || command == "build" || command == "build-names";
    CommandTimer command_timer(timed_command);

    ProjectInfo project;
    if (!project.open()) return 1;

    if (command == "build-all") {
        if (!options_only(argc, argv, 2)) { console::error("build-all accepts options only"); return 1; }
        return project.build_all(true, has_option(argc, argv, 2, "-p"));
    }
    if (command == "build-updated" || (command == "build" && argc == 2)) {
        if (!options_only(argc, argv, 2)) { console::error("build-updated accepts options only"); return 1; }
        return project.build_all(false, has_option(argc, argv, 2, "-p"));
    }
    if (command == "build" || command == "build-names") {
        if (!valid_options(argc, argv, 2)) { console::error("unknown build option"); return 1; }
        std::vector<std::string> names;
        for (int i = 2; i < argc; ++i) if (argv[i][0] != '-') names.emplace_back(argv[i]);
        if (names.empty()) { console::error("build-names requires at least one tracked name"); return 1; }
        return project.build_names(names, true, has_option(argc, argv, 2, "-p"));
    }
    if (command == "build-auto") {
        if (!options_only(argc, argv, 2)) { console::error("build-auto accepts options only"); return 1; }
        BuildAutoQuitKey quit_key;
        std::cout << console::heading("Nift build-auto") << '\n';
        std::cout << console::dim("watching for changes every 200 ms") << '\n';
        std::cout << "build output: " << console::path(build_auto_log_path) << '\n';
        if (quit_key.interactive())
            std::cout << "press " << console::good("q") << " to stop\n";
        else
            std::cout << console::dim("non-interactive mode; stop the process to exit") << '\n';
        std::cout << std::flush;

        bool stop_requested = false;
        while (!stop_requested) {
            stop_requested = quit_key.pressed();
            if (stop_requested) break;

            std::string captured_output;
            int result = 0;
            {
                console::ScopedPlainOutput plain_output;
                ScopedStreamCapture capture;
                ProjectInfo fresh;
                if (!fresh.open()) result = 1;
                else result = fresh.build_all(false);
                captured_output = capture.str();
            }

            if (!write_if_changed(build_auto_log_path, captured_output)) {
                console::error("failed to write build-auto output log");
                return 1;
            }
            if (result != 0) return result;

            const auto sleep_step = std::chrono::milliseconds(20);
            auto remaining = build_auto_poll_interval;
            while (remaining.count() > 0 && !stop_requested) {
                const auto delay = std::min(remaining, sleep_step);
                std::this_thread::sleep_for(delay);
                remaining -= delay;
                stop_requested = quit_key.pressed();
            }
        }

        std::cout << console::good("build-auto stopped") << '\n';
        return 0;
    }

    if (command == "track") {
        if (argc < 3) { console::error("track requires a name"); return 1; }
        if (argc > 5) { console::error("track received too many arguments"); return 1; }
        const std::string name = argv[2];
        if (name.empty() || (name != "/" && fs::path(name).is_absolute()) || filesystem::has_parent_component(name)) { console::error("tracked name must stay inside the configured content/output directories"); return 1; }
        if (project.find(name)) { console::error("already tracking '" + name + "'"); return 1; }
        const std::string title = argc > 3 ? argv[3] : fs::path(name).filename().string();
        const std::string templ = argc > 4 ? argv[4] : project.config.default_template;
        if (title.empty() || templ.empty()) { console::error("name, title and template path must be non-empty"); return 1; }
        TrackedInfo candidate{name, title, templ, "", "", std::nullopt};
        if (project.conflicts_with_tracked_path(candidate)) {
            console::error("tracked name resolves to a content/output path already managed by another tracked name");
            return 1;
        }
        project.tracked.push_back(std::move(candidate));
        project.invalidate_tracked_index();
        const fs::path new_content = project.content_path(project.tracked.back());
        if (!filesystem::path_exists(new_content) && !filesystem::write_file(new_content, "")) return 1;
        return project.save_tracking() ? 0 : 1;
    }

    if (command == "untrack" || command == "rm" || command == "del") {
        if (argc < 3) return 1;
        for (int i = 2; i < argc; ++i) {
            const std::string name = argv[i];
            auto it = std::find_if(project.tracked.begin(), project.tracked.end(), [&](const TrackedInfo& info) { return info.name == name; });
            if (it == project.tracked.end()) continue;
            const TrackedInfo value = *it;
            if (command != "untrack") remove_page_build_state(project, value, true);
            project.tracked.erase(it);
            project.invalidate_tracked_index();
        }
        return project.save_tracking() ? 0 : 1;
    }

    if (command == "cp" || command == "copy" || command == "mv" || command == "move") {
        if (argc != 4) { console::error(command + " requires exactly a source and destination name"); return 1; }
        const std::string source_name = argv[2];
        const std::string destination_name = argv[3];
        if ((destination_name != "/" && fs::path(destination_name).is_absolute()) || filesystem::has_parent_component(destination_name)) {
            console::error("destination name must stay inside the configured content/output directories");
            return 1;
        }
        TrackedInfo* source = project.find(source_name);
        if (!source) { console::error("not tracking source name '" + source_name + "'"); return 1; }
        if (project.find(destination_name)) { console::error("already tracking destination name '" + destination_name + "'"); return 1; }

        const TrackedInfo source_copy = *source;
        TrackedInfo destination = source_copy;
        destination.name = destination_name;
        if (project.conflicts_with_tracked_path(destination)) {
            console::error("destination name resolves to a content/output path already managed by another tracked name");
            return 1;
        }

        const fs::path source_content = project.content_path(source_copy);
        const fs::path destination_content = project.content_path(destination);
        const fs::path source_sidecar = user_dependencies_path(project, source_copy);
        const fs::path destination_sidecar = user_dependencies_path(project, destination);
        if (filesystem::path_exists(destination_content) || filesystem::path_exists(destination_sidecar)) {
            console::error("destination content or dependency sidecar already exists and is not tracked");
            return 1;
        }

        std::error_code error;
        fs::create_directories(destination_content.parent_path(), error);
        error.clear();
        fs::copy_file(source_content, destination_content, error);
        if (error) { console::error("failed to copy source content"); return 1; }

        const bool has_sidecar = filesystem::path_exists(source_sidecar);
        if (has_sidecar) {
            fs::create_directories(destination_sidecar.parent_path(), error);
            error.clear();
            fs::copy_file(source_sidecar, destination_sidecar, error);
            if (error) {
                fs::remove(destination_content, error);
                console::error("failed to copy dependency sidecar");
                return 1;
            }
        }

        project.tracked.push_back(destination);
        project.invalidate_tracked_index();
        if (command == "mv" || command == "move") {
            project.tracked.erase(std::remove_if(project.tracked.begin(), project.tracked.end(), [&](const TrackedInfo& info) { return info.name == source_name; }), project.tracked.end());
            project.invalidate_tracked_index();
        }

        if (!project.save_tracking()) {
            fs::remove(destination_content, error);
            if (has_sidecar) fs::remove(destination_sidecar, error);
            return 1;
        }

        if (command == "mv" || command == "move") remove_page_build_state(project, source_copy, true);
        return 0;
    }

    if (command == "info" || command == "info-all" || command == "info-names" || command == "info-tracking" || command == "status") {
        if (argc > 2 && !valid_options(argc, argv, 2)) return 1;
        if (command != "info" && !options_only(argc, argv, 2)) {
            console::error(command + " accepts options only");
            return 1;
        }

        if (command == "status") {
            struct StatusEntry {
                const TrackedInfo* info = nullptr;
                std::vector<std::string> reasons;
            };

            std::vector<StatusEntry> pending;
            pending.reserve(project.tracked.size());
            if (project.tracked.size() < 2) {
                for (const auto& info : project.tracked) {
                    auto reasons = project.build_reasons(info);
                    if (!reasons.empty()) pending.push_back({&info, std::move(reasons)});
                }
            } else {
                const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
                std::size_t thread_count = project.config.build_threads < 0 ? static_cast<std::size_t>(-static_cast<long long>(project.config.build_threads)) * hardware : (project.config.build_threads == 0 ? hardware : static_cast<std::size_t>(project.config.build_threads));
                thread_count = std::max<std::size_t>(1, std::min(thread_count, project.tracked.size()));
                std::vector<std::vector<std::string>> reasons(project.tracked.size());
                std::atomic<std::size_t> next{0};
                std::vector<std::thread> workers;
                workers.reserve(thread_count);
                for (std::size_t t = 0; t < thread_count; ++t) {
                    workers.emplace_back([&] {
                        while (true) {
                            const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
                            if (index >= project.tracked.size()) break;
                            reasons[index] = project.build_reasons(project.tracked[index]);
                        }
                    });
                }
                for (auto& worker : workers) worker.join();
                for (std::size_t i = 0; i < project.tracked.size(); ++i)
                    if (!reasons[i].empty()) pending.push_back({&project.tracked[i], std::move(reasons[i])});
            }

            if (pending.empty()) {
                std::cout << console::heading("Nift status") << '\n';
                std::cout << console::good("✓") << " all " << project.tracked.size() << " tracked "
                          << (project.tracked.size() == 1 ? "page is" : "pages are") << " up to date\n";
                return 0;
            }

            constexpr std::size_t detailed_status_limit = 10;
            constexpr std::size_t status_sample_limit = 5;
            const bool full_detail = has_option(argc, argv, 2, "-p");

            std::cout << console::heading("Nift status") << '\n';
            std::cout << pending.size() << " of " << project.tracked.size() << " tracked "
                      << (project.tracked.size() == 1 ? "page needs" : "pages need") << " rebuilding\n\n";

            if (full_detail || pending.size() <= detailed_status_limit) {
                for (const auto& entry : pending) {
                    std::cout << console::path(entry.info->name, true) << '\n';
                    for (const auto& reason : entry.reasons)
                        std::cout << console::dim("  ↳ " + reason) << '\n';
                }
            } else {
                std::map<std::string, std::size_t> reason_counts;
                for (const auto& entry : pending)
                    for (const auto& reason : entry.reasons) ++reason_counts[reason];

                std::cout << console::dim("rebuild causes:") << '\n';
                for (const auto& [reason, count] : reason_counts)
                    std::cout << "  " << console::dim("↳ " + reason + " → " + std::to_string(count) +
                               (count == 1 ? " page" : " pages")) << '\n';

                std::cout << '\n' << console::dim("affected pages: ");
                const std::size_t shown = std::min(status_sample_limit, pending.size());
                for (std::size_t i = 0; i < shown; ++i) {
                    if (i) std::cout << console::dim(", ");
                    std::cout << console::path(pending[i].info->name);
                }
                if (pending.size() > shown)
                    std::cout << console::dim("  +" + std::to_string(pending.size() - shown) + " more");
                std::cout << '\n';
            }

            std::cout << "\nrun " << console::good("nift build-updated") << " to rebuild "
                      << (pending.size() == 1 ? "this page" : "these pages") << '\n';
            return 0;
        }

        if (command == "info-names") {
            json::Document names = json::Document::make_object();
            names["tracked"] = json::Document::make_array();
            for (const auto& info : project.tracked) names["tracked"].push_back(info.name);
            print_json_document(names, "Tracked names", "Names currently managed by this Nift project.");
            return 0;
        }

        if (command == "info" && argc > 2) {
            json::Document result = json::Document::make_object();
            result["tracked"] = json::Document::make_array();
            bool has_untracked = false;
            for (int i = 2; i < argc; ++i) {
                if (argv[i][0] == '-') continue;
                const TrackedInfo* info = project.find(argv[i]);
                if (info) {
                    result["tracked"].push_back(tracked_entry_json(project, *info));
                } else {
                    if (!has_untracked) {
                        result["not-tracking"] = json::Document::make_array();
                        has_untracked = true;
                    }
                    result["not-tracking"].push_back(argv[i]);
                }
            }
            print_json_document(result, "Tracked file information", "Resolved paths and metadata for the requested tracked names.");
            return 0;
        }

        json::Document result = json::Document::make_object();
        if (command == "info-tracking") {
            result["tracking-file"] = ".nift/tracked.json";
            result["tracked-count"] = static_cast<int>(project.tracked.size());
        }
        result["tracked"] = json::Document::make_array();
        for (const auto& info : project.tracked) result["tracked"].push_back(tracked_entry_json(project, info));
        const std::string heading = command == "info-tracking" ? "Tracking information" : "All tracked file information";
        const std::string explanation = command == "info-tracking"
            ? "Tracking state plus the resolved paths Nift uses for each entry."
            : "Complete metadata for every tracked entry in the project.";
        print_json_document(result, heading, explanation);
        return 0;
    }

    if (command == "watch") {
        if (argc < 3) return 1;
        if (argc > 6) { console::error("watch received too many arguments"); return 1; }
        WatchExtension extension{
            argc > 3 ? argv[3] : project.config.content_ext,
            argc > 4 ? argv[4] : project.config.default_template,
            argc > 5 ? argv[5] : project.config.output_ext
        };
        return project.watch_list().add(project, argv[2], extension) ? 0 : 1;
    }
    if (command == "unwatch") {
        if (argc != 3) { console::error("unwatch requires exactly one directory"); return 1; }
        return project.watch_list().remove(project, argv[2]) ? 0 : 1;
    }
    if (command == "info-watching") {
        if (argc != 2) { console::error("info-watching does not accept additional arguments"); return 1; }
        print_json_document(watch_json(project.watch_list()), "Watching information",
                            "Directories Nift scans automatically and the extension rules applied to each one.");
        return 0;
    }

    return 1;
}
