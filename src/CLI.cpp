#include "FeatureFlags.h"
#include "ProcessEnvironment.h"
#include "CLI.h"
#include "FileSystem.h"
#include "JsonFile.h"
#include "PackageMetadata.h"
#include "PackageGraphCommands.h"
#include "PackageGraphQuery.h"
#include "PackageTransaction.h"
#include <minify/Minify.h>
#include <map>
#include <memory>
#include "ProjectInfo.h"
#include "ProjectRead.h"
#include "ProjectModel.h"
#include "ProjectOwnership.h"
#include "WatchList.h"
#include "handover_content.h"
#include "migration_content.h"
#include "rewrite_content.h"
#include "redesign_content.h"
#include "Parser.h"
#include "Proc.h"
#include "RenderHost.h"
#include "ScriptHost.h"
#include "Serve.h"
#include "Json.h"
// Console.h pulls <windows.h> on Windows; include it after the standard-library
// heavy headers so libstdc++'s <filesystem>/<locale> are parsed before any
// windows.h macros are visible (mingw locale/gthread fragility).
#include "Console.h"
#include "rt/parser.h"
#include "rt/prevalidate.h"
#include "rt/script_runner.h"
#include "rt/session.h"
#include "rt/eval_result.h"
#include "shell/CommandDispatch.h"
#include "shell/Spawn.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <fstream>
#ifndef _WIN32
#include <glob.h>
#endif
#include <optional>
#include <set>
#include <sstream>
#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#endif
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
bool cli_contains_bytes(const nift::RuntimeValue& value) {
    if (value.is_bytes()) return true;
    if (value.is_array())
        return std::any_of(value.array.begin(), value.array.end(), cli_contains_bytes);
    if (value.is_object())
        return std::any_of(value.object.begin(), value.object.end(), [](const auto& entry) {
            return cli_contains_bytes(entry.second);
        });
    return false;
}

// Portable environment assignment (Windows has no setenv()).
void nift_setenv(const char* name, const char* value, int /*overwrite*/) {
    std::lock_guard<std::mutex> lock(nift_environment_mutex());
#ifdef _WIN32
    _putenv_s(name, value);
#else
    ::setenv(name, value, 1);
#endif
    nift::flags::note_environment_changed();
}
constexpr const char* version_text = "Nift v4.12.0";
constexpr auto build_auto_poll_interval = std::chrono::milliseconds(200);
constexpr const char* build_auto_log_path = ".nift/build-auto.log";

// feature-site-dx: change detection for `build --auto` after a failed build.
// A snapshot maps every file under the project root to (size, mtime),
// skipping .nift/, .git/, node_modules/ and the output directory. The output
// directory is compared after normalisation (weakly_canonical: `public`,
// `public/`, `./public`, an absolute path and a symlink to the directory are
// all the same directory); one outside the project is simply never walked.
// Directory symlinks are not followed. Inputs outside the project root are
// not watched while in the failed state.
using BuildAutoSnapshot = std::map<std::string, std::pair<std::uintmax_t, long long>>;
BuildAutoSnapshot build_auto_snapshot(const fs::path& root, const std::string& output_dir) {
    BuildAutoSnapshot snapshot;
    auto normal_dir = [](const fs::path& path) {
        std::error_code error;
        fs::path result = fs::weakly_canonical(path, error);
        if (error) result = fs::absolute(path, error).lexically_normal();
        if (!result.has_filename() && result.has_parent_path() && result != result.root_path()) result = result.parent_path();
        return result;
    };
    const fs::path walk_root = normal_dir(root);
    const fs::path output = output_dir.empty() ? walk_root
        : normal_dir(fs::path(output_dir).is_absolute() ? fs::path(output_dir) : root / output_dir);
    const bool skip_output = output != walk_root;
    std::error_code ec;
    fs::recursive_directory_iterator it(walk_root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const fs::path path = it->path();
        const std::string entry = path.filename().generic_string();
        std::error_code type_ec;
        if (it->is_directory(type_ec)) {
            if (it.depth() == 0 && (entry == ".nift" || entry == ".git" || entry == "node_modules")) { it.disable_recursion_pending(); continue; }
            if (skip_output && path == output) { it.disable_recursion_pending(); continue; }
            continue;
        }
        std::error_code stat_ec;
        const auto size = it->file_size(stat_ec);
        const auto time = it->last_write_time(stat_ec);
        snapshot[path.generic_string()] = {stat_ec ? 0 : size, static_cast<long long>(time.time_since_epoch().count())};
    }
    return snapshot;
}

// feature-site-dx (round 2, B3): when to retry after a failed build.
// - Healthy: every poll runs an incremental build (no snapshots).
// - The first failure from a healthy state retries once, because what
//   changed while it ran is unknown.
// - Every later attempt is bracketed by snapshots. Files that changed while
//   it ran are hashed; a file whose content equals what the previous attempt
//   left there is the build's own repeated write and is ignored. Anything
//   else that changed during the attempt (a user edit, an atomic rename, a
//   new or deleted file) causes another attempt, so no edit is lost.
// - If the same files change on two consecutive attempts with new content
//   each time, a build hook rewrites watched files on every build: that is
//   reported once and the watcher waits for an edit instead of looping.
// - While idle after a failure, any difference from the post-attempt state
//   triggers the next attempt.
volatile std::sig_atomic_t build_auto_stop_signal = 0;
extern "C" void build_auto_on_signal(int) { build_auto_stop_signal = 1; }

class BuildAutoRetryPolicy {
public:
    BuildAutoRetryPolicy(fs::path root, std::string output_dir) : root_(std::move(root)), output_dir_(std::move(output_dir)) {}
    bool should_attempt() const { return !failing_ || retry_ || build_auto_snapshot(root_, output_dir_) != idle_; }
    void before_attempt() { if (failing_) pre_ = build_auto_snapshot(root_, output_dir_); else pre_.reset(); }
    // Returns a one-time report when a hook rewrites watched files every build.
    std::string after_failure() {
        BuildAutoSnapshot post = build_auto_snapshot(root_, output_dir_);
        std::string report;
        if (!pre_) { retry_ = true; previous_valid_ = false; written_.clear(); }
        else {
            std::set<std::string> changed;
            for (const auto& [path, meta] : *pre_) { auto found = post.find(path); if (found == post.end() || found->second != meta) changed.insert(path); }
            for (const auto& entry : post) if (!pre_->count(entry.first)) changed.insert(entry.first);
            std::map<std::string, std::size_t> hashes;
            std::set<std::string> significant;
            for (const auto& path : changed) {
                const std::size_t hash = post.count(path) ? std::hash<std::string>{}(filesystem::read_file(path)) : 0;
                hashes[path] = hash;
                const auto previous = written_.find(path);
                if (previous == written_.end() || previous->second != hash) significant.insert(path);
            }
            if (significant.empty()) retry_ = false;
            else if (previous_valid_ && significant == previous_changed_) {
                retry_ = false;
                if (!loop_reported_) {
                    loop_reported_ = true;
                    report = "a build hook modifies watched files on every build (";
                    std::size_t shown = 0;
                    for (const auto& path : significant) { if (shown++) report += ", "; if (shown > 3) { report += "..."; break; } report += fs::path(path).lexically_relative(fs::weakly_canonical(root_)).generic_string(); }
                    report += "); waiting for an edit before retrying";
                }
            } else retry_ = true;
            written_ = std::move(hashes);
            previous_changed_ = std::move(significant);
            previous_valid_ = true;
        }
        failing_ = true;
        idle_ = std::move(post);
        return report;
    }
    void after_success() { *this = BuildAutoRetryPolicy(root_, output_dir_); }
    bool failing() const { return failing_; }
private:
    fs::path root_; std::string output_dir_;
    bool failing_ = false, retry_ = false, previous_valid_ = false, loop_reported_ = false;
    BuildAutoSnapshot idle_;
    std::optional<BuildAutoSnapshot> pre_;
    std::map<std::string, std::size_t> written_;
    std::set<std::string> previous_changed_;
};


