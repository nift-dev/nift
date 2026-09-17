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

    bool page_project_metadata(const TrackedInfo& info, json::Document& out, std::string&) const override {
        const content_model::Model* model = project_.content_model_value();
        if (!model) return false;
        out = compute_tracked_metadata(project_.root, project_.config, info, *model);
        if (!model->errors.empty()) out["_error"] = json::Document(model->errors.front());
        return true;
    }

    static std::string page_ref_of(const std::string& name) { return std::string("\x1fnift:page:") + name; }

    bool page_ref_for(const std::string& name, std::string& ref) const override {
        const HierarchyIndex* hi = project_.hierarchy_index();
        if (!hi || hi->index_of(name) == HierarchyIndex::NO_PARENT) return false;
        ref = page_ref_of(name);
        return true;
    }

    bool resolve_page_member(const std::string& page_name, const std::string& member,
                             json::Document& out, std::string& error) const override {
        const HierarchyIndex* hi = project_.hierarchy_index();
        if (!hi) return false;
        const std::size_t idx = hi->index_of(page_name);
        if (idx == HierarchyIndex::NO_PARENT) return false;
        const TrackedInfo& info = project_.tracked[idx];
        if (member == "parent") {
            const std::size_t p = hi->parent_index(idx);
            out = p == HierarchyIndex::NO_PARENT ? json::Document(nullptr) : json::Document(page_ref_of(project_.tracked[p].name));
            return true;
        }
        if (member == "children" || member == "ancestors" || member == "descendants" || member == "siblings") {
            std::vector<std::size_t> list;
            if (member == "children") {
                if (const auto* c = hi->children(idx)) list = *c;
            } else if (member == "siblings") {
                const std::size_t p = hi->parent_index(idx);
                if (p != HierarchyIndex::NO_PARENT)
                    if (const auto* c = hi->children(p)) for (const auto ch : *c) if (ch != idx) list.push_back(ch);
            } else if (member == "ancestors") {
                std::size_t p = hi->parent_index(idx);
                while (p != HierarchyIndex::NO_PARENT) { list.push_back(p); p = hi->parent_index(p); }
            } else { // descendants: pre-order DFS over tracked-ordered children
                std::vector<std::size_t> stack;
                if (const auto* c = hi->children(idx)) stack.assign(c->rbegin(), c->rend());
                while (!stack.empty()) {
                    const std::size_t cur = stack.back(); stack.pop_back();
                    list.push_back(cur);
                    if (const auto* cc = hi->children(cur))
                        for (auto it = cc->rbegin(); it != cc->rend(); ++it) stack.push_back(*it);
                }
            }
            out = json::Document::make_array();
            out.array.reserve(list.size());
            for (const auto i : list) out.array.emplace_back(page_ref_of(project_.tracked[i].name));
            return true;
        }
        if (member == "name" || member == "title" || member == "path" || member == "output_path" ||
            member == "url" || member == "type" || member == "metadata" || member == "content") {
            if (member == "name") { out = json::Document(info.name); return true; }
            if (member == "title") { out = json::Document(info.title); return true; }
            if (member == "type") { out = info.type ? json::Document(*info.type) : json::Document(nullptr); return true; }
            const auto source = project_read::content_path_of(project_.root, project_.config, info);
            const auto output = project_read::output_path_of(project_.root, project_.config, info);
            if (member == "path") { out = json::Document(project_read::relative_of(project_.root, source)); return true; }
            if (member == "output_path") { out = json::Document(project_read::relative_of(project_.root, output)); return true; }
            if (member == "url") { out = json::Document("/" + project_read::relative_of(project_.root, output).substr(project_.config.output_dir.size())); return true; }
            if (member == "content") { out = json::Document(filesystem::file_exists(source) ? filesystem::read_file(source) : std::string{}); return true; }
            if (member == "metadata") {
                const content_model::Model* model = project_.content_model_value();
                json::Document meta = model
                    ? compute_tracked_metadata(project_.root, project_.config, info, *model)
                    : json::Document::make_object();
                out = std::move(meta);
                return true;
            }
        }
        return false;
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
