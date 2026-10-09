#pragma once
#include "Types.h"
#include "SourceView.h"
#include "RenderHost.h"
#include <filesystem>
#include <optional>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <functional>
#include "Proc.h"
#include "Ast.h"

namespace json { class Document; }
class PackageTransaction;

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

class ExecutionOutput {
public:
    explicit ExecutionOutput(bool capture = false) : capture_(capture) {}
    void write_stdout(const std::string& text);
    void write_stderr(const std::string& text);
    void write_warning(const std::string& text);
    std::string stdout_text() const;
    std::string stderr_text() const;
    std::string warning_text() const;

private:
    bool capture_ = false;
    mutable std::mutex mutex_;
    std::string stdout_text_;
    std::string stderr_text_;
    std::string warning_text_;
};

class Parser {
    friend struct SourceTranslationTestAccess;
public:
    // Shared filesystem type-inspection result backing exists/is_file/is_dir/stat.
    // One metadata query (std::filesystem::status) derives exists + type; size is
    // filled only for regular files. Following symlinks is the consistent default
    // for all four predicates (matching std::filesystem::status).
    struct FsInfo {
        bool exists = false;
        bool error = false;
        std::string type;
        std::uint64_t size = 0;
        std::string error_message;
    };
    FsInfo inspect_path(const std::filesystem::path& path) const;
    using TimerClock = std::function<std::chrono::steady_clock::time_point()>;
    Parser(RenderHost& host, TrackedInfo& tracked_info,
           std::shared_ptr<ExecutionOutput> execution_output = {},
           TimerClock timer_clock = {});
    ~Parser();
    RenderResult render();

    // Native script hosts used by direct scripts / the Nift shell.
    RenderResult run_script(const std::string& source, const std::filesystem::path& source_path, bool project_build = false);
    bool run_embedded_script(const std::string& source, const std::filesystem::path& source_path, nift::RuntimeValue& value, std::string& error, nift::detail::Diagnostic* diagnostic = nullptr);
    RenderResult run_statement(const std::string& source, const std::filesystem::path& source_path);
    void reset_script_control();
    bool finalize_script_resources(std::string& error);
    void finalize_execution_workers();

    // Host-owned invocation identity exposed to standalone scripts/shells as
    // immutable `cmd` + `args`. A declaration may intentionally shadow these
    // root bindings; assignment to the injected bindings is rejected.
    void set_script_invocation(std::string cmd, std::vector<std::string> args);
    const std::string& script_cmd() const { return script_cmd_; }
    const std::vector<std::string>& script_args() const { return script_args_; }
    bool invoke_ffi_callback_i64(const std::string& callable_tag, std::int64_t arg, std::int64_t& out, std::string& error);

    // Parser-reported statement state for the interactive shell: the REPL asks
    // the parser whether the accumulated input is a complete statement, an
    // incomplete prefix that needs more input, or a balanced-but-invalid form.
    enum class StatementState { Complete, Incomplete, Invalid };
    StatementState statement_state(const std::string& source) const;

    // Live parser-context completion candidates for the interactive shell:
    // user/imported callables, structs and current variable bindings matching
    // the prefix. Builtin/PATH/filesystem completion lives in the CLI shell.
    std::vector<std::string> shell_completions(const std::string& prefix) const;

    // Shared single-expression host used by `nift eval`; identical evaluator to templates/scripts.
    bool eval_expression(const std::string& expression, nift::RuntimeValue& value, std::string& error, nift::detail::Diagnostic* diagnostic = nullptr);
    bool invoke_callable(const std::string& name, const std::vector<nift::RuntimeValue>& args, nift::RuntimeValue& value, std::string& error, nift::detail::Diagnostic* diagnostic = nullptr);
    bool contains_timer_resource(const nift::RuntimeValue& value) const;
    bool contains_error_resource(const nift::RuntimeValue& value) const;
    std::uint64_t begin_timer_operation() const { return next_timer_instance_id_; }
    void finish_timer_operation(std::uint64_t checkpoint);
    void rollback_timer_operation(std::uint64_t checkpoint);
    std::uint64_t begin_file_operation() const { return next_file_instance_id_; }
    void rollback_file_operation(std::uint64_t checkpoint);
    std::size_t timer_instance_count() const { return timer_instances_.size(); }
    void set_execution_output(std::shared_ptr<ExecutionOutput> output) { execution_output_ = std::move(output); }

