#include "ProjectState.h"
#include "FileSystem.h"
#include "JsonFile.h"
#include <minify/Minify.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <set>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {

bool string_field(const json::Document& object, const std::string& key, std::string& destination) {
    if (object.has(key) && object[key].is_string()) destination = object[key].string;
    return !object.has(key) || object[key].is_string();
}

bool valid_contract_name(const std::string& name) {
    if (name.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) return false;
    return std::all_of(name.begin() + 1, name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    });
}

bool reserved_contract_name(const std::string& name) {
    static const std::unordered_set<std::string> names = {
        "title", "name", "content-path", "output-path", "template-path",
        "build-timezone", "build-time", "build-UTC-time", "build-date",
        "build-UTC-date", "build-YYYY", "build-YY", "build-OS", "loop"
    };
    return names.count(name) != 0;
}

bool valid_tracked_name(const std::string& name) {
    if (name == "/") return true;
    if (name.empty()) return false;
    const fs::path path(name);
    return !path.is_absolute() && !filesystem::has_parent_component(name);
}

} // namespace

ProjectState::~ProjectState() = default;

bool ProjectState::open(const std::filesystem::path& root, std::string& error) {
    root_ = fs::absolute(root).lexically_normal();
    config_ = Config{};
    tracked_.clear();
    tracked_index_.clear();
    {
        std::lock_guard<std::mutex> lock(source_cache_mutex_);
        shared_source_cache_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(json_cache_mutex_);
        shared_json_cache_.clear();
    }
    return load_config(root_ / ".nift/config.json", error) &&
           load_tracking(root_ / ".nift/tracked.json", error);
}

