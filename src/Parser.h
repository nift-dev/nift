#pragma once
#include "Types.h"
#include "RenderHost.h"
#include <filesystem>
#include <optional>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <fstream>
#include "Process.h"

namespace json { class Document; }

// Internal source of a template or page within a render. Either filesystem
// backed (path is non-empty; content is read through the host) or in-memory
// (path empty; text holds the content and logical_name provides an identity
// for diagnostics and, later, relative @input resolution).
struct RenderSource {
    std::filesystem::path path;   // non-empty => filesystem-backed source
    std::string text;             // in-memory content when path is empty
    std::string logical_name;     // logical identity/base for text sources
    std::string dependency;       // dependency spelling recorded for path sources
};

class Parser {
public:
    Parser(RenderHost& host, TrackedInfo& tracked_info);
    RenderResult render();

    // Native script hosts used by `nift run` / `nift sh`.
    RenderResult run_script(const std::string& source, const std::filesystem::path& source_path);
    RenderResult run_statement(const std::string& source, const std::filesystem::path& source_path);
    void reset_script_control();
    bool finalize_script_resources(std::string& error);

    // Parser-reported statement state for the interactive shell: the REPL asks
    // the parser whether the accumulated input is a complete statement, an
    // incomplete prefix that needs more input, or a balanced-but-invalid form.
    enum class StatementState { Complete, Incomplete, Invalid };
    StatementState statement_state(const std::string& source) const;

    // Shared single-expression host used by `nift eval`; identical evaluator to templates/scripts.
    bool eval_expression(const std::string& expression, json::Document& value, std::string& error);

    // Shared template+page composition: parse template_source, let @content
    // pull page_source, and (when require_exactly_one_content) enforce the
    // exactly-one-@content rule. The CLI's render() and the embedded Engine
    // both go through this path.
    RenderResult render_composed(const RenderSource& template_source,
                                 const std::optional<RenderSource>& page_source,
                                 bool require_exactly_one_content);

private:
    RenderHost& host_;
    std::optional<RenderSource> page_source_;
    TrackedInfo& tracked_info_;
    std::vector<std::filesystem::path> input_stack_;
    RenderResult result_;
    int code_block_depth_ = 0;
    int html_comment_depth_ = 0;
    std::unordered_map<std::string, std::shared_ptr<const json::Document>> json_bindings_;
    std::unordered_map<std::string, std::shared_ptr<const json::Document>> contract_bindings_;
    std::vector<std::vector<std::string>> json_binding_scopes_;
    struct VariableBinding {
        std::shared_ptr<json::Document> value;
        int type = 0;
        bool mutable_binding = true;
        bool deep_readonly = false;
        std::shared_ptr<std::shared_ptr<json::Document>> slot;
        VariableBinding() : slot(std::make_shared<std::shared_ptr<json::Document>>(value)) {}
        VariableBinding(std::shared_ptr<json::Document> v, int t, bool m, bool d)
            : value(std::move(v)), type(t), mutable_binding(m), deep_readonly(d), slot(std::make_shared<std::shared_ptr<json::Document>>(value)) {}
        void sync() { if (slot) value = *slot; }
        void rebind(std::shared_ptr<json::Document> v) { value=std::move(v); if(slot)*slot=value; }
    };
    std::vector<std::unordered_map<std::string, VariableBinding>> variable_scopes_;
    struct Callable { std::vector<std::string> params; std::string variadic_param; std::string body; std::filesystem::path source_path; bool fragment = false; };
    std::unordered_map<std::string, Callable> callables_;
    struct LambdaInstance {
        std::vector<std::string> params;
        std::string variadic_param;
        std::string body;
        bool block = false;
        std::filesystem::path source_path;
        std::unordered_map<std::string, VariableBinding> captures;
    };
    std::unordered_map<std::string, std::shared_ptr<LambdaInstance>> lambda_instances_;
    std::uint64_t next_lambda_instance_id_ = 1;