    // Render a scalar/string value for output or command arguments.
    std::string render_expression_value(const nift::RuntimeValue& value) const;

    // Shared template+page composition: parse template_source, let @content
    // pull page_source, and (when require_exactly_one_content) enforce the
    // exactly-one-@content rule. The CLI's render() and the embedded Engine
    // both go through this path.
    RenderResult render_composed(const RenderSource& template_source,
                                 const std::optional<RenderSource>& page_source,
                                 bool require_exactly_one_content);

private:
    // Consume a callable's return into `out`, propagating any location that the
    // return carried so a following declaration can rebind to the same location.
    void consume_return(nift::RuntimeValue& out) {
        last_call_return_loc_root_ = pending_control_.ref_root_slot;
        last_call_return_loc_path_ = pending_control_.ref_path;
        out = pending_control_.value ? std::move(*pending_control_.value) : nift::RuntimeValue(nullptr);
        pending_control_ = {};
    }
    RenderHost& host_;
    std::shared_ptr<ExecutionOutput> execution_output_;
    std::optional<RenderSource> page_source_;
    TrackedInfo& tracked_info_;
    std::vector<std::filesystem::path> input_stack_;
    RenderResult result_;
    std::optional<nift::detail::Diagnostic> active_diagnostic_;
    nift::detail::SourceView expression_failure_view_;
    std::optional<nift::RuntimeValue> active_recoverable_;
    int code_block_depth_ = 0;
    int html_comment_depth_ = 0;
    std::unordered_map<std::string, std::shared_ptr<const nift::RuntimeValue>> json_bindings_;
    std::unordered_map<std::string, std::shared_ptr<const nift::RuntimeValue>> contract_bindings_;
    std::vector<std::vector<std::string>> json_binding_scopes_;
    struct PathComponent {
        enum class Kind { Index, Key };
        Kind kind = Kind::Index;
        std::size_t index = 0;
        std::string key;
        static PathComponent at(std::size_t i) { PathComponent p; p.index=i; return p; }
        static PathComponent member(std::string k) { PathComponent p; p.kind=Kind::Key; p.key=std::move(k); return p; }
        bool operator==(const PathComponent& o) const { return kind==o.kind && (kind==Kind::Index ? index==o.index : key==o.key); }
    };
    struct VariableBinding {
        std::shared_ptr<nift::RuntimeValue> value;
        int type = 0;
        bool mutable_binding = true;
        bool deep_readonly = false;
        bool is_script_invocation = false;
        std::shared_ptr<std::shared_ptr<nift::RuntimeValue>> slot;
        // A nested aggregate binding is a logical location reference.  It owns
        // the root binding slot plus a parsed key/index path; it never owns a
        // pointer into vector<Document>, so parent reallocation cannot dangle it.
        std::shared_ptr<std::shared_ptr<nift::RuntimeValue>> ref_root_slot;
        std::vector<PathComponent> ref_path;
        bool ref_valid = true;
        VariableBinding() : slot(std::make_shared<std::shared_ptr<nift::RuntimeValue>>(value)) {}
        VariableBinding(std::shared_ptr<nift::RuntimeValue> v, int t, bool m, bool d)
            : value(std::move(v)), type(t), mutable_binding(m), deep_readonly(d), is_script_invocation(false), slot(std::make_shared<std::shared_ptr<nift::RuntimeValue>>(value)) {}
        bool is_location_ref() const { return static_cast<bool>(ref_root_slot); }
        nift::RuntimeValue* resolve_location() const {
            if(!ref_root_slot || !*ref_root_slot) return nullptr;
            nift::RuntimeValue* cur=ref_root_slot->get();
            for(const auto& p:ref_path){
                if(p.kind==PathComponent::Kind::Index){
                    if(!cur->is_array() || p.index>=cur->array.size()) return nullptr;
                    cur=&cur->array[p.index];
                } else {
                    cur=cur->find_member(p.key);
                    if(!cur) return nullptr;
                }
            }
            return cur;
        }
        void sync() {
            if(ref_root_slot){
                auto root=*ref_root_slot; nift::RuntimeValue* p=resolve_location(); ref_valid=(p!=nullptr);
                value=(p&&root)?std::shared_ptr<nift::RuntimeValue>(root,p):std::shared_ptr<nift::RuntimeValue>{};
            } else if(slot) value=*slot;
        }
        void rebind(std::shared_ptr<nift::RuntimeValue> v) { ref_root_slot.reset(); ref_path.clear(); ref_valid=true; value=std::move(v); if(slot)*slot=value; }
    };
    static void copy_capture_bindings(
        std::unordered_map<std::string, VariableBinding>& destination,
        const std::unordered_map<std::string, VariableBinding>& source);
    std::vector<std::unordered_map<std::string, VariableBinding>> variable_scopes_;
    // Bounded syntax-only cache. No values, binding descriptors or source views.
    std::unordered_map<std::string,std::shared_ptr<const nift::ast::Expr>> large_string_plans_;
    std::size_t large_string_plan_bytes_=0;

