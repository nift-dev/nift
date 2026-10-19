#pragma once
// Expression compilation for the Runtime 2.0 compiler (CP11), and the
// per-unit compiler state it shares with statement compilation
// (compiler.cpp). Internal to the compiler.
//
// CP12 (D-018): one compiled representation per function and one meaning for
// every expression wherever it appears: legacy's canonical (textual)
// semantics. Legacy runs loop bodies and the functions called from them on a
// second (prepared) evaluator; where a construct in such code could behave
// differently there, the unit is refused before execution
// ("context:*" reasons, compiler_rules.cpp), except for the approved
// differences (D-018: exactly-once evaluation, keys of print(path) evaluated
// once, errors reported where they occur; D-009#1/#5).
#include "rt/ast.h"
#include "rt/codegen.h"
#include "rt/natives/builtin.h"
#include "rt/infer.h"
#include "rt/resolver.h"
#include "rt/text_type.h"


#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace nift::rt::detail {

class UnitCompiler {
public:
    UnitCompiler(const Ast& ast, NodeId root, const UnitPlan& plan, const UnitKinds& kinds,
                 const std::vector<bool>& loop_called);

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

private:
    struct Loop {
        std::size_t start = 0;
        std::vector<std::size_t> breaks;
    };

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
    bool assign(const Node& node);
    bool compound_assign(const Node& node);
    bool inc_dec(const Node& node);
    bool if_chain(const Node& node);
    bool while_loop(const Node& node);
    bool return_statement(const Node& node);
    bool assign_value(const Node& node, const Binding& b);
    bool compound_op(const Node& node, const Binding& b);
    bool loop_jump(const Node& node);
    bool expression_statement(const Node& node);
    bool new_binding(std::string_view name, const Node& at, std::uint16_t& reg, Binding& binding);

    // compiler_expr.cpp: expressions
    bool expr_to(NodeId id, std::uint16_t dst);
    bool expr_any(NodeId id, std::uint16_t& operand);
    bool reg_any(NodeId id, std::uint16_t& reg);
    bool constant_of(const Node& node, Value& value, bool& is_constant);
    bool binary(const Node& node, std::uint16_t dst);
    bool logical(const Node& node, std::uint16_t dst);
    bool coalesce(const Node& node, std::uint16_t dst);
    bool ternary(const Node& node, std::uint16_t dst);
    bool unary(const Node& node, std::uint16_t dst);
    bool call(const Node& node, std::uint16_t dst);
    bool method_call(const Node& node, std::uint16_t dst);
    // CP14: a pure built-in over a fresh window (receiver: kNoNode for a free
    // function); W1, W2/W4 refusals.
    bool builtin_call(const Node& node, const natives::BuiltinSpec& spec, NodeId receiver, std::uint16_t dst);
    bool unobservable_argument(NodeId id);
    bool array_literal(const Node& node, std::uint16_t dst);
    bool object_literal(const Node& node, std::uint16_t dst);
    bool member(const Node& node, std::uint16_t dst);
    bool index(const Node& node, std::uint16_t dst);
    bool local_register(const Node& node, std::uint16_t& reg);
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
    bool loop_reachable() const { return loop_region() || (in_function_ && loop_called_[function_]); }
    void update_flags();
    // A binding type legacy derives from `type` may differ from the type of
    // the value (kinds) in scope `scope` (functions 0..n-1, main n).
    bool may_double(const TextType& type, std::size_t scope) const;
    bool type_mismatch_possible(const TextType& type, std::uint8_t kinds, std::size_t scope) const;
    // Can the binding-type check of an assignment of a value of kinds
    // `value` to binding `b` fail? Legacy words that error differently in
    // loop code (D-018c), so such loop code is refused
    // (context:assignment-error, review round 2).
    bool binding_error_possible(const Binding& b, std::uint8_t value) const;
    std::size_t scope_index() const { return in_function_ ? function_ : plan_.functions.size(); }
    // Where an error raised by `node` is reported: its own start (D-018c).
    std::uint32_t site(const Node& node) const { return node.begin; }
    std::uint32_t prepared_begin(const Node& node) const;
    std::uint32_t prepared_end(const Node& node) const;
    bool statement_preparable(const Node& node) const;
    bool kinds_within(NodeId id, std::uint8_t allowed, const Node& at, const char* reason);
    bool no_alias(NodeId value, const Node& at);
    // kKindUnknown always comes with every kind, so a "may be X" test is
    // conservative for it too (review CP12 round 1, D1).
    std::uint8_t kinds(NodeId id) const {
        const std::uint8_t k = kinds_.of(id);
        return (k & kKindUnknown) && !probe_ ? static_cast<std::uint8_t>(kKindAll | kKindUnknown) : k;
    }
    // True when kinds `k` include kKindUnknown and the check must pass (probe
    // mode only); outside the probe it records that Unknown decided a refusal.
    bool unknown_defers(std::uint8_t k) const {
        if (!(k & kKindUnknown)) return false;
        if (!probe_) unknown_hit_ = true;
        return probe_;
    }
    bool no_trailing_comma(const Node& node);
    bool temp(std::uint16_t& reg);

    bool fail(const Node& node, std::string reason);
    bool builder_ok();
    std::string_view text(const Node& node) const { return ast_.text(node); }

    bool probe_ = false;
    mutable std::vector<std::shared_ptr<const TextType>> text_types_;  // text_type_at memo, per NodeId
    std::vector<std::unordered_set<std::string_view>> assigned_fn_;   // collect_assigned per function body
    mutable bool unknown_hit_ = false;
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
    int index_depth_ = 0;             // inside index brackets
    Scopes scopes_;
    std::unordered_set<std::string_view> assigned_;
    std::vector<Loop> loops_;
    bool in_function_ = false;
    int depth_ = 0;
    static constexpr int kMaxExpressionNesting = 48;
    std::string reason_;
    std::uint32_t offset_ = 0;
};

} // namespace nift::rt::detail
