#pragma once
// Expression compilation for the Runtime 2.0 compiler (CP11), and the
// per-unit compiler state it shares with statement compilation
// (compiler.cpp). Internal to the compiler.
//
// CP12: every instruction is compiled for the legacy evaluator that would run
// it (textual or prepared, kInstrPrepared), and every instruction that can
// fail carries the source position legacy reports for that failure in that
// context ("error sites", compiler_rules.cpp). A function reachable from both
// kinds of context is compiled twice (one Proto per variant); the variant of a
// call is fixed by the calling context, so no decision is left to run time.
#include "rt/ast.h"
#include "rt/codegen.h"
#include "rt/infer.h"
#include "rt/resolver.h"
#include "rt/text_type.h"

#include <array>
#include <deque>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace nift::rt::detail {

enum class Variant : std::uint8_t { Textual = 0, Prepared = 1 };

class UnitCompiler {
public:
    UnitCompiler(const Ast& ast, const UnitPlan& plan, const UnitKinds& kinds);

    // compiler.cpp
    // Compiles main, then every function variant a compiled call needs (and a
    // textual variant of each function no call reaches, so its body is still
    // checked), into `program`.
    bool compile_program(NodeId root, Program& program);
    const std::string& reason() const { return reason_; }
    std::uint32_t offset() const { return offset_; }

private:
    struct Loop {
        std::size_t start = 0;
        std::vector<std::size_t> breaks;
    };

    // compiler.cpp: statements
    bool compile_main(NodeId program, std::unique_ptr<Proto>& out);
    bool compile_function(std::size_t f, Variant variant, std::unique_ptr<Proto>& out);
    std::uint16_t proto_for(std::size_t f, Variant variant);
    bool begin(std::string name, NodeId body);
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
    bool reevaluated(const Node& node, NodeId value) const;
    bool declare_value(const Node& node, std::uint16_t reg);
    bool assign_value(const Node& node, const Binding& b);
    bool compound_op(const Node& node, const Binding& b, bool prepared_code);
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
    // compiler_rules.cpp: legacy context, error sites and kind checks (CP12)
    bool prepared() const { return (variant_ == Variant::Prepared || loop_depth_ > 0) && !textual_statement_; }
    void update_flags();
    // Statement-level errors (no failing expression) of a function legacy
    // runs prepared are reported at the call in the caller.
    void caller_site(std::size_t at) {
        if (variant_ == Variant::Prepared && prepared()) fb_->at(at).flags |= kInstrCallerSite;
    }
    std::uint32_t site(const Node& node) const;
    std::uint32_t prepared_begin(const Node& node) const;
    std::uint32_t prepared_end(const Node& node) const;
    bool statement_preparable(const Node& node) const;
    bool kinds_within(NodeId id, std::uint8_t allowed, const Node& at, const char* reason);
    bool no_alias(NodeId value, const Node& at);
    std::uint8_t kinds(NodeId id) const { return kinds_.of(id); }
    bool no_trailing_comma(const Node& node);
    bool temp(std::uint16_t& reg);

    bool fail(const Node& node, std::string reason);
    bool builder_ok();
    std::string_view text(const Node& node) const { return ast_.text(node); }

    const Ast& ast_;
    const UnitPlan& plan_;
    const UnitKinds& kinds_;
    std::unique_ptr<FunctionBuilder> fb_;
    // Variants: proto index per (function, variant), 0 = not yet requested.
    std::vector<std::array<std::uint16_t, 2>> variant_proto_;
    std::deque<std::pair<std::size_t, Variant>> pending_;
    std::uint16_t next_proto_ = 1;
    // Parameters whose binding type is observable in the body (assigned, or
    // named in a text whose type legacy infers): a textual call passes their
    // text-derived types (legacy bind_call_param, P5).
    std::vector<std::vector<bool>> observable_;
    // Context of the code being compiled.
    Variant variant_ = Variant::Textual;
    int loop_depth_ = 0;
    bool textual_statement_ = false;  // a statement legacy runs textually inside prepared code
    std::uint32_t return_site_ = kNoSite;  // textual `return`: errors at the statement
    std::uint32_t if_site_ = kNoSite;      // textual `if` condition: errors at the `if`
    std::uint32_t index_site_ = kNoSite;   // textual index key: errors at the index expression
    int call_args_ = 0;                    // inside user-call arguments
    int index_depth_ = 0;                  // inside index brackets
    NodeId print_path_ = kNoNode;          // the path argument of a prepared print()
    bool print_statement_ = false;         // compiling a print statement's call
    NodeId id_of(const Node& node) const { return static_cast<NodeId>(&node - &ast_.at(0)); }
    static constexpr std::uint32_t kNoSite = 0xffffffffu;
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