bool valid_runtime_platform(const std::string& value) {
    if (value.empty()) return false;
    auto first = static_cast<unsigned char>(value.front());
    if (!((first >= 'a' && first <= 'z') || (first >= '0' && first <= '9'))) return false;
    for (unsigned char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    return true;
}

bool parse_runtime_platform_selector(const std::string& arg, std::string& platform) {
    if (arg.rfind("--platform=",0)==0) { platform=arg.substr(11); return valid_runtime_platform(platform); }
    return false;
}



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

bool append_line_if_missing(const fs::path& path, const std::string& line) {
    std::string contents;
    if (filesystem::file_exists(path)) contents = filesystem::read_file(path);
    std::istringstream input(contents);
    std::string existing;
    while (std::getline(input, existing))
        if (existing == line) return true;
    if (!contents.empty() && contents.back() != '\n') contents.push_back('\n');
    contents += line + "\n";
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
        case json::Type::Number:
        case json::Type::StrNumber: {
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
    if (!info.template_path.empty()) entry["template-path"] = info.template_path;
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
        << console::dim("Production-grade, dependency-aware website builds") << "\n\n"
        << "Fast incremental and parallel builds, precise transitive dependency tracking,\n"
        << "structured data and validation, with framework-agnostic output.\n\n"
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
    row("build", "[names...] [options]", "Incremental build, or explicitly build the named pages");
    row("build --all", "", "Build all tracked files");
    row("build --auto", "", "Watch and rebuild on change; errors are printed and it keeps watching");
    row("build --strict", "", "Unknown $[name] values are errors (also: \"strict-values\": true)");
    row("build --no-warn=<kind>", "", "Silence warnings: unknown-directive, path-outside-output, unknown-value, all");
    row("build --repair", "", "Repair/reconstruct derived build state");

    std::cout << '\n' << console::dim("Project") << '\n';
    row("track", "<name> [title] [template]", "Track a new file");
    row("untrack", "<names...>", "Stop tracking without deleting content");
    row("rm", "<names...>", "Remove tracking, content and output");
    row("cp / mv", "<source> <destination>", "Copy or move a tracked file");
    row("watch / unwatch", "<directory>", "Manage watched directories");
    row("serve", "[--port N] [--host H] [--dir PATH]", "Serve the output dir locally (Python 3.8+; development only)");

    std::cout << '\n' << console::dim("Inspect") << '\n';
    row("info", "[names...]", "Show project or page information");
    row("info --all", "", "Show all tracked-page information");
    row("info --watching", "", "Show watching information");
    row("info --tracking", "", "Show tracking information");
    row("info --names", "", "List tracked names");
    row("status", "[-p]", "Show pages that need rebuilding and why");

    std::cout << '\n' << console::dim("Scripting") << '\n';
    row("<path>", "[args...]", "Run a native Nift script");
    row("(no command)", "", "Start the persistent native Nift shell");

    std::cout << '\n' << console::dim("Packages") << '\n';
    row("add", "<source> [--ref=REF]", "Add a Git, GitHub or local Nift package");
    row("remove", "<name>", "Remove a package dependency");
    row("install", "", "Install locked/package manifest dependencies");
    row("update", "[name]", "Refresh floating package refs and lock commits");
    row("packages", "[name]", "Explain why packages are installed (dependency graph)");

    std::cout << '\n' << console::dim("General") << '\n';
    row("init", "[--target=platform] [--ext=.ext] [--handover] [--migration|--rewrite|--redesign] [--MODE-existing=POLICY]", "Create a project; rewrite/redesign also add missing workbooks to existing projects (experimental)");
    row("minify", "[-i|--in-place] <files...>", "Minify to *.min.ext by default; -i overwrites sources");
    row("about", "", "About Nift and where to learn more");
    row("version", "", "Show version information");
    row("commands", "", "Show this command reference");
}


static fs::path package_manifest_path(){ return fs::current_path()/"manifest.json"; }
static fs::path package_lock_path(){ return fs::current_path()/".nift"/"packages.lock.json"; }
static fs::path package_root(){ return fs::absolute(fs::current_path()/".nift"/"packages").lexically_normal(); }
static bool package_store_root(fs::path& root, std::string& error) {
    const fs::path project = fs::absolute(fs::current_path()).lexically_normal();
    root = package_root();
    std::error_code ec;
    const fs::path canonical_project = fs::weakly_canonical(project, ec);
    if (ec) { error = "cannot resolve the project package store: " + ec.message(); return false; }
    const fs::path expected = (canonical_project/".nift"/"packages").lexically_normal();
    const fs::path canonical_root = fs::weakly_canonical(root, ec);
    if (ec || canonical_root != expected) {
        error = "package store must stay inside the project: " + root.generic_string();
        return false;
    }
    return true;
}
static bool package_path_for_name(const std::string& name, fs::path& path, std::string& error) {
    if (!filesystem::valid_package_name(name)) {
        error = "invalid package name: " + name;
        return false;
    }
    fs::path root;
    if (!package_store_root(root, error)) return false;
    path = (root/name).lexically_normal();
    if (path.parent_path() != root) {
        error = "package path escapes .nift/packages: " + name;
        return false;
    }
    return true;
}
static bool package_validate_manifest(const fs::path& dir,std::string& name,std::string& error){
    package_metadata::Manifest manifest;fs::path entry;
    if(!package_metadata::load_package(dir,manifest,entry,error))return false;
    name=manifest.name;return true;
}
static bool load_project_manifest(bool allow_missing,package_metadata::Manifest& manifest,std::string& error){
    if(!filesystem::path_exists(package_manifest_path())){
        if(allow_missing)return true;
        error="manifest.json: file does not exist";return false;
    }
    return package_metadata::load_manifest(package_manifest_path(),false,manifest,error);
}
static bool load_existing_lock(package_metadata::Lock& v1_lock,bool& v1_exists,json::Document& v2_doc,bool& v2_exists,std::string& error){
    v1_exists=v2_exists=false;
    if(!filesystem::path_exists(package_lock_path()))return true;
    if(!load_json_file(package_lock_path(),v2_doc,error))return false;
    const package_graph::LockFormat format=package_graph::detect_lock_format(v2_doc);
    if(format==package_graph::LockFormat::V2){v2_exists=true;return true;}
    if(format==package_graph::LockFormat::V1){package_metadata::Lock l;if(!package_metadata::parse_lock(v2_doc,l,error))return false;v1_lock=l;v1_exists=true;v2_doc=json::Document();return true;}
    error="packages.lock.json has an unsupported or mixed format";return false;
}
static int package_add_cli(int argc,char**argv){
    if(argc<3){console::error("add requires a package source");return 1;}std::string source=argv[2],ref="latest";for(int i=3;i<argc;++i){std::string a=argv[i];if(a.rfind("--ref=",0)==0)ref=a.substr(6);else{console::error("unknown add option: "+a);return 1;}}
    if(!package_metadata::safe_external_text(source)||!package_metadata::safe_external_text(ref)){console::error("package source and ref must be non-empty safe strings");return 1;}
    const bool local_path=package_metadata::source_is_local_path(source);if(local_path&&!fs::is_directory(fs::path(source))){console::error("local package source is not a directory: "+source);return 1;}std::string error;fs::path root;
    if(!package_store_root(root,error)){console::error(error);return 1;}
    PackageTransaction transaction(fs::current_path());if(!transaction.acquire(error)){console::error(error);return 1;}fs::path transaction_root;if(!transaction.create_staging(transaction_root,error)){console::error(error);return 1;}
    package_metadata::Manifest manifest;if(!load_project_manifest(true,manifest,error)){console::error(error);return 1;}
    package_metadata::Lock v1_lock;bool v1_exists=false;json::Document v2_doc;bool v2_exists=false;
    if(!load_existing_lock(v1_lock,v1_exists,v2_doc,v2_exists,error)){console::error(error);return 1;}
    if(v1_exists&&!package_metadata::validate_lock(manifest,v1_lock,error)){console::error(error);return 1;}
    auto conflict_guard=[&](const std::string& n)->bool{if(manifest.dependencies.count(n)){console::error("package '" + n + "' is already a dependency; use 'nift update "+n+"' or 'nift remove "+n+"' first");return true;}return false;};
    std::string name;
    if(local_path){fs::path src=fs::absolute(source).lexically_normal();if(!package_validate_manifest(src,name,error)){console::error(error);return 1;}if(conflict_guard(name))return 1;}
    else {const fs::path candidate=transaction_root/"new"/".candidate";std::string commit;if(!package_graph_commands::git_checkout(source,ref,candidate,commit,error)){console::error("package clone failed: "+error);return 1;}if(!package_validate_manifest(candidate,name,error)){console::error(error);return 1;}if(conflict_guard(name))return 1;std::error_code ec;fs::rename(candidate,transaction_root/"new"/name,ec);if(ec){console::error("cannot stage package: "+ec.message());return 1;}}
    const std::string requested=local_path?"local":ref;
    manifest.has_dependencies=true;manifest.dependencies[name]={source,requested};
    package_graph_commands::AcquireResult result;
    if(!package_graph_commands::acquire_graph(fs::current_path(),transaction_root,manifest,v1_exists?&v1_lock:nullptr,v2_exists?&v2_doc:nullptr,package_graph::ResolveMode::ResolveUpdate,nullptr,nullptr,result,error)){console::error(error);return 1;}
    if(!transaction.commit(result.operations,result.manifest_document,result.lock_document,error)){console::error(error);return 1;}
    std::cout<<"added "<<name<<"\n";return 0;
}
static int package_remove_cli(int argc,char**argv){
    if(argc!=3){console::error("remove requires one package name");return 1;}
    const std::string n=argv[2];std::string error;fs::path dest;
    if(!package_path_for_name(n,dest,error)){console::error(error);return 1;}
    PackageTransaction transaction(fs::current_path());if(!transaction.acquire(error)){console::error(error);return 1;}fs::path transaction_root;if(!transaction.create_staging(transaction_root,error)){console::error(error);return 1;}
    package_metadata::Manifest manifest;if(!load_project_manifest(false,manifest,error)){console::error(error);return 1;}
    if(!manifest.dependencies.count(n)){console::error("package is not a declared dependency: "+n);return 1;}
    package_metadata::Lock v1_lock;bool v1_exists=false;json::Document v2_doc;bool v2_exists=false;
    if(!load_existing_lock(v1_lock,v1_exists,v2_doc,v2_exists,error)){console::error(error);return 1;}
    if(v1_exists&&!package_metadata::validate_lock(manifest,v1_lock,error)){console::error(error);return 1;}
    manifest.dependencies.erase(n);
    std::map<std::string,std::string> root_locked_commits;
    if(v1_exists)for(const auto& item:v1_lock)if(manifest.dependencies.count(item.first))root_locked_commits[item.first]=item.second.commit;
    package_graph_commands::AcquireResult result;
    if(!package_graph_commands::acquire_graph(fs::current_path(),transaction_root,manifest,v1_exists?&v1_lock:nullptr,v2_exists?&v2_doc:nullptr,package_graph::ResolveMode::ResolveUpdate,nullptr,root_locked_commits.empty()?nullptr:&root_locked_commits,true,result,error)){console::error(error);return 1;}
    if(!transaction.commit(result.operations,result.manifest_document,result.lock_document,error)){console::error(error);return 1;}
    return 0;
}
static int package_install_cli(bool update,const std::string& only={}){
    std::string e;fs::path root;if(!package_store_root(root,e)){console::error(e);return 1;}PackageTransaction transaction(fs::current_path());if(!transaction.acquire(e)){console::error(e);return 1;}fs::path transaction_root;if(!transaction.create_staging(transaction_root,e)){console::error(e);return 1;}
    package_metadata::Manifest manifest;if(!load_project_manifest(false,manifest,e)){console::error(e);return 1;}if(manifest.dependencies.empty()){console::error("manifest.json has no dependencies");return 1;}
    if(!only.empty()&&!filesystem::valid_package_name(only)){console::error("invalid package name: "+only);return 1;}
    if(!only.empty()&&!manifest.dependencies.count(only)){console::error("package is not a declared dependency: "+only);return 1;}
    package_metadata::Lock v1_lock;bool v1_exists=false;json::Document v2_doc;bool v2_exists=false;
    if(!load_existing_lock(v1_lock,v1_exists,v2_doc,v2_exists,e)){console::error(e);return 1;}
    if(v1_exists&&!package_metadata::validate_lock(manifest,v1_lock,e)){console::error(e);return 1;}
    package_graph::ResolveMode mode=package_graph::ResolveMode::ResolveUpdate;
    const std::string* target_root=nullptr;
    std::map<std::string,std::string> root_locked_commits;
    if(update){
        if(!only.empty()){if(!v2_exists&&!v1_exists){console::error("targeted update requires a package lock");return 1;}mode=package_graph::ResolveMode::TargetedUpdate;target_root=&only;}
        else mode=package_graph::ResolveMode::ResolveUpdate;
    }else{
        if(v2_exists)mode=package_graph::ResolveMode::LockedInstall;
        else if(v1_exists){for(const auto& item:v1_lock)if(manifest.dependencies.count(item.first))root_locked_commits[item.first]=item.second.commit;}
    }
    package_graph_commands::AcquireResult result;
    if(!package_graph_commands::acquire_graph(fs::current_path(),transaction_root,manifest,v1_exists?&v1_lock:nullptr,v2_exists?&v2_doc:nullptr,mode,target_root,root_locked_commits.empty()?nullptr:&root_locked_commits,result,e)){console::error(e);return 1;}
    if(!transaction.commit(result.operations,result.manifest_document,result.lock_document,e)){console::error(e);return 1;}
    for(const auto& op:result.operations){if(op.kind!=PackageTransaction::Kind::Replace)continue;std::string commit="local";const auto node=result.graph.packages.find(op.name);if(node!=result.graph.packages.end())commit=node->second.commit;std::cout<<(update?"updated ":"installed ")<<op.name<<" ("<<commit<<")\n";}
    return 0;
}

static int package_query_cli(int argc,char**argv){
    bool want_json=false;std::string name;bool name_set=false;
    for(int i=2;i<argc;++i){std::string a=argv[i];if(a=="--json")want_json=true;else if(name_set){console::error("packages takes at most one package name");return 1;}else{name=a;name_set=true;}}
    std::string error;package_metadata::Manifest manifest;
    if(!load_project_manifest(false,manifest,error)){console::error(error);return 1;}
    json::Document lock_doc;
    if(!filesystem::path_exists(package_lock_path())){console::error("packages.lock.json is missing; run nift install first");return 1;}
    if(!load_json_file(package_lock_path(),lock_doc,error)){console::error(error);return 1;}
    const package_graph::LockFormat format=package_graph::detect_lock_format(lock_doc);
    if(format!=package_graph::LockFormat::V2){console::error("packages query requires a v2 graph lock (current lock is legacy/direct-only; run nift install to migrate)");return 1;}
    package_graph::GraphLock graph;
    if(!package_graph::parse_graph_lock(lock_doc,graph,error)){console::error(error);return 1;}
    if(want_json){
        // Semantically-structured output: the lock graph plus per-package paths.
        json::Document result=json::Document::make_object();
        result["lockfileVersion"]=json::Document(2);
        json::Document packages=json::Document::make_object();
        std::map<std::string,std::vector<package_graph::GraphPath>> by_target;
        if(!package_graph::enumerate_graph_paths(manifest,graph,by_target,error)){console::error(error);return 1;}
        for(const auto& node:graph.packages){
            json::Document entry=json::Document::make_object();
            entry["source"]=json::Document(node.second.source);
            entry["commit"]=json::Document(node.second.commit);
            json::Document paths=json::Document::make_array();
            const auto found=by_target.find(node.first);
            if(found!=by_target.end()){
                for(const auto& path:found->second){
                    json::Document p=json::Document::make_object();
                    json::Document nodes=json::Document::make_array();
                    for(const auto& n:path.nodes)nodes.array.emplace_back(n);
                    p["nodes"]=nodes;
                    p["source"]=json::Document(path.requirement.source);
                    p["requested"]=json::Document(path.requirement.requested);
                    paths.array.push_back(std::move(p));
                }
            }
            entry["paths"]=paths;
            packages[node.first]=entry;
        }
        result["packages"]=packages;
        std::cout<<result.dump(2)+"\n";
        return 0;
    }
    std::string text=package_graph::explain_graph(manifest,graph,name,error);
    if(text.empty()){console::error(error);return 1;}
    std::cout<<text;
    return 0;
}

bool has_option(int argc, char** argv, int start, const std::string& option) {
    for (int i = start; i < argc; ++i)
        if (argv[i] == option) return true;
    return false;
}

fs::path user_dependencies_path(const ProjectInfo& project, const TrackedInfo& info) {
    fs::path path = project.content_path(info);
    path.replace_extension(".deps.json");
    return path;
}

void remove_page_build_state(const ProjectInfo& project, const TrackedInfo& info, bool remove_content_files) {
    if (remove_content_files) {
        filesystem::remove_owned_file(project.content_path(info));
        filesystem::remove_owned_file(user_dependencies_path(project, info));
    }
    filesystem::remove_owned_file(project.output_path(info));
    filesystem::remove_owned_file(project.info_path(info));
    filesystem::remove_owned_file(filesystem::hash_file_path(project.root, project.content_path(info)));
    filesystem::remove_owned_file(filesystem::hash_file_path(project.root, user_dependencies_path(project, info)));
}

enum class TransformationMode { None, Migration, Rewrite, Redesign };

std::string transformation_name(TransformationMode mode) {
    if (mode == TransformationMode::Migration) return "migration";
    if (mode == TransformationMode::Rewrite) return "rewrite";
    if (mode == TransformationMode::Redesign) return "redesign";
    return "";
}

std::string transformation_workbook(TransformationMode mode) {
    if (mode == TransformationMode::Migration) return "MIGRATION.md";
    if (mode == TransformationMode::Rewrite) return "REWRITE.md";
    return "REDESIGN.md";
}

struct InitOptions {
    std::string extension = ".html";
    std::string target;
    bool handover = false;
    TransformationMode mode = TransformationMode::None;
    std::string existing = "error";
};

struct InitTarget {
    std::string name;
    std::string output_dir = "public/";
};

const std::vector<InitTarget>& init_targets() {
    static const std::vector<InitTarget> targets = {
        {"vercel", ".vercel/output/static/"},
        {"netlify", "public/"},
        {"amplify", ".amplify-hosting/static/"},
        {"azure", "public/"},
        {"firebase", "public/"},
        {"render", "public/"},
        {"cloudflare", "public/"},
        {"github-pages", "public/"},
        {"supabase", "public/"},
    };
    return targets;
}

const InitTarget* find_init_target(const std::string& name) {
    for (const auto& target : init_targets())
        if (target.name == name) return &target;
    return nullptr;
}

bool target_extension_supported(const std::string& extension) {
    return extension == ".html" || extension == ".htm";
}

bool html_family_extension(const std::string& extension) {
    return extension == ".html" || extension == ".htm" || extension == ".php";
}

void print_init_targets() {
    std::cerr << "  supported targets:";
    for (const auto& target : init_targets()) std::cerr << " " << target.name;
    std::cerr << '\n';
}

bool parse_init_options(int argc, char** argv, InitOptions& options) {
    bool saw_ext = false;
    bool saw_target = false;
    std::map<TransformationMode, std::string> policies;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--ext=", 0) == 0) {
            if (saw_ext) {
                console::error("init extension specified more than once");
                return false;
            }
            saw_ext = true;
            options.extension = arg.substr(6);
            if (options.extension.empty()) {
                console::error("--ext requires a non-empty extension, for example '--ext=.html'");
                return false;
            }
            continue;
        }
        if (arg == "--ext") {
            console::error("--ext requires '=EXT', for example '--ext=.html'");
            return false;
        }
        if (arg.rfind("--target=", 0) == 0) {
            if (saw_target) {
                console::error("init target specified more than once");
                return false;
            }
            saw_target = true;
            options.target = arg.substr(9);
            if (options.target.empty()) {
                console::error("--target requires a platform name");
                print_init_targets();
                return false;
            }
            continue;
        }
        if (arg == "--target") {
            console::error("--target requires '=PLATFORM', for example '--target=vercel'");
            return false;
        }
        if (arg == "--handover") {
            options.handover = true;
            continue;
        }
        bool transformation_option = false;
        for (const auto mode : {TransformationMode::Migration, TransformationMode::Rewrite, TransformationMode::Redesign}) {
            const std::string flag = "--" + transformation_name(mode);
            const std::string policy_flag = flag + "-existing";
            if (arg == flag) {
                if (options.mode != TransformationMode::None && options.mode != mode) {
                    console::error("init transformation modes are mutually exclusive");
                    return false;
                }
                options.mode = mode;
                transformation_option = true;
                break;
            }
            if (arg == policy_flag) {
                console::error(policy_flag + " requires '=POLICY', for example '" + policy_flag + "=keep'");
                return false;
            }
            if (arg.rfind(policy_flag + "=", 0) == 0) {
                const std::string value = arg.substr(policy_flag.size() + 1);
                if (value != "error" && value != "keep" && value != "append" && value != "replace") {
                    console::error(policy_flag + " must be one of: error, keep, append, replace");
                    return false;
                }
                policies[mode] = value;
                transformation_option = true;
                break;
            }
        }
        if (transformation_option) continue;
        if (!arg.empty() && arg[0] == '-') {
            console::error("unknown init option '" + arg + "'");
            return false;
        }

        console::error("positional init arguments are no longer supported");
        if (filesystem::valid_extension(arg))
            std::cerr << "  use '--ext=" << arg << "' instead\n";
        else
            std::cerr << "  use 'nift init', '--ext=.ext', and/or '--target=platform'\n";
        return false;
    }

    for (const auto& entry : policies) {
        // Preserve the historical migration-policy-only no-op for plain init.
        if (options.mode == TransformationMode::None && entry.first == TransformationMode::Migration) continue;
        if (entry.first != options.mode) {
            console::error("existing-file policy does not match the selected transformation mode");
            return false;
        }
        options.existing = entry.second;
    }

    if (!filesystem::valid_extension(options.extension)) {
        console::error("init extension must begin with '.' and cannot contain path separators");
        return false;
    }

    if (!options.target.empty()) {
        const InitTarget* target = find_init_target(options.target);
        if (!target) {
            console::error("unknown init target '" + options.target + "'");
            print_init_targets();
            return false;
        }
        if (!target_extension_supported(options.extension)) {
            console::error("extension '" + options.extension + "' is not supported by target '" + options.target + "'");
            std::cerr << "  platform targets currently initialise static HTML sites; use '.html' or '.htm'\n";
            return false;
        }
    }

    return true;
}

bool write_target_files(const InitOptions& options) {
    if (options.target.empty()) return true;

    if (options.target == "vercel") {
        json::Document config = json::Document::make_object();
        config["version"] = 3;
        return save_json_file(".vercel/output/config.json", config) &&
               append_line_if_missing(".gitignore", ".vercel/output/static/");
    }

    if (options.target == "amplify") {
        json::Document manifest = json::Document::make_object();
        manifest["version"] = 1;
        manifest["routes"] = json::Document::make_array();
        json::Document route = json::Document::make_object();
        route["path"] = "/*";
        route["target"] = json::Document::make_object();
        route["target"]["kind"] = "Static";
        manifest["routes"].push_back(route);
        manifest["framework"] = json::Document::make_object();
        manifest["framework"]["name"] = "nift";
        manifest["framework"]["version"] = std::string(version_text).substr(std::string("Nift v").size());
        return save_json_file(".amplify-hosting/deploy-manifest.json", manifest) &&
               append_line_if_missing(".gitignore", ".amplify-hosting/static/");
    }

    if (options.target == "netlify") {
        return write_if_changed("netlify.toml",
            "[build]\n"
            "  command = \"nift build\"\n"
            "  publish = \"public\"\n");
    }

    if (options.target == "firebase") {
        json::Document config = json::Document::make_object();
        config["hosting"] = json::Document::make_object();
        config["hosting"]["public"] = "public";
        config["hosting"]["ignore"] = json::Document::make_array();
        config["hosting"]["ignore"].push_back("firebase.json");
        config["hosting"]["ignore"].push_back("**/.*");
        config["hosting"]["ignore"].push_back("**/node_modules/**");
        return save_json_file("firebase.json", config);
    }

    if (options.target == "render") {
        return write_if_changed("render.yaml",
            "services:\n"
            "  - type: web\n"
            "    name: nift-site\n"
            "    runtime: static\n"
            "    buildCommand: nift build\n"
            "    staticPublishPath: ./public\n");
    }

    if (options.target == "cloudflare") {
        return write_if_changed("wrangler.toml",
            "name = \"nift-site\"\n"
            "pages_build_output_dir = \"./public\"\n");
    }

    if (options.target == "azure") {
        // Azure Static Web Apps consumes staticwebapp.config.json from the root
        // of the deployed output. Track the source file through Nift so every
        // clean build reproduces it under public/.
        json::Document config = json::Document::make_object();
        return save_json_file("content/staticwebapp.config.json", config);
    }

    if (options.target == "supabase") {
        // Supabase is backend infrastructure rather than a static frontend host.
        // Keep Nift's ordinary public/ output; users can add Supabase CLI state
        // separately when they actually need local backend development.
        return true;
    }

    if (options.target == "github-pages") {
        // GitHub Pages accepts the ordinary public/ artifact. Deployment
        // workflow setup is documented separately because CI must first make
        // a Nift executable available on the runner.
        return true;
    }

    return true;
}

