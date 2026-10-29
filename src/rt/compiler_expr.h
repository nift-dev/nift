#pragma once
// Expression compilation for the Runtime 2.0 compiler (CP11), and the per-unit compiler state it shares with
// statement compilation (compiler.cpp). Internal to the compiler.
//
// CP12 (D-018): one compiled representation per function and one meaning for every expression wherever it appears:
// legacy's canonical (textual) semantics. Legacy runs loop bodies and the functions called from them on a second
// (prepared) evaluator; where a construct in such code could behave differently there, the unit is refused before
// execution ("context:*" reasons, compiler_rules.cpp), except for the approved differences (D-018: exactly-once
// evaluation, keys of print(path) evaluated once, errors reported where they occur; D-009#1/#5).
#include "rt/ast.h"
#include "rt/compiler.h"
#include "rt/codegen.h"
#include "rt/globals.h"
#include "rt/natives/builtin.h"
#include "rt/infer.h"
#include "rt/resolver.h"
#include "rt/text_type.h"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace nift::rt::detail {
class UnitCompiler {
public:
    UnitCompiler(const Ast& ast, NodeId root, const UnitPlan& plan, const UnitKinds& kinds,
                 const std::vector<bool>& loop_called, const GlobalPlan* globals = nullptr);
    // Compiles main (proto 0) and every function (proto k + 1) into `program`.
    bool compile_program(NodeId root, Program& program);
    const std::string& reason() const { return reason_; }
    std::uint32_t offset() const { return offset_; }
    // kKindUnknown (something the compiler refuses anyway) is never treated
    // as acceptable (review CP12 round 1, D1). A unit whose refusal involved
    // it is compiled a second time in probe mode, where Unknown defers as
    // before, only to report the underlying refusal reason; the probe's
    // program is discarded even if it compiles (compiler.cpp).
    bool unknown_hit() const { return unknown_hit_; }
    void probe_unknown() { probe_ = true; }
    void set_session(SessionCompileContext* session);
    void expression_result() { expression_result_ = true; }
    void set_parallel_tasks(bool enabled) { parallel_tasks_ = enabled; }
    void set_module_names(const std::vector<std::string_view>* names) { module_names_ = names; }
private:
    SessionCompileContext* session_ = nullptr;
    std::unordered_map<std::string, Binding> session_bindings_;
    bool session_function(NodeId id);
    bool session_admit();
    bool session_render(NodeId id);
    bool session_order_call_types(const Node&, NodeId, std::uint16_t, bool&);
    bool session_call_types(const Node& node, NodeId callee, std::uint16_t base);
    bool session_publish(std::string_view name, const Binding& binding);
    bool expression_result_ = false;
    struct Loop {
        std::size_t start = 0;
        std::vector<std::size_t> breaks;
        std::vector<std::size_t> continues;  // patched after the body (For) or to start (While)
        std::size_t try_depth = 0;           // CP13B: try scopes active at loop entry
    };
public:
    struct Capture {
        enum class Source : std::uint8_t { LocalCell, OuterUpvalue };
        Source source = Source::LocalCell;
        std::uint16_t index = 0;
        Binding binding;
    };
    struct LambdaTask {
        NodeId lambda = kNoNode;
        std::uint16_t proto_slot = 0;
        std::vector<std::string_view> upvalue_names;
        std::vector<NodeId> upvalue_decls;
        std::vector<bool> upvalue_loop;
        std::vector<bool> upvalue_readonly;
        std::vector<bool> upvalue_const_array;
        std::vector<Capture> captures;
    };
private:
    // compiler.cpp: statements
    bool compile_main(NodeId program, std::unique_ptr<Proto>& out);
    bool compile_function(std::size_t f, std::unique_ptr<Proto>& out);
    void compute_may_double();
    bool begin(std::string name, NodeId body, const std::unordered_set<std::string_view>* assigned = nullptr);
    // The text type of a node's type text (infer_text_type), memoised per
    // node: the unit analyses and the compiler ask for the same nodes.
    const TextType& text_type_at(NodeId id) const;
    bool finish(std::unique_ptr<Proto>& out);
    bool statements(const Node& block);
    bool statement(NodeId id);
    bool declare(const Node& node);
    bool assignment_expr(const Node& node, std::uint16_t dst);
    bool assign(const Node& node, const std::uint16_t* result = nullptr);
    bool compound_assign(const Node& node);
    bool inc_dec(const Node& node);
    bool if_chain(const Node& node);
    bool while_loop(const Node& node);
    bool try_statement(const Node& node);
    bool throw_statement(const Node& node);
    bool collection_failure(const Node&,std::uint16_t,std::string);
    int collection_method_call(const Node&,std::uint16_t);
    bool collection_call(const Node& node, std::uint16_t dst, int method);
    bool comparator_sort(const Node& node, std::uint16_t dst, const Binding& binding);
    bool array_mutation(const Node& node, std::uint16_t dst, int method);
    bool error_new(const Node& node, std::uint16_t dst);
    bool error_method_call(const Node& node, std::uint16_t dst, std::string_view name, NodeId receiver);
    bool error_member_access(const Node& node, std::uint16_t dst, NodeId receiver);
    // CP22 coroutine units (compiler_coro.cpp): Call or Spawn for function f;
    // `type(future)` and `sleep(ms)` (-1: not handled, 0 refused, 1 compiled);
    // `.done()`/`.status()` on a future (same convention); `await e`.
    OpCode call_opcode(std::size_t f) const;
    int coro_call(const Node& node, std::string_view name, const natives::BuiltinSpec* builtin, std::uint16_t dst);
    int future_method(const Node& node, std::uint16_t dst);
    bool thread_call(const Node& node, std::string_view name, std::uint16_t dst);  // CP22, else `callable:<name>`
    bool await_expr(const Node& node, std::uint16_t dst);
    // CP12C slice 2: the default `for` (compiler_loops*.cpp).
    struct ForState;
    bool for_loop(const Node& node);
    bool for_setup(ForState& state, const Node& node);
    bool for_prepare_bindings(ForState& state, const Node& node);
    bool for_step(ForState& state);
    bool for_emit_body(ForState& state);
    bool for_finish(ForState& state);
    bool for_read_source(const Binding& b, std::uint16_t& reg, bool& temp_owned);
    bool for_lazy_read(const Binding& b, std::uint16_t& reg);
    bool for_source_binding(const Node& iterable, const Binding*& binding) const;
    bool for_body_reads(const Node& body, std::string_view name, std::uint32_t& first_offset) const;
    bool for_body_writes(const Node& body, std::string_view name) const;
    bool for_body_captures(const Node& body, std::string_view name) const;
    bool for_source_may_shrink(const Node& body, const Binding* place, std::string_view name) const;
    bool for_source_may_change(const Node& body, const Binding* place, std::string_view name) const;
    bool for_declare_binding(std::string_view name, const Node& at, bool captured, KindSet decl_kinds,
                             Binding& binding);
    bool for_bind_value(const Binding& binding, std::uint16_t value, std::uint16_t type_reg, bool first,
                        std::uint16_t flag);
    bool for_emit_loop_metadata(const Binding& binding, std::uint16_t index, std::uint16_t count);
    bool for_clear_register(std::uint16_t reg);
    bool return_statement(const Node& node);
    bool assign_value(const Node& node, const Binding& b);
    bool compound_op(const Node& node, const Binding& b);
    bool atomic_update(const Node& node, const Binding& b, std::uint16_t dst, bool expression);
    bool atomic_store(const Node& node, const Binding& b);
    bool atomic_read(NodeId id, std::uint16_t source, std::uint16_t& dst);
    bool loop_jump(const Node& node);
    bool expression_statement(const Node& node);
    bool new_binding(std::string_view name, const Node& at, std::uint16_t& reg, Binding& binding);
    std::size_t emit_binding_read(OpCode op, std::uint16_t dst, std::uint16_t source);
    bool read_binding(const Binding& b, std::uint16_t& reg);
    bool binding_cell(const Binding& b, std::uint16_t& reg);
    bool indexed_binding(const Binding& b, std::uint16_t& reg);
    bool store_indexed_binding(const Binding& b, std::uint16_t value);
    bool store_binding(const Binding& b, std::uint16_t value, std::uint16_t text_type);
    // compiler_expr.cpp: expressions
    bool expr_to(NodeId id, std::uint16_t dst);
    bool expr_any(NodeId id, std::uint16_t& operand);
    bool reg_any(NodeId id, std::uint16_t& reg);
    bool constant_of(const Node& node, Value& value, bool& is_constant);
    // CP15B: lower an interpolated String node (children: StringPart/literal
    // text and expressions) into existing string operations.
    bool interpolation(const Node& node, std::uint16_t dst);
    // Conservative local scalar proof for interpolation parts. The unit kind
    // inference does not visit String children (infer*.cpp is not ours), so
    // this uses bindings, literals, operators, native result rules and simple
    // user-function returns. kKindUnknown means "not proven".
    KindSet interpolation_kind_impl(NodeId id, std::unordered_set<std::size_t>& active,
                                         bool literal_only) const;
    KindSet interpolation_kind(NodeId id, std::unordered_set<std::size_t>& active) const;
    // Literal/operator-only kinds for a callee return expression: never
    // resolves an identifier through the caller's scopes and never follows a
    // call. Used only by interpolation_function_kind (F1).
    KindSet interpolation_literal_kind(NodeId id) const;
    KindSet interpolation_function_kind(std::size_t f, std::unordered_set<std::size_t>& active) const;
    void collect_returns(NodeId id, std::vector<NodeId>& out) const;
    bool binary(const Node& node, std::uint16_t dst);
    bool logical(const Node& node, std::uint16_t dst);
    bool coalesce(const Node& node, std::uint16_t dst);
    bool ternary(const Node& node, std::uint16_t dst);
    bool unary(const Node& node, std::uint16_t dst);
    bool inc_dec_expr(const Node& node, std::uint16_t dst);
    bool call(const Node& node, std::uint16_t dst);
    bool call_value(const Node& node, NodeId callee, bool member, std::uint16_t dst);
    bool method_call(const Node& node, std::uint16_t dst);
    // CP14: a pure built-in over a fresh window (receiver: kNoNode for a free
    // function); W1, W2/W4 refusals.
    int session_native_call(const Node&, NodeId, std::string_view, std::uint16_t);
    bool builtin_arguments_ok(const Node&, const natives::BuiltinSpec&, NodeId);
    bool builtin_call(const Node& node, const natives::BuiltinSpec& spec, NodeId receiver, std::uint16_t dst);
    bool unobservable_argument(NodeId id);
    bool array_literal(const Node& node, std::uint16_t dst);
    bool object_literal(const Node& node, std::uint16_t dst);
    bool member(const Node& node, std::uint16_t dst);
    bool index(const Node& node, std::uint16_t dst);
    bool local_register(const Node& node, std::uint16_t& reg);
    // CP12C closures: lambda construction, deferred Proto compilation and
    // the static capture pass that decides which bindings need cells.
    bool lambda_value(const Node& node, std::uint16_t dst);
    std::uint32_t collection_offset(const Node&) const;
    std::uint16_t collection_diagnostic_flags(const Node&,NodeId,bool comparator=false) const;
    bool collection_callback_shape(const Node&,NodeId,std::uint32_t argc);
    bool compile_lambda_body(const Node&,const NodeId*,bool collection_view);
    bool session_lambda_definition(const LambdaTask& task, Proto& out);
    bool session_lambda_captures(NodeId id, const Node& node, std::vector<std::string_view>& names,
                                 std::vector<bool>& loop, std::vector<bool>& readonly,
                                 std::vector<bool>& const_array);
    bool lambda_upvalues(const LambdaTask& task);
    bool compile_lambda_task(const struct LambdaTask& task, std::unique_ptr<Proto>& out, bool collection_view = false);
    void prepare_captures();
    bool captured_binding(NodeId id) const { return captured_bindings_.count(id) != 0; }
    void collect_lambda_names(NodeId id, std::vector<std::string_view>& out) const;
    NodeId node_id(const Node& node) const { return static_cast<NodeId>(&node - &ast_.at(0)); }
    bool false_jump(NodeId cond, std::vector<std::size_t>& jumps);
    bool has_call(NodeId id) const;
    bool plain_postfix(const Node& node);
    bool ends_with_not(NodeId id) const;
    bool infix_left_ok(const Node& node);
    bool comparison_chain_ok(const Node& node);
    bool element_target_ok(const Node& target);
    std::string_view type_text(const Node& node) const;
    bool text_type_of(const TextType& type, const Node& at, std::uint16_t& operand);
    bool text_type_operand(NodeId value, std::uint16_t& operand);
    bool pass_param_types(std::size_t f, std::uint32_t argc, const NodeId* args, std::uint16_t base);
    // compiler_rules.cpp: legacy contexts and kind checks (CP12, D-018)
    // Code legacy runs on its prepared evaluator: a loop region of this
    // function (except statements legacy runs textually), or anywhere in a
    // function called from such code.
    bool loop_region() const { return loop_depth_ > 0 && !textual_statement_; }
    bool loop_reachable() const {  // CP12b: struct methods count as called from loops (conservative)
        return loop_region() || (!in_lambda_ && in_function_ &&
                                 (loop_called_[function_] || plan_.is_method(function_)));
    }
    void update_flags();
    // A binding type legacy derives from `type` may differ from the type of
    // the value (kinds) in scope `scope` (functions 0..n-1, main n).
    bool may_double(const TextType& type, std::size_t scope) const;
    bool type_mismatch_possible(const TextType& type, KindSet kinds, std::size_t scope) const;
    // Can the binding-type check of an assignment of a value of kinds
    // `value` to binding `b` fail? Legacy words that error differently in
    // loop code (D-018c), so such loop code is refused
    // (context:assignment-error, review round 2).
    bool binding_error_possible(const Binding& b, KindSet value) const;
    std::size_t scope_index() const { return in_function_ ? function_ : plan_.functions.size(); }
    // Where an error raised by `node` is reported: its own start (D-018c).
    std::uint32_t site(const Node& node) const { return node.begin; }
    std::uint32_t prepared_begin(const Node& node) const;
    std::uint32_t prepared_end(const Node& node) const;
    bool statement_preparable(const Node& node) const;
    bool kinds_within(NodeId id, KindSet allowed, const Node& at, const char* reason);
    // compiler_call.cpp: a member or index path in user-call arguments.
    bool argument_path(const Node& node, std::string& note);
    bool bare_argument(const Node& call, std::uint32_t k) const;
    const Node& chain_enter(const Node& node);
    bool receiver_text_wording(const Node& node, const Node& chain) const;
    bool may_fault(NodeId id) const;
    bool no_alias(NodeId value, const Node& at);
    // kKindUnknown always comes with every kind, so a "may be X" test is
    // conservative for it too (review CP12 round 1, D1).
    KindSet kinds(NodeId id) const {
        const KindSet k = kinds_.of(id);
        // A4/B3: an empty kind set inside a lambda is unresolved, not "any";
        // treat it as Unknown so no check treats it as satisfying every kind.
        if (k == 0 && in_lambda_) return static_cast<KindSet>(kKindAll | kKindUnknown);
        return (k & kKindUnknown) && !probe_ ? static_cast<KindSet>(kKindAll | kKindUnknown) : k;
    }
    // True when kinds `k` include kKindUnknown and the check must pass (probe
    // mode only); outside the probe it records that Unknown decided a refusal.
    bool unknown_defers(KindSet k) const {
        if (!(k & kKindUnknown)) return false;
        if (!probe_) unknown_hit_ = true;
        return probe_;
    }
    bool no_trailing_comma(const Node& node);
    bool temp(std::uint16_t& reg);
    bool operand_window(std::uint16_t base, std::uint32_t count);
    bool window_value(NodeId value, std::uint16_t dst);
    // CP12b globals (compiler_globals.cpp): the binding of `name` in the code
    // being compiled: a local, or (in a function) the top-level binding a
    // resolved free name denotes (Binding::global); nullptr otherwise.
    const Binding* find_binding(std::string_view name) const;
    const Binding* find_lambda_binding(std::string_view name) const;
    void note_global_declaration(const Node& node, const Binding& binding);
    bool propagate_global_doubles();
    // Reads a global's value (or type) slot into a fresh temporary.
    bool global_read(std::uint16_t slot, std::uint16_t& reg);
    bool global_read_value(std::uint16_t slot, std::uint16_t& reg, std::string_view name);
    bool global_check_defined(std::uint16_t slot, std::string_view name);
    bool print_argument_ok(NodeId id, const Node& arg);
    // A global aggregate taken out of its slot for an in-place update (so the
    // update does not copy it), and put back after.
    bool global_take(const Binding& b, std::uint16_t& reg, std::string_view name = {});
    void global_put(const Binding& b, std::uint16_t reg);
    std::size_t binding_scope(const Binding& b) const { return b.global ? plan_.functions.size() : scope_index(); }
    // CP12b structs (compiler_structs.cpp): instances live in bindings and are
    // used only as receivers of their fields and methods (no copies, so
    // legacy's references and the VM's values never differ).
    // Legacy's loop evaluator fails on struct field access (L42) in loop
    // bodies and in functions called from loops; a method's own body is
    // canonical unless it has a loop of its own.
    bool field_in_loop_code() const {
        return loop_region() || (in_function_ && !plan_.is_method(function_) && loop_called_[function_]);
    }
    int current_struct() const { return in_function_ && plan_.is_method(function_) ? plan_.method_of[function_] : -1; }
    const StructFieldInfo* own_field(std::string_view name) const;
    int struct_of_receiver(NodeId object, bool& is_this) const;
    bool receiver_slot(NodeId object, std::uint16_t dst);
    bool struct_construct(const Node& decl, int s, std::uint16_t reg);
    bool struct_call(const Node& node, int s, const StructMethodInfo& m, NodeId receiver, int receiver_reg,
                     std::uint16_t dst);
    bool struct_member(const Node& node, int s, bool is_this, std::uint16_t dst);
    bool struct_method(const Node& node, int s, std::uint16_t dst);  // `p.m(args)`, `this.m(args)`
    // A call by name of a struct (refused: only `p := Name(args)`) or, in a
    // method, of a method of its struct: 1 compiled, 0 refused, -1 neither.
    int struct_callee(const Node& node, std::string_view name, std::uint16_t dst);
    bool field_access_ok(const Node& at, int s, std::string_view name, bool is_this, const StructFieldInfo*& field);
    bool field_read(const Node& at, NodeId receiver, const StructFieldInfo& field, std::uint16_t dst);
    bool field_assign(const Node& node, NodeId receiver, const StructFieldInfo& field);
    bool field_update(const Node& node, NodeId receiver, const StructFieldInfo& field);
    // The field a compound/increment target names (`x` in a method, `this.x`,
    // `p.x`), checked (field_access_ok; a refusal sets reason_); nullptr otherwise.
    const StructFieldInfo* struct_target(const Node& target);
    NodeId target_receiver(const Node& target) const { return target.kind == NodeKind::Member ? target.a : kNoNode; }
    std::uint16_t this_reg_ = 0;  // a method's register holding its instance's stack slot
    bool fail(const Node& node, std::string reason);
    bool legacy_fatal(const Node& at, std::string message, std::uint16_t dst);  // CP25 (compiler_rules.cpp)
    bool const_write(const Node& node, const Node* value, const std::uint16_t* result, const Binding* update = nullptr);
    bool builder_ok();
    std::string_view text(const Node& node) const { return ast_.text(node); }
    bool probe_ = false;
    const GlobalPlan* globals_ = nullptr;
    const std::vector<std::string_view>* module_names_ = nullptr;  // CompileLimits::module_names
    std::unordered_map<std::string_view, Binding> global_bindings_;  // main's binding of each global
    std::unordered_map<std::string_view, Binding> top_level_bindings_;  // every direct top-level declaration
    std::unordered_set<NodeId> top_level_decl_ids_;
    std::unordered_set<NodeId> captured_bindings_;
    std::unordered_set<NodeId> selector_roots_;
    std::unordered_set<std::string_view> declared_names_;
    std::vector<LambdaTask> lambda_tasks_;
    Program* program_ = nullptr;
    mutable std::vector<std::shared_ptr<const TextType>> text_types_;  // text_type_at memo, per NodeId
    std::vector<std::unordered_set<std::string_view>> assigned_fn_;   // collect_assigned per function body
    mutable bool unknown_hit_ = false;
    bool parallel_tasks_ = false;
    const Ast& ast_;
    const UnitPlan& plan_;
    const UnitKinds& kinds_;
    std::unique_ptr<FunctionBuilder> fb_;
    NodeId root_ = kNoNode;
    const std::vector<bool>& loop_called_;  // functions called from loop code (rt/prepared.h)
    // Parameters whose binding type is observable in the body (assigned, or
    // named in a text whose type legacy infers): every call passes their
    // text-derived types (legacy bind_call_param, the canonical P5 rule).
    std::vector<std::vector<bool>> observable_;
    // Per scope (functions, then main): names whose binding type may be
    // double, so that the binding can hold a value of another type.
    std::vector<std::unordered_set<std::string_view>> may_double_;
    // Per scope: names whose binding type is always double (every
    // declaration typed double by its text): any number is assignable.
    std::vector<std::unordered_set<std::string_view>> always_double_;
    // Context of the code being compiled.
    std::size_t function_ = 0;
    int loop_depth_ = 0;
    bool textual_statement_ = false;  // a statement legacy runs textually inside loop code
    int call_args_ = 0;               // inside user-call arguments
    NodeId path_arg_ = kNoNode;       // the user-call argument being compiled (argument_path)
    NodeId path_chain_ = kNoNode;     // the next receiver of an admitted path argument
    const Node* chain_top_ = nullptr;   // the outermost member/index chain (chain_enter)
    const Node* chain_next_ = nullptr;  // its next receiver
    const Node* print_arg_ = nullptr;   // the native call argument being compiled
    std::string_view global_note_;      // full access path for checked binding reads
    std::uint32_t return_read_offset_ = kNoNode;  // active lambda return expression
    bool global_offset_set_ = false;
    std::uint32_t global_offset_ = 0;
    int index_depth_ = 0;             // inside index brackets
    Scopes scopes_;
    std::unordered_set<std::string_view> assigned_;
    std::vector<Loop> loops_;
    std::vector<int> try_scopes_;  // CP13B: loop depth at each active try entry
    // CP17 unwind release (owner decision, 2026-10-28): a block left normally
    // clears the registers it allocated, so a managed file it abandoned is
    // dropped (and reported) when the block ends, never kept alive by a stale
    // register until a later Error unwinds it silently. Only blocks that can
    // hold a handle (kKindHandle anywhere in them) emit the clears.
    struct BlockRegs {
        std::uint16_t mark = 0, high = 0, outer_high = 0;
        bool clear = false;
    };
    BlockRegs block_begin(NodeId block);
    void block_end(BlockRegs& regs);              // after the block's code: records its range
    void block_clear(const BlockRegs& regs);      // emits the clears (at each normal exit)
    bool block_holds_handles(NodeId block) const;
    std::uint32_t error_call_offset_ = kNoNode;  // `throw error(...)`: report the throw
    std::uint32_t error_call_offset_saved_ = kNoNode;

    bool in_function_ = false;
    bool in_lambda_ = false;
    int depth_ = 0;
    static constexpr int kMaxExpressionNesting = 48;
    std::string reason_;
    std::uint32_t offset_ = 0;
};

} // namespace nift::rt::detail
