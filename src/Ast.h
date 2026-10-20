#pragma once
#include "Outcome.h"
#include "RuntimeValue.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nift::ast {
struct SourceSpan { std::size_t begin=0, end=0; };
enum class Kind { Literal, Binding, Unary, Binary, Logical, Coalesce, Index, Member, SafeIndex, SafeMember, Range, Call, Lambda, Array, Object, Legacy };
// Encoded spellings permit a cheap stale-code guard for writable internal ASTs.
enum class NumericBinaryOp : unsigned {
    Unknown=0, Add='+', Subtract='-', Multiply='*', Divide='/', Modulo='%',
    Equal='=' | ('=' << 8), NotEqual='!' | ('=' << 8),
    Less='<', LessEqual='<' | ('=' << 8), Greater='>', GreaterEqual='>' | ('=' << 8)
};
enum class LiteralRole { ObjectKeyOrSynthetic, ExpressionString, QuotedData, OutputQuoted };
struct Expr {
    LiteralRole literal_role=LiteralRole::ObjectKeyOrSynthetic;
    SourceSpan literal_token_span{};
    std::string literal_token;
    Kind kind=Kind::Legacy; NumericBinaryOp numeric_op=NumericBinaryOp::Unknown; SourceSpan span{}; std::string text, op, name; std::vector<std::string> params;
    // Manual/changed spellings retain canonical textual dispatch.
    void prepare_numeric_operator();
    NumericBinaryOp prepared_numeric_operator() const;
    nift::RuntimeValue literal;
    std::unique_ptr<Expr> left, right; std::vector<std::unique_ptr<Expr>> items;
    // Set once after parsing (annotate_completion): a literal whose token needs the
    // canonical decoder (escape or `$`), and a call routed through the canonical
    // output/presentation completion (print/err/warn, stringify/prettify/highlight).
    bool literal_needs_completion=false; bool output_completion=false;
};
struct Context {
    std::function<nift::detail::EvalOutcome<nift::RuntimeValue>(const Expr&)> literal_outcome;
    // Parser-owned admission: empty is UnsupportedBeforeEffects. An admitted
    // completion is terminal and must never fall through to original source.
    std::function<std::optional<nift::detail::EvalOutcome<nift::RuntimeValue>>(const Expr&)> completion_boundary;
    // Narrow ordinary named-call hook. Empty means declined before effects.
    std::function<std::optional<nift::detail::EvalOutcome<nift::RuntimeValue>>(const Expr&)> source_call;
    std::function<void(SourceSpan)> failure_origin;
    // Project a failed location path without evaluating any operand again.
    std::function<void(const Expr&, const Expr&, std::string&)> location_failure;
    // Component operands carry their own failure; parent paths cannot claim it.
    unsigned location_operand_depth=0;
    std::function<bool(const nift::RuntimeValue&, const std::string&, std::size_t, std::string&)> method_arity_check;
    std::function<bool(const std::string&, SourceSpan, nift::RuntimeValue&, std::string&)> legacy_with_span;
    std::function<bool(const std::string&, nift::RuntimeValue&, std::string&)> resolve;
    std::function<bool(const std::string&, std::shared_ptr<const nift::RuntimeValue>&, std::string&)> resolve_ref;
    std::function<bool(const std::string&, nift::RuntimeValue&, std::string&)> legacy;
    std::function<bool(const std::string&, std::vector<nift::RuntimeValue>&&, nift::RuntimeValue&, std::string&)> call;
    std::function<bool(const nift::RuntimeValue&, const std::string&, std::vector<nift::RuntimeValue>&&, nift::RuntimeValue&, std::string&)> native_method;
    // Optional canonical method arity validation, applied before any method
    // argument evaluation so an arity error cannot run argument side effects.
    // Optional value-based method completion: dispatches a method against an
    // already-evaluated receiver and arguments, preserving the original call
    // span for diagnostics. Empty means decline.
    std::function<bool(const nift::RuntimeValue&, const std::string&, std::vector<nift::RuntimeValue>&&, SourceSpan, const std::string&, nift::RuntimeValue&, std::string&)> method_fallback;
    std::function<nift::detail::EvalOutcome<nift::RuntimeValue>(
        const std::string&, std::vector<nift::RuntimeValue>&&)> call_outcome;
    std::function<nift::detail::EvalOutcome<nift::RuntimeValue>(
        const nift::RuntimeValue&, const std::string&,
        std::vector<nift::RuntimeValue>&&)> native_method_outcome;
    std::optional<nift::detail::Diagnostic> propagated_diagnostic;
    std::function<void(nift::detail::Diagnostic)> propagate_diagnostic;
    std::optional<nift::RuntimeValue> propagated_recoverable;
    std::function<void(nift::RuntimeValue)> propagate_recoverable;
    // Optional: given an argument expression text, report whether it denotes a
    // location reference that must keep its identity across the call boundary.
    // When set and true for any argument, the prepared Call dispatch is skipped
    // and evaluation falls back to the legacy evaluator (the oracle), which
    // performs the location-aware binding.
    std::function<bool(const std::string&, const std::string&)> arg_is_location;
    // Value-only native calls never need the location probe, which may inspect
    // complex argument syntax before normal one-time argument evaluation.
    std::function<bool(const std::string&)> call_is_value_only;
    std::function<std::string(const nift::RuntimeValue&)> render;
};
enum class StmtKind { Block, Declaration, Assignment, CompoundAssignment, Increment, Expression, If, While, For, Break, Continue, Return, Function, Struct, Enum, Import, Export, Script, Legacy };
struct Stmt { StmtKind kind=StmtKind::Legacy; SourceSpan span{}; std::string text, name, op; int decl_type=0; bool source_location_diagnostics=false; std::unique_ptr<Expr> expr, target, condition, iterable; std::vector<std::string> params, bindings; std::string variadic_param; std::vector<std::unique_ptr<Stmt>> body, else_body; std::vector<std::unique_ptr<Expr>> call_args; };
struct StatementParseResult { std::unique_ptr<Stmt> stmt; std::string error; bool supported=false; };
struct ParseResult { std::unique_ptr<Expr> expr; std::string error; bool supported=false; };
enum class TemplateKind { Literal, Expression, Script, If, For, Fragment, Function, LegacyDirective };
struct TemplateNode { TemplateKind kind=TemplateKind::Literal; SourceSpan span{}; std::string text; std::unique_ptr<Expr> expr; std::vector<std::unique_ptr<TemplateNode>> children; };
struct TemplateParseResult { std::vector<std::unique_ptr<TemplateNode>> nodes; std::string error; bool supported=false; };
TemplateParseResult parse_template(const std::string& source);
StatementParseResult parse_statement(const std::string& source);
ParseResult parse_expression(const std::string& source);
// Separate canonical pure-plan entry; ordinary preparation limits stay fixed.
ParseResult parse_literal_payload_expression(const std::string& source);
ParseResult parse_expression_at(const std::string& source, std::size_t start, std::size_t length = std::string::npos);
void rebase(Expr& expression, std::size_t offset);
void rebase(Stmt& statement, std::size_t offset);
bool evaluate(const Expr& expr, Context& ctx, nift::RuntimeValue& out, std::string& error);
bool truthy(const nift::RuntimeValue& value);
void fold_constants(Expr& expr);
} // namespace nift::ast