namespace {

constexpr const char* investigation_readme =
    "# investigation\n"
    "\n"
    "Migration investigation state and evidence. These files accumulate during the\n"
    "migration and are preserved across reruns:\n"
    "\n"
    "- `STATUS.md` - resumable checkpoint ledger: where the migration is now.\n"
    "- `BASELINE.md` - upstream reference, source model and frozen baseline.\n"
    "- `EXTERNAL-INPUTS.md` - external/generated inputs a Git SHA does not capture.\n"
    "- `KNOWN-DIVERGENCES.md` - divergence ledger (upstream vs migration regression).\n"
    "- `PARITY-CONTRACT.md` - what parity means for this migration.\n"
    "\n"
    "Methodology lives in MIGRATION.md. Keep STATUS.md current after each\n"
    "checkpoint.\n";

constexpr const char* migration_readme =
    "# Migration project\n"
    "\n"
    "This project is being migrated to Nift. This README is a concise operational\n"
    "entry point; the full method is in MIGRATION.md.\n"
    "\n"
    "- Status: see `investigation/STATUS.md`.\n"
    "- Upstream reference: see `investigation/BASELINE.md`.\n"
    "- Source model (authored / rendered / hybrid): see `investigation/BASELINE.md`.\n"
    "- Architecture: Nift generation may compose with independently prepared browser-side islands/bundles.\n"
    "- Production-equivalent build: <record command in investigation/BASELINE.md>.\n"
    "- Validation / parity command: <record command in investigation/PARITY-CONTRACT.md>.\n"
    "- Method: `MIGRATION.md`. Current state: `HANDOVER.md`. Agent instructions: `AGENTS.md`.\n"
    "\n"
    "The initial Nift scaffold is placeholder material and must not be counted as\n"
    "migrated content in parity or benchmark claims.\n";

constexpr const char* investigation_baseline =
    "# BASELINE.md\n"
    "\n"
    "The frozen upstream reference. Complete this before migrating content.\n"
    "\n"
    "## Upstream reference\n"
    "\n"
    "- Upstream repository:\n"
    "- Upstream commit SHA:\n"
    "- Upstream source directory:\n"
    "- Production build command:\n"
    "- Toolchain / runtime versions:\n"
    "- Tool/binary hashes (where relevant):\n"
    "\n"
    "## Source model\n"
    "\n"
    "Choose exactly one and describe it:\n"
    "\n"
    "- [ ] authored (human + agent; Markdown/MDX/frontmatter preserved)\n"
    "- [ ] rendered (agent-primary; rendered HTML/CSS/JS + explicit metadata/nav)\n"
    "- [ ] hybrid (describe):\n"
    "\n"
    "Publication/behaviour parity does not require different source models to\n"
    "preserve identical source semantics.\n"
    "\n"
    "## Complete production pipeline\n"
    "\n"
    "A successful build is not necessarily the complete publication. List every\n"
    "production step in order (build, search/index generation, post-processing,\n"
    "API/reference generation, downloads/exports, asset processing, deployment\n"
    "transforms, registry-generated data, client-island/bundle preparation, multi-stage re-builds):\n"
    "\n"
    "1.\n"
    "\n"
    "## Frozen reference output\n"
    "\n"
    "REFERENCE OUTPUT (immutable, upstream):\n"
    "    <absolute path>\n"
    "MIGRATION OUTPUT (Nift, changes over time):\n"
    "    <absolute path>\n"
    "\n"
    "Never point parity comparison at MIGRATION OUTPUT on both sides.\n"
    "\n"
    "## Upstream nondeterminism classification\n"
    "\n"
    "- [ ] byte deterministic\n"
    "- [ ] semantic deterministic\n"
    "- [ ] nondeterministic but bounded/understood (describe)\n"
    "- [ ] unresolved\n"
    "\n"
    "## Baseline measurements\n"
    "\n"
    "- Upstream build time (method, median/range):\n"
    "- Upstream peak RSS:\n"
    "- Environment / hardware:\n";

constexpr const char* investigation_external_inputs =
    "# EXTERNAL-INPUTS.md\n"
    "\n"
    "A pinned Git SHA does not necessarily define the complete production input\n"
    "set. Inventory inputs that can move independently of Git or the toolchain.\n"
    "\n"
    "Only fill in rows that apply; this is a checklist, not bureaucracy.\n"
    "\n"
    "## Inventory\n"
    "\n"
    "For each relevant input record: source URL/system; version/revision; captured\n"
    "body/hash; capture date; deterministic (yes/no/unknown); can move independently\n"
    "of Git SHA (yes/no); credentials required; cache boundary; must be frozen for\n"
    "parity (yes/no).\n"
    "\n"
    "| Input | Source | Version/revision | Captured hash | Date | Deterministic | Moves w/o Git | Creds | Cache boundary | Freeze |\n"
    "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |\n"
    "| | | | | | | | | | |\n"
    "\n"
    "## Categories to consider\n"
    "\n"
    "- network-fetched inputs;\n"
    "- registry-derived inputs;\n"
    "- generated API/reference data;\n"
    "- environment-derived inputs;\n"
    "- tool-version-derived inputs;\n"
    "- search/index inputs;\n"
    "- other generated publication inputs.\n";

constexpr const char* investigation_known_divergences =
    "# KNOWN-DIVERGENCES.md\n"
    "\n"
    "Every difference from the frozen reference must be recorded and classified.\n"
    "Distinguish inherited upstream behaviour from a migration regression.\n"
    "\n"
    "| ID | Route/component | Observed behaviour | Classification | Evidence | Approval/rationale | Resolution |\n"
    "| --- | --- | --- | --- | --- | --- | --- |\n"
    "| | | | | | | |\n"
    "\n"
    "Classification: inherited upstream / intentional migration difference /\n"
    "unresolved / blocking.\n"
    "\n"
    "A migration is not complete while any entry is unresolved or blocking.\n";

constexpr const char* investigation_status =
    "# STATUS.md\n"
    "\n"
    "Resumable migration state. A new agent should be able to read `AGENTS.md`,\n"
    "`MIGRATION.md`, this file and `HANDOVER.md` and know exactly where the\n"
    "migration is without reconstructing history.\n"
    "\n"
    "## Phases\n"
    "\n"
    "Mark each: not started / in progress / blocked / done.\n"
    "\n"
    "| Phase | Status | Acceptance criteria | Required evidence | Commands | Commit |\n"
    "| --- | --- | --- | --- | --- | --- |\n"
    "| 1 Baseline frozen | | | | | |\n"
    "| 2 Parity contract + fixtures | | | | | |\n"
    "| 3 Initial Nift structure + compatibility proof | | | | | |\n"
    "| 4 Shared shells/templates | | | | | |\n"
    "| 5 Authored content | | | | | |\n"
    "| 6 Source compatibility | | | | | |\n"
    "| 7 Route/content/render/browser/behaviour parity | | | | | |\n"
    "| 8 Incremental correctness | | | | | |\n"
    "| 9 Performance campaign | | Profiles, general improvements or justified deferrals | | | |\n"
    "| 10 Final parity revalidation | | Complete parity contract after optimization | | | |\n"
    "| 11 Final benchmark campaign | | Optimized, parity-certified production pipeline | | | |\n"
    "| 12 Clean-checkout verification | | | | | |\n"
    "| 13 Handover / final report | | | | | |\n"
    "\n"
    "GATE: compatibility proof must precede broad content translation. Do not mark\n"
    "phase 5 in progress until phases 1-4 acceptance criteria are met.\n"
    "GATE: complete parity precedes profiling/optimization; full parity revalidation\n"
    "after the campaign precedes final benchmarking.\n"
    "\n"
    "## Architecture and campaign evidence\n"
    "\n"
    "- Significant retained/introduced islands and reasons (parity, source model,\n"
    "  accessibility, shared state, maintenance and bundle/runtime cost):\n"
    "- Island/bundle preparation and mounting commands:\n"
    "- Profiles/hotspots and before/after measurements:\n"
    "- General improvements, tradeoffs and deliberately deferred bottlenecks:\n"
    "\n"
    "## Current\n"
    "\n"
    "- Checkpoint:\n"
    "- Commit SHA:\n"
    "- Known blockers:\n"
    "- Next checkpoint:\n";

constexpr const char* investigation_parity_contract =
    "# PARITY-CONTRACT.md\n"
    "\n"
    "What \"parity\" means for THIS migration. Select what applies and record the\n"
    "exact rule and command for each.\n"
    "\n"
    "- [ ] route parity\n"
    "- [ ] generated-file parity\n"
    "- [ ] content parity\n"
    "- [ ] browser/visual parity (viewports):\n"
    "- [ ] runtime/interactive behaviour\n"
    "- [ ] keyboard/accessibility behaviour\n"
    "- [ ] redirects\n"
    "- [ ] search/indexing\n"
    "- [ ] downloads/exports\n"
    "\n"
    "Equality rule per item (byte vs semantic):\n"
    "\n"
    "- Allowed divergences: see `KNOWN-DIVERGENCES.md`.\n"
    "- Parity command(s):\n"
    "- Reference used for comparison: see `BASELINE.md` frozen reference output.\n";

constexpr const char* kAgentsMigrationBlock =
    "<!-- nift:migration:start -->\n"
    "## Nift migration\n"
    "\n"
    "This project is being migrated to Nift.\n"
    "\n"
    "Read MIGRATION.md for the method, investigation/STATUS.md for where the\n"
    "migration is, and HANDOVER.md for current state.\n"
    "The initial Nift scaffold is placeholder; the baseline must be frozen before\n"
    "any content translation.\n"
    "Preserve the source baseline until parity verification is complete.\n"
    "Follow migration checkpoints. Framework islands are valid Nift architecture;\n"
    "Nift generation composes with independently prepared browser-side bundles.\n"
    "Prove parity, then profile/optimize, revalidate full parity, and benchmark.\n"
    "Maintain route/content/behaviour parity unless divergence is explicitly approved.\n"
    "Record and classify known divergences (investigation/KNOWN-DIVERGENCES.md).\n"
    "Do not modify Nift core to solve source-project compatibility gaps without\n"
    "stopping and reporting the requirement.\n"
    "Prefer compatibility adapters/stages over rewriting source semantics solely for\n"
    "cleanliness.\n"
    "Run the required parity/build checks before checkpoint commits.\n"
    "Update HANDOVER.md and investigation/STATUS.md after meaningful checkpoints.\n"
    "Do not declare completion without clean-checkout verification.\n"
    "<!-- nift:migration:end -->\n";


enum class MigrationPolicy { Error, Keep, Append, Replace };

MigrationPolicy migration_policy(const std::string& value) {
    if (value == "keep") return MigrationPolicy::Keep;
    if (value == "append") return MigrationPolicy::Append;
    if (value == "replace") return MigrationPolicy::Replace;
    return MigrationPolicy::Error;
}

std::size_t count_substrings(const std::string& text, const std::string& needle) {
    std::size_t count = 0, pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) { ++count; pos += needle.size(); }
    return count;
}

// Write a canonical template under an existing-file policy. Fresh generation
// (error/replace) and keep write canonical bytes unchanged; append wraps the
// guidance in a deterministic marker block and never duplicates; incomplete or
// multiple owned blocks fail safely.
bool write_migration_file(const std::string& path, const std::string& content,
                          MigrationPolicy policy, const std::string& start_marker,
                          const std::string& end_marker, bool exists,
                          const std::string& mode = "migration") {
    if (policy == MigrationPolicy::Error) {
        if (exists) {
            std::cerr << "  " << path << " already exists (use --" << mode << "-existing=keep, append, or replace)\n";
            return false;
        }
        return filesystem::write_file(path, content);
    }
    if (policy == MigrationPolicy::Keep) {
        if (exists) return true;
        return filesystem::write_file(path, content);
    }
    if (policy == MigrationPolicy::Replace)
        return filesystem::write_file(path, content);
    // Append: managed block, idempotent, fail-safe on malformed state.
    std::string existing;
    if (exists) {
        existing = filesystem::read_file(path);
        const std::size_t sc = count_substrings(existing, start_marker);
        const std::size_t ec = count_substrings(existing, end_marker);
        if (sc == 1 && ec == 1 && existing.find(start_marker) < existing.find(end_marker)) return true;
        if (sc != 0 || ec != 0) return false;
    }
    std::string block = start_marker + "\n" + content;
    if (content.empty() || content.back() != '\n') block += "\n";
    block += end_marker + "\n";
    std::string merged = existing;
    if (!merged.empty() && merged.back() != '\n') merged += "\n";
    merged += block;
    return filesystem::write_file(path, merged);
}

// AGENTS.md managed-block state classification.
enum class AgentsState { None, Valid, StartOnly, EndOnly, Multiple };

AgentsState agents_block_state(const std::string& text, const std::string& mode = "migration") {
    const std::string start = "<!-- nift:" + mode + ":start -->";
    const std::string end = "<!-- nift:" + mode + ":end -->";
    const std::size_t sc = count_substrings(text, start);
    const std::size_t ec = count_substrings(text, end);
    if (sc > 1 || ec > 1) return AgentsState::Multiple;
    if (sc == 1 && ec == 1) {
        if (text.find(start) < text.find(end)) return AgentsState::Valid;
        return AgentsState::Multiple;
    }
    if (sc == 1 || ec == 1) return AgentsState::StartOnly;
    return AgentsState::None;
}

// Augment AGENTS.md with Nift's managed migration block: create the file when
// absent, append the block when no block exists, replace exactly one valid
// block, and fail safely on incomplete/multiple markers.
bool augment_agents_file(const std::string& mode, const std::string& block) {
    const std::string start = "<!-- nift:" + mode + ":start -->";
    const std::string end = "<!-- nift:" + mode + ":end -->";
    std::string existing;
    if (fs::exists("AGENTS.md")) existing = filesystem::read_file("AGENTS.md");
    const AgentsState state = agents_block_state(existing, mode);
    if (state == AgentsState::Multiple) {
        std::cerr << "  AGENTS.md has an invalid Nift migration block; refusing to modify it\n";
        return false;
    }
    if (state == AgentsState::StartOnly || state == AgentsState::EndOnly) {
        std::cerr << "  AGENTS.md has an incomplete Nift migration marker; refusing to modify it\n";
        return false;
    }
    if (state == AgentsState::Valid) {
        const std::size_t s = existing.find(start);
        const std::size_t e = existing.find(end);
        std::size_t block_end = e + end.size();
        if (block_end < existing.size() && existing[block_end] == '\n') ++block_end;
        std::string updated = existing.substr(0, s) + block;
        if (block_end < existing.size()) updated += existing.substr(block_end);
        if (!updated.empty() && updated.back() != '\n') updated += "\n";
        return filesystem::write_file("AGENTS.md", updated);
    }
    std::string merged = existing;
    if (!merged.empty() && merged.back() != '\n') merged += "\n";
    if (!merged.empty()) merged += "\n";
    merged += block;
    return filesystem::write_file("AGENTS.md", merged);
}

using GuidanceFiles = std::vector<std::pair<std::string, std::string>>;

std::string guidance_marker(const std::string& path) {
    if (path == "README.md") return "readme";
    if (path == "investigation/README.md") return "investigation";
    std::string name = fs::path(path).stem().string();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name;
}

bool valid_template_boundary(const std::string& path, const std::string& marker) {
    if (!fs::exists(path)) return true;
    const std::string text = filesystem::read_file(path);
    const std::string start = "<!-- nift:" + marker + ":start -->";
    const std::string end = "<!-- nift:" + marker + ":end -->";
    const auto sc = count_substrings(text, start), ec = count_substrings(text, end);
    if ((sc == 0 && ec == 0) || (sc == 1 && ec == 1 && text.find(start) < text.find(end))) return true;
    console::error("cannot initialise transformation project: invalid managed boundary in " + path);
    return false;
}

std::string transformation_agents(TransformationMode mode) {
    if (mode == TransformationMode::Migration) return kAgentsMigrationBlock;
    const std::string name = transformation_name(mode), workbook = transformation_workbook(mode);
    const std::string contract = mode == TransformationMode::Rewrite ?
        "Preserve the product, visual design, routes, content, browser/keyboard/accessibility and SEO behaviour.\n"
        "Replace implementation freely; old architecture is reference evidence, not a constraint.\n" :
        "Replace implementation and deliberately redesign the UX. Protect explicitly inventoried requirements,\n"
        "content, capabilities, legal/accessibility obligations and SEO/inbound route value.\n"
        "Approve design direction before broad implementation; record every route redirect or retirement.\n";
    return "<!-- nift:" + name + ":start -->\n## Nift " + name + " (EXPERIMENTAL, v4.8)\n\n" + contract +
        "Read " + workbook + ", investigation/STATUS.md and HANDOVER.md before work.\n"
        "The initial scaffold is placeholder material. Freeze reference evidence before building.\n"
        "Transformation intent is independent of authored/rendered/hybrid source model.\n"
        "React, Vue, Svelte, Solid, Web Components and vanilla JavaScript islands are valid architecture;\n"
        "prepare browser-side bundles independently of Nift generation. Record accessibility/state/cost tradeoffs.\n"
        "Validate the full contract, run a performance campaign, fully revalidate, then benchmark the final pipeline.\n"
        "Stop and report any requirement to modify Nift core; do not silently change runtime semantics.\n"
        "Record approved differences in investigation/KNOWN-DIVERGENCES.md; unresolved blockers prevent completion.\n"
        "Update STATUS.md and HANDOVER.md at checkpoints. Require clean-checkout proof before completion.\n"
        "Publication, destructive changes and changes of transformation intent require explicit authorization.\n"
        "<!-- nift:" + name + ":end -->\n";
}

GuidanceFiles transformation_guidance(TransformationMode mode) {
    if (mode == TransformationMode::Migration) return {
        {"README.md", migration_readme}, {"investigation/README.md", investigation_readme},
        {"investigation/STATUS.md", investigation_status}, {"investigation/BASELINE.md", investigation_baseline},
        {"investigation/EXTERNAL-INPUTS.md", investigation_external_inputs},
        {"investigation/KNOWN-DIVERGENCES.md", investigation_known_divergences},
        {"investigation/PARITY-CONTRACT.md", investigation_parity_contract}};
    const bool rewrite = mode == TransformationMode::Rewrite;
    const std::string name = transformation_name(mode), workbook = transformation_workbook(mode);
    const std::string reference = rewrite ? "REFERENCE.md" : "DESIGN-BRIEF.md";
    const std::vector<std::string> phases = rewrite ? std::vector<std::string>{
        "Understand old product", "Freeze design/behaviour reference", "Inventory content/routes/features",
        "Extract requirements", "Choose Nift-native architecture", "Prove representative vertical slice",
        "Rebuild shared structure", "Rebuild full corpus", "Validate design/behaviour parity",
        "Performance campaign", "Full parity revalidation", "Final benchmarks", "Clean-checkout proof", "Handover"} :
        std::vector<std::string>{"Audit existing project", "Extract immutable requirements",
        "Identify strengths/failures/obsolete parts", "Inventory content/capabilities/routes", "Define redesign goals",
        "Define information architecture", "Define design system", "Define interaction model", "Choose source model",
        "Build representative prototype", "Validate and approve direction", "Build complete project",
        "Accessibility/browser validation", "Performance campaign", "Final contract revalidation",
        "Final meaningful benchmark/comparison", "Clean-checkout proof", "Handover"};
    std::string status = "# STATUS.md\n\nResumable " + name + " state (EXPERIMENTAL, v4.8). Read " + workbook +
        ", AGENTS.md and HANDOVER.md.\n\nMark each phase: not started / in progress / blocked / done.\n\n"
        "| Phase | Status | Acceptance criteria | Required evidence | Commands | Commit |\n"
        "| --- | --- | --- | --- | --- | --- |\n";
    for (std::size_t i = 0; i < phases.size(); ++i)
        status += "| " + std::to_string(i + 1) + " " + phases[i] + " | | | | | |\n";
    status += rewrite ? "\nGATE: freeze product/design/behaviour evidence and prove a vertical slice before broad rebuilding.\n" :
        "\nGATE: approve representative design direction against protected requirements before broad rebuilding.\n";
    status += "GATE: complete validation precedes profiling/optimization; full revalidation precedes final benchmarks.\n\n"
        "## Architecture and campaign evidence\n\n"
        "- Significant retained/introduced islands; source model, accessibility/shared-state/maintenance/cost reasons:\n"
        "- Complete production pipeline, including independently prepared browser-side bundles:\n"
        "- Profiles/hotspots, general improvements, before/after measurements and justified deferrals:\n"
        "- Final validation command/result and equivalent benchmark workloads (qualify changed workloads):\n\n"
        "## Current\n\n- Checkpoint:\n- Commit SHA:\n- Blockers/approved decisions:\n- Next checkpoint:\n";
    GuidanceFiles files = {
        {"README.md", "# " + name + " project\n\nEXPERIMENTAL agent-oriented workflow, v4.8.\n\nRead " + workbook +
            " for methodology, investigation/STATUS.md for checkpoints, HANDOVER.md for current state and AGENTS.md for the contract.\n"
            "The initial scaffold is placeholder material, excluded from parity and benchmark claims.\n"
            "Record authored/rendered/hybrid source model and the complete production pipeline in investigation/" + reference + ".\n"
            "Nift generation may compose with independently prepared browser-side islands/bundles.\n"},
        {"investigation/STATUS.md", status},
        {"investigation/EXTERNAL-INPUTS.md", investigation_external_inputs},
        {"investigation/KNOWN-DIVERGENCES.md", "# KNOWN-DIVERGENCES.md\n\nRecord differences from frozen evidence; distinguish inherited defects, approved decisions and regressions.\n\n"
            "| ID | Route/component | Difference | Classification | Evidence | Approval/rationale | Resolution |\n"
            "| --- | --- | --- | --- | --- | --- | --- |\n| | | | | | | |\n\nUnresolved or blocking entries prevent completion.\n"},
        {"investigation/CONTENT-INVENTORY.md", "# CONTENT-INVENTORY.md\n\nInventory routes, content, assets, metadata, search/download/export and client interactions.\n\n"
            "| Item/route | Source | Requirement | Proposed disposition | Owner/approval | Validation evidence |\n"
            "| --- | --- | --- | --- | --- | --- |\n| | | | | | |\n"}};
    const std::string source_model = "\n## Source model and production pipeline\n\nChoose authored / rendered / hybrid independently of transformation intent.\n"
        "Record source SHA, immutable reference output, candidate output, toolchain/binary hashes, external inputs,\n"
        "all production stages including island/bundle preparation, deployment transforms, search and exports.\n"
        "Never compare candidate output against itself. Record nondeterminism and byte/semantic equality rules.\n";
    if (rewrite) {
        files.push_back({"investigation/REFERENCE.md", "# REFERENCE.md\n\nFreeze the old product's design and behaviour as immutable evidence.\n"
            "Record repository/SHA, screenshots/viewports, routes, content, browser/keyboard/accessibility/SEO behaviour and reference build command.\n"
            "Old implementation is evidence, not architecture to copy.\n" + source_model});
        files.push_back({"investigation/BEHAVIOR-CONTRACT.md", "# BEHAVIOR-CONTRACT.md\n\nSpecify route/content, redirects, search/download/export, responsive/browser interactions, keyboard/accessibility and SEO/meta parity.\n"
            "Record equality rule, fixtures and validation command for each. Differences require explicit approval.\n"});
        files.push_back({"investigation/DESIGN-CONTRACT.md", "# DESIGN-CONTRACT.md\n\nFreeze visual design, layout, typography, breakpoints, assets and interaction states.\n"
            "Record reference viewports and comparison tolerances; new internal architecture does not authorize design changes.\n"});
    } else {
        files.push_back({"investigation/REQUIREMENTS.md", "# REQUIREMENTS.md\n\nExtract immutable requirements and protected content/capabilities, legal/compliance material, accessibility,\n"
            "SEO/inbound route value, downloads/exports/search and retained brand assets.\n"
            "For each requirement record source evidence, acceptance command and approval for any deliberate change.\n"});
        files.push_back({"investigation/DESIGN-BRIEF.md", "# DESIGN-BRIEF.md\n\nDefine goals, information architecture, visual/design system, typography/layout/navigation and interaction model.\n"
            "The old project is source material, not a visual or architecture specification.\n"
            "Record representative prototype, user approval and protected-requirement validation before broad rebuilding.\n" + source_model});
        files.push_back({"investigation/ROUTE-MAP.md", "# ROUTE-MAP.md\n\nRoute changes require deliberate decisions and evidence. Preserve inbound value with redirects where appropriate.\n\n"
            "| Old route | New route | Redirect/retirement decision | Approval | Evidence/test |\n"
            "| --- | --- | --- | --- | --- |\n| | | | | |\n"});
    }
    std::string index = "# investigation\n\n" + name + " state and evidence. Methodology: " + workbook + ". Keep STATUS.md current at each checkpoint.\n\n";
    for (const auto& file : files) if (file.first.rfind("investigation/", 0) == 0)
        index += "- `" + fs::path(file.first).filename().string() + "`\n";
    files.push_back({"investigation/README.md", index});
    return files;
}


}  // namespace

