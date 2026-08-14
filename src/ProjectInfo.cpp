#include "ProjectInfo.h"
#include "BuildProgress.h"
#include "Console.h"
#include "FileSystem.h"
#include "JsonFile.h"
#include "Parser.h"
#include "WatchList.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <thread>

namespace fs = std::filesystem;

namespace {
// Keep ordinary builds informative without flooding the terminal on large sites.
constexpr std::size_t detailed_build_output_limit = 10;
constexpr std::size_t summary_path_sample_limit = 5;

fs::path find_project_root(fs::path path = fs::current_path()) {
    std::error_code error;
    path = fs::absolute(path, error);
    while (true) {
        if (filesystem::path_exists(path / ".nift/config.json")) return path;
        const fs::path parent = path.parent_path();
        if (parent == path) return {};
        path = parent;
    }
}

bool string_field(const json::Document& object, const std::string& key, std::string& destination) {
    if (object.has(key) && object[key].is_string()) destination = object[key].string;
    return !object.has(key) || object[key].is_string();
}
}

ProjectInfo::ProjectInfo() : watch_(new WatchList) {}
ProjectInfo::~ProjectInfo() { delete watch_; }
WatchList& ProjectInfo::watch_list() { return *watch_; }

bool ProjectInfo::open() {
    root = find_project_root();
    if (root.empty()) {
        console::error("not inside a Nift project");
        return false;
    }
    return load_config() && load_tracking() && watch_->load(root);
}

bool ProjectInfo::load_config() {
    const fs::path path = root / ".nift/config.json";
    json::Document document;
    std::string error;
    if (!load_json_file(path, document, error) || !document.is_object() || !document.has("config") || !document["config"].is_object()) {
        console::error(console::path(relative(path), true) + ": invalid project config" + (error.empty() ? "" : " (" + error + ")"));
        return false;
    }

    const auto& value = document["config"];
    if (!string_field(value, "content-dir", config.content_dir) ||
        !string_field(value, "content-ext", config.content_ext) ||
        !string_field(value, "output-dir", config.output_dir) ||
        !string_field(value, "output-ext", config.output_ext) ||
        !string_field(value, "default-template", config.default_template) ||
        !string_field(value, "incremental-mode", config.incremental_mode)) {
        console::error(console::path(relative(path), true) + ": config string fields must contain JSON strings");
        return false;
    }

    if (value.has("build-threads")) {
        if (!value["build-threads"].is_number()) {
            console::error(console::path(relative(path), true) + ": build-threads must be an integer");
            return false;
        }
        config.build_threads = value["build-threads"].as_int();
    }

    if (config.incremental_mode != "modified" && config.incremental_mode != "hash" && config.incremental_mode != "hybrid") {
        console::error(console::path(relative(path), true) + ": incremental-mode must be modified, hash or hybrid");
        return false;
    }
    return true;
}

bool ProjectInfo::load_tracking() {
    const fs::path path = root / ".nift/tracked.json";
    if (!filesystem::path_exists(path)) {
        console::error(console::path(relative(path), true) + ": invalid tracked.json (file does not exist)");
        return false;
    }

    std::string source = filesystem::read_file(path);
    tracked.clear();
    // Most tracked records are around 100-200 bytes on disk. Reserving from the
    // source size avoids repeated vector growth without requiring a first parse.
    tracked.reserve(std::max<std::size_t>(8, source.size() / 160));

    bool entries_valid = true;
    std::string entry_error;
    std::string parse_error;
    const bool parsed = json::Document::for_each_array_item(
        source, "tracked",
        [&](json::Document&& entry) {
            if (!entry.is_object() || !entry.has("name") || !entry["name"].is_string() ||
                !entry.has("title") || !entry["title"].is_string() ||
                !entry.has("template") || !entry["template"].is_string()) {
                entries_valid = false;
                entry_error = "every tracked entry must be an object with string name/title/template fields";
                return false;
            }

            TrackedInfo info{
                std::move(entry["name"].string),
                std::move(entry["title"].string),
                std::move(entry["template"].string),
                "", ""
            };
            if (entry.has("content-ext")) {
                if (!entry["content-ext"].is_string()) {
                    entries_valid = false;
                    entry_error = "tracked content-ext must be a string";
                    return false;
                }
                info.content_ext = std::move(entry["content-ext"].string);
            }
            if (entry.has("output-ext")) {
                if (!entry["output-ext"].is_string()) {
                    entries_valid = false;
                    entry_error = "tracked output-ext must be a string";
                    return false;
                }
                info.output_ext = std::move(entry["output-ext"].string);
            }
            tracked.emplace_back(std::move(info));
            return true;
        }, parse_error);

    if (!parsed || !entries_valid) {
        const std::string details = entries_valid ? parse_error : entry_error;
        console::error(console::path(relative(path), true) + ": invalid tracked.json" +
                       (details.empty() ? "" : " (" + details + ")"));
        tracked.clear();
        return false;
    }
    rebuild_tracked_index();
    return true;
}

