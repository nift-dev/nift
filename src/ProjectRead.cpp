#include "ProjectRead.h"
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

} // namespace

namespace project_read {

std::string mapped_name(const TrackedInfo& info) {
    std::string name = info.name;
    if (name == "/") name = "index";
    else if (!name.empty() && name.back() == '/') name += "index";
    return name;
}

fs::path content_path_of(const fs::path& root, const Config& config, const TrackedInfo& info) {
    return root / config.content_dir / (mapped_name(info) + (info.content_ext.empty() ? config.content_ext : info.content_ext));
}

fs::path output_path_of(const fs::path& root, const Config& config, const TrackedInfo& info) {
    return root / config.output_dir / (mapped_name(info) + (info.output_ext.empty() ? config.output_ext : info.output_ext));
}

fs::path pagination_output_path_of(const fs::path& root, const Config& config, const TrackedInfo& info, std::size_t page) {
    if (page <= 1) return output_path_of(root, config, info);
    const std::string extension = info.output_ext.empty() ? config.output_ext : info.output_ext;
    if (info.name == "/" || (!info.name.empty() && info.name.back() == '/')) {
        const fs::path primary = output_path_of(root, config, info);
        return primary.parent_path() / (std::to_string(page) + extension);
    }
    const fs::path primary = output_path_of(root, config, info);
    return primary.parent_path() / (primary.stem().generic_string() + "-" + std::to_string(page) + extension);
}

std::string relative_of(const fs::path& root, const fs::path& path) {
    const fs::path normalized = path.lexically_normal();
    const fs::path relative_path = normalized.lexically_relative(root.lexically_normal());
    return relative_path.empty() ? normalized.generic_string() : relative_path.generic_string();
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

bool load_config(const fs::path& root, Config& config, std::string& error) {
    const fs::path path = root / ".nift/config.json";
    json::Document document;
    std::string parse_error;
    if (!load_json_file(path, document, parse_error) || !document.is_object() || !document.has("config") ||
        !document["config"].is_object()) {
        error = "invalid project config" + (parse_error.empty() ? "" : " (" + parse_error + ")");
        return false;
    }

    const auto& value = document["config"];
    if (!string_field(value, "content-dir", config.content_dir) ||
        !string_field(value, "content-ext", config.content_ext) ||
        !string_field(value, "output-dir", config.output_dir) ||
        !string_field(value, "output-ext", config.output_ext) ||
        !string_field(value, "default-template", config.default_template) ||
        !string_field(value, "incremental-mode", config.incremental_mode)) {
        error = "config string fields must contain JSON strings";
        return false;
    }

    if (config.content_dir.empty()) {
        error = "content-dir must be non-empty";
        return false;
    }
    if (!filesystem::valid_extension(config.content_ext) || !filesystem::valid_extension(config.output_ext)) {
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
        config.build_threads = value["build-threads"].as_int();
    }

    config.contracts.clear();
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
            const fs::path contract_path = (root / source.string).lexically_normal();
            if (!filesystem::path_within(root, contract_path)) {
                error = "contract '" + name + "' path must stay inside the Nift project: " + source.string;
                return false;
            }
            config.contracts.emplace(name, source.string);
        }
    }

    config.minify_exts.clear();
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
            config.minify_exts.insert(std::move(normalized_extension));
        }
    }

    if (config.incremental_mode != "modified" && config.incremental_mode != "hash" && config.incremental_mode != "hybrid") {
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

bool load_tracking(const fs::path& root, const Config& config, std::vector<TrackedInfo>& tracked, std::string& error) {
    const fs::path path = root / ".nift/tracked.json";
    if (!filesystem::path_exists(path)) {
        error = "invalid tracked.json (file does not exist)";
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

            const fs::path derived_content = content_path_of(root, config, info).lexically_normal();
            const fs::path derived_output = output_path_of(root, config, info).lexically_normal();
            const fs::path template_path = (root / info.template_path).lexically_normal();
            if (!info.template_path.empty() && (derived_content == template_path || derived_output == template_path)) {
                entries_valid = false;
                entry_error = "tracked template path cannot be the same as its content or output path";
                return false;
            }
            tracked.emplace_back(std::move(info));
            return true;
        }, parse_error);

    if (!parsed || !entries_valid) {
        const std::string details = entries_valid ? parse_error : entry_error;
        error = "invalid tracked.json" + (details.empty() ? "" : " (" + details + ")");
        tracked.clear();
        return false;
    }

    // Parsing no longer needs the tracked.json source. Release it before the
    // uniqueness/path pass so the source buffer does not overlap the largest
    // temporary validation allocation.
    source.clear();
    source.shrink_to_fit();

    // Uniqueness: no duplicate tracked names, no two entries resolving to the
    // same content path, no two entries resolving to the same output path.
    {
        std::vector<const std::string*> names;
        names.reserve(tracked.size());
        for (const auto& info : tracked) names.push_back(&info.name);
        std::sort(names.begin(), names.end(),
                  [](const std::string* a, const std::string* b) { return *a < *b; });
        for (std::size_t i = 1; i < names.size(); ++i) {
            if (*names[i - 1] == *names[i]) {
                error = "invalid tracked.json (duplicate tracked name '" + *names[i] + "')";
                tracked.clear();
                return false;
            }
        }
    }

    {
        std::vector<std::string> paths;
        paths.reserve(tracked.size());
        for (const auto& info : tracked)
            paths.push_back(content_path_of(root, config, info).lexically_normal().generic_string());
        std::sort(paths.begin(), paths.end());
        if (std::adjacent_find(paths.begin(), paths.end()) != paths.end()) {
            error = "invalid tracked.json (tracked entries resolve to the same content or output path)";
            tracked.clear();
            return false;
        }

        paths.clear();
        for (const auto& info : tracked)
            paths.push_back(output_path_of(root, config, info).lexically_normal().generic_string());
        std::sort(paths.begin(), paths.end());
        if (std::adjacent_find(paths.begin(), paths.end()) != paths.end()) {
            error = "invalid tracked.json (tracked entries resolve to the same content or output path)";
            tracked.clear();
            return false;
        }
    }

    return true;
}

} // namespace project_read