    enum class CollectionKind { Stack, Queue, PriQue, Map, SortedMap, Set, SortedSet };
    struct CollectionInstance {
        CollectionKind kind = CollectionKind::Stack;
        std::vector<json::Document> values;
        std::vector<std::pair<json::Document, json::Document>> entries;
    };
    std::unordered_map<std::string, std::shared_ptr<CollectionInstance>> collection_instances_;
    std::uint64_t next_collection_instance_id_ = 1;
    struct StreamInstance {
        enum class Kind { Input, Output };
        Kind kind = Kind::Input;
        std::shared_ptr<std::ifstream> input;
        std::shared_ptr<std::ofstream> output;
        bool closed = false;
    };
    std::unordered_map<std::string, std::shared_ptr<StreamInstance>> stream_instances_;
    std::uint64_t next_stream_instance_id_ = 1;
    struct FileInstance {
        std::filesystem::path path;
        std::string mode, working, saved;
        std::size_t cursor = 0;
        bool open = false, dirty = false, existed_at_open = false;
    };
    std::unordered_map<std::string, std::shared_ptr<FileInstance>> file_instances_;
    std::uint64_t next_file_instance_id_ = 1;
    struct CommandInstance { std::vector<ProcessSpec> stages; };
    std::unordered_map<std::string, std::shared_ptr<CommandInstance>> command_instances_;
    std::uint64_t next_command_instance_id_ = 1;
    struct StructField { std::string name; std::string initializer; bool private_member = false; };
    struct StructMethod { Callable callable; bool private_member = false; bool constructor = false; };
    struct StructDefinition { std::string name; std::vector<StructField> fields; std::unordered_map<std::string, StructMethod> methods; };
    struct StructInstance { std::string type_name; std::unordered_map<std::string, VariableBinding> fields; };
    std::unordered_map<std::string, StructDefinition> structs_;
    std::unordered_map<std::string, std::shared_ptr<StructInstance>> struct_instances_;
    std::uint64_t next_struct_instance_id_ = 1;
    std::vector<std::shared_ptr<StructInstance>> receiver_stack_;
    bool invoke_struct_method(std::shared_ptr<StructInstance> instance, const StructMethod& method, const std::vector<json::Document>& args, const std::vector<std::string>& arg_sources, json::Document& out, std::string& error);
    int nift_binding_type_from_text(const std::string& source, const json::Document& value) const;
    int expression_type(const std::string& source) const;
    bool reference_would_cycle(const std::string& target_id, const std::string& container_id) const;
    bool last_expression_mutation_ = false;
    int function_call_depth_ = 0;
    bool in_fragment_body_ = false;
    bool in_import_program_ = false;
    bool standalone_script_host_ = false;
    bool strict_script_mode_ = false;
    std::vector<std::string> requested_exports_;
    enum class ControlFlow { None, Return, Break, Continue };
    struct PendingControl {
        ControlFlow kind = ControlFlow::None;
        std::shared_ptr<json::Document> value;
    };
    PendingControl pending_control_;
    int callable_call_depth_ = 0;
    int loop_depth_ = 0;
    bool pagination_collecting_ = false;
    bool pagination_context_active_ = false;
    std::size_t pagination_current_ = 1;
    std::size_t pagination_total_ = 1;
    std::string pagination_items_text_;
    std::filesystem::path pagination_current_output_;

    RenderResult parse(const std::string& source, const std::filesystem::path& source_path, int depth);
    bool translate_function_program(const std::string& source, std::string& translated, std::string& error) const;
    RenderResult execute_native_program(const std::string& source, const std::filesystem::path& source_path, int depth);
    bool execute_import_file(const std::string& argument, const std::filesystem::path& caller_path, int depth, std::string& error);
    std::string metadata(const std::string& key) const;
    bool json_value(const std::string& expression, std::string& value, std::string& error);
    bool interpolate_parameter(const std::string& parameter,
                               std::string& resolved,
                               std::string& error);
    bool resolve_json_value(const std::string& expression,
                            std::shared_ptr<const json::Document>& value,
                            std::string& error);
    bool evaluate_expression(const std::string& expression, json::Document& value, std::string& error);
    bool evaluate_collection_value(const std::string& expression, json::Document& value, std::string& error);
    bool evaluate_condition(const std::string& expression, bool& value, std::string& error);
    std::string render_expression_value(const json::Document& value) const;
    bool serialize_value(const json::Document& value, bool pretty, std::string& output, std::string& error, int depth = 0) const;
    bool resolve_pagination_value(const std::string& expression, std::shared_ptr<const json::Document>& value) const;
    std::string path_to_page(std::size_t page);
    bool scalar_literal(const std::string& text, json::Document& value, std::string& error) const;
    std::string trim_copy(const std::string& text) const;
    void push_json_scope();
    void pop_json_scope();
    void push_variable_scope();
    void pop_variable_scope();
    bool find_balanced(const std::string& source,
                       std::size_t open_position,
                       char open_char,
                       char close_char,
                       std::size_t& close_position) const;
    std::string path_to(const std::string& argument, const std::string& directive);
    void fail(const std::filesystem::path& source_path, const std::string& source, std::size_t offset, const std::string& message);
};