bool ProjectInfo::save_tracking() const {
    rebuild_tracked_index();
    json::Document document = json::Document::make_object();
    document["tracked"] = json::Document::make_array();
    for (const auto& info : tracked) {
        json::Document entry = json::Document::make_object();
        entry["name"] = info.name;
        entry["title"] = info.title;
        entry["template"] = info.template_path;
        if (!info.content_ext.empty()) entry["content-ext"] = info.content_ext;
        if (!info.output_ext.empty()) entry["output-ext"] = info.output_ext;
        document["tracked"].push_back(entry);
    }
    return save_json_file(root / ".nift/tracked.json", document);
}

void ProjectInfo::rebuild_tracked_index() const {
    tracked_index_.clear();
    tracked_index_.reserve(tracked.size());
    for (std::size_t i = 0; i < tracked.size(); ++i) tracked_index_[tracked[i].name] = i;
    tracked_index_size_ = tracked.size();
}

TrackedInfo* ProjectInfo::find(const std::string& name) {
    if (tracked_index_size_ != tracked.size()) rebuild_tracked_index();
    const auto it = tracked_index_.find(name);
    return it == tracked_index_.end() ? nullptr : &tracked[it->second];
}

const TrackedInfo* ProjectInfo::find(const std::string& name) const {
    if (tracked_index_size_ != tracked.size()) rebuild_tracked_index();
    const auto it = tracked_index_.find(name);
    return it == tracked_index_.end() ? nullptr : &tracked[it->second];
}

void ProjectInfo::invalidate_tracked_index() {
    tracked_index_size_ = static_cast<std::size_t>(-1);
}


fs::path ProjectInfo::content_path(const TrackedInfo& info) const {
    std::string name = info.name;
    if (name == "/") name = "index";
    else if (!name.empty() && name.back() == '/') name += "index";
    return root / config.content_dir / (name + (info.content_ext.empty() ? config.content_ext : info.content_ext));
}

fs::path ProjectInfo::output_path(const TrackedInfo& info) const {
    std::string name = info.name;
    if (name == "/") name = "index";
    else if (!name.empty() && name.back() == '/') name += "index";
    return root / config.output_dir / (name + (info.output_ext.empty() ? config.output_ext : info.output_ext));
}

fs::path ProjectInfo::info_path(const TrackedInfo& info) const {
    const fs::path output = output_path(info).lexically_normal();
    fs::path relative_output_path = output.lexically_relative(root.lexically_normal());
    if (relative_output_path.empty()) relative_output_path = output;
    std::string relative_output = relative_output_path.generic_string();
    const std::string extension = info.output_ext.empty() ? config.output_ext : info.output_ext;
    if (relative_output.size() >= extension.size() &&
        relative_output.compare(relative_output.size() - extension.size(), extension.size(), extension) == 0)
        relative_output.resize(relative_output.size() - extension.size());
    return root / ".nift" / (relative_output + ".info.json");
}

