#pragma once
#include "Types.h"
#include <filesystem>
#include <string>
#include <vector>

class ProjectInfo;

class WatchList {
public:
    std::vector<WatchDirectory> directories;

    bool load(const std::filesystem::path& project_root);
    bool save(const std::filesystem::path& project_root) const;
    bool reconcile(ProjectInfo& project);
    bool add(ProjectInfo& project, std::string directory, const WatchExtension& extension);
    bool remove(ProjectInfo& project, std::string directory);
};
