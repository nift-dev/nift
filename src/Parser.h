#pragma once
#include "Types.h"
#include <filesystem>
#include <vector>
#include <string>

class ProjectInfo;

class Parser {
public:
    Parser(ProjectInfo& project, TrackedInfo& tracked_info);
    RenderResult render();

private:
    ProjectInfo& project_;
    TrackedInfo& tracked_info_;
    std::vector<std::filesystem::path> input_stack_;
    RenderResult result_;

    RenderResult parse(const std::string& source, const std::filesystem::path& source_path, int depth);
    std::string metadata(const std::string& key) const;
    std::string path_to(const std::string& argument);
    void fail(const std::filesystem::path& source_path, const std::string& source, std::size_t offset, const std::string& message);
};