std::string ProjectInfo::relative(const fs::path& path) const {
    const fs::path normalized = path.lexically_normal();
    const fs::path relative_path = normalized.lexically_relative(root.lexically_normal());
    return relative_path.empty() ? normalized.generic_string() : relative_path.generic_string();
}

const std::string* ProjectInfo::read_shared_source(const fs::path& path) const {
    const std::string key = path.lexically_normal().generic_string();
    {
        std::lock_guard<std::mutex> lock(source_cache_mutex_);
        const auto it = shared_source_cache_.find(key);
        if (it != shared_source_cache_.end()) return it->second.get();
    }

    auto contents = std::make_unique<const std::string>(filesystem::read_file(path));
    const std::string* result = contents.get();
    {
        std::lock_guard<std::mutex> lock(source_cache_mutex_);
        auto [it, inserted] = shared_source_cache_.emplace(key, std::move(contents));
        if (!inserted) result = it->second.get();
    }
    return result;
}

bool ProjectInfo::load_user_dependencies(const TrackedInfo& info, std::set<std::string>& dependencies, BuildError* build_error) const {
    fs::path path = content_path(info);
    path.replace_extension(".deps.json");
    if (!filesystem::path_exists(path)) return true;

    json::Document document;
    std::string error;
    if (!load_json_file(path, document, error) || !document.is_object() || !document.has("dependencies") || !document["dependencies"].is_array()) {
        if (build_error) *build_error = {info.name, path, 0, "invalid user dependencies JSON" + (error.empty() ? "" : ": " + error)};
        return false;
    }

    for (const auto& value : document["dependencies"].array) {
        if (!value.is_string()) {
            if (build_error) *build_error = {info.name, path, 0, "'dependencies' array contains a non-string member"};
            return false;
        }
        if (!filesystem::path_exists(root / value.string)) {
            if (build_error) *build_error = {info.name, path, 0, "dependency does not exist: " + value.string};
            return false;
        }
        dependencies.insert(value.string);
    }
    return true;
}

bool ProjectInfo::hash_changed_cached(const fs::path& dependency) const {
    const std::string key = dependency.lexically_normal().generic_string();
    {
        std::lock_guard<std::mutex> lock(hash_mutex_);
        const auto it = hash_change_cache_.find(key);
        if (it != hash_change_cache_.end()) return it->second;
    }

    const bool changed = filesystem::stored_hash_changed(root, dependency);
    {
        std::lock_guard<std::mutex> lock(hash_mutex_);
        // Keep this cache deliberately small. Its job is to retain hot shared
        // dependencies (templates/partials), not every one-off content file.
        constexpr std::size_t max_cached_hash_results = 512;
        if (hash_change_cache_.size() < max_cached_hash_results)
            hash_change_cache_.emplace(key, changed);
    }
    return changed;
}

void ProjectInfo::reset_build_caches() {
    {
        std::lock_guard<std::mutex> lock(hash_mutex_);
        hash_change_cache_.clear();
        refreshed_hashes_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(source_cache_mutex_);
        shared_source_cache_.clear();
    }
}

void ProjectInfo::refresh_hash_once(const fs::path& dependency) {
    const fs::path normalized = dependency.lexically_normal();
    const fs::path content_root = (root / config.content_dir).lexically_normal();
    const fs::path relative_to_content = normalized.lexically_relative(content_root);

    // Content files are normally unique to one tracked page, so remembering all
    // 10,000 of them merely to prove they are unique wastes memory. Shared
    // templates, partials and explicit dependencies still use the dedupe set.
    const bool is_content_file = !relative_to_content.empty() &&
                                 *relative_to_content.begin() != "..";
    if (!is_content_file) {
        const std::string key = normalized.generic_string();
        std::lock_guard<std::mutex> lock(hash_mutex_);
        if (!refreshed_hashes_.insert(key).second) return;
    }
    filesystem::write_stored_hash(root, normalized);
}

