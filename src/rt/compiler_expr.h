#pragma once
// Expression compilation for the Runtime 2.0 compiler (CP11), and the
// per-unit compiler state it shares with statement compilation
// (compiler.cpp). Internal to the compiler.
#include "rt/ast.h"
#include "rt/codegen.h"
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
    UnitCompiler(const Ast& ast, const UnitPlan& plan) : ast_(ast), plan_(plan) {}

    // compiler.cpp
    bool compile_main(NodeId program, std::unique_ptr<Proto>& out);
    bool compile_function(NodeId fn, std::unique_ptr<Proto>& out);
    const std::string& reason() const { return reason_; }
    std::uint32_t offset() const { return offset_; }

private:
    struct Loop {
        std::size_t start = 0;
        std::vector<std::size_t> breaks;
    };

    // compiler.cpp: statements
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
    bool param_check(NodeId arg, std::uint16_t reg);
    bool no_trailing_comma(const Node& node);
    bool temp(std::uint16_t& reg);

    bool fail(const Node& node, std::string reason);
    bool builder_ok();
    std::string_view text(const Node& node) const { return ast_.text(node); }

    const Ast& ast_;
    const UnitPlan& plan_;
    std::unique_ptr<FunctionBuilder> fb_;
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
