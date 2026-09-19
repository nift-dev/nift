#pragma once
#include "RenderHost.h"
#include "Types.h"
#include "HierarchyIndex.h"
#include "ProjectRead.h"
#include "ProjectModel.h"
#include "FileSystem.h"
#include "Json.h"
#include <filesystem>
#include <string>
#include <optional>
#include <mutex>
#include <memory>

namespace fs = std::filesystem;

// Shared standalone-script render host used by `nift run`, `nift sh`, `nift
// eval` and the v4.4 build-hook runner. Resolves the nearest Nift project at
// or above its root and exposes the read-only project model on demand.
class ScriptRenderHost final : public RenderHost {
public:
    explicit ScriptRenderHost(fs::path root) : root_(fs::absolute(std::move(root)).lexically_normal()) {
        fs::path probe=root_; while(!probe.empty()){Config c;std::vector<TrackedInfo> t;std::string e;if(project_read::load_config(probe,c,e)&&project_read::load_tracking(probe,c,t,e)){root_=probe;config_=std::move(c);tracked_=std::move(t);project_found_=true;break;}auto parent=probe.parent_path();if(parent==probe)break;probe=parent;}
    }
    const fs::path& root() const override { return root_; }
    std::string relative(const fs::path& p) const override { std::error_code ec; auto r=fs::relative(p,root_,ec); return ec?p.generic_string():r.generic_string(); }
    const std::string& output_dir() const override { static const std::string empty; return empty; }
    int build_threads() const override { return 1; }
    fs::path content_path(const TrackedInfo&) const override { return {}; }
    fs::path output_path(const TrackedInfo&) const override { return {}; }
    fs::path pagination_output_path(const TrackedInfo&,std::size_t) const override { return {}; }
    bool has_output_context() const override { return false; }
    std::optional<TrackedOutput> tracked_output_path(const std::string&) const override { return std::nullopt; }
    const std::shared_ptr<const json::Document>* binding(const std::string& name) const override {
        if (name == "project" && project_found_) {
            if (!project_value_) project_value_ = make_project_value(root_, config_, tracked_);
            return project_value_ ? &project_value_ : nullptr;
        }
        return nullptr;
    }
    bool is_contract_name(const std::string&) const override { return false; }
    const std::string* contract_source(const std::string&) const override { return nullptr; }
    HostSource read_shared_source(const fs::path& p) const override { if(!filesystem::file_exists(p))return {}; cache_=filesystem::read_file(p); return {nift::HostStatus::Found,&cache_,{}}; }
    std::shared_ptr<const json::Document> read_shared_json(const fs::path& p,std::string& error) const override { if(!filesystem::file_exists(p)){error="JSON file does not exist";return {};} json::Document parsed; if(!json::Document::parse(filesystem::read_file(p),parsed,error))return {}; return std::make_shared<json::Document>(std::move(parsed)); }
    bool source_exists(const fs::path& p) const override { return filesystem::file_exists(p); }
    bool source_readable(const fs::path& p) const override { return filesystem::file_exists(p); }
    nift::HostResult environment(const std::string& name) const override { const char* v=std::getenv(name.c_str()); return v?nift::HostResult{nift::HostStatus::Found,v,{}}:nift::HostResult{}; }
private:
    fs::path root_; mutable std::string cache_; mutable std::shared_ptr<const json::Document> project_value_;
    Config config_; std::vector<TrackedInfo> tracked_; bool project_found_ = false;
    mutable std::once_flag hierarchy_flag_; mutable std::shared_ptr<const HierarchyIndex> hierarchy_;

    const HierarchyIndex* hierarchy() const {
        std::call_once(hierarchy_flag_, [this] {
            hierarchy_ = std::make_shared<const HierarchyIndex>(HierarchyIndex::build(tracked_));
        });
        return hierarchy_.get();
    }
    static std::string page_ref_of(const std::string& name) { return std::string("\x1fnift:page:") + name; }
public:
    bool page_ref_for(const std::string& name, std::string& ref) const override {
        const HierarchyIndex* hi = hierarchy();
        if (!hi || hi->index_of(name) == HierarchyIndex::NO_PARENT) return false;
        ref = page_ref_of(name);
        return true;
    }
    bool resolve_page_member(const std::string& page_name, const std::string& member,
                             json::Document& out, std::string& error) const override {
        const HierarchyIndex* hi = hierarchy();
        if (!hi) return false;
        const std::size_t idx = hi->index_of(page_name);
        if (idx == HierarchyIndex::NO_PARENT) return false;
        const TrackedInfo& info = tracked_[idx];
        if (member == "parent") {
            const std::size_t p = hi->parent_index(idx);
            out = p == HierarchyIndex::NO_PARENT ? json::Document(nullptr) : json::Document(page_ref_of(tracked_[p].name));
            return true;
        }
        if (member == "children" || member == "ancestors" || member == "descendants" || member == "siblings") {
            std::vector<std::size_t> list;
            if (member == "children") { if (const auto* c = hi->children(idx)) list = *c; }
            else if (member == "siblings") { const std::size_t p = hi->parent_index(idx); if (p != HierarchyIndex::NO_PARENT) if (const auto* c = hi->children(p)) for (const auto ch : *c) if (ch != idx) list.push_back(ch); }
            else if (member == "ancestors") { std::vector<std::size_t> chain; std::size_t p = hi->parent_index(idx); while (p != HierarchyIndex::NO_PARENT) { chain.push_back(p); p = hi->parent_index(p); } for (auto it = chain.rbegin(); it != chain.rend(); ++it) list.push_back(*it); }
            else { std::vector<std::size_t> stack; if (const auto* c = hi->children(idx)) stack.assign(c->rbegin(), c->rend()); while (!stack.empty()) { const std::size_t cur = stack.back(); stack.pop_back(); list.push_back(cur); if (const auto* cc = hi->children(cur)) for (auto it = cc->rbegin(); it != cc->rend(); ++it) stack.push_back(*it); } }
            out = json::Document::make_array();
            out.array.reserve(list.size());
            for (const auto i : list) out.array.emplace_back(page_ref_of(tracked_[i].name));
            return true;
        }
        if (member == "name") { out = json::Document(info.name); return true; }
        if (member == "title") { out = json::Document(info.title); return true; }
        if (member == "type") { out = info.type ? json::Document(*info.type) : json::Document(nullptr); return true; }
        if (member == "path" || member == "output_path" || member == "url" || member == "content") {
            const auto source = project_read::content_path_of(root_, config_, info);
            const auto output = project_read::output_path_of(root_, config_, info);
            if (member == "path") { out = json::Document(project_read::relative_of(root_, source)); return true; }
            if (member == "output_path") { out = json::Document(project_read::relative_of(root_, output)); return true; }
            if (member == "url") { out = json::Document("/" + project_read::relative_of(root_, output).substr(config_.output_dir.size())); return true; }
            out = json::Document(filesystem::file_exists(source) ? filesystem::read_file(source) : std::string{});
            return true;
        }
        if (member == "metadata") { out = json::Document::make_object(); return true; }
        return false;
    }
};