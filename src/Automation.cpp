#include "Automation.h"
#include "ProjectInfo.h"
#include "ProjectOwnership.h"
#include "FileSystem.h"
#include "Console.h"
#include <chrono>

namespace fs = std::filesystem;

bool automation_open_project(const fs::path& cwd, ProjectInfo& project, std::string& error) {
    fs::path root = fs::absolute(cwd).lexically_normal();
    while (true) {
        if (filesystem::path_exists(root / ".nift/config.json")) {
            project.root = root;
            if (!project.open()) { error = "cannot open Nift project at " + root.generic_string(); return false; }
            return true;
        }
        fs::path parent = root.parent_path();
        if (parent == root) break;
        root = parent;
    }
    error = "not a Nift project (no .nift/config.json found from " + fs::absolute(cwd).generic_string() + ")";
    return false;
}

NiftOperationResult automation_build(ProjectInfo& project, const NiftBuildRequest& request) {
    NiftOperationResult r;
    const auto start = std::chrono::steady_clock::now();
    int code = 1;
    console::ScopedQuietBuild quiet;
    switch (request.mode) {
        case NiftBuildRequest::Mode::Updated: code = project.build_all(false, request.explain); break;
        case NiftBuildRequest::Mode::All:     code = project.build_all(true, request.explain); break;
        case NiftBuildRequest::Mode::Repair:  code = project.build_all(true, request.explain, /*repair=*/true); break;
        case NiftBuildRequest::Mode::Names:   code = project.build_names(request.names, true, request.explain); break;
        case NiftBuildRequest::Mode::Auto:
            r.errors.push_back("auto watch build is not available through the synchronous script automation API");
            r.ok = false;
            break;
    }
    r.exit_code = code;
    r.ok = code == 0;
    r.affected = project.last_build_affected;
    r.duration_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return r;
}

NiftOperationResult automation_track(ProjectInfo& project, const std::string& name, const std::string& title, const std::string& template_path) {
    NiftOperationResult r;
    if (project.find(name)) { r.ok = false; r.exit_code = 1; r.errors.push_back("already tracking name '" + name + "'"); return r; }
    TrackedInfo info;
    info.name = name;
    info.title = title;
    info.template_path = template_path;
    if (project.conflicts_with_tracked_path(info)) { r.ok = false; r.exit_code = 1; r.errors.push_back("name resolves to a path already managed by another tracked name"); return r; }
    const fs::path content = project.content_path(info);
    if (!filesystem::path_exists(content) && !filesystem::write_file(content, "")) { r.ok = false; r.exit_code = 1; r.errors.push_back("could not create tracked content file"); return r; }
    project.tracked.push_back(std::move(info));
    project.invalidate_tracked_index();
    r.ok = project.save_tracking();
    r.exit_code = r.ok ? 0 : 1;
    if (!r.ok) r.errors.push_back("could not write .nift/tracked.json");
    if (r.ok) r.affected.push_back(name);
    return r;
}

NiftOperationResult automation_untrack(ProjectInfo& project, const std::vector<std::string>& names, bool remove_files) {
    NiftOperationResult r;
    ProjectOwnership ownership(project.root / ".nift/.unfinished");
    const ProjectOwnership::State state = ownership.acquire();
    if (state == ProjectOwnership::State::Live) { r.ok = false; r.exit_code = 1; r.errors.push_back("another build appears to be running; refusing to mutate build state"); return r; }
    if (state == ProjectOwnership::State::Failed) { r.ok = false; r.exit_code = 1; r.errors.push_back("could not acquire the build lock"); return r; }
    if (state == ProjectOwnership::State::Stale) { r.ok = false; r.exit_code = 1; r.errors.push_back("unfinished build detected; run build --repair before changing tracking"); return r; }
    for (const auto& name : names) {
        auto it = std::find_if(project.tracked.begin(), project.tracked.end(), [&](const TrackedInfo& info) { return info.name == name; });
        if (it == project.tracked.end()) continue;
        const TrackedInfo value = *it;
        if (remove_files) {
            filesystem::remove_owned_file(project.content_path(value));
            filesystem::remove_owned_file(project.output_path(value));
            filesystem::remove_owned_file(project.info_path(value));
            filesystem::remove_owned_file(filesystem::hash_file_path(project.root, project.content_path(value)));
        }
        project.tracked.erase(it);
        project.invalidate_tracked_index();
        r.affected.push_back(name);
    }
    const bool ok = project.save_tracking();
    if (ok) ownership.finish();
    r.ok = ok; r.exit_code = ok ? 0 : 1;
    if (!ok) r.errors.push_back("could not write .nift/tracked.json");
    return r;
}

std::vector<std::string> automation_status(ProjectInfo& project) {
    std::vector<std::string> out;
    for (auto& info : project.tracked) {
        std::string reason;
        if (project.needs_build(info, &reason)) out.push_back(info.name);
    }
    return out;
}

std::vector<std::string> automation_tracked_names(ProjectInfo& project) {
    std::vector<std::string> out;
    for (const auto& info : project.tracked) out.push_back(info.name);
    return out;
}