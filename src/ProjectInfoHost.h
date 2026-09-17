#pragma once
#include "RenderHost.h"
#include "ProjectInfo.h"
#include "FileSystem.h"
#include "ProjectModel.h"

#include <cstdlib>
#include <optional>
#include <string>

// RenderHost adapter over ProjectInfo for the CLI. Every method forwards to the
// exact pre-CP1 implementation, so rendering behaviour is unchanged.
class ProjectInfoHost : public RenderHost {
public:
    explicit ProjectInfoHost(ProjectInfo& project) : project_(project) {}

    const std::filesystem::path& root() const override { return project_.root; }
    std::string relative(const std::filesystem::path& path) const override { return project_.relative(path); }
    const std::string& output_dir() const override { return project_.config.output_dir; }
    int build_threads() const override { return project_.config.build_threads; }

    std::filesystem::path content_path(const TrackedInfo& info) const override { return project_.content_path(info); }
    std::filesystem::path output_path(const TrackedInfo& info) const override { return project_.output_path(info); }
    std::filesystem::path pagination_output_path(const TrackedInfo& info, std::size_t page) const override {
        return project_.pagination_output_path(info, page);
    }

    bool has_output_context() const override { return true; }
    std::optional<TrackedOutput> tracked_output_path(const std::string& name) const override {
        if (const TrackedInfo* target = project_.find(name)) {
            TrackedOutput out;
            out.path = project_.output_path(*target);
            out.index_page = target->name == "/" || (!target->name.empty() && target->name.back() == '/');
            return out;
        }
        return std::nullopt;
    }

    const std::shared_ptr<const json::Document>* binding(const std::string& name) const override {
        if (name == "project") {
            if (!project_value_) project_value_ = project_.project_value();
            return project_value_ ? &project_value_ : nullptr;
        }
        return nullptr;
    }

    const json::Document* project_file_value(const std::string& name) const override {
        if (!project_value_) project_value_ = project_.project_value();
        if (!project_value_ || !project_value_->is_object() || !project_value_->has("files"))
            return nullptr;
        const auto& files = (*project_value_)["files"];
        if (!files.is_array()) return nullptr;
        const auto index = project_.tracked_index_of(name);
        if (!index || *index >= files.array.size()) return nullptr;
        const json::Document& entry = files.array[*index];
        if (!entry.is_object() || !entry.has("name") || !entry["name"].is_string() ||
            entry["name"].string != name)
            return nullptr;
        return &entry;
    }

    bool is_contract_name(const std::string& name) const override {
        return project_.config.contracts.count(name) != 0;
    }
    const std::string* contract_source(const std::string& name) const override {
        const auto it = project_.config.contracts.find(name);
        return it == project_.config.contracts.end() ? nullptr : &it->second;
    }

    HostSource read_shared_source(const std::filesystem::path& path) const override {
        const std::string* content = project_.read_shared_source(path);
        if (content == nullptr) return {nift::HostStatus::NotFound, nullptr, ""};
        return {nift::HostStatus::Found, content, ""};
    }
    std::shared_ptr<const json::Document> read_shared_json(const std::filesystem::path& path,
                                                           std::string& error) const override {
        return project_.read_shared_json(path, error);
    }

    bool source_exists(const std::filesystem::path& path) const override { return filesystem::path_exists(path); }
    bool source_readable(const std::filesystem::path& path) const override { return filesystem::file_readable(path); }

    nift::HostResult environment(const std::string& name) const override {
        if (const char* value = std::getenv(name.c_str()))
            return {nift::HostStatus::Found, std::string(value), ""};
        return {nift::HostStatus::NotFound, "", ""};
    }

private:
    ProjectInfo& project_;
    mutable std::shared_ptr<const json::Document> project_value_;
};
