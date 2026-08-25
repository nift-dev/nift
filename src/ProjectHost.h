#pragma once
#include "RenderHost.h"
#include "ProjectState.h"
#include "FileSystem.h"

#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

// RenderHost adapter over the read-only ProjectState snapshot (PA2). This lets
// the existing Parser/rendering core consume real Nift project knowledge:
// content/template/input loading, JSON loading, contracts, tracked output
// lookup, current-output geometry for @pathto (including the 404 rule) and
// pagination source/geometry.
//
// Like the CLI's ProjectInfoHost, a ProjectHost is a per-render host and
// has_output_context() is always true: a project-backed render is a tracked
// page with a real output location. Unlike ProjectInfoHost it is built on the
// immutable ProjectState snapshot, so it owns none of the CLI/build
// responsibilities: no writes, no build decisions, no implicit tracking repair,
// no watch/hash machinery.
//
// ProjectHost is stateless over the snapshot (it holds references only) and is
// safe to construct per render; the snapshot's shared read caches are
// mutex-protected, so concurrent renders through separate hosts over one
// snapshot observe the same concurrency contract as the standalone Engine.
class ProjectHost : public RenderHost {
public:
    explicit ProjectHost(
        const ProjectState& state,
        const std::unordered_map<std::string, std::shared_ptr<const json::Document>>* render_bindings = nullptr,
        std::function<nift::HostResult(std::string_view)> environment_provider = nullptr)
        : state_(state), render_bindings_(render_bindings), environment_provider_(std::move(environment_provider)) {}

    const std::filesystem::path& root() const override { return state_.root(); }
    std::string relative(const std::filesystem::path& path) const override { return state_.relative(path); }
    const std::string& output_dir() const override { return state_.config().output_dir; }
    int build_threads() const override { return state_.config().build_threads; }

    std::filesystem::path content_path(const TrackedInfo& info) const override { return state_.content_path(info); }
    std::filesystem::path output_path(const TrackedInfo& info) const override { return state_.output_path(info); }
    std::filesystem::path pagination_output_path(const TrackedInfo& info, std::size_t page) const override {
        return state_.pagination_output_path(info, page);
    }

    bool has_output_context() const override { return true; }
    std::optional<TrackedOutput> tracked_output_path(const std::string& name) const override {
        const TrackedInfo* target = state_.find(name);
        if (target == nullptr) return std::nullopt;
        TrackedOutput out;
        out.path = state_.output_path(*target);
        out.index_page = target->name == "/" || (!target->name.empty() && target->name.back() == '/');
        return out;
    }

    // Per-render host value bindings (the eventual Engine defaults/Context
    // overlays). Resolved by the parser before @json bindings and contracts,
    // exactly like the standalone Engine seam. No bindings => nullptr.
    const std::shared_ptr<const json::Document>* binding(const std::string& name) const override {
        if (render_bindings_ != nullptr) {
            const auto it = render_bindings_->find(name);
            if (it != render_bindings_->end()) return &it->second;
        }
        return nullptr;
    }

    bool is_contract_name(const std::string& name) const override {
        return state_.config().contracts.count(name) != 0;
    }
    const std::string* contract_source(const std::string& name) const override {
        const auto it = state_.config().contracts.find(name);
        return it == state_.config().contracts.end() ? nullptr : &it->second;
    }

    HostSource read_shared_source(const std::filesystem::path& path) const override {
        const std::string* content = state_.read_shared_source(path);
        if (content == nullptr) return {nift::HostStatus::NotFound, nullptr, ""};
        return {nift::HostStatus::Found, content, ""};
    }
    std::shared_ptr<const json::Document> read_shared_json(const std::filesystem::path& path,
                                                           std::string& error) const override {
        return state_.read_shared_json(path, error);
    }

    bool source_exists(const std::filesystem::path& path) const override { return filesystem::path_exists(path); }
    bool source_readable(const std::filesystem::path& path) const override { return filesystem::file_readable(path); }

    nift::HostResult environment(const std::string& name) const override {
        if (environment_provider_) return environment_provider_(name);
        if (const char* value = std::getenv(name.c_str()))
            return {nift::HostStatus::Found, std::string(value), ""};
        return {nift::HostStatus::NotFound, "", ""};
    }

private:
    const ProjectState& state_;
    const std::unordered_map<std::string, std::shared_ptr<const json::Document>>* render_bindings_;
    std::function<nift::HostResult(std::string_view)> environment_provider_;
};