    enum class SourceProvenance { FileBacked, InMemory };
    // Prepared calls retain an immutable defining path. Other contexts own their
    // path directly; diagnostic origin and semantic authority remain distinct.
    struct SourceContext {
        std::filesystem::path owned_path;
        std::shared_ptr<const std::filesystem::path> cached_path;
        SourceProvenance provenance = SourceProvenance::FileBacked;
        nift::detail::SourceView view{};
        SourceContext() = default;
        SourceContext(const std::filesystem::path& path, SourceProvenance origin,
                      nift::detail::SourceView source_view = {})
            : owned_path(path),
              provenance(origin), view(std::move(source_view)) {}
        SourceContext(std::shared_ptr<const std::filesystem::path> path, SourceProvenance origin,
                      nift::detail::SourceView source_view)
            : cached_path(std::move(path)), provenance(origin), view(std::move(source_view)) {}
        const std::filesystem::path& path() const {
            return cached_path ? *cached_path : owned_path;
        }
    };
    struct ResourcePathAuthority {
        std::filesystem::path project_root;
        std::filesystem::path filesystem_root;
        bool enforce_project_root = true;
        bool enforce_filesystem_root = false;
    };
    struct PackageProvenance {
        std::filesystem::path project_root;
        std::filesystem::path package_root;
        std::string name;
        std::string source;   // canonical/resolved source identity
        std::string commit;   // exact commit or "local"
    };
    struct ModuleEnv;
    struct Callable { std::vector<std::string> params; std::string variadic_param; std::string body; std::filesystem::path source_path; bool fragment = false; bool async = false; std::shared_ptr<ModuleEnv> module_env; SourceProvenance source_provenance = SourceProvenance::FileBacked; nift::detail::SourceView body_view{}; };
    struct ModuleEnv {
        std::uint64_t identity = 0;
        std::filesystem::path source_path;
        SourceProvenance source_provenance = SourceProvenance::FileBacked;
        std::filesystem::path import_base;
        std::filesystem::path package_root;
        std::shared_ptr<const PackageProvenance> package_provenance;
        std::unordered_map<std::string, Callable> callables;
        std::unordered_map<std::string, VariableBinding> vars;
    };
    struct LexicalEnvironmentState {
        std::shared_ptr<ModuleEnv> module_env;
        bool active = false;
    };
    struct SavedLexicalScopes { std::vector<std::unordered_map<std::string, VariableBinding>> scopes; std::shared_ptr<ModuleEnv> module_env; };
    std::vector<SavedLexicalScopes> saved_lexical_scopes_;
    std::unordered_map<std::string, Callable> callables_;
    // Prepared AST bodies for user callables, cached on first prepared call.
    struct PreparedCallable { std::shared_ptr<const std::filesystem::path> source_path; nift::detail::SourceView view; bool ready=false; std::vector<std::unique_ptr<nift::ast::Stmt>> stmts; };
    std::unordered_map<const Callable*, PreparedCallable> prepared_callables_;
    std::shared_ptr<ModuleEnv> active_module_env_;
    std::shared_ptr<ModuleEnv> loading_module_env_;
    std::unordered_map<std::uint64_t, std::shared_ptr<ModuleEnv>> module_envs_;
    std::uint64_t next_module_identity_ = 1;
    std::vector<SourceContext> source_context_stack_;
    std::vector<std::filesystem::path> source_path_stack_; // Compatibility view for prepared import ownership.
    ResourcePathAuthority resource_path_authority_;
    struct LambdaInstance {
        std::vector<std::string> params;
        std::string variadic_param;
        std::string body;
        bool block = false;
        bool async = false;
        std::filesystem::path source_path;
        SourceProvenance source_provenance = SourceProvenance::FileBacked;
        // Published descriptors are immutable; invocation frames hydrate copies.
        std::shared_ptr<const std::unordered_map<std::string, VariableBinding>> captures;
        std::shared_ptr<ModuleEnv> module_env;
        nift::detail::SourceView body_view{};
    };
    std::unordered_map<std::string, std::shared_ptr<LambdaInstance>> lambda_instances_;
    std::weak_ptr<const std::unordered_map<std::string, VariableBinding>> last_capture_snapshot_;
    std::shared_ptr<const std::unordered_map<std::string, VariableBinding>> snapshot_callback_captures();
    std::uint64_t next_lambda_instance_id_ = 1;
    std::uint64_t next_runtime_temporary_id_ = 1;
    struct ThreadInstance {
        std::thread worker;
        mutable std::mutex mutex;
        mutable std::mutex join_mutex;
        bool joined = false;
        nift::detail::WorkerCompletion<nift::RuntimeValue> completion;
        ~ThreadInstance(){ std::lock_guard<std::mutex> guard(join_mutex); if(worker.joinable()) worker.join(); }
    };
    std::unordered_map<std::string, std::shared_ptr<ThreadInstance>> thread_instances_;
    std::vector<std::shared_ptr<ThreadInstance>> owned_thread_instances_;
    std::uint64_t next_thread_instance_id_ = 1;
    struct MutexInstance {
        mutable std::mutex state_mutex;
        std::condition_variable cv;
        bool locked = false;
        std::thread::id owner;
        nift::RuntimeValue value;
    };
    std::unordered_map<std::string, std::shared_ptr<MutexInstance>> mutex_instances_;
    struct AtomicInstance {
        enum class Kind { Int, Bool };
        Kind kind = Kind::Int;
        std::atomic<std::int64_t> int_value{0};
        std::atomic<bool> bool_value{false};
    };
    std::unordered_map<std::string, std::shared_ptr<AtomicInstance>> atomic_instances_;
    struct AsyncInstance {
        mutable std::mutex mutex;
        std::condition_variable cv;
        nift::detail::WorkerCompletion<nift::RuntimeValue> completion;
    };
    std::unordered_map<std::string, std::shared_ptr<AsyncInstance>> async_instances_;
    std::vector<std::shared_ptr<AsyncInstance>> owned_async_instances_;
    std::vector<std::function<void()>> deferred_worker_cleanups_;
    struct TimerInstance {
        enum class State { Stopped, Running, Paused };
        State state = State::Stopped;
        std::chrono::milliseconds elapsed{0};
        std::chrono::steady_clock::time_point started{};
    };
    TimerClock timer_clock_;
    const std::uint64_t timer_owner_id_;
    std::unordered_map<std::uint64_t, std::shared_ptr<TimerInstance>> timer_instances_;
    std::uint64_t next_timer_instance_id_ = 1;
    bool make_timer(const std::vector<nift::RuntimeValue>& args, nift::RuntimeValue& out, std::string& error);
    bool call_timer_method(const nift::RuntimeValue& receiver, const std::string& method,
                           const std::vector<nift::RuntimeValue>& args,
                           nift::RuntimeValue& out, std::string& error);
    bool callable_contains_timer_resource(const nift::RuntimeValue& callable) const;
    struct PreparedFilesystemOperand {
        std::string text;
        std::size_t begin=0, length=0;
        bool quoted=false;
        std::shared_ptr<const nift::ast::Expr> pure_plan;
    };
    static std::shared_ptr<const nift::ast::Expr> prepare_pure_string_plan(const std::string& source, bool large=false);
    bool evaluate_pure_string_plan(const nift::ast::Expr& expression, nift::RuntimeValue& out);
    struct PreparedFilesystemOperation {
        enum class Kind { Stat, Copy, Move, FileMethod, FileFactory };
        Kind kind=Kind::Stat;
        std::string receiver;
        std::string method;
        std::vector<PreparedFilesystemOperand> operands;
    };
    // Syntax-only canonical recipes, bounded by a conservative owned-memory
    // allowance. No values, bindings, cwd or source-view owners are retained.
    std::unordered_map<std::string,std::shared_ptr<const PreparedFilesystemOperation>> canonical_file_plans_;
    std::size_t canonical_file_plan_bytes_=0;
    static bool known_file_method(std::string_view method);
    bool execute_file_method(const nift::RuntimeValue& base,const std::string& method,std::size_t operand_count,
        const std::function<bool(std::size_t,nift::RuntimeValue&)>& operand,
        const std::function<bool(const std::string&,nift::RuntimeValue&)>& evaluate_token,
        nift::RuntimeValue& out,std::string& error,bool& handled);
    bool execute_filesystem_operation(PreparedFilesystemOperation::Kind kind, std::size_t operand_count,
        const std::function<bool(std::size_t,nift::RuntimeValue&)>& operand,
        nift::RuntimeValue& out, std::string& error);
    std::shared_ptr<const PreparedFilesystemOperation> prepare_filesystem_operation(const std::string& source) const;
    bool evaluate_filesystem_expression(const std::string& expression, nift::RuntimeValue& value, std::string& error, nift::detail::SourceView view, const PreparedFilesystemOperation* recipe);
    bool evaluate_expression_impl(const std::string& expression, nift::RuntimeValue& value, std::string& error, nift::detail::SourceView view, const PreparedFilesystemOperation* recipe=nullptr);
    struct FfiLibraryInstance {
        void* handle = nullptr;
        bool closed = false;
        ~FfiLibraryInstance();
    };
    std::unordered_map<std::string, std::shared_ptr<FfiLibraryInstance>> ffi_libraries_;
    std::unordered_map<std::string, void*> ffi_pointers_;
    std::uint64_t next_ffi_library_id_ = 1;
    std::uint64_t next_ffi_pointer_id_ = 1;
    struct FfiBufferInstance { std::vector<unsigned char> bytes; };
    std::unordered_map<std::string, std::shared_ptr<FfiBufferInstance>> ffi_buffers_;
    std::unordered_map<std::string, std::string> ffi_callbacks_i64_;
    std::uint64_t next_ffi_buffer_id_ = 1;
    std::uint64_t next_ffi_callback_id_ = 1;

