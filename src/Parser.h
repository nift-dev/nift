#pragma once
#include "Types.h"
#include <filesystem>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

class ProjectInfo;
namespace json { class Document; }

class Parser {
public:
    Parser(ProjectInfo& project, TrackedInfo& tracked_info);
    RenderResult render();

private:
    ProjectInfo& project_;
    TrackedInfo& tracked_info_;
    std::vector<std::filesystem::path> input_stack_;
    RenderResult result_;
    int code_block_depth_ = 0;
    int html_comment_depth_ = 0;
    std::unordered_map<std::string, std::shared_ptr<const json::Document>> json_bindings_;
    std::vector<std::vector<std::string>> json_binding_scopes_;

    RenderResult parse(const std::string& source, const std::filesystem::path& source_path, int depth);
    std::string metadata(const std::string& key) const;
    bool json_value(const std::string& expression, std::string& value, std::string& error) const;
    bool interpolate_parameter(const std::string& parameter,
                               std::string& resolved,
                               std::string& error) const;
    bool resolve_json_value(const std::string& expression,
                            std::shared_ptr<const json::Document>& value,
                            std::string& error) const;
    bool evaluate_condition(const std::string& expression, bool& value, std::string& error) const;
    bool scalar_literal(const std::string& text, json::Document& value, std::string& error) const;
    std::string trim_copy(const std::string& text) const;
    void push_json_scope();
    void pop_json_scope();
    bool find_balanced(const std::string& source,
                       std::size_t open_position,
                       char open_char,
                       char close_char,
                       std::size_t& close_position) const;
    std::string path_to(const std::string& argument);
    void fail(const std::filesystem::path& source_path, const std::string& source, std::size_t offset, const std::string& message);
};
