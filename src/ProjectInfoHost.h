#pragma once
#include "RenderHost.h"
#include "ProjectInfo.h"

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

    const TrackedInfo* find(const std::string& name) const override { return project_.find(name); }

    bool is_contract_name(const std::string& name) const override {
        return project_.config.contracts.count(name) != 0;
    }
    const std::string* contract_source(const std::string& name) const override {
        const auto it = project_.config.contracts.find(name);
        return it == project_.config.contracts.end() ? nullptr : &it->second;
    }

    const std::string* read_shared_source(const std::filesystem::path& path) const override {
        return project_.read_shared_source(path);
    }
    std::shared_ptr<const json::Document> read_shared_json(const std::filesystem::path& path,
                                                           std::string& error) const override {
        return project_.read_shared_json(path, error);
    }

private:
    ProjectInfo& project_;
};
