#pragma once

#include "SourceView.h"
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace nift { class RuntimeValue; }
class RenderHost;

#if !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#define NIFT_PARSER_HELPER_HIDDEN __attribute__((visibility("hidden")))
#else
#define NIFT_PARSER_HELPER_HIDDEN
#endif

namespace nift::detail {

NIFT_PARSER_HELPER_HIDDEN int nift_binding_type(const nift::RuntimeValue& value);
NIFT_PARSER_HELPER_HIDDEN const char* nift_binding_type_name(int type);
NIFT_PARSER_HELPER_HIDDEN bool nift_type_assignable(int from, int to);
extern NIFT_PARSER_HELPER_HIDDEN const int kMaxCallableDepth;

NIFT_PARSER_HELPER_HIDDEN bool parse_runtime_json(const std::string& text,
                                                  nift::RuntimeValue& value,
                                                  std::string& error);
NIFT_PARSER_HELPER_HIDDEN bool call_runtime_host(
    const RenderHost& host, const std::string& name,
    const std::vector<nift::RuntimeValue>& args,
    nift::RuntimeValue& out, std::string& error);
NIFT_PARSER_HELPER_HIDDEN std::string runtime_scalar_key(const nift::RuntimeValue& value);
NIFT_PARSER_HELPER_HIDDEN bool runtime_contains_bytes(const nift::RuntimeValue& value);
NIFT_PARSER_HELPER_HIDDEN std::string runtime_bytes_string(const nift::RuntimeValue& value);
NIFT_PARSER_HELPER_HIDDEN bool nift_atomic_add_sub_checked(
    std::atomic<std::int64_t>& value, std::int64_t operand, bool subtract,
    std::int64_t& before, std::int64_t& after);
NIFT_PARSER_HELPER_HIDDEN bool nift_unix_epoch_milliseconds(
    std::int64_t& value, std::string& error);
NIFT_PARSER_HELPER_HIDDEN bool nift_secure_random_bytes(
    std::size_t count, nift::RuntimeValue& value, std::string& error);

NIFT_PARSER_HELPER_HIDDEN bool numeric_exponent_sign(const std::string& text, std::size_t sign);

NIFT_PARSER_HELPER_HIDDEN bool glob_has_magic(const std::string& value);
NIFT_PARSER_HELPER_HIDDEN bool glob_component_match(const std::string& pattern, const std::string& name);
NIFT_PARSER_HELPER_HIDDEN std::vector<std::filesystem::path> glob_expand(const std::filesystem::path& resolved_pattern);

NIFT_PARSER_HELPER_HIDDEN bool strip_presentation_chain(const std::string& text, std::string& base,
                                                        bool& pretty, bool& highlight);

NIFT_PARSER_HELPER_HIDDEN std::string unescape_parameter_string(const std::string& text);
NIFT_PARSER_HELPER_HIDDEN std::vector<std::string> parse_parameters(
    const std::string& text, bool& ok, std::vector<bool>* quoted = nullptr);
NIFT_PARSER_HELPER_HIDDEN std::vector<SourceText> parse_parameters(
    const SourceText& text, bool& ok, std::vector<bool>* quoted = nullptr);
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
    SourceView view{};
};

NIFT_PARSER_HELPER_HIDDEN ControlBlockBody normalize_control_block_body(std::string body, SourceView view = {});
inline ControlBlockBody normalize_control_block_body(const SourceText& body) {
    return normalize_control_block_body(static_cast<const std::string&>(body), body.view);
}
NIFT_PARSER_HELPER_HIDDEN std::string insertion_indent(const std::string& output);
NIFT_PARSER_HELPER_HIDDEN void append_indented(
    std::string& output, const std::string& text, const std::string& indent,
    int initial_code_block_depth = 0, bool trim_trailing_newline = true);

NIFT_PARSER_HELPER_HIDDEN std::string entity(const std::string& value, bool& ok);

}  // namespace nift::detail

#undef NIFT_PARSER_HELPER_HIDDEN