bool ProjectInfo::dependency_changed(const fs::path& dependency, const fs::path& page_info_path) const {
    if (!filesystem::path_exists(dependency)) return true;
    if (config.incremental_mode == "modified")
        return filesystem::modified_time(dependency) > filesystem::modified_time(page_info_path);
    if (config.incremental_mode == "hash")
        return hash_changed_cached(dependency);
    return filesystem::modified_time(dependency) > filesystem::modified_time(page_info_path) ||
           hash_changed_cached(dependency);
}

std::vector<std::string> ProjectInfo::build_reasons(const TrackedInfo& info) const {
    std::vector<std::string> reasons;
    const fs::path output = output_path(info);
    const fs::path page_info = info_path(info);

    if (!filesystem::path_exists(output)) reasons.push_back("generated output is missing");
    if (!filesystem::path_exists(page_info)) {
        reasons.push_back("page build metadata is missing");
        return reasons;
    }

    json::Document document;
    std::string error;
    if (!load_json_file(page_info, document, error) || !document.is_object() ||
        !document.has("dependencies") || !document["dependencies"].is_array()) {
        reasons.push_back("page build metadata is invalid");
        return reasons;
    }

    for (const auto& value : document["dependencies"].array) {
        if (!value.is_string()) {
            reasons.push_back("page build metadata has an invalid dependency");
            continue;
        }

        const fs::path dependency = root / value.string;
        if (!filesystem::path_exists(dependency))
            reasons.push_back("dependency removed: " + value.string);
        else if (dependency_changed(dependency, page_info))
            reasons.push_back("dependency changed: " + value.string);
    }

    // A user .deps.json file can itself be added or become invalid between builds.
    // Its dependencies are also present in page-info after a successful build, but
    // loading it here catches sidecar metadata changes before parsing starts.
    std::set<std::string> user_dependencies;
    if (!load_user_dependencies(info, user_dependencies, nullptr)) {
        reasons.push_back("user dependency metadata is invalid");
    } else {
        for (const auto& dependency_name : user_dependencies) {
            const std::string reason = "dependency changed: " + dependency_name;
            const std::string removed = "dependency removed: " + dependency_name;
            if (std::find(reasons.begin(), reasons.end(), reason) != reasons.end() ||
                std::find(reasons.begin(), reasons.end(), removed) != reasons.end()) continue;

            const fs::path dependency = root / dependency_name;
            if (!filesystem::path_exists(dependency))
                reasons.push_back(removed);
            else if (dependency_changed(dependency, page_info))
                reasons.push_back(reason);
        }
    }

    return reasons;
}

bool ProjectInfo::needs_build(const TrackedInfo& info, std::string* reason) const {
    const auto reasons = build_reasons(info);
    if (reasons.empty()) return false;
    if (reason) *reason = reasons.front();
    return true;
}

void ProjectInfo::print_build_error(const BuildError& error) const {
    std::lock_guard<std::mutex> lock(console::output_mutex);
    std::cerr << console::error_label() << " while building " << console::path(error.tracked_name, true) << '\n';
    if (!error.source_file.empty()) {
        std::cerr << "  " << console::path(relative(error.source_file), true);
        if (error.line) {
            std::cerr << ':' << error.line;
            if (error.column) std::cerr << ':' << error.column;
        }
        std::cerr << '\n';
    }
    std::cerr << "  " << error.message << '\n';
    if (!error.source_line.empty()) {
        std::cerr << "    " << error.source_line << '\n';
        if (error.column) std::cerr << "    " << std::string(error.column - 1, ' ') << "^\n";
    }
}