    enum class CollectionKind { Stack, Queue, PriQue, Map, SortedMap, Set, SortedSet };
    struct CollectionInstance {
        CollectionKind kind = CollectionKind::Stack;
        std::vector<nift::RuntimeValue> values;
        std::vector<std::pair<nift::RuntimeValue, nift::RuntimeValue>> entries;
        // O(1) membership index for Set/SortedSet scalar values. The key is
        // canonical for plain scalars (bool/number/non-marker string), so key
        // presence implies structural equality; marked references (struct/
        // collection/callable) use the empty key and fall back to the linear
        // scan. has_huge_int tracks any StrNumber so a Number add never misses
        // a numerically-equal big-integer member (Number vs StrNumber equality).
        std::unordered_set<std::string> scalar_keys;
        // Scalar-key -> vector position for Map/SortedMap, keyed by the same
        // runtime_scalar_key canon as scalar_keys. Locates the entries-vector
        // entry in O(1) for get/contains/replacement while the ordered vector
        // remains authoritative. Callers must re-confirm structural equality
        // at the located position: canonical keys can be over-broad (every
        // NaN fingerprints identically), so a mismatch falls back to the
        // linear scan. Rebuilt after erase and after sorted_map re-sorts.
        std::unordered_map<std::string, std::size_t> scalar_positions;
        bool has_huge_int = false;
    };
    std::unordered_map<std::string, std::shared_ptr<CollectionInstance>> collection_instances_;
    std::uint64_t next_collection_instance_id_ = 1;
    struct StreamInstance {
        enum class Kind { Input, Output };
        Kind kind = Kind::Input;
        std::shared_ptr<std::ifstream> input;
        std::shared_ptr<std::ofstream> output;
        std::filesystem::path path;
        bool open = false;
        bool closed = false;
    };
    std::unordered_map<std::string, std::shared_ptr<StreamInstance>> stream_instances_;
    std::uint64_t next_stream_instance_id_ = 1;
    struct FileInstance {
        std::filesystem::path path;
        std::string mode, working, saved;
        std::size_t cursor = 0;
        bool open = false, dirty = false, existed_at_open = false;
        // A clean saved state aliases working without a second owned buffer.
        bool saved_is_working = false;
        void prepare_mutation(){if(saved_is_working){saved=working;saved_is_working=false;}}
        void update_dirty(){dirty=!saved_is_working&&working!=saved;if(!dirty){saved_is_working=true;saved.clear();}}
    };
    std::unordered_map<std::string, std::shared_ptr<FileInstance>> file_instances_;
    std::uint64_t next_file_instance_id_ = 1;
    bool make_file_value(std::filesystem::path path, nift::RuntimeValue& out, std::string& error);
    struct CommandInstance { std::vector<ProcessSpec> stages; };
    std::unordered_map<std::string, std::shared_ptr<CommandInstance>> command_instances_;
    std::uint64_t next_command_instance_id_ = 1;
    // Lazily opened project/build automation context for native script bindings.
    std::shared_ptr<class ProjectInfo> automation_project_;
    std::filesystem::path automation_root_;
    struct StructField { std::string name; std::string initializer; bool private_member = false; nift::detail::SourceView initializer_view{}; };
    struct StructMethod { Callable callable; bool private_member = false; bool constructor = false; };
    struct StructDefinition { std::string name; std::vector<StructField> fields; std::unordered_map<std::string, StructMethod> methods; std::shared_ptr<ModuleEnv> module_env; };
    struct StructInstance {
        std::string type_name;
        std::unordered_map<std::string, VariableBinding> fields;
        // The StructDefinition an instance was created from (owned by its
        // defining module when applicable). Dispatch resolves the definition
        // through the instance so module-local and cross-package facades work
        // without relying on the global name registry, which would collide
        // when two modules define the same type name.
        std::shared_ptr<StructDefinition> definition;
    };
    const StructDefinition* struct_definition_for(const std::shared_ptr<StructInstance>& inst) const;
    std::unordered_map<std::string, StructDefinition> structs_;
    std::unordered_map<std::string, std::shared_ptr<StructInstance>> struct_instances_;
    std::uint64_t next_struct_instance_id_ = 1;
    std::vector<std::shared_ptr<StructInstance>> receiver_stack_;
    bool invoke_struct_method(std::shared_ptr<StructInstance> instance, const StructMethod& method, const std::vector<nift::RuntimeValue>& args, const std::vector<std::string>& arg_sources, nift::RuntimeValue& out, std::string& error);
    int nift_binding_type_from_text(const std::string& source, const nift::RuntimeValue& value) const;
    int expression_type(const std::string& source) const;
    bool reference_would_cycle(const std::string& target_id, const std::string& container_id) const;
    bool last_expression_mutation_ = false;
    // Location of the most recent call result that returned a location ref, so
    // a declaration `c := f(loc)` can rebind c to the same root+path location.
    std::shared_ptr<std::shared_ptr<nift::RuntimeValue>> last_call_return_loc_root_;
    std::vector<PathComponent> last_call_return_loc_path_;
    int function_call_depth_ = 0;
    bool in_fragment_body_ = false;
    bool in_import_program_ = false;
    bool standalone_script_host_ = false;
    bool strict_script_mode_ = false;
    std::string script_cmd_ = "<repl>";
    std::vector<std::string> script_args_;
    void install_script_invocation_bindings();
    std::vector<std::string> requested_exports_;
    enum class ControlFlow { None, Return, Break, Continue };
    struct PendingControl {
        ControlFlow kind = ControlFlow::None;
        std::shared_ptr<nift::RuntimeValue> value;
        // Location carried by a `return <location>` so the returned reference
        // keeps root+path identity across the call boundary instead of being
        // reduced to a value copy.
        std::shared_ptr<std::shared_ptr<nift::RuntimeValue>> ref_root_slot;
        std::vector<PathComponent> ref_path;
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

