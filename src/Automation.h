#pragma once
#include <string>
#include <vector>
#include <filesystem>

// v4.4 structured automation boundary. Script bindings and (eventually) CLI
// presentation delegate to ProjectInfo through these request/result types;
// no automation operation spawns the `nift` executable.
struct NiftOperationResult {
    bool ok = true;
    int exit_code = 0;
    std::vector<std::string> affected;   // page names touched by the operation
    std::vector<std::string> errors;
    double duration_seconds = 0.0;
};

struct NiftBuildRequest {
    enum class Mode { Updated, All, Names, Auto, Repair };
    Mode mode = Mode::Updated;
    std::vector<std::string> names;
    bool explain = false;
};

class ProjectInfo;

// Open the nearest Nift project (a directory containing `.nift/config.json`)
// at or above `cwd`. `project` is fully opened on success.
bool automation_open_project(const std::filesystem::path& cwd, ProjectInfo& project, std::string& error);

// Run a build through ProjectInfo directly (no subprocess).
NiftOperationResult automation_build(ProjectInfo& project, const NiftBuildRequest& request);

// Tracking operations through ProjectInfo directly.
NiftOperationResult automation_track(ProjectInfo& project, const std::string& name, const std::string& title, const std::string& template_path);
NiftOperationResult automation_untrack(ProjectInfo& project, const std::vector<std::string>& names, bool remove_files);

// Names that currently need a rebuild (status), and all tracked names.
std::vector<std::string> automation_status(ProjectInfo& project);
std::vector<std::string> automation_tracked_names(ProjectInfo& project);