bool ProjectInfo::write_page_info(const TrackedInfo& info, const std::set<std::string>& dependencies) const {
    std::size_t estimated_size = 96 + info.name.size() + info.title.size() + info.template_path.size();
    for (const auto& dependency : dependencies) estimated_size += dependency.size() + 10;

    std::string output;
    output.reserve(estimated_size);
    output += "{\n  \"name\": \"";
    json::Document::append_escaped_string(output, info.name);
    output += "\",\n  \"title\": \"";
    json::Document::append_escaped_string(output, info.title);
    output += "\",\n  \"template\": \"";
    json::Document::append_escaped_string(output, info.template_path);
    output += "\",\n  \"dependencies\": [";

    if (!dependencies.empty()) output.push_back('\n');
    std::size_t index = 0;
    for (const auto& dependency : dependencies) {
        output += "    \"";
        json::Document::append_escaped_string(output, dependency);
        output.push_back('\"');
        if (++index != dependencies.size()) output.push_back(',');
        output.push_back('\n');
    }
    if (!dependencies.empty()) output += "  ";
    output += "]\n}\n";

    return filesystem::write_readonly_file(info_path(info), output);
}

bool ProjectInfo::build_one(TrackedInfo& info) {
    const fs::path content = content_path(info);
    if (!filesystem::path_exists(content)) {
        print_build_error({info.name, content, 0, "content file does not exist"});
        return false;
    }

    Parser parser(*this, info);
    RenderResult result = parser.render();
    if (!result.ok) { print_build_error(result.error); return false; }
    if (!load_user_dependencies(info, result.dependencies, &result.error)) { print_build_error(result.error); return false; }

    const fs::path output = output_path(info);
    if (!filesystem::write_readonly_file(output, result.output)) {
        print_build_error({info.name, output, 0, "failed to write generated output"});
        return false;
    }

    if (config.incremental_mode != "modified") {
        for (const auto& dependency : result.dependencies) {
            const fs::path dependency_path = root / dependency;
            if (filesystem::path_exists(dependency_path)) refresh_hash_once(dependency_path);
        }
    }

    if (!write_page_info(info, result.dependencies)) {
        print_build_error({info.name, info_path(info), 0, "failed to write page build metadata"});
        return false;
    }

    return true;
}