bool initialise_project(const InitOptions& options) {
    // Existing-project transformation workspaces only add missing files. Even
    // explicit replace/append options cannot overwrite an existing project.
    if(options.mode==TransformationMode::Rewrite || options.mode==TransformationMode::Redesign) {
        fs::path existing=fs::current_path();
        while(true){if(fs::exists(existing/".nift/config.json")&&fs::exists(existing/".nift/tracked.json"))break;const auto parent=existing.parent_path();if(parent==existing){existing.clear();break;}existing=parent;}
        if(!existing.empty()) {
            const std::string mode=transformation_name(options.mode);
            auto files=transformation_guidance(options.mode);
            files.emplace_back(transformation_workbook(options.mode),options.mode==TransformationMode::Rewrite?rewrite_content:redesign_content);
            files.emplace_back("HANDOVER.md",handover_content);
            files.emplace_back("AGENTS.md",transformation_agents(options.mode));
            for(const auto& file:files) {
                const auto destination=existing/file.first;
                std::error_code ec;
                const auto state=fs::symlink_status(destination,ec);
                if(fs::exists(state)){std::cout<<"existing file kept: "<<file.first<<'\n';continue;}
                // Never follow an existing scaffold directory symlink.
                for(auto parent=destination.parent_path();parent!=existing;parent=parent.parent_path()) {
                    ec.clear();if(fs::is_symlink(fs::symlink_status(parent,ec))){console::error("cannot scaffold through symlink: "+parent.generic_string());return false;}
                }
                ec.clear();fs::create_directories(destination.parent_path(),ec);
                if(ec||!filesystem::write_file(destination,file.second)){console::error("failed to scaffold "+file.first);return false;}
            }
            std::cout<<"Added missing "<<mode<<" workspace files; existing project files kept\n";
            return true;
        }
    }
    // Refuse to initialize over an existing Nift project. A project root is
    // identified by its project configuration; a partial .nift directory that
    // is not yet a project (for example one left behind by a failed earlier
    // init, or carrying only a .lock) may be completed safely, which is what
    // makes the existing-`.lock` cases below reachable through `nift init`.
    if (fs::exists(".nift/config.json") || fs::exists(".nift/tracked.json")) {
        console::error("cannot initialise project");
        std::cerr << "  this directory is already a Nift project\n";
        return false;
    }

    const MigrationPolicy policy = migration_policy(options.existing);
    const bool transforming = options.mode != TransformationMode::None;
    const std::string mode = transformation_name(options.mode);
    const std::string workbook = transforming ? transformation_workbook(options.mode) : "";
    if (transforming) {
        const std::string agents = fs::exists("AGENTS.md") ? filesystem::read_file("AGENTS.md") : "";
        for (const auto other : {TransformationMode::Migration, TransformationMode::Rewrite, TransformationMode::Redesign}) {
            const std::string name = transformation_name(other);
            const AgentsState state = agents_block_state(agents, name);
            if (other != options.mode && (fs::exists(transformation_workbook(other)) || state != AgentsState::None)) {
                console::error("cannot initialise " + mode + " project: existing " + name + " contract conflicts");
                return false;
            }
            if (state != AgentsState::None && state != AgentsState::Valid) {
                console::error("cannot initialise " + mode + " project: AGENTS.md has an invalid Nift " + name + " block");
                return false;
            }
        }
        // Check every owned append boundary before creating any project files.
        // A malformed boundary never grants ownership, even under replace/keep.
        for (const auto& file : transformation_guidance(options.mode)) {
            if (!valid_template_boundary(file.first, mode + "-" + guidance_marker(file.first))) return false;
        }
        if (!valid_template_boundary(workbook, mode + "-template") ||
            !valid_template_boundary("HANDOVER.md", "handover-template")) return false;
        if (policy == MigrationPolicy::Error) {
            std::vector<std::string> conflicts;
            if (fs::exists(workbook)) conflicts.push_back(workbook);
            if (fs::exists("HANDOVER.md")) conflicts.emplace_back("HANDOVER.md");
            if (!conflicts.empty()) {
                console::error("cannot initialise " + mode + " project: existing files conflict");
                for (const auto& c : conflicts) std::cerr << "  " << c << " already exists\n";
                std::cerr << "  use --" << mode << "-existing=keep, append, or replace to handle existing " << mode << " files\n";
                return false;
            }
        }
    }

    const InitTarget* target = options.target.empty() ? nullptr : find_init_target(options.target);
    const std::string output_dir = target ? target->output_dir : "public/";
    const bool html_family = html_family_extension(options.extension);

    fs::create_directories(".nift");
    // The persistent project-local serialization lock is part of a valid
    // project: establish it (creating, or repairing an empty file, while never
    // replacing a non-empty one) before any project configuration is written,
    // so a failed init leaves no misleading successful project state and a
    // fresh or partial project is never missing its .lock. This does not
    // create .unfinished.
    std::string lock_error;
    if (!ProjectOwnership::ensure_lock_file(".nift", &lock_error)) {
        console::error("cannot initialise project: " + lock_error);
        return false;
    }
    fs::create_directories("content");
    fs::create_directories("templates");
    fs::create_directories(output_dir);
    if (html_family) {
        fs::create_directories(fs::path(output_dir) / "assets/css");
        fs::create_directories(fs::path(output_dir) / "assets/js");
    }

    json::Document config = json::Document::make_object();
    config["config"] = json::Document::make_object();
    config["config"]["content-dir"] = "content/";
    config["config"]["content-ext"] = options.extension;
    config["config"]["output-dir"] = output_dir;
    config["config"]["output-ext"] = options.extension;
    config["config"]["default-template"] = html_family ? "templates/template.html" : "";
    config["config"]["build-threads"] = -1;
    config["config"]["incremental-mode"] = "modified";
    config["config"]["minify-exts"] = json::Document::make_array();
    if (!save_json_file(".nift/config.json", config)) return false;

    json::Document tracked = json::Document::make_object();
    tracked["tracked"] = json::Document::make_array();
    auto add = [&](const std::string& name, const std::string& title, const std::string& templ, const std::string& ce = "", const std::string& oe = "") {
        json::Document value = json::Document::make_object();
        value["name"] = name; value["title"] = title;
        if (!templ.empty()) value["template"] = templ;
        if (!ce.empty()) value["content-ext"] = ce;
        if (!oe.empty()) value["output-ext"] = oe;
        tracked["tracked"].push_back(value);
    };
    add("/", "index", html_family ? "templates/template.html" : "");
    if (options.target == "azure")
        add("staticwebapp.config", "Azure Static Web Apps config", "", ".json", ".json");
    if (!save_json_file(".nift/tracked.json", tracked)) return false;

    if (!filesystem::write_file("content/index" + options.extension, "")) return false;
    if (html_family) {
        if (!filesystem::write_file(fs::path(output_dir) / "assets/css/style.css", "")) return false;
        if (!filesystem::write_file(fs::path(output_dir) / "assets/js/script.js", "")) return false;
        if (!filesystem::write_file("templates/head.html", "<meta charset=\"utf-8\">\n<title>$[title]</title>\n")) return false;
        if (!filesystem::write_file("templates/template.html", "<!doctype html>\n<html lang=\"en\">\n\t<head>\n\t\t@input(\"templates/head.html\")\n\t</head>\n\t<body>\n@content\n\t</body>\n</html>\n")) return false;
    }

    if (!write_target_files(options)) return false;

    // New projects ignore the runtime serialization lock through Git. The rule
    // is scoped to .nift/.lock (never a bare *.lock or .lock), because the
    // lock is a runtime coordination artifact, not portable project
    // configuration.
    if (!append_line_if_missing(".gitignore", ".nift/.lock")) return false;

    // The handover is part of project creation: it is written alongside the
    // other scaffold files and before the initial build, so a project created
    // with --handover contains HANDOVER.md even if the first build later fails
    // for an unrelated reason. It goes in the project root (never under the
    // output directory) and stays writable as a living document the user keeps
    // alongside the source.
    if (transforming) {
        const char* content = options.mode == TransformationMode::Migration ? migration_content :
                              options.mode == TransformationMode::Rewrite ? rewrite_content : redesign_content;
        if (!write_migration_file(workbook, content, policy,
                "<!-- nift:" + mode + "-template:start -->", "<!-- nift:" + mode + "-template:end -->",
                fs::exists(workbook), mode) ||
            !write_migration_file("HANDOVER.md", handover_content, policy,
                "<!-- nift:handover-template:start -->", "<!-- nift:handover-template:end -->",
                fs::exists("HANDOVER.md"), mode) ||
            !augment_agents_file(mode, transformation_agents(options.mode))) {
            console::error("failed to write " + mode + " contract files");
            return false;
        }
        fs::create_directories("investigation");
        for (const auto& file : transformation_guidance(options.mode)) {
            const bool exists = fs::exists(file.first);
            const MigrationPolicy effective = policy == MigrationPolicy::Error && exists ? MigrationPolicy::Keep : policy;
            const std::string marker = mode + "-" + guidance_marker(file.first);
            if (!write_migration_file(file.first, file.second, effective,
                    "<!-- nift:" + marker + ":start -->", "<!-- nift:" + marker + ":end -->", exists, mode)) {
                console::error("failed to write " + mode + " guidance files");
                return false;
            }
        }
    } else if (options.handover) {
        if (!filesystem::write_file("HANDOVER.md", std::string(handover_content))) {
            console::error("failed to write HANDOVER.md");
            return false;
        }
    }

    ProjectInfo project;
    return project.open() && project.build_all(true) == 0;
}
}


static int run_script_shell_loop(Parser& parser, bool load_rc, nift::rt::VmSession* session = nullptr);

enum class SessionDisplay { Repl, Script, Rc };
static nift::rt::VmOptions session_options() {
    nift::rt::VmOptions options;
    options.flush = [](std::string& output, void*) {
        std::cout.write(output.data(), static_cast<std::streamsize>(output.size())); output.clear();
    };
    if (const auto* threshold = std::getenv("NIFT_VM_FLUSH")) {
        char* end = nullptr; const auto value = std::strtoull(threshold, &end, 10);
        if (end && *end == 0 && value) options.flush_threshold = static_cast<std::size_t>(value);
    }
    return options;
}
static int run_vm_session_statement(nift::rt::VmSession& session, const std::string& source,
                                    const std::string& path, bool inspect = true,
                                    SessionDisplay display = SessionDisplay::Repl) {
    bool expression = false;
    if (inspect) {
        nift::rt::Ast ast(source);
        const auto parsed = nift::rt::parse_script(ast);
        if (parsed.ok && ast.at(parsed.root).count == 1)
            expression = ast.at(ast.list(ast.at(parsed.root))[0]).kind == nift::rt::NodeKind::ExprStmt;
    }
    std::string output;
    const auto result = expression ? session.evaluate(source, path, output) : session.execute(source, path, output, inspect);
    if (!output.empty()) std::cout << output;
    const auto mode = nift::rt::vm_mode_from_environment();
    if (mode == nift::rt::VmMode::Classify || mode == nift::rt::VmMode::ClassifyAll) return 0;
    if (!result.admitted) { console::error("NIFT_VM session refused: " + result.refusal); return 1; }
    if (result.execution.reason != nift::rt::StopReason::None) {
        std::string message = result.execution.message;
        if (display == SessionDisplay::Rc) message = "niftrc: " + message;
        if (display == SessionDisplay::Script) {
            const auto origin = result.execution.error_origin ? nift::rt::SourceOrigin{
                result.execution.origin_source, result.execution.origin_line, result.execution.origin_column} :
                result.execution.report_origin;
            if (!origin.path.empty() && origin.line)
                message = origin.path + ':' + std::to_string(origin.line) + ':' +
                          std::to_string(origin.column) + ": " + message;
        }
        console::error(message);
        return result.execution.reason == nift::rt::StopReason::Internal ? 70 : 1;
    }
    if (!expression && result.execution.returned && !result.execution.return_inspection && result.value.is_null())
        std::cout << "null\n";
    if ((expression || result.execution.returned) && !result.value.is_null()) {
        nift::RuntimeValue value;
        std::string error;
        if (!nift::rt::eval_result_to_legacy(result.value, value, error)) { console::error(error); return 70; }
        if (!(value.is_string() && value.string.empty()))
            std::cout << ((!expression || result.presentation) && value.is_string() ? value.string : value.dump()) << '\n';
    }
    return 0;
}

static int run_script_source(const std::string& source, const fs::path& source_path,
                             const std::string& cmd, const std::vector<std::string>& script_args,
                             const fs::path& host_root, bool interactive_after = false, const std::string& target = "native") {
    // Runtime 2.0 CP13 (D-022 #3): the whole unit is syntax-validated before
    // engine selection, so a unit routed to legacy cannot bypass it. On
    // failure nothing has run: no output, no effects. NIFT_PREVALIDATE=only
    // validates and exits (0 valid, 65 invalid) without running (gate tool);
    // NIFT_PREVALIDATE=off skips validation (test observation of the engines'
    // own behaviour only, e.g. rules_test.py's legacy column; never a mode).
    const char* prevalidate_mode = std::getenv("NIFT_PREVALIDATE");
    if (!(prevalidate_mode && std::string(prevalidate_mode) == "off")) {
        const auto verdict = nift::rt::prevalidate(source);
        const bool validate_only = prevalidate_mode && std::string(prevalidate_mode) == "only";
        if (!verdict.ok) {
            const auto where = nift::rt::locate(source, verdict.offset);
            // The gate's check mode also needs the code and the rule (as
            // `rt-parse-dump prevalidate` prints them); a normal run prints
            // the diagnostic only.
            console::error(source_path.generic_string() + ':' + std::to_string(where.line) + ':' +
                           std::to_string(where.column) + ": " + verdict.message +
                           (validate_only ? std::string(" [") + verdict.code + "] " + verdict.rule : std::string()));
            return validate_only ? 65 : 1;
        }
        if (validate_only) return 0;
    }
    if (interactive_after && target == "native" && nift::rt::vm_mode_from_environment() != nift::rt::VmMode::Off) {
        nift::rt::VmSession session(session_options());
        const int status = run_vm_session_statement(session, source, source_path.generic_string(), false,
                                                    SessionDisplay::Script);
        if (status != 0) return status;
        ScriptRenderHost host(host_root, target); TrackedInfo info; Parser parser(host, info);
        parser.set_script_invocation(cmd, script_args);
        return run_script_shell_loop(parser, false, &session);
    }
    // Runtime 2.0 CP11: opt-in VM (NIFT_VM=1); returns false to run legacy unchanged.
    if (!interactive_after && target == "native") {
        int vm_exit = 0;
        if (nift::rt::run_script_on_vm(source, source_path, vm_exit)) return vm_exit;
    }
    ScriptRenderHost host(host_root, target); TrackedInfo info; Parser parser(host,info);
    parser.set_script_invocation(cmd, script_args);
    auto rr=parser.run_script(source,source_path);
    if(!rr.ok){
        const std::string message=rr.error.message.empty()?"script failed":rr.error.message;
        if(!rr.error.source_file.empty() && rr.error.line>0) {
            std::ostringstream where;
            where << rr.error.source_file.generic_string() << ':' << rr.error.line;
            if(rr.error.column>0) where << ':' << rr.error.column;
            console::error(where.str() + ": " + message);
        } else console::error(message);
        return 1;
    }
    if(!rr.output.empty())std::cout<<rr.output<<'\n';
    if(interactive_after) return run_script_shell_loop(parser, /*load_rc=*/false);
    return 0;
}