    RenderResult parse(const std::string& source, const std::filesystem::path& source_path, int depth,
                       SourceProvenance source_provenance, nift::detail::SourceView view = {});
    RenderResult parse(const std::string& source, const std::filesystem::path& source_path, int depth);
    bool translate_function_program(const std::string& source, std::string& translated, std::string& error,
        nift::detail::SourceView input_view = {}, nift::detail::SourceView* output_view = nullptr,
        nift::detail::DiagnosticOrigin* failure_origin = nullptr) const;
    RenderResult execute_native_program(const std::string& source, const std::filesystem::path& source_path, int depth,
                                         SourceProvenance source_provenance, bool rollback_files_on_failure = true, nift::detail::SourceView view = {});
    bool execute_import_file(const std::string& argument, const std::filesystem::path& caller_path, int depth, bool legacy_syntax, std::string& error);
    std::string metadata(const std::string& key) const;
    bool json_value(const std::string& expression, std::string& value, std::string& error);
    bool interpolate_parameter(const std::string& parameter,
                               std::string& resolved,
                               std::string& error);
    bool resolve_json_value(const std::string& expression,
                            std::shared_ptr<const nift::RuntimeValue>& value,
                            std::string& error);
    bool evaluate_expression(const std::string& expression, nift::RuntimeValue& value, std::string& error, nift::detail::SourceView view = {});
    bool evaluate_expression(const nift::detail::SourceText& expression, nift::RuntimeValue& value, std::string& error) {
        return evaluate_expression(static_cast<const std::string&>(expression), value, error, expression.view);
    }
    bool resolve_owned_resource_path(const std::string& name,
                                     const std::vector<nift::RuntimeValue>& args,
                                     nift::RuntimeValue& value,
                                     std::string& error) const;
    bool acquire_package_read(const std::shared_ptr<const PackageProvenance>& provenance,
                              std::unique_ptr<PackageTransaction>& reader,
                              std::string& error) const;
    bool evaluate_collection_value(const std::string& expression, nift::RuntimeValue& value, std::string& error);
    bool evaluate_condition(const std::string& expression, bool& value, std::string& error);
    bool serialize_value(const nift::RuntimeValue& value, bool pretty, std::string& output, std::string& error, int depth = 0, bool reject_errors = false) const;
    bool resolve_pagination_value(const std::string& expression, std::shared_ptr<const nift::RuntimeValue>& value) const;
    std::string path_to_page(std::size_t page);
    bool scalar_literal(const std::string& text, nift::RuntimeValue& value, std::string& error) const;
    std::string trim_copy(const std::string& text) const;
    nift::detail::SourceText trim_copy(const nift::detail::SourceText& text) const { return text.trimmed(); }
    void push_json_scope();
    void pop_json_scope();
    void push_variable_scope();
    void pop_variable_scope();
    LexicalEnvironmentState enter_lexical_environment(std::shared_ptr<ModuleEnv> module_env);
    void leave_lexical_environment(LexicalEnvironmentState state);
    std::string named_callable_tag(const std::string& name, const std::shared_ptr<ModuleEnv>& owner) const;
    bool resolve_named_callable(const std::string& tag, const Callable*& callable, std::shared_ptr<ModuleEnv>& owner) const;
    struct WorkerCloneMemo {
        std::unordered_map<const nift::RuntimeValue*, std::shared_ptr<nift::RuntimeValue>> values;
        std::unordered_map<const std::shared_ptr<nift::RuntimeValue>*, std::shared_ptr<std::shared_ptr<nift::RuntimeValue>>> slots;
    };
    std::shared_ptr<nift::RuntimeValue> clone_worker_value(const std::shared_ptr<nift::RuntimeValue>& value,
                                                          WorkerCloneMemo& memo) const;
    std::shared_ptr<std::shared_ptr<nift::RuntimeValue>> clone_worker_slot(
        const std::shared_ptr<std::shared_ptr<nift::RuntimeValue>>& slot, WorkerCloneMemo& memo) const;
    VariableBinding clone_worker_binding(const VariableBinding& binding, WorkerCloneMemo& memo) const;
    void clone_worker_module_graph(std::unordered_map<std::string, Callable>& callables,
                                   std::unordered_map<std::uint64_t, std::shared_ptr<ModuleEnv>>& modules,
                                   WorkerCloneMemo& memo) const;
    bool set_named_callable_async(const std::string& tag, bool async);
    bool find_balanced(const std::string& source,
                       std::size_t open_position,
                       char open_char,
                       char close_char,
                       std::size_t& close_position) const;
    std::string path_to(const std::string& argument, const std::string& directive);
    void fail(const std::filesystem::path& source_path, const std::string& source, std::size_t offset, const std::string& message);
    bool fail_recoverable(nift::detail::DiagnosticCode code, std::string message, std::string& error);
    bool fail_fatal(nift::detail::DiagnosticCode code, std::string message, std::string& error);
    bool stream_open(std::shared_ptr<StreamInstance> stream, const std::filesystem::path& path, std::string& error);
    bool stream_close(std::shared_ptr<StreamInstance> stream, std::string& error);
    enum class StreamExtraction { Ok, Eof, Conversion, Backend };
    bool stream_write_value(std::shared_ptr<StreamInstance> stream, const nift::RuntimeValue& value,
                            const std::string& label, std::string& error);
    StreamExtraction stream_extract_token(StreamInstance& stream, nift::RuntimeValue& destination,
                                          std::string& error);
    void append_diagnostic_frame(nift::detail::DiagnosticFrameKind kind,
                                 std::string label,
                                 std::string compatibility_prefix = {});
};