int ProjectInfo::build_many(const std::vector<BuildJob>& jobs, bool targeted, bool full_detail, std::size_t requested_count) {
    if (jobs.empty()) {
        std::lock_guard<std::mutex> lock(console::output_mutex);
        if (targeted) return 1;
        std::cout << console::good("✓") << ' ' << requested_count << " tracked "
                  << (requested_count == 1 ? "page is" : "pages are") << " up to date\n";
        return 0;
    }

    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    std::size_t thread_count = config.build_threads < 0 ? static_cast<std::size_t>(-config.build_threads) * hardware : (config.build_threads == 0 ? hardware : static_cast<std::size_t>(config.build_threads));
    thread_count = std::max<std::size_t>(1, std::min(thread_count, jobs.size()));

    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> completed{0};
    std::vector<unsigned char> succeeded(jobs.size(), 0);
    BuildProgress progress(jobs.size(), completed);

    auto worker = [&] {
        while (true) {
            const std::size_t index = next.fetch_add(1);
            if (index >= jobs.size()) break;
            succeeded[index] = build_one(*jobs[index].info) ? 1 : 0;
            ++completed;
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) workers.emplace_back(worker);
    for (auto& worker_thread : workers) worker_thread.join();

    std::size_t successful_count = 0;
    for (unsigned char success : succeeded) if (success) ++successful_count;
    const std::size_t failed_count = jobs.size() - successful_count;

    std::lock_guard<std::mutex> lock(console::output_mutex);

    bool has_rebuild_reasons = false;
    for (const auto& job : jobs) {
        if (!job.reasons.empty()) { has_rebuild_reasons = true; break; }
    }

    const bool detailed = full_detail || (has_rebuild_reasons && jobs.size() <= detailed_build_output_limit);
    if (detailed) {
        for (std::size_t i = 0; i < jobs.size(); ++i) {
            if (!succeeded[i]) continue;
            std::cout << console::good("built") << ' ' << console::path(jobs[i].info->name) << '\n';
            for (const auto& reason : jobs[i].reasons)
                std::cout << console::dim("    ↳ " + reason) << '\n';
        }
    } else if (has_rebuild_reasons) {
        std::map<std::string, std::size_t> reason_counts;
        for (std::size_t i = 0; i < jobs.size(); ++i) {
            if (!succeeded[i]) continue;
            for (const auto& reason : jobs[i].reasons) ++reason_counts[reason];
        }

        if (!reason_counts.empty()) {
            std::cout << console::dim("rebuild causes:") << '\n';
            for (const auto& [reason, count] : reason_counts)
                std::cout << "  " << console::dim("↳ " + reason + " → " + std::to_string(count) + (count == 1 ? " page" : " pages")) << '\n';
        }

        std::cout << console::dim("affected pages: ");
        std::size_t shown = 0;
        for (std::size_t i = 0; i < jobs.size() && shown < summary_path_sample_limit; ++i) {
            if (!succeeded[i]) continue;
            if (shown) std::cout << console::dim(", ");
            std::cout << console::path(jobs[i].info->name);
            ++shown;
        }
        if (successful_count > shown) std::cout << console::dim("  +" + std::to_string(successful_count - shown) + " more");
        std::cout << '\n';
    }

    if (targeted) {
        // Keep the historical wording because scripts and the regression suite rely on it.
        if (failed_count == 0) std::cout << console::good("📦") << " all " << successful_count << " specified files built successfully\n";
        else std::cout << successful_count << " specified files built successfully\n";
    } else if (failed_count == 0) {
        const bool incremental = !jobs.empty() && !jobs.front().reasons.empty();
        std::cout << console::good("📦") << ' ' << successful_count << ' '
                  << (successful_count == 1 ? "page " : "pages ")
                  << (incremental ? "rebuilt" : "built") << " successfully\n";
    } else {
        std::cout << successful_count << " of " << jobs.size() << " pages built successfully\n";
    }

    return failed_count == 0 ? 0 : 1;
}

int ProjectInfo::build_all(bool force, bool explain) {
    if (!reconcile_watch()) return 1;
    reset_build_caches();

    std::vector<BuildJob> jobs;
    jobs.reserve(tracked.size());
    if (force || tracked.size() < 2) {
        for (auto& info : tracked) {
            if (force) jobs.push_back({&info, {}});
            else {
                auto reasons = build_reasons(info);
                if (!reasons.empty()) jobs.push_back({&info, std::move(reasons)});
            }
        }
    } else {
        const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
        std::size_t thread_count = config.build_threads < 0 ? static_cast<std::size_t>(-config.build_threads) * hardware : (config.build_threads == 0 ? hardware : static_cast<std::size_t>(config.build_threads));
        thread_count = std::max<std::size_t>(1, std::min(thread_count, tracked.size()));

        std::vector<std::vector<std::string>> reasons(tracked.size());
        std::atomic<std::size_t> next{0};
        std::vector<std::thread> workers;
        workers.reserve(thread_count);
        for (std::size_t t = 0; t < thread_count; ++t) {
            workers.emplace_back([&] {
                while (true) {
                    const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
                    if (index >= tracked.size()) break;
                    reasons[index] = build_reasons(tracked[index]);
                }
            });
        }
        for (auto& worker : workers) worker.join();
        for (std::size_t i = 0; i < tracked.size(); ++i)
            if (!reasons[i].empty()) jobs.push_back({&tracked[i], std::move(reasons[i])});
    }

    return build_many(jobs, false, explain, tracked.size());
}

int ProjectInfo::build_names(const std::vector<std::string>& names, bool, bool explain) {
    if (!reconcile_watch()) return 1;
    reset_build_caches();
    std::vector<BuildJob> jobs;
    for (const auto& name : names) {
        if (TrackedInfo* info = find(name)) jobs.push_back({info, {}});
        else {
            std::lock_guard<std::mutex> lock(console::output_mutex);
            std::cout << "not tracking:\n " << name << '\n';
        }
    }
    if (jobs.empty()) return 1;
    return build_many(jobs, true, explain, names.size());
}

bool ProjectInfo::reconcile_watch() { return watch_->reconcile(*this); }