static int run_script_file(const fs::path& path, const std::vector<std::string>& script_args = {}, bool interactive_after = false, const std::string& target = "native") {
    std::error_code path_ec;
    const fs::path absolute=fs::absolute(path, path_ec).lexically_normal();
    if (path_ec) { console::error("script: cannot resolve script path: " + path.string() + " (" + path_ec.message() + ")"); return 1; }
    if(!filesystem::file_exists(absolute)){console::error("script: script does not exist: "+path.string());return 1;}
    std::string source=filesystem::read_file(absolute);
    // A leading shebang (#!/usr/bin/env nift) is a script header, not Nift
    // syntax. Strip the first line so direct file execution accepts it too.
    if(source.rfind("#!",0)==0){
        const std::size_t nl=source.find('\n');
        source = (nl==std::string::npos) ? "" : source.substr(nl+1);
    }
    return run_script_source(source, absolute, path.string(), script_args, absolute.parent_path(), interactive_after, target);
}

static int run_inline_script(const std::string& source, const std::vector<std::string>& script_args, bool interactive_after, const std::string& target = "native") {
    // `exit`/`quit` are shell control words. Accept them as the initial inline
    // program too so `nift -i -c "exit"` is a useful non-interactive probe and
    // does not unexpectedly enter a REPL.
    std::string trimmed=source;
    const auto first=trimmed.find_first_not_of(" \t\r\n");
    const auto last=trimmed.find_last_not_of(" \t\r\n");
    trimmed=first==std::string::npos?std::string():trimmed.substr(first,last-first+1);
    if(trimmed=="exit"||trimmed=="quit") return 0;
    return run_script_source(source, fs::path("<command-line>"), "<command-line>", script_args,
                             fs::current_path(), interactive_after, target);
}


static int run_stdin_script(const std::vector<std::string>& script_args, bool interactive_after, const std::string& target = "native") {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    if (std::cin.bad()) { console::error("stdin: failed while reading program source"); return 1; }
    std::string source=buffer.str();
    if (source.find('\0') != std::string::npos) {
        console::error("stdin: program source contains a NUL byte");
        return 1;
    }
    return run_script_source(source, fs::path("<stdin>"), "<stdin>", script_args,
                             fs::current_path(), interactive_after, target);
}

static int run_eval(int argc, char** argv) {
    bool json_output=false, capabilities=false; std::string expression;
    for(int i=2;i<argc;++i){std::string a=argv[i];if(a=="--json")json_output=true;else if(a=="--capabilities")capabilities=true;else if(expression.empty())expression=a;else{std::cerr<<"eval: expected one expression\n";return 2;}}
    if(capabilities){if(!expression.empty()){std::cerr<<"eval: --capabilities does not take an expression\n";return 2;}std::cout<<"{\"command\":\"eval\",\"contract\":1,\"json_output\":true,\"project_context\":true,\"value_methods\":[\"keys\",\"values\",\"entries\",\"has\",\"get\",\"merge\",\"map\",\"filter\",\"find\",\"find_index\",\"sort_by\",\"unique\",\"flatten\",\"sum\",\"min\",\"max\",\"group_by\"]}\n";return 0;}
    if(expression.empty()){std::cerr<<"eval: expression required\n";return 2;}
    nift::RuntimeValue value;std::string error;
    int vm_exit=0;
    const auto vm_mode=nift::rt::vm_mode_from_environment();
#if defined(NIFT_CP19_EVAL_MUTANT_BYPASS) // test-only deliberate legacy misrouting
    const bool vm_handled=false;
#else
    const bool vm_handled=nift::rt::eval_expression_on_vm(expression,value,error,vm_exit);
#endif
    if(vm_handled) {
        if(vm_exit!=0){if(!error.empty())std::cerr<<"eval: "<<error<<'\n';return vm_exit;}
        if(vm_mode==nift::rt::VmMode::Classify||vm_mode==nift::rt::VmMode::ClassifyAll)return 0;
    } else {
        ScriptRenderHost host(fs::current_path());TrackedInfo info;Parser parser(host,info);
        if(!parser.eval_expression(expression,value,error)){std::cerr<<"eval: "<<error<<'\n';return 2;}
    }
    if(cli_contains_bytes(value)){std::cerr<<"eval: bytes values are not serializable; decode as UTF-8 first\n";return 2;}
    if(nift::runtime_contains_error(value)){std::cerr<<"eval: Error values are not serializable\n";return 2;}
    if(nift::runtime_contains_timer(value)){std::cerr<<"eval: timer values are not serializable\n";return 2;}
    if(json_output)std::cout<<value.dump()<<'\n';else if(value.is_string())std::cout<<value.string<<'\n';else std::cout<<value.dump()<<'\n';
    return 0;
}


struct ShellWord {
    std::string text;
    // One entry per character: 1 when the character was quoted or escaped and
    // must not act as a glob metacharacter or shell operator.
    std::vector<unsigned char> quoted;
    bool operator_token = false;
};

static std::vector<ShellWord> shell_words(const std::string& line) {
    std::vector<ShellWord> out;
    std::string cur;
    std::vector<unsigned char> mask;
    bool sq = false, dq = false, esc = false;
    auto flush = [&]() {
        if (!cur.empty()) {
            ShellWord word;
            word.text = cur;
            word.quoted = mask;
            out.push_back(std::move(word));
        }
        cur.clear();
        mask.clear();
    };
    auto push_operator = [&](const std::string& op) {
        flush();
        ShellWord word;
        word.text = op;
        word.quoted.assign(op.size(), 0);
        word.operator_token = true;
        out.push_back(std::move(word));
    };
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (esc) {
            cur += c;
            mask.push_back(1);
            esc = false;
            continue;
        }
        if (c == '\\' && !sq) { esc = true; continue; }
        if (c == '\'' && !dq) { sq = !sq; continue; }
        if (c == '"' && !sq) { dq = !dq; continue; }
        if (sq || dq) {
            cur += c;
            mask.push_back(1);
            continue;
        }

        // `2>`, `2>>` and `2>&1` are fd-redirection operators only when the
        // leading `2` was unquoted and is the whole current word.
        if (c == '>' && cur == "2" && !mask.empty() && mask.back() == 0) {
            if (i + 2 < line.size() && line.compare(i, 3, ">&1") == 0) {
                cur.clear();
                mask.clear();
                push_operator("2>&1");
                i += 2;
                continue;
            }
            if (i + 1 < line.size() && line[i + 1] == '>') {
                cur.clear();
                mask.clear();
                push_operator("2>>");
                i += 1;
                continue;
            }
            cur.clear();
            mask.clear();
            push_operator("2>");
            continue;
        }
        if (i + 1 < line.size()) {
            const std::string two = line.substr(i, 2);
            if (two == "&&" || two == "||" || two == ">>") {
                push_operator(two);
                i += 1;
                continue;
            }
        }
        if (c == '|' || c == '<' || c == '>' || c == '&' || c == ';') {
            push_operator(std::string(1, c));
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            flush();
            continue;
        }
        cur += c;
        mask.push_back(0);
    }
    flush();

#ifdef NIFT_SHELL_DISPATCH_MUTANT_QUOTING
    // Test-only negative control: discard quoting/escaping provenance and
    // treat every operator-shaped word as an operator, restoring C1's defect.
    for (auto& word : out) {
        std::fill(word.quoted.begin(), word.quoted.end(), 0);
        if (word.text == "&&" || word.text == "||" || word.text == ";" ||
            word.text == "|" || word.text == "<" || word.text == ">" ||
            word.text == ">>" || word.text == "2>" || word.text == "2>>" ||
            word.text == "2>&1" || word.text == "&") {
            word.operator_token = true;
        }
    }
#endif
    return out;
}

static std::vector<std::string> shell_texts(const std::vector<ShellWord>& words) {
    std::vector<std::string> texts;
    texts.reserve(words.size());
    for (const auto& word : words) texts.push_back(word.text);
    return texts;
}

static std::vector<bool> shell_operator_flags(const std::vector<ShellWord>& words) {
    std::vector<bool> flags;
    flags.reserve(words.size());
    for (const auto& word : words) flags.push_back(word.operator_token);
    return flags;
}

static std::string quote_nift_string(const std::string& x) {
    std::string r = "\"";
    for (char c : x) {
        if (c == '\\' || c == '\"') r += '\\';
        r += c;
    }
    return r + "\"";
}

static bool shell_glob_expand_word(const ShellWord& word, std::vector<std::string>& out) {
#ifndef _WIN32
    bool expand = false;
    for (std::size_t i = 0; i < word.text.size(); ++i) {
        const char c = word.text[i];
        const bool quoted = i < word.quoted.size() && word.quoted[i] != 0;
        if (!quoted && (c == '*' || c == '?' || c == '[')) {
            expand = true;
            break;
        }
    }
    if (!expand) {
        out.push_back(word.text);
        return true;
    }
    std::string pattern;
    pattern.reserve(word.text.size() * 2);
    for (std::size_t i = 0; i < word.text.size(); ++i) {
        const char c = word.text[i];
        const bool quoted = i < word.quoted.size() && word.quoted[i] != 0;
        if (quoted && (c == '*' || c == '?' || c == '[' || c == ']' || c == '\\')) {
            pattern += '\\';
        }
        pattern += c;
    }
    glob_t g{};
    const int rc = glob(pattern.c_str(), 0, nullptr, &g);
    if (rc == GLOB_NOMATCH) {
        out.push_back(word.text);
        return true;
    }
    if (rc != 0) {
        globfree(&g);
        return false;
    }
    for (std::size_t i = 0; i < g.gl_pathc; ++i) out.emplace_back(g.gl_pathv[i]);
    globfree(&g);
    return true;
#else
    out.push_back(word.text);
    return true;
#endif
}

// Interpolate $[expr] inside one command word using the live parser. The
// replacement inherits the quote provenance of the `$` that opened it.
static bool command_token_interpolate_word(Parser& parser, ShellWord& word, std::string& error,
                                            nift::rt::VmSession* session = nullptr) {
    if (word.quoted.size() != word.text.size()) word.quoted.resize(word.text.size(), 0);
    std::size_t i = 0;
    while ((i = word.text.find("$[", i)) != std::string::npos) {
        const std::size_t close = word.text.find(']', i + 2);
        if (close == std::string::npos) {
            error = "unterminated $[...] in command argument: " + word.text;
            return false;
        }
        const std::string expression = word.text.substr(i + 2, close - i - 2);
        nift::RuntimeValue value;
        std::string eval_error;
        bool evaluated = false;
        if (session) {
            std::string output;
            const auto result = session->evaluate(expression, "<interpolation>", output);
            if (!output.empty()) std::cout << output;
            if (!result.admitted) eval_error = "NIFT_VM session refused: " + result.refusal;
            else if (result.execution.reason != nift::rt::StopReason::None) eval_error = result.execution.message;
            else evaluated = nift::rt::eval_result_to_legacy(result.value, value, eval_error);
        } else evaluated = parser.eval_expression(expression, value, eval_error);
        if (!evaluated) {
            error = "command interpolation failed: $[" + expression + "]: " + eval_error;
            return false;
        }
        if (cli_contains_bytes(value)) {
            error = "command interpolation failed: bytes values must be decoded as UTF-8";
            return false;
        }
        if (nift::runtime_contains_error(value)) {
            error = "command interpolation failed: Error values cannot be rendered as text";
            return false;
        }
        if (nift::runtime_contains_timer(value)) {
            error = "command interpolation failed: timer values cannot be rendered as text";
            return false;
        }
        const bool quoted = i < word.quoted.size() && word.quoted[i] != 0;
        const std::string rendered = parser.render_expression_value(value);
        word.text.replace(i, close - i + 1, rendered);
        word.quoted.erase(word.quoted.begin() + static_cast<std::ptrdiff_t>(i),
                          word.quoted.begin() + static_cast<std::ptrdiff_t>(close + 1));
        word.quoted.insert(word.quoted.begin() + static_cast<std::ptrdiff_t>(i),
                           rendered.size(), quoted ? 1 : 0);
        i += rendered.size();
    }
    return true;
}
static void add_shell_words(std::set<std::string>& target, const std::string& words) {
    std::istringstream input(words);
    std::string word;
    while (input >> word) target.insert(word);
}

static nift::shell::ShellSnapshot make_shell_snapshot(const Parser& parser,
                                                     nift::rt::VmSession* session = nullptr) {
    nift::shell::ShellSnapshot snapshot;
    add_shell_words(snapshot.shell_builtins, "cd exit quit jobs fg bg wait");
    add_shell_words(snapshot.command_builtins, "print pwd");
    // Bare-token Nift expressions. Command names that D-042 requires to
    // resolve externally (ls, cp, mv, rm, cat, mkdir, touch, ...) are
    // deliberately absent; they take the PATH branch even without arguments.
    add_shell_words(snapshot.nift_expression_names,
        "true false null build cmd epoch err env os arch hardware_concurrency "
        "thread await mutex max min module_path page print warn pwd "
        "secure_random_bytes sha256 sha256_file sleep timer platform getenv "
        "setenv unsetenv exists file open open_bytes which run len range filter "
        "map sum keys values entries type string int float bool array object");
    if (session) for (const auto& name : session->namespace_names()) snapshot.user_functions.insert(name);
    else for (const auto& name : parser.shell_completions("")) snapshot.user_functions.insert(name);
    snapshot.executable_exists = [](const std::string& name) {
        std::string resolved;
        return nift_find_executable(name, resolved);
    };
#ifdef _WIN32
    snapshot.windows = true;
#endif
    return snapshot;
}

static bool shell_env_assignment_token(const std::string& token) {
    const std::size_t eq = token.find('=');
    if (eq == std::string::npos || eq == 0) return false;
    if (!(std::isalpha(static_cast<unsigned char>(token[0])) || token[0] == '_')) return false;
    for (std::size_t i = 0; i < eq; ++i) {
        const unsigned char c = static_cast<unsigned char>(token[i]);
        if (!(std::isalnum(c) || c == '_')) return false;
    }
    return true;
}

static std::size_t shell_name_index(const std::vector<std::string>& tokens) {
    std::size_t i = 0;
    while (i < tokens.size() && shell_env_assignment_token(tokens[i])) ++i;
    if (i < tokens.size() && tokens[i] == "command") ++i;
    return i;
}

static std::string shell_nift_call(const std::vector<std::string>& tokens, std::size_t name_index) {
    std::string expression = tokens[name_index] + "(";
    for (std::size_t i = name_index + 1; i < tokens.size(); ++i) {
        if (i > name_index + 1) expression += ",";
        expression += quote_nift_string(tokens[i]);
    }
    expression += ")";
    return expression;
}

template <class Table>
static int run_shell_job_builtin_impl(const std::string& builtin,
                                      const std::vector<std::string>& args,
                                      Table* table,
                                      bool print_errors) {
    if (builtin == "jobs") {
        if (!args.empty()) {
            if (print_errors) console::error("jobs: expected no arguments");
            return 2;
        }
        if (!table) {
            if (print_errors) console::error("jobs: no interactive job table");
            return 2;
        }
        for (const auto& job : table->jobs(true)) {
            const char* state = job.state == ShellJobState::Running ? "Running"
                              : (job.state == ShellJobState::Stopped ? "Stopped" : "Done");
            std::cout << "[" << job.id << "] " << state;
            if (job.state == ShellJobState::Done) std::cout << " (" << job.exit_code << ")";
            std::cout << "  " << job.command << '\n';
        }
        return 0;
    }

    if (!table) {
        if (print_errors) console::error(builtin + ": no interactive job table");
        return 2;
    }
    if (args.size() > 1) {
        if (print_errors) console::error(builtin + ": expected at most one job id");
        return 2;
    }
    int id = 0;
    if (args.size() == 1) {
        std::string raw = args[0];
        if (!raw.empty() && raw[0] == '%') raw.erase(raw.begin());
        try {
            id = std::stoi(raw);
        } catch (...) {
            if (print_errors) console::error(builtin + ": invalid job id");
            return 2;
        }
    }
    if (id == 0 && builtin != "wait") {
        auto list = table->jobs(false);
        if (list.empty()) {
            if (print_errors) console::error(builtin + ": no current job");
            return 1;
        }
        id = list.back().id;
    }
    if (builtin == "fg") {
        auto result = table->foreground(id);
        if (!result.error.empty()) {
            if (print_errors) console::error("fg: " + result.error);
            return 1;
        }
        return result.exit_code;
    }
    if (builtin == "bg") {
        std::string error;
        if (!table->background(id, error)) {
            if (print_errors) console::error("bg: " + error);
            return 1;
        }
        return 0;
    }
    auto result = id ? table->wait(id) : table->wait_all();
    if (!result.error.empty()) {
        if (print_errors) console::error("wait: " + result.error);
        return 1;
    }
    return result.exit_code;
}

static int run_shell_job_builtin(const std::string& builtin,
                                 const std::vector<std::string>& args,
                                 nift::shell::JobTable* jobs,
                                 ShellJobTable* legacy_jobs,
                                 bool interactive,
                                 bool print_errors) {
    if (interactive && legacy_jobs && legacy_jobs->supported())
        return run_shell_job_builtin_impl(builtin, args, legacy_jobs, print_errors);
    return run_shell_job_builtin_impl(builtin, args, jobs, print_errors);
}

