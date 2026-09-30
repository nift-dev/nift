#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace nift { class RuntimeValue; }

#if defined(__GNUC__) || defined(__clang__)
#define NIFT_PARSER_HELPER_HIDDEN __attribute__((visibility("hidden")))
#else
#define NIFT_PARSER_HELPER_HIDDEN
#endif

namespace nift::detail {

NIFT_PARSER_HELPER_HIDDEN bool numeric_exponent_sign(const std::string& text, std::size_t sign);

NIFT_PARSER_HELPER_HIDDEN bool glob_has_magic(const std::string& value);
NIFT_PARSER_HELPER_HIDDEN bool glob_component_match(const std::string& pattern, const std::string& name);
NIFT_PARSER_HELPER_HIDDEN std::vector<std::filesystem::path> glob_expand(const std::filesystem::path& resolved_pattern);

NIFT_PARSER_HELPER_HIDDEN bool strip_presentation_chain(const std::string& text, std::string& base,
                                                        bool& pretty, bool& highlight);

NIFT_PARSER_HELPER_HIDDEN std::string unescape_parameter_string(const std::string& text);
NIFT_PARSER_HELPER_HIDDEN std::vector<std::string> parse_parameters(
    const std::string& text, bool& ok, std::vector<bool>* quoted = nullptr);
NIFT_PARSER_HELPER_HIDDEN bool parse_callable_parameters(const std::string& text,
                                                         std::vector<std::string>& params,
                                                         std::string& variadic_param);
NIFT_PARSER_HELPER_HIDDEN bool reserved_binding_name(const std::string& name);
NIFT_PARSER_HELPER_HIDDEN bool built_in_metadata_name(const std::string& name);

NIFT_PARSER_HELPER_HIDDEN bool parse_for_collection_clause(
    const std::string& clause, std::string& collection_expression,
    std::string& sort_expression, bool& descending, std::string& error);
NIFT_PARSER_HELPER_HIDDEN std::shared_ptr<const nift::RuntimeValue> make_loop_metadata(
    std::size_t index, std::size_t length);
NIFT_PARSER_HELPER_HIDDEN bool sortable_scalar(const nift::RuntimeValue& value);
NIFT_PARSER_HELPER_HIDDEN int compare_sort_keys(const nift::RuntimeValue& left,
                                                const nift::RuntimeValue& right);

struct NIFT_PARSER_HELPER_HIDDEN ControlBlockBody {
    std::string text;
    bool multiline = false;
};

NIFT_PARSER_HELPER_HIDDEN ControlBlockBody normalize_control_block_body(std::string body);
NIFT_PARSER_HELPER_HIDDEN std::string insertion_indent(const std::string& output);
NIFT_PARSER_HELPER_HIDDEN void append_indented(
    std::string& output, const std::string& text, const std::string& indent,
    int initial_code_block_depth = 0, bool trim_trailing_newline = true);

NIFT_PARSER_HELPER_HIDDEN std::string entity(const std::string& value, bool& ok);

}  // namespace nift::detail

#undef NIFT_PARSER_HELPER_HIDDEN