bool ProjectState::load_config(const fs::path& path, std::string& error) {
    json::Document document;
    std::string parse_error;
    if (!load_json_file(path, document, parse_error) || !document.is_object() || !document.has("config") ||
        !document["config"].is_object()) {
        error = "invalid project config" + (parse_error.empty() ? "" : " (" + parse_error + ")");
        return false;
    }

    const auto& value = document["config"];
    if (!string_field(value, "content-dir", config_.content_dir) ||
        !string_field(value, "content-ext", config_.content_ext) ||
        !string_field(value, "output-dir", config_.output_dir) ||
        !string_field(value, "output-ext", config_.output_ext) ||
        !string_field(value, "default-template", config_.default_template) ||
        !string_field(value, "incremental-mode", config_.incremental_mode)) {
        error = "config string fields must contain JSON strings";
        return false;
    }

    if (config_.content_dir.empty()) {
        error = "content-dir must be non-empty";
        return false;
    }
    if (!filesystem::valid_extension(config_.content_ext) || !filesystem::valid_extension(config_.output_ext)) {
        error = "content-ext and output-ext must begin with '.' and cannot contain path separators";
        return false;
    }

    if (value.has("build-threads")) {
        if (!value["build-threads"].is_number() || !std::isfinite(value["build-threads"].num) ||
            std::floor(value["build-threads"].num) != value["build-threads"].num ||
            value["build-threads"].num < static_cast<double>(std::numeric_limits<int>::min()) ||
            value["build-threads"].num > static_cast<double>(std::numeric_limits<int>::max())) {
            error = "build-threads must be an integer";
            return false;
        }
        config_.build_threads = value["build-threads"].as_int();
    }

    config_.contracts.clear();
    if (value.has("contracts")) {
        if (!value["contracts"].is_object()) {
            error = "contracts must be an object mapping names to project-relative JSON paths";
            return false;
        }
        for (const auto& entry : value["contracts"].object) {
            const std::string& name = entry.first;
            const auto& source = entry.second;
            if (!valid_contract_name(name)) {
                error = "contract name '" + name + "' must be an identifier using letters, digits and underscores";
                return false;
            }
            if (reserved_contract_name(name)) {
                error = "contract name '" + name + "' conflicts with built-in metadata/reserved bindings";
                return false;
            }
            if (!source.is_string() || source.string.empty()) {
                error = "contract '" + name + "' must map to a non-empty JSON path string";
                return false;
            }
            const fs::path contract_path = (root_ / source.string).lexically_normal();
            if (!filesystem::path_within(root_, contract_path)) {
                error = "contract '" + name + "' path must stay inside the Nift project: " + source.string;
                return false;
            }
            config_.contracts.emplace(name, source.string);
        }
    }

    config_.minify_exts.clear();
    if (value.has("minify-exts")) {
        if (!value["minify-exts"].is_array()) {
            error = "minify-exts must be an array of extension strings";
            return false;
        }
        for (const auto& item : value["minify-exts"].array) {
            if (!item.is_string() || !filesystem::valid_extension(item.string)) {
                error = "every minify-exts entry must be an extension string beginning with '.'";
                return false;
            }
            minify::Format format;
            if (!minify::format_for_extension(item.string, format)) {
                error = "unsupported minify-exts entry: " + item.string;
                return false;
            }
            std::string normalized_extension = item.string;
            std::transform(normalized_extension.begin(), normalized_extension.end(), normalized_extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            config_.minify_exts.insert(std::move(normalized_extension));
        }
    }

    if (config_.incremental_mode != "modified" && config_.incremental_mode != "hash" && config_.incremental_mode != "hybrid") {
        error = "incremental-mode must be modified, hash or hybrid";
        return false;
    }

    // Unknown config keys fail loudly, exactly like the CLI: a project must
    // never believe a setting is honoured when it is not.
    static const std::unordered_set<std::string> known_config_keys = {
        "content-dir", "content-ext", "output-dir", "output-ext",
        "default-template", "incremental-mode", "build-threads",
        "contracts", "minify-exts",
    };
    for (const auto& entry : value.object) {
        if (!known_config_keys.count(entry.first)) {
            error = "unknown config key '" + entry.first + "'";
            return false;
        }
    }
    return true;
}

bool ProjectState::load_tracking(const fs::path& path, std::string& error) {
    if (!filesystem::path_exists(path)) {
        error = "invalid tracked.json (file does not exist)";
        return false;
    }

    const std::string source = filesystem::read_file(path);
    tracked_.clear();

    bool entries_valid = true;
    std::string entry_error;
    std::string parse_error;
    const bool parsed = json::Document::for_each_array_item(
        source, "tracked",
        [&](json::Document&& entry) {
            if (!entry.is_object() || !entry.has("name") || !entry["name"].is_string() ||
                !entry.has("title") || !entry["title"].is_string() ||
                (entry.has("template") && !entry["template"].is_string())) {
                entries_valid = false;
                entry_error = "every tracked entry must be an object with string name/title fields and an optional string template field";
                return false;
            }

            TrackedInfo info{
                std::move(entry["name"].string),
                std::move(entry["title"].string),
                entry.has("template") ? std::move(entry["template"].string) : std::string{},
                "", "", std::nullopt, std::nullopt
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
            if (entry.has("minify")) {
                if (!entry["minify"].is_bool()) {
                    entries_valid = false;
                    entry_error = "tracked minify override must be a boolean";
                    return false;
                }
                info.minify = entry["minify"].boolean;
            }
            if (entry.has("paginate")) {
                const auto& paginate = entry["paginate"];
                if (!paginate.is_object() || !paginate.has("items-per-page") ||
                    !paginate["items-per-page"].is_number() ||
                    !std::isfinite(paginate["items-per-page"].num) ||
                    std::floor(paginate["items-per-page"].num) != paginate["items-per-page"].num ||
                    paginate["items-per-page"].num < 1) {
                    entries_valid = false;
                    entry_error = "tracked paginate must be an object with positive integer items-per-page";
                    return false;
                }
                PaginationConfig pagination;
                pagination.items_per_page = static_cast<std::size_t>(paginate["items-per-page"].num);
                if (paginate.has("template")) {
                    if (!paginate["template"].is_string()) {
                        entries_valid = false;
                        entry_error = "paginate template must be a string";
                        return false;
                    }
                    pagination.template_path = paginate["template"].string;
                }
                if (paginate.has("separator")) {
                    if (!paginate["separator"].is_string()) {
                        entries_valid = false;
                        entry_error = "paginate separator must be a string";
                        return false;
                    }
                    pagination.separator_path = paginate["separator"].string;
                }
                info.paginate = std::move(pagination);
            }
            if (!valid_tracked_name(info.name)) {
                entries_valid = false;
                entry_error = "tracked names must be project-relative and cannot contain '..' path components";
                return false;
            }
            if ((!info.content_ext.empty() && !filesystem::valid_extension(info.content_ext)) ||
                (!info.output_ext.empty() && !filesystem::valid_extension(info.output_ext))) {
                entries_valid = false;
                entry_error = "tracked content-ext/output-ext overrides must begin with '.' and cannot contain path separators";
                return false;
            }

            const fs::path derived_content = content_path(info).lexically_normal();
            const fs::path derived_output = output_path(info).lexically_normal();
            const fs::path template_path = (root_ / info.template_path).lexically_normal();
            if (!info.template_path.empty() && (derived_content == template_path || derived_output == template_path)) {
                entries_valid = false;
                entry_error = "tracked template path cannot be the same as its content or output path";
                return false;
            }
            tracked_.emplace_back(std::move(info));
            return true;
        }, parse_error);

    if (!parsed || !entries_valid) {
        const std::string details = entries_valid ? parse_error : entry_error;
        error = "invalid tracked.json" + (details.empty() ? "" : " (" + details + ")");
        tracked_.clear();
        return false;
    }

    // Uniqueness: no duplicate tracked names, no two entries resolving to the
    // same content path, no two entries resolving to the same output path.
    {
        std::vector<const std::string*> names;
        names.reserve(tracked_.size());
        for (const auto& info : tracked_) names.push_back(&info.name);
        std::sort(names.begin(), names.end(),
                  [](const std::string* a, const std::string* b) { return *a < *b; });
        for (std::size_t i = 1; i < names.size(); ++i) {
            if (*names[i - 1] == *names[i]) {
                error = "invalid tracked.json (duplicate tracked name '" + *names[i] + "')";
                tracked_.clear();
                return false;
            }
        }
    }

    {
        std::vector<std::string> paths;
        paths.reserve(tracked_.size());
        for (const auto& info : tracked_)
            paths.push_back(content_path(info).lexically_normal().generic_string());
        std::sort(paths.begin(), paths.end());
        if (std::adjacent_find(paths.begin(), paths.end()) != paths.end()) {
            error = "invalid tracked.json (tracked entries resolve to the same content or output path)";
            tracked_.clear();
            return false;
        }

        paths.clear();
        for (const auto& info : tracked_)
            paths.push_back(output_path(info).lexically_normal().generic_string());
        std::sort(paths.begin(), paths.end());
        if (std::adjacent_find(paths.begin(), paths.end()) != paths.end()) {
            error = "invalid tracked.json (tracked entries resolve to the same content or output path)";
            tracked_.clear();
            return false;
        }
    }

    tracked_index_.clear();
    tracked_index_.reserve(tracked_.size());
    for (std::size_t i = 0; i < tracked_.size(); ++i) tracked_index_[tracked_[i].name] = i;
    return true;
}

const TrackedInfo* ProjectState::find(const std::string& name) const {
    const auto it = tracked_index_.find(name);
    return it == tracked_index_.end() ? nullptr : &tracked_[it->second];
}

fs::path ProjectState::content_path(const TrackedInfo& info) const {
    std::string name = info.name;
    if (name == "/") name = "index";
    else if (!name.empty() && name.back() == '/') name += "index";
    return root_ / config_.content_dir / (name + (info.content_ext.empty() ? config_.content_ext : info.content_ext));
}

fs::path ProjectState::output_path(const TrackedInfo& info) const {
    std::string name = info.name;
    if (name == "/") name = "index";
    else if (!name.empty() && name.back() == '/') name += "index";
    return root_ / config_.output_dir / (name + (info.output_ext.empty() ? config_.output_ext : info.output_ext));
}

fs::path ProjectState::pagination_output_path(const TrackedInfo& info, std::size_t page) const {
    if (page <= 1) return output_path(info);
    const std::string extension = info.output_ext.empty() ? config_.output_ext : info.output_ext;
    if (info.name == "/" || (!info.name.empty() && info.name.back() == '/')) {
        const fs::path primary = output_path(info);
        return primary.parent_path() / (std::to_string(page) + extension);
    }
    const fs::path primary = output_path(info);
    return primary.parent_path() / (primary.stem().generic_string() + "-" + std::to_string(page) + extension);
}

std::string ProjectState::relative(const fs::path& path) const {
    const fs::path normalized = path.lexically_normal();
    const fs::path relative_path = normalized.lexically_relative(root_.lexically_normal());
    return relative_path.empty() ? normalized.generic_string() : relative_path.generic_string();
}

const std::string* ProjectState::read_shared_source(const fs::path& path) const {
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

std::shared_ptr<const json::Document> ProjectState::read_shared_json(const fs::path& path, std::string& error) const {
    const fs::path normalized = fs::absolute(path).lexically_normal();
    const std::string key = normalized.generic_string();

    std::lock_guard<std::mutex> lock(json_cache_mutex_);
    const auto existing = shared_json_cache_.find(key);
    if (existing != shared_json_cache_.end()) return existing->second;

    if (!filesystem::path_exists(normalized)) {
        error = "JSON file does not exist";
        return {};
    }
    if (!filesystem::file_readable(normalized)) {
        error = "JSON file is not readable";
        return {};
    }

    const std::string source = filesystem::read_file(normalized);
    auto document = std::make_shared<json::Document>();
    if (!json::Document::parse(source, *document, error)) return {};

    std::shared_ptr<const json::Document> immutable = document;
    shared_json_cache_.emplace(key, immutable);
    return immutable;
}