static int dispatch_shell_state(Parser& parser,
                                const std::string& builtin,
                                const std::vector<std::string>& args,
                                nift::shell::JobTable* jobs,
                                ShellJobTable* legacy_jobs,
                                bool interactive,
                                bool print_errors,
                                bool& exit_requested,
                                int& exit_code, nift::rt::VmSession* session = nullptr) {
    if (builtin == "exit" || builtin == "quit") {
        if (args.size() > 1) {
            if (print_errors) console::error(builtin + ": expected at most one status");
            return 2;
        }
        exit_requested = true;
        exit_code = 0;
        if (args.size() == 1) {
            try {
                exit_code = std::stoi(args[0]);
            } catch (...) {
                if (print_errors) console::error(builtin + ": invalid exit status");
                exit_code = 2;
            }
        }
        return exit_code;
    }
    if (builtin == "cd") {
        std::string expression;
        if (args.empty()) {
            const char* home = std::getenv("HOME");
            if (!home || !*home) {
                if (print_errors) console::error("cd: HOME is not set");
                return 1;
            }
            expression = "cd(" + quote_nift_string(home) + ")";
        } else if (args.size() == 1) {
            expression = "cd(" + quote_nift_string(args[0]) + ")";
        } else {
            if (print_errors) console::error("cd: too many arguments");
            return 2;
        }
        if (session) return run_vm_session_statement(*session, expression, "<repl>");
    auto result = parser.run_statement(expression, "<repl>");
        if (!result.ok) {
            if (print_errors) console::error(result.error.message);
            return 1;
        }
        if (!result.output.empty()) std::cout << result.output << '\n';
        return 0;
    }
    return run_shell_job_builtin(builtin, args, jobs, legacy_jobs, interactive, print_errors);
}

static int run_nift_command(Parser& parser,
                            const std::vector<std::string>& tokens,
                            bool print_errors, nift::rt::VmSession* session = nullptr) {
    const std::size_t name_index = shell_name_index(tokens);
    if (name_index >= tokens.size()) {
        if (print_errors) console::error("nift: empty command");
        return 2;
    }
    const std::string expression = shell_nift_call(tokens, name_index);
    if (session) return run_vm_session_statement(*session, expression, "<repl>");
    auto result = parser.run_statement(expression, "<repl>");
    if (result.ok) {
        if (!result.output.empty()) std::cout << result.output << '\n';
        return 0;
    }
    if (print_errors) console::error(result.error.message);
    return 1;
}

static int run_external_words(Parser& parser,
                              const std::vector<ShellWord>& words,
                              const std::string& line,
                              bool print_errors,
                              nift::shell::JobTable* jobs,
                              ShellJobTable* legacy_jobs,
                              bool interactive) {
    (void)parser;
    if (std::getenv("NIFT_NO_PROCESS")) {
        if (print_errors) console::error("external process execution disabled");
        return 126;
    }
    bool has_ops = false;
    for (const auto& word : words) {
        if (!word.operator_token) continue;
        const std::string& t = word.text;
        if (t == "|" || t == "<" || t == ">" || t == ">>" || t == "2>" ||
            t == "2>>" || t == "2>&1" || t == "&") {
            has_ops = true;
        }
    }

    std::vector<ProcessSpec> stages(1);
    std::size_t stage_index = 0;
    bool expect_in = false, expect_out = false, expect_err = false;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const ShellWord& word = words[i];
        const std::string& token = word.text;
        if (word.operator_token) {
            if (token == "|") { stages.emplace_back(); ++stage_index; continue; }
            if (token == "<") { expect_in = true; continue; }
            if (token == ">" || token == ">>") {
                expect_out = true;
                stages[stage_index].append_stdout = token == ">>";
                continue;
            }
            if (token == "2>") { expect_err = true; continue; }
            if (token == "2>>") { expect_err = true; stages[stage_index].append_stderr = true; continue; }
            if (token == "2>&1") { stages[stage_index].merge_stderr = true; continue; }
            if (token == "&") {
                if (i + 1 != words.size()) {
                    if (print_errors) console::error("& must terminate a pipeline");
                    return 2;
                }
                continue;
            }
        }
        if (expect_in) { stages[stage_index].stdin_path = token; expect_in = false; continue; }
        if (expect_out) { stages[stage_index].stdout_path = token; expect_out = false; continue; }
        if (expect_err) { stages[stage_index].stderr_path = token; expect_err = false; continue; }
        if (stages[stage_index].program.empty()) {
            const std::size_t eq = token.find('=');
            if (!word.operator_token && eq != std::string::npos && eq > 0 &&
                shell_env_assignment_token(token)) {
                stages[stage_index].env[token.substr(0, eq)] = token.substr(eq + 1);
                continue;
            }
            stages[stage_index].program = token;
        } else {
            std::vector<std::string> expanded;
            if (!shell_glob_expand_word(word, expanded)) {
                if (print_errors) console::error("glob expansion failed: " + token);
                return 2;
            }
            stages[stage_index].args.insert(stages[stage_index].args.end(),
                                            expanded.begin(), expanded.end());
        }
    }

    const bool background = !words.empty() && words.back().operator_token && words.back().text == "&";
    if (background) {
        if (interactive ? !legacy_jobs : !jobs) {
            if (print_errors) console::error("background jobs require the interactive shell");
            return 2;
        }
    }
    for (const auto& stage : stages) {
        if (stage.program.empty()) {
            if (print_errors) console::error("empty command in pipeline");
            return 2;
        }
    }
    if (background) {
        ProcessResult result;
        if (interactive && legacy_jobs && legacy_jobs->supported()) {
            result = legacy_jobs->launch(stages, line, false);
        } else if (!interactive && jobs && jobs->supported()) {
            result = jobs->launch(stages, line, false);
        } else {
            if (print_errors) console::error("interactive job control is not supported on Windows");
            return 2;
        }
        int status = result.exit_code;
        if (!result.error.empty()) {
            if (print_errors) console::error(result.error);
            return status < 0 ? 2 : status;
        }
        auto list = (interactive && legacy_jobs) ? legacy_jobs->jobs(false) : jobs->jobs(false);
        if (!list.empty()) {
            const auto& job = list.back();
            std::cout << "[" << job.id << "] " << job.process_group << '\n';
        }
        return 0;
    }

    const bool direct = !has_ops && stages.size() == 1;
    ProcessResult result;
#if defined(NIFT_CP20_MUTANT) && NIFT_CP20_MUTANT == 10
    if (interactive && legacy_jobs) {
#else
    if (interactive && legacy_jobs && legacy_jobs->supported()) {
#endif
        // J1(a): the complete legacy foreground job-control path stays until
        // CP21 adds Host stop/continue. This preserves Ctrl-C/Ctrl-Z/fg/bg.
        result = legacy_jobs->launch(stages, line, true);
    } else if (jobs && jobs->supported()) {
        result = jobs->launch(stages, line, true);
    } else {
        if (direct) stages[0].foreground_terminal = true;
        result = nift::shell::run_pipeline(stages, line, direct);
    }
    if (!result.error.empty() && print_errors) console::error(result.error);
    return result.exit_code;
}
static int execute_shell_command(Parser& parser,
                                 const std::string& line,
                                 bool print_errors,
                                 nift::shell::JobTable* jobs,
                                 ShellJobTable* legacy_jobs,
                                 bool interactive,
                                 const nift::shell::ShellSnapshot* supplied_snapshot = nullptr,
                                 bool* exit_requested_out = nullptr,
                                 int* exit_code_out = nullptr,
                                 nift::rt::VmSession* session = nullptr) {
    auto words = shell_words(line);
    if (words.empty()) return 0;
    const auto texts = shell_texts(words);
    const auto operator_flags = shell_operator_flags(words);
    const nift::shell::ShellSnapshot snapshot =
        supplied_snapshot ? *supplied_snapshot : make_shell_snapshot(parser, session);
    nift::shell::LinePlan plan = nift::shell::classify_line(texts, operator_flags, snapshot);

#ifndef NIFT_SHELL_DISPATCH_MUTANT_SYNTAX
    // C3: validate the whole line before any interpolation or dispatch. This
    // includes segments that &&/|| control flow would otherwise skip.
    std::string syntax_error;
    if (!nift::shell::validate_line_syntax(texts, operator_flags, syntax_error)) {
        if (print_errors) console::error(syntax_error);
        return 2;
    }
#endif

#ifdef NIFT_SHELL_DISPATCH_MUTANT_EAGER_INTERPOLATION
    // Test-only negative control: restore eager interpolation of every
    // segment, including segments that control flow will skip.
    for (auto& word : words) {
        if (word.operator_token) continue;
        std::string error;
        if (!command_token_interpolate_word(parser, word, error, session)) {
            if (print_errors) console::error(error);
            return 2;
        }
    }
#endif

    const auto vm_mode = nift::rt::vm_mode_from_environment();
    if (session && (vm_mode == nift::rt::VmMode::Classify || vm_mode == nift::rt::VmMode::ClassifyAll)) return 0;
    bool exit_requested = false;
    int exit_code = 0;
    int last = 0;
    for (auto& segment : plan.segments) {
        const bool should = segment.connector.empty() ||
            (segment.connector == "&&" ? last == 0 :
             segment.connector == "||" ? last != 0 : true);
        if (!should) continue;

        std::vector<ShellWord> segment_words(
            words.begin() + static_cast<std::ptrdiff_t>(segment.begin),
            words.begin() + static_cast<std::ptrdiff_t>(segment.end));

        // The explicit `command` escape is removed before interpolation.
        // `segment.first.command_prefix` came from the raw classification, so
        // interpolation cannot create or remove this escape.
        if (segment.first.command_prefix) {
            std::size_t index = 0;
            while (index < segment_words.size() && !segment_words[index].operator_token &&
                   shell_env_assignment_token(segment_words[index].text)) {
                ++index;
            }
            if (index < segment_words.size() && !segment_words[index].operator_token &&
                segment_words[index].text == "command") {
                segment_words.erase(segment_words.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }

#ifndef NIFT_SHELL_DISPATCH_MUTANT_EAGER_INTERPOLATION
        bool interpolation_failed = false;
        for (auto& word : segment_words) {
            if (word.operator_token) continue;
            std::string error;
            if (!command_token_interpolate_word(parser, word, error, session)) {
                if (print_errors) console::error(error);
                interpolation_failed = true;
                last = 2;
                break;
            }
        }
        if (interpolation_failed) continue;
#endif

        const std::vector<std::string> segment_texts = shell_texts(segment_words);

#ifdef NIFT_SHELL_DISPATCH_MUTANT
        // Test-only negative control: restore the old "try a command-style Nift
        // callable first, then external" behaviour.
        if (!(segment.has_pipe || segment.has_redirection || segment.background) &&
            segment.first.kind != nift::shell::CommandKind::ShellBuiltin) {
            const std::size_t mutant_name_index = shell_name_index(segment_texts);
            if (mutant_name_index < segment_texts.size()) {
                auto mutant_result = parser.run_statement(
                    shell_nift_call(segment_texts, mutant_name_index), "<repl>");
                if (mutant_result.ok) {
                    if (!mutant_result.output.empty()) std::cout << mutant_result.output << '\n';
                    last = 0;
                    continue;
                }
            }
            last = run_external_words(parser, segment_words, line, print_errors, jobs, legacy_jobs, interactive);
            continue;
        }
#endif

        if (segment.has_pipe || segment.has_redirection || segment.background) {
            if (segment.first.kind == nift::shell::CommandKind::NotFound) {
                if (print_errors) console::error("nift: " + segment.first.name + ": command not found");
                last = 127;
                continue;
            }
            if (segment.first.kind != nift::shell::CommandKind::External) {
                if (print_errors) {
                    console::error("nift: " + segment.first.name + ": " +
                                   nift::shell::command_kind_name(segment.first.kind) +
                                   " is not supported in a pipeline or redirection");
                }
                last = 2;
                continue;
            }
            last = run_external_words(parser, segment_words, line, print_errors, jobs, legacy_jobs, interactive);
            continue;
        }

        switch (segment.first.kind) {
            case nift::shell::CommandKind::ShellBuiltin: {
                const std::size_t name_index = shell_name_index(segment_texts);
                std::vector<std::string> args;
                if (name_index + 1 < segment_texts.size()) {
                    args.assign(segment_texts.begin() + static_cast<std::ptrdiff_t>(name_index + 1),
                                segment_texts.end());
                }
                last = dispatch_shell_state(parser, segment.first.name, args, jobs, legacy_jobs, interactive, print_errors,
                                            exit_requested, exit_code, session);
                break;
            }
            case nift::shell::CommandKind::UserFunction:
            case nift::shell::CommandKind::NiftBuiltin:
                last = run_nift_command(parser, segment_texts, print_errors, session);
                break;
            case nift::shell::CommandKind::External:
                last = run_external_words(parser, segment_words, line, print_errors, jobs, legacy_jobs, interactive);
                break;
            case nift::shell::CommandKind::NotFound:
                if (print_errors) console::error("nift: " + segment.first.name + ": command not found");
                last = 127;
                break;
            case nift::shell::CommandKind::NiftStatement:
                if (print_errors) console::error("nift: Nift statement is not valid in a command chain");
                last = 2;
                break;
        }
        if (exit_requested) break;
    }
    if (exit_requested_out) *exit_requested_out = exit_requested;
    if (exit_code_out) *exit_code_out = exit_code;
    return exit_requested ? exit_code : last;
}


static fs::path nift_history_path(){
    if(const char* home=std::getenv("HOME")) return fs::path(home)/".nift_history";
#ifdef _WIN32
    if(const char* home=std::getenv("USERPROFILE")) return fs::path(home)/".nift_history";
#endif
    return fs::path(".nift_history");
}
static std::vector<std::string> load_nift_history(){
    std::vector<std::string> h;std::ifstream in(nift_history_path());std::string line;while(std::getline(in,line))if(!line.empty())h.push_back(line);if(h.size()>1000)h.erase(h.begin(),h.end()-1000);return h;
}
static void append_nift_history(const std::string& line){if(line.empty())return;std::ofstream out(nift_history_path(),std::ios::app);if(out)out<<line<<'\n';}
static const std::vector<std::string>& nift_completion_names(){
    static const std::vector<std::string> names={"build","cat","cd","cmd","copy","cp","epoch","err","exists","file","getenv","env","os","arch","hardware_concurrency","thread","await","mutex","atomic<int>","atomic<bool>","jobs","fg","bg","wait","ls","make_dir","max","min","mkdir","module_path","move","mv","open","open_bytes","package_path","page","print","warn","pwd","remove","rm","run","secure_random_bytes","setenv","sha256","sha256_file","sleep","timer","platform","touch","unsetenv","which"};
    return names;
}
#ifndef _WIN32  // used only by the POSIX line editor below (MinGW -Werror: unused-function)
static bool shell_identifier_name(const std::string& name){
    if(name.empty())return false;
    if(!(std::isalpha(static_cast<unsigned char>(name[0]))||name[0]=='_'))return false;
    for(char c:name)if(!(std::isalnum(static_cast<unsigned char>(c))||c=='_'))return false;
    return true;
}
static bool nift_call_form_completion_name(const std::string& candidate){
    if(!shell_identifier_name(candidate))return false;
    if(candidate=="print"||candidate=="pwd"||candidate=="cd"||candidate=="exit"||candidate=="quit"||
       candidate=="jobs"||candidate=="fg"||candidate=="bg"||candidate=="wait")return false;
    const auto& names=nift_completion_names();
    return std::find(names.begin(),names.end(),candidate)!=names.end();
}
#endif
static std::vector<std::string> nift_shell_completions(const std::string& prefix){
    std::set<std::string> out;for(const auto& b:nift_completion_names())if(b.rfind(prefix,0)==0)out.insert(b);
    if(const char* path=std::getenv("PATH")){std::stringstream ss(path);std::string dir;while(std::getline(ss,dir,':')){std::error_code ec;for(auto it=fs::directory_iterator(dir,ec);!ec&&it!=fs::directory_iterator();it.increment(ec)){auto n=it->path().filename().string();if(n.rfind(prefix,0)==0)out.insert(n);}}}
    fs::path pp=prefix.empty()?fs::path("."):fs::path(prefix);fs::path parent=pp.has_parent_path()?pp.parent_path():fs::path(".");std::string leaf=pp.filename().string();std::error_code ec;for(auto it=fs::directory_iterator(parent,ec);!ec&&it!=fs::directory_iterator();it.increment(ec)){auto n=it->path().filename().string();if(n.rfind(leaf,0)==0){auto c=(pp.has_parent_path()?parent/fs::path(n):fs::path(n)).generic_string();if(it->is_directory(ec))c+="/";out.insert(c);}}
    return {out.begin(),out.end()};
}
// The prompt is portable (path rendering + console colour) and used by the
// shell on every platform, so it lives outside the POSIX-only helper block.
namespace {
std::string shell_prompt_text(bool continuation) {
    if (continuation) return "... ";
    std::string shown = fs::current_path().generic_string();
    if (const char* home = std::getenv("HOME")) {
        std::string h = fs::path(home).lexically_normal().generic_string();
        if (shown == h) shown = "~";
        else if (!h.empty() && shown.rfind(h + "/", 0) == 0) shown = "~" + shown.substr(h.size());
    }
    return console::paint(shown, "1;32", console::stdout_colour_enabled()) + "$ ";
}
} // namespace
#ifndef _WIN32
namespace {
std::string quote_for_shell_arg(const std::string& s) {
    if (s.find_first_of(" \t\"'$`\\") == std::string::npos) return s;
    std::string r = "'";
    for (char c : s) { if (c == '\'') r += "'\\''"; else r += c; }
    return r + "'";
}
// Union of the static shell completions (builtins/PATH/files) and the live
// parser context (user/imported callables, structs, variable bindings).
std::vector<std::string> full_shell_completions(const Parser& parser, const std::string& prefix) {
    std::set<std::string> out;
    for (auto& c : nift_shell_completions(prefix)) out.insert(c);
    for (auto& c : parser.shell_completions(prefix)) out.insert(c);
    return {out.begin(), out.end()};
}
// Command-style candidates only (builtins + PATH executables + parser names),
// used for the leading token so plain cwd files do not crowd command TABs.
std::vector<std::string> command_completions(const Parser& parser, const std::string& prefix) {
    std::set<std::string> out;
    std::set<std::string> command_words;
    add_shell_words(command_words, "cd exit quit jobs fg bg wait print pwd");
    std::set<std::string> shell_state;
    add_shell_words(shell_state, "cd exit quit jobs fg bg wait");
    // N1: Nift built-ins outside the command allow-list are offered in call
    // form only (open(, file(, exists(, ...), never as bare command words.
    // Parser-visible names that are also built-ins are therefore suppressed
    // in bare form and offered only through the call-form candidate.
    std::set<std::string> call_form_only;
    for (const auto& name : nift_completion_names()) {
        if (!nift_call_form_completion_name(name)) continue;
        call_form_only.insert(name);
        const std::string candidate = name + "(";
        if (candidate.rfind(prefix, 0) == 0) out.insert(candidate);
    }
    for (auto& c : parser.shell_completions(prefix)) {
        if (!call_form_only.count(c)) out.insert(c);
    }
    for (const auto& b : command_words)
        if (b.rfind(prefix, 0) == 0) out.insert(b);
    if (const char* path = std::getenv("PATH")) {
        std::stringstream ss(path); std::string dir;
        while (std::getline(ss, dir, ':')) { std::error_code ec;
            for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) { auto n = it->path().filename().string(); if (n.rfind(prefix, 0) == 0) out.insert(n); } }
    }
    return {out.begin(), out.end()};
}
void complete_token(Parser& parser, std::string& buf, size_t& cursor) {
    if (cursor == 0) return;
    size_t start = cursor;
    while (start > 0 && buf[start-1] != ' ' && buf[start-1] != '\t' && buf[start-1] != '(' && buf[start-1] != ',') --start;
    const bool leading = (start == 0);
    std::string prefix = buf.substr(start, cursor - start);
    // Strip a leading quote from the token prefix.
    size_t tok = start;
    if (!prefix.empty() && (prefix[0] == '"' || prefix[0] == '\'')) { tok = start + 1; prefix = buf.substr(tok, cursor - tok); }
    const bool pathish = prefix.find('/') != std::string::npos || prefix.rfind("./", 0) == 0 || prefix.rfind("~/", 0) == 0 || prefix.rfind(".", 0) == 0;
    std::vector<std::string> cands;
    if (leading && !pathish) {
        // D-042: command position advertises only shell-state commands,
        // print/pwd, parser-visible user names and PATH executables. Nift
        // expression names remain available through `nift complete` and
        // argument/file completion, but are not offered as commands.
        cands = command_completions(parser, prefix);
    } else {
        cands = full_shell_completions(parser, prefix);
    }
    if (cands.empty()) return;
    std::sort(cands.begin(), cands.end());
    cands.erase(std::unique(cands.begin(), cands.end()), cands.end());
    if (cands.size() == 1) {
        std::string ins = quote_for_shell_arg(cands[0]);
        buf.replace(tok, cursor - tok, ins);
        cursor = tok + ins.size();
        return;
    }
    std::string common = cands[0];
    for (size_t i = 1; i < cands.size(); ++i) { size_t k = 0; while (k < common.size() && k < cands[i].size() && common[k] == cands[i][k]) ++k; common.resize(k); }
    const bool all_call_form = std::all_of(cands.begin(), cands.end(), [](const std::string& c) {
        return !c.empty() && c.back() == '(';
    });
    const std::string call = common + "(";
    const bool call_candidate = std::find(cands.begin(), cands.end(), call) != cands.end();
    const bool only_bare_or_call = std::all_of(cands.begin(), cands.end(), [&](const std::string& c) {
        return c == common || (!c.empty() && c.back() == '(');
    });
    if (call_candidate && nift_call_form_completion_name(common) && common.size() >= prefix.size()) {
        // A built-in call-form candidate wins over bare command words and
        // unrelated PATH names sharing the same prefix (ope -> open(, even
        // when a PATH open/opencode also exists).
        buf.replace(tok, cursor - tok, call);
        cursor = tok + call.size();
        return;
    }
    if (call_candidate && only_bare_or_call && common.size() >= prefix.size()) {
        // Prefer the call form when a bare command word and a built-in call
        // share the same prefix (e.g. a PATH `open` plus `open(`).
        buf.replace(tok, cursor - tok, call);
        cursor = tok + call.size();
        return;
    }
    if (all_call_form && common.size() >= prefix.size() && (common.empty() || common.back() != '(')) {
        // Multiple call-form built-ins share a bare prefix (open( and
        // open_bytes()). Complete to the shortest call form rather than
        // inserting a bare built-in command word.
        if (std::find(cands.begin(), cands.end(), call) != cands.end()) {
            buf.replace(tok, cursor - tok, call);
            cursor = tok + call.size();
            return;
        }
        std::cout << '\n';
        for (auto& c : cands) std::cout << c << ' ';
        std::cout << '\n';
        return;
    }
    if (common.size() > prefix.size()) { buf.replace(tok, cursor - tok, common); cursor = tok + common.size(); }
    else { std::cout << '\n'; for (auto& c : cands) std::cout << c << ' '; std::cout << '\n'; }
}
enum class ShellRead { Line, Eof, Interrupt };
ShellRead interactive_read_line(Parser& parser, const std::string& prompt, std::vector<std::string>& history, size_t& hist_pos, std::string& out) {
    if (!isatty(STDIN_FILENO)) { std::cout << prompt << std::flush; if (!std::getline(std::cin, out)) return ShellRead::Eof; return ShellRead::Line; }
    termios old{}; tcgetattr(STDIN_FILENO, &old);
    termios raw = old; raw.c_lflag &= ~(ICANON | ECHO | ISIG); raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    std::string buf; size_t cursor = 0;
    auto redraw = [&] {
        std::cout << "\r\x1b[2K" << prompt << buf;
        size_t right = buf.size() - cursor; if (right) std::cout << "\x1b[" << right << "D";
        std::cout.flush();
    };
    redraw();
    ShellRead result = ShellRead::Line;
    while (true) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) { result = ShellRead::Eof; break; }
        if (c == '\n' || c == '\r') { std::cout << '\n'; break; }
        if (c == 0x7f || c == 0x08) { if (cursor > 0) { buf.erase(cursor - 1, 1); --cursor; redraw(); } continue; }
        if (c == '\t') { complete_token(parser, buf, cursor); redraw(); continue; }
        if (c == 0x03) { std::cout << '\n'; result = ShellRead::Interrupt; break; }
        if (c == 0x04) { if (buf.empty()) { result = ShellRead::Eof; break; } continue; }
        if (c == 0x1b) { char seq[2]; if (read(STDIN_FILENO, seq, 2) != 2) continue;
            if (seq[0] == '[' && seq[1] == 'A') { if (hist_pos > 0) { --hist_pos; buf = history[hist_pos]; cursor = buf.size(); redraw(); } continue; }
            if (seq[0] == '[' && seq[1] == 'B') { if (hist_pos < history.size()) { ++hist_pos; buf = (hist_pos == history.size()) ? std::string{} : history[hist_pos]; cursor = buf.size(); redraw(); } continue; }
            if (seq[0] == '[' && seq[1] == 'C') { if (cursor < buf.size()) { ++cursor; redraw(); } continue; }
            if (seq[0] == '[' && seq[1] == 'D') { if (cursor > 0) { --cursor; redraw(); } continue; }
            continue;
        }
        if (c >= 32) { buf.insert(cursor, 1, c); ++cursor; redraw(); }
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    out = buf;
    return result;
}
}
#endif
static int run_script_shell_loop(Parser& parser, bool load_rc, nift::rt::VmSession* session) {
    std::unique_ptr<nift::rt::VmSession> owned_session;
    if (!session && nift::rt::vm_mode_from_environment() != nift::rt::VmMode::Off) {
        owned_session = std::make_unique<nift::rt::VmSession>(session_options()); session = owned_session.get();
    }
    std::string pending; nift::shell::JobTable jobs; ShellJobTable legacy_jobs;
    auto history=load_nift_history();
#ifndef _WIN32
    size_t hist_pos=history.size();
#endif
#ifndef _WIN32
    const bool interactive = isatty(STDIN_FILENO);
#endif
    if (load_rc) if (const char* home = std::getenv("HOME")) {
        fs::path rc = fs::path(home) / ".niftrc";
        if (filesystem::file_exists(rc)) {
            if (session) {
                if (run_vm_session_statement(*session, filesystem::read_file(rc), rc.generic_string(), false, SessionDisplay::Rc)) return 1;
            } else { auto rr = parser.run_statement(filesystem::read_file(rc), rc);
                if (!rr.ok) { console::error("niftrc: " + rr.error.message); return 1; } }
        }
    }
    while(true){const std::string prompt=shell_prompt_text(!pending.empty());
#ifndef _WIN32
        std::string line;ShellRead r;
        if(interactive){r=interactive_read_line(parser,prompt,history,hist_pos,line);}
        else {r=std::getline(std::cin,line)?ShellRead::Line:ShellRead::Eof;}
        if(r==ShellRead::Eof)break;
        if(r==ShellRead::Interrupt){pending.clear();continue;}
#else
        std::string line;if(_isatty(_fileno(stdin)))std::cout<<prompt<<std::flush;if(!std::getline(std::cin,line))break;
#endif
        if(pending.empty()&&(line=="exit"||line=="quit"))break;
        if(pending.empty()&&!line.empty()){append_nift_history(line);if(history.size()>=1000)history.erase(history.begin());history.push_back(line);
#ifndef _WIN32
            hist_pos=history.size();
#endif
        }
        pending+=line+"\n";
        // CP85: the parser reports whether the accumulated input is complete,
        // an incomplete prefix (keep reading), or invalid (balanced but
        // malformed, executed so the canonical diagnostic is shown).
        const std::string trimmed=pending.substr(0,pending.find_last_not_of("\r\n")+1);
        auto shell_line_words=shell_words(trimmed);
        auto toks=shell_texts(shell_line_words);
        const auto snapshot=make_shell_snapshot(parser, session);
        if(!nift::shell::is_nift_statement_line(toks,trimmed,snapshot)){
            bool exit_requested=false; int exit_code=0;
#ifndef _WIN32
            const bool forced_scripted = std::getenv("NIFT_CP20_FORCE_SCRIPTED") != nullptr;
            const bool interactive_shell = interactive && !forced_scripted;
#else
            const bool forced_interactive = std::getenv("NIFT_CP20_FORCE_INTERACTIVE") != nullptr;
            const bool interactive_shell = _isatty(_fileno(stdin)) != 0 || forced_interactive;
#endif
            const auto status = execute_shell_command(parser,trimmed,true,&jobs,&legacy_jobs,interactive_shell,
                                                      &snapshot,&exit_requested,&exit_code,session);
            if (status == 70) return status;
            pending.clear();
            if(exit_requested) return exit_code;
            continue;
        }
        const Parser::StatementState st=parser.statement_state(pending);
        if(st==Parser::StatementState::Incomplete)continue;
        if (session) {
            const auto status = run_vm_session_statement(*session, pending, "<repl>");
            pending.clear(); if (status == 70) return status; continue;
        }
        auto rr=parser.run_statement(pending,"<repl>");pending.clear();
        if(!rr.ok){ console::error(rr.error.message);continue; }
        if(!rr.output.empty())std::cout<<rr.output<<'\n';
    }
    if(jobs.has_live_jobs()) jobs.wait_all();
    if(legacy_jobs.has_live_jobs()) legacy_jobs.wait_all();
    std::string resource_error;
    if(!parser.finalize_script_resources(resource_error)){console::error(resource_error);return 1;}
    return 0;
}

static int run_script_shell() {
    ScriptRenderHost host(fs::current_path()); TrackedInfo info; Parser parser(host,info);
    parser.set_script_invocation("<repl>", {});
    return run_script_shell_loop(parser, /*load_rc=*/true);
}

int run_cli(int argc, char** argv) {
    // --platform is a global execution/build selector and may appear before or
    // after the command/script.  `--` is a hard boundary: anything after it is
    // passed through literally and is never interpreted as a Nift option.
    std::string selected_platform = "native";
    bool platform_selected = false;
    std::vector<std::string> normalized_storage;
    normalized_storage.reserve(static_cast<std::size_t>(argc));
    normalized_storage.emplace_back(argc > 0 && argv[0] ? argv[0] : "nift");
    bool literal_tail = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (!literal_tail && arg == "--") {
            literal_tail = true;
            normalized_storage.push_back(arg);
            continue;
        }
        if (!literal_tail && arg.rfind("--platform=", 0) == 0) {
            std::string parsed;
            if (!parse_runtime_platform_selector(arg, parsed)) {
                console::error("invalid platform selector '" + arg + "'");
                return 2;
            }
            if (platform_selected) {
                console::error("multiple platform selections are not allowed");
                return 2;
            }
            selected_platform = std::move(parsed);
            platform_selected = true;
            continue;
        }
        normalized_storage.push_back(arg);
    }
    std::vector<char*> normalized_argv;
    normalized_argv.reserve(normalized_storage.size());
    for (auto& arg : normalized_storage) normalized_argv.push_back(arg.data());
    argc = static_cast<int>(normalized_argv.size());
    argv = normalized_argv.data();

    const std::string command = argc > 1 ? argv[1] : "";
    const bool platform_capable_command = command.empty() || command == "build" ||
        command == "-e" || command == "-c" || command == "-i" || command == "-" ||
        command == "--no-process" || command.rfind("--fs-root=", 0) == 0 ||
        (filesystem::file_exists(command) && !fs::is_directory(command));
    if (platform_selected && !platform_capable_command) {
        console::error("--platform is only valid for script execution, the REPL, and build");
        return 2;
    }
    if (command.empty()) {
        if (!platform_selected) return run_script_shell();
        ScriptRenderHost host(fs::current_path(), selected_platform);
        TrackedInfo info; Parser parser(host, info);
        parser.set_script_invocation("<repl>", {});
        return run_script_shell_loop(parser, true);
    }

    // Full-program command-line execution. -e is canonical; -c is an exact
    // alias. -i may prefix either an inline program or a script path and keeps
    // the same Parser instance alive after successful initial execution.
    if (command == "-e" || command == "-c" || command == "-i") {
        bool interactive = command == "-i";
        int index = 2;
        std::string mode = command;
        if (interactive) {
            if (index >= argc) return run_script_shell();
            mode = argv[index++];
        }
        if (mode == "-e" || mode == "-c") {
            if (index >= argc) { console::error(mode + " requires program source"); return 2; }
            const std::string source = argv[index++];
            std::vector<std::string> script_args;
            bool literal_args=false;
            for (; index < argc; ++index) {
                const std::string a=argv[index];
                if (!literal_args && a=="--") { literal_args=true; continue; }
                if (!literal_args && a=="-i") { interactive=true; continue; }
                script_args.push_back(a);
            }
            return run_inline_script(source, script_args, interactive, selected_platform);
        }
        if (mode == "-") {
            std::vector<std::string> script_args;
            bool literal_args=false;
            for (; index < argc; ++index) {
                const std::string a=argv[index];
                if (!literal_args && a=="--") { literal_args=true; continue; }
                script_args.push_back(a);
            }
            return run_stdin_script(script_args, /*interactive_after=*/true, selected_platform);
        }
        if (filesystem::file_exists(mode) && !fs::is_directory(mode)) {
            std::vector<std::string> script_args;
            bool literal_args=false;
            for (; index < argc; ++index) {
                const std::string a=argv[index];
                if (!literal_args && a=="--") { literal_args=true; continue; }
                if (!literal_args && a=="--no-process") nift_setenv("NIFT_NO_PROCESS","1",1);
                else if (!literal_args && a.rfind("--fs-root=",0)==0) nift_setenv("NIFT_FS_ROOT",a.substr(10).c_str(),1);
                else script_args.push_back(a);
            }
            return run_script_file(mode, script_args, /*interactive_after=*/true, selected_platform);
        }
        console::error("-i requires -e, -c, or an existing script path");
        return 2;
    }
    if (command == "-") {
        std::vector<std::string> script_args;
        bool literal_args=false;
        for (int i=2; i<argc; ++i) {
            const std::string a=argv[i];
            if (!literal_args && a=="--") { literal_args=true; continue; }
            script_args.push_back(a);
        }
        return run_stdin_script(script_args, /*interactive_after=*/false, selected_platform);
    }

    // Zero-source shell capability options replace the old `nift sh <option>`
    // spelling. General source/interactive option parsing is completed later
    // in the v4.5 invocation checkpoints.
    if (command == "--no-process" || command.rfind("--fs-root=", 0) == 0) {
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--no-process") nift_setenv("NIFT_NO_PROCESS", "1", 1);
            else if (a.rfind("--fs-root=", 0) == 0) nift_setenv("NIFT_FS_ROOT", a.substr(10).c_str(), 1);
            else { console::error("unknown shell option '" + a + "'"); return 1; }
        }
        return run_script_shell();
    }

    if (command == "complete") {
        const std::string prefix = argc > 2 ? argv[2] : "";
        for (const auto& item : nift_shell_completions(prefix)) std::cout << item << '\n';
        return 0;
    }
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
    if (command == "init") {
        InitOptions options;
        if (!parse_init_options(argc, argv, options)) return 1;
        return initialise_project(options) ? 0 : 1;
    }
    if (command == "init-html") {
        console::error("command 'init-html' has been removed");
        std::cerr << "  use 'nift init' instead\n";
        return 1;
    }

    if (command == "add") return package_add_cli(argc,argv);
    if (command == "remove") return package_remove_cli(argc,argv);
    if (command == "install") return package_install_cli(false);
    if (command == "update") { if(argc>3){console::error("update takes at most one package name");return 1;} return package_install_cli(true,argc==3?argv[2]:std::string{}); }
    if (command == "packages") return package_query_cli(argc,argv);

    if (command == "eval") {
        for (int i = 2; i < argc; ++i) if (std::string(argv[i]) == "--no-process") nift_setenv("NIFT_NO_PROCESS", "1", 1);
        return run_eval(argc, argv);
    }


    if (command == "serve") return run_serve(argc, argv);

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

    // Native script execution: `nift script.f`, `nift ./script.f` and
    // `nift path/to/script.f` execute the file directly. This is
    // what makes `#!/usr/bin/env nift` work. Only applied when the argument
    // genuinely resolves to an existing file, so a typoed command name is not
    // silently turned into an arbitrary path.
    if (filesystem::file_exists(command) && !fs::is_directory(command)) {
        std::vector<std::string> script_args; bool literal_args=false;
        for (int i = 2; i < argc; ++i) {
            const std::string a = argv[i];
            if (!literal_args && a == "--") { literal_args=true; continue; }
            if (!literal_args && a == "--no-process") nift_setenv("NIFT_NO_PROCESS","1",1);
            else if (!literal_args && a.rfind("--fs-root=", 0) == 0) nift_setenv("NIFT_FS_ROOT", a.substr(10).c_str(), 1);
            else script_args.push_back(a);
        }
        return run_script_file(command, script_args, false, selected_platform);
    }

    // Historical spellings removed by the CLI unification. These return an
    // error that points at the replacement; they perform no action and are
    // intentionally not advertised by `nift commands` (same precedent as the
    // earlier init-html removal).
    static const std::pair<const char*, const char*> removed_command_hints[] = {
        {"build-all", "use 'nift build --all' instead"},
        {"build-updated", "use 'nift build' instead"},
        {"build-names", "use 'nift build <names...>' instead"},
        {"build-auto", "use 'nift build --auto' instead"},
        {"info-all", "use 'nift info --all' instead"},
        {"info-watching", "use 'nift info --watching' instead"},
        {"info-tracking", "use 'nift info --tracking' instead"},
        {"info-names", "use 'nift info --names' instead"},
    };
    for (const auto& [removed, hint] : removed_command_hints) {
        if (command == removed) {
            console::error("command '" + std::string(removed) + "' has been removed; " + hint);
            return 1;
        }
    }

    const std::set<std::string> project_commands = {
        "build", "info", "status",
        "track", "untrack", "rm", "del", "cp", "copy", "mv", "move",
        "watch", "unwatch"
    };
    if (!project_commands.count(command)) {
        console::error("unknown command '" + command + "' and path does not exist");
        std::cerr << "  run 'nift commands' to list available commands\n";
        return 1;
    }

    const bool timed_command = command == "build";
    CommandTimer command_timer(timed_command);

    ProjectInfo project;
    if (!project.open()) return 1;

    if (command == "build") {
        // Modes are mutually exclusive: positional names, --all, --auto,
        // --repair. -p (explain rebuild reasons) is orthogonal and combinable.
        // --no-process extends the script process restriction to build hooks.
        bool all_mode = false, auto_mode = false, repair_mode = false, explain = false, no_process = false;
        // feature-site-dx: --strict and --no-warn=<kind> are orthogonal to
        // the build mode and add to the project's config settings.
        bool strict_values = false;
        std::set<std::string> no_warn;
        std::vector<std::string> names;
        int mode_flags = 0;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--all") { all_mode = true; ++mode_flags; }
            else if (arg == "--auto") { auto_mode = true; ++mode_flags; }
            else if (arg == "--repair") { repair_mode = true; ++mode_flags; }
            else if (arg == "-p") explain = true;
            else if (arg == "--no-process") no_process = true;
            else if (arg == "--strict") strict_values = true;
            else if (arg.rfind("--no-warn=", 0) == 0) {
                const std::string kind = arg.substr(10);
                const auto& kinds = site_warning_kinds();
                if (kind != "all" && std::find(kinds.begin(), kinds.end(), kind) == kinds.end()) {
                    console::error("unknown warning kind '" + kind + "' (expected all, unknown-directive, path-outside-output or unknown-value)");
                    return 1;
                }
                no_warn.insert(kind);
            }
            else if (!arg.empty() && arg[0] == '-') { console::error("unknown build option '" + arg + "'"); return 1; }
            else names.emplace_back(arg);
        }
        if (no_process) nift_setenv("NIFT_NO_PROCESS", "1", 1);
        auto apply_checks = [&](ProjectInfo& target) {
            if (strict_values) target.config.strict_values = true;
            target.config.no_warn.insert(no_warn.begin(), no_warn.end());
        };
        apply_checks(project);
        project.target_ = selected_platform;
        if (mode_flags > 1 || (mode_flags == 1 && !names.empty())) {
            console::error("build modes are mutually exclusive");
            return 1;
        }

        if (all_mode) return project.build_all(true, explain);
        if (repair_mode) {
            // CP2: repair is the only mode allowed to take over a stale marker
            // (no live owner). It refuses on a live build like everything else.
            // The CP1 forced-rebuild reconstruction path is retained; the full
            // distrust/sweep semantics are completed by the CP4 campaign.
            return project.build_all(true, explain, /*repair=*/true);
        }
        if (auto_mode) {
            BuildAutoQuitKey quit_key;
            std::cout << console::heading("Nift build --auto") << '\n';
            std::cout << console::dim("watching for changes every 200 ms") << '\n';
            std::cout << "build output: " << console::path(build_auto_log_path)
                      << console::dim(" (errors and warnings are also printed here)") << '\n';
            if (quit_key.interactive())
                std::cout << "press " << console::good("q") << " to stop\n";
            else
                std::cout << console::dim("non-interactive mode; stop the process to exit") << '\n';
            std::cout << std::flush;

            // feature-site-dx: a failed build no longer ends the watcher. The
            // failure is printed to stderr (once per distinct set of
            // diagnostics) as well as written to the log.
            //
            // Retry policy: BuildAutoRetryPolicy (above).
            // feature-site-dx: SIGINT/SIGTERM stop the watcher cleanly: a build
            // in progress finishes (so no interrupted-build state is left),
            // then it prints "build --auto stopped" and exits 0.
            build_auto_stop_signal = 0;
            const auto previous_int = std::signal(SIGINT, build_auto_on_signal);
            const auto previous_term = std::signal(SIGTERM, build_auto_on_signal);
            BuildAutoRetryPolicy retry_policy(project.root, project.config.output_dir);
            std::string last_reported;
            bool stop_requested = false;
            auto diagnostic_key = [](const std::string& text) {
                std::string key; std::istringstream in(text); std::string line;
                while (std::getline(in, line))
                    if (line.rfind("error:", 0) == 0 || line.rfind("warning:", 0) == 0 ||
                        (line.rfind("  ", 0) == 0 && line.rfind("    \xE2\x86\xB3", 0) != 0)) key += line + '\n';
                return key;
            };
            while (!stop_requested) {
                stop_requested = quit_key.pressed() || build_auto_stop_signal != 0;
                if (stop_requested) break;

                if (retry_policy.should_attempt()) {
                    const bool was_failing = retry_policy.failing();
                    retry_policy.before_attempt();
                    std::string captured_output;
                    int result = 0;
                    {
                        console::ScopedPlainOutput plain_output;
                        ScopedStreamCapture capture;
                        ProjectInfo fresh;
                        if (!fresh.open()) result = 1;
                        else {
                            apply_checks(fresh);
                            fresh.build_mode_hint_ = "auto";
                            result = fresh.build_all(false);
                        }
                        captured_output = capture.str();
                    }

                    if (!write_if_changed(build_auto_log_path, captured_output)) {
                        console::error("failed to write build-auto output log");
                        return 1;
                    }
                    if (result != 0) {
                        const std::string key = diagnostic_key(captured_output);
                        if (!was_failing || key != last_reported) {
                            std::cerr << console::error_label() << " build failed; still watching (fix the error and save)\n"
                                      << captured_output << std::flush;
                            last_reported = key;
                        }
                        const std::string report = retry_policy.after_failure();
                        if (!report.empty()) std::cerr << console::warning_label() << ' ' << report << '\n' << std::flush;
                    } else {
                        const bool warned = captured_output.find("warning: ") != std::string::npos;
                        const std::string key = warned ? diagnostic_key(captured_output) : std::string();
                        if (was_failing) {
                            std::cerr << console::good("build succeeded again") << '\n';
                            if (warned) std::cerr << captured_output;
                            std::cerr << std::flush;
                        } else if (warned && key != last_reported) {
                            std::cerr << captured_output << std::flush;
                        }
                        last_reported = key;
                        retry_policy.after_success();
                    }
                }

                const auto sleep_step = std::chrono::milliseconds(20);
                auto remaining = build_auto_poll_interval;
                while (remaining.count() > 0 && !stop_requested) {
                    const auto delay = std::min(remaining, sleep_step);
                    std::this_thread::sleep_for(delay);
                    remaining -= delay;
                    stop_requested = quit_key.pressed() || build_auto_stop_signal != 0;
                }
            }

            std::signal(SIGINT, previous_int);
            std::signal(SIGTERM, previous_term);
            std::cout << console::good("build --auto stopped") << '\n';
            return 0;
        }
        if (!names.empty()) return project.build_names(names, true, explain);
        return project.build_all(false, explain);
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
        TrackedInfo candidate{name, title, templ, "", "", std::nullopt, std::nullopt, std::nullopt, std::nullopt, {}};
        if (project.conflicts_with_tracked_path(candidate)) {
            console::error("tracked name resolves to a content/output path already managed by another tracked name");
            return 1;
        }
        project.tracked.push_back(std::move(candidate));
        project.invalidate_tracked_index();
        // Persist authoritative tracking state FIRST, then create the content
        // file (order B). A failed save leaves tracked.json unchanged (the
        // in-memory entry is popped so the process state matches); a failed
        // content creation leaves a tracked entry with a missing content file,
        // which the next build reports precisely ("content file does not exist")
        // -- far more recoverable than an orphan content file Nift has no
        // knowledge of. track never participates in the build ownership epoch.
        if (!project.save_tracking()) {
            project.tracked.pop_back();
            project.invalidate_tracked_index();
            return 1;
        }
        const fs::path new_content = project.content_path(project.tracked.back());
        if (!filesystem::path_exists(new_content) && !filesystem::write_file(new_content, "")) {
            console::error("could not create tracked content file: " + new_content.generic_string());
            return 1;
        }
        return 0;
    }

    if (command == "untrack" || command == "rm" || command == "del") {
        if (argc < 3) return 1;
        std::set<std::string> removed;
        for (int i = 2; i < argc; ++i) removed.insert(argv[i]);
        for (const auto& info : project.tracked) {
            if (removed.count(info.name)) continue;
            for (const auto& dependency : info.pipeline().depends) {
                if (removed.count(dependency)) {
                    console::error("cannot remove prerequisite '" + dependency + "' while tracked item '" + info.name + "' depends on it");
                    return 1;
                }
            }
        }
        // CP2.1: these mutators run a full ownership epoch instead of a probe,
        // so the lock is held continuously for the whole mutation window (no
        // TOCTOU against a concurrently starting build). A live owner and a
        // stale .unfinished both refuse: stale derived state is untrusted and
        // only a build (which recovers it automatically, D-020) or `build
        // --repair` may mutate it. A crash mid-mutation leaves .unfinished, so
        // the next build recovers.
        ProjectOwnership ownership(project.root / ".nift/.unfinished");
        const ProjectOwnership::State ownership_state = ownership.acquire();
        if (ownership_state == ProjectOwnership::State::Live) {
            console::error("another build appears to be running; refusing to mutate build state");
            return 1;
        }
        if (ownership_state == ProjectOwnership::State::Failed) {
            console::error("could not acquire the build lock");
            return 1;
        }
        if (ownership_state == ProjectOwnership::State::Stale) {
            console::error("unfinished build detected; generated build state may be incomplete. run `nift build` (it recovers automatically, as `nift build --repair`) before changing tracking.");
            return 1;
        }
        for (int i = 2; i < argc; ++i) {
            const std::string name = argv[i];
            auto it = std::find_if(project.tracked.begin(), project.tracked.end(), [&](const TrackedInfo& info) { return info.name == name; });
            if (it == project.tracked.end()) continue;
            const TrackedInfo value = *it;
            if (command != "untrack") remove_page_build_state(project, value, true);
            project.tracked.erase(it);
            project.invalidate_tracked_index();
        }
        // D-020: drop the removed names from the failed-page list, so a
        // re-tracked name starts clean.
        const bool ok = project.save_tracking() && project.prune_failed_pages();
        if (ok) ownership.finish();
        return ok ? 0 : 1;
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

        // CP2.1: full ownership epoch for the mutation window (no probe). All
        // validation above runs before acquisition, so a validation failure
        // does not leave a marker; a crash during the mutations below leaves
        // .unfinished so the next build recovers automatically (D-020).
        ProjectOwnership ownership(project.root / ".nift/.unfinished");
        const ProjectOwnership::State ownership_state = ownership.acquire();
        if (ownership_state == ProjectOwnership::State::Live) {
            console::error("another build appears to be running; refusing to mutate build state");
            return 1;
        }
        if (ownership_state == ProjectOwnership::State::Failed) {
            console::error("could not acquire the build lock");
            return 1;
        }
        if (ownership_state == ProjectOwnership::State::Stale) {
            console::error("unfinished build detected; generated build state may be incomplete. run `nift build` (it recovers automatically, as `nift build --repair`) before changing tracking.");
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
            for (auto& info : project.tracked) {
                if (std::find(info.pipeline().depends.begin(), info.pipeline().depends.end(), source_name) == info.pipeline().depends.end()) continue;
                for (auto& dependency : info.mutable_pipeline().depends)
                    if (dependency == source_name) dependency = destination_name;
            }
        }

        if (!project.save_tracking()) {
            fs::remove(destination_content, error);
            if (has_sidecar) fs::remove(destination_sidecar, error);
            return 1;
        }

        if (command == "mv" || command == "move") remove_page_build_state(project, source_copy, true);
        // D-020: a moved page's failed-list entry goes with its old name.
        if (!project.prune_failed_pages()) return 1; // marker kept: next build recovers
        ownership.finish();
        return 0;
    }

    if (command == "info" || command == "status") {
        if (command == "status") {
            // status takes no names and only the -p explain option; reject
            // unknown options and stray positional arguments like build/info.
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (!arg.empty() && arg[0] == '-') {
                    // feature-site-dx: --strict reports pages that a strict
                    // build would rebuild (and fail); --no-warn filters the
                    // listed outstanding warnings.
                    if (arg == "--strict") { project.config.strict_values = true; continue; }
                    if (arg.rfind("--no-warn=", 0) == 0) {
                        const std::string kind = arg.substr(10);
                        const auto& kinds = site_warning_kinds();
                        if (kind != "all" && std::find(kinds.begin(), kinds.end(), kind) == kinds.end()) {
                            console::error("unknown warning kind '" + kind + "' (expected all, unknown-directive, path-outside-output or unknown-value)");
                            return 1;
                        }
                        project.config.no_warn.insert(kind);
                        continue;
                    }
                    if (arg != "-p") { console::error("unknown status option '" + arg + "'"); return 1; }
                } else {
                    console::error("status takes no page names");
                    return 1;
                }
            }

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

            // feature-site-dx (review R1): warnings recorded at each page's
            // last build, listed on stderr so stdout keeps its format.
            {
                const auto outstanding = project.outstanding_warnings();
                std::size_t count = 0;
                for (const auto& entry : outstanding) count += entry.second.size();
                if (count) {
                    std::cerr << console::warning_label() << ' ' << count << " outstanding build warning"
                              << (count == 1 ? "" : "s") << " (recorded at each page's last build):\n";
                    for (const auto& [page, lines] : outstanding)
                        for (const auto& line : lines) std::cerr << "  " << page << ": " << line << '\n';
                }
            }
            if (pending.empty()) {
                std::cout << console::heading("Nift status") << '\n';
                std::cout << console::good("✓") << " all " << project.tracked.size() << " tracked "
                          << (project.tracked.size() == 1 ? "file is" : "files are") << " up to date\n";
                return 0;
            }

            constexpr std::size_t detailed_status_limit = 10;
            constexpr std::size_t status_sample_limit = 5;
            const bool full_detail = has_option(argc, argv, 2, "-p");

            std::cout << console::heading("Nift status") << '\n';
            std::cout << pending.size() << " of " << project.tracked.size() << " tracked "
                      << (project.tracked.size() == 1 ? "file needs" : "files need") << " rebuilding\n\n";

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
                               (count == 1 ? " file" : " files")) << '\n';

                std::cout << '\n' << console::dim("affected files: ");
                const std::size_t shown = std::min(status_sample_limit, pending.size());
                for (std::size_t i = 0; i < shown; ++i) {
                    if (i) std::cout << console::dim(", ");
                    std::cout << console::path(pending[i].info->name);
                }
                if (pending.size() > shown)
                    std::cout << console::dim("  +" + std::to_string(pending.size() - shown) + " more");
                std::cout << '\n';
            }

            std::cout << "\nrun " << console::good("nift build") << " to rebuild "
                      << (pending.size() == 1 ? "this file" : "these files") << '\n';
            return 0;
        }

        // Modes are mutually exclusive: positional names, --all, --watching,
        // --tracking, --names. -p is not used by info. --all is the explicit
        // spelling of the default all-entries view and needs no state.
        bool watching_mode = false, tracking_mode = false, names_mode = false;
        std::vector<std::string> names;
        int mode_flags = 0;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--all") { ++mode_flags; }
            else if (arg == "--watching") { watching_mode = true; ++mode_flags; }
            else if (arg == "--tracking") { tracking_mode = true; ++mode_flags; }
            else if (arg == "--names") { names_mode = true; ++mode_flags; }
            else if (!arg.empty() && arg[0] == '-') { console::error("unknown info option '" + arg + "'"); return 1; }
            else names.emplace_back(arg);
        }
        if (mode_flags > 1 || (mode_flags == 1 && !names.empty())) {
            console::error("info modes are mutually exclusive");
            return 1;
        }

        if (watching_mode) {
            print_json_document(watch_json(project.watch_list()), "Watching information",
                                "Directories Nift scans automatically and the extension rules applied to each one.");
            return 0;
        }
        if (tracking_mode) {
            json::Document result = json::Document::make_object();
            result["tracking-file"] = ".nift/tracked.json";
            result["tracked-count"] = static_cast<int>(project.tracked.size());
            result["tracked"] = json::Document::make_array();
            for (const auto& info : project.tracked) result["tracked"].push_back(tracked_entry_json(project, info));
            print_json_document(result, "Tracking information",
                                "Tracking state plus the resolved paths Nift uses for each entry.");
            return 0;
        }
        if (names_mode) {
            json::Document names_doc = json::Document::make_object();
            names_doc["tracked"] = json::Document::make_array();
            for (const auto& info : project.tracked) names_doc["tracked"].push_back(info.name);
            print_json_document(names_doc, "Tracked names", "Names currently managed by this Nift project.");
            return 0;
        }

        // --all is the explicit spelling of the default all-entries view.
        if (!names.empty()) {
            json::Document result = json::Document::make_object();
            result["tracked"] = json::Document::make_array();
            bool has_untracked = false;
            for (const auto& name : names) {
                const TrackedInfo* info = project.find(name);
                if (info) {
                    result["tracked"].push_back(tracked_entry_json(project, *info));
                } else {
                    if (!has_untracked) {
                        result["not-tracking"] = json::Document::make_array();
                        has_untracked = true;
                    }
                    result["not-tracking"].push_back(name);
                }
            }
            print_json_document(result, "Tracked file information", "Resolved paths and metadata for the requested tracked names.");
            return 0;
        }

        json::Document result = json::Document::make_object();
        result["tracked"] = json::Document::make_array();
        for (const auto& info : project.tracked) result["tracked"].push_back(tracked_entry_json(project, info));
        print_json_document(result, "All tracked file information", "Complete metadata for every tracked entry in the project.");
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

    return 1;
}
