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
struct Expr {
    Kind kind=Kind::Legacy; SourceSpan span{}; std::string text, op, name; std::vector<std::string> params;
    nift::RuntimeValue literal;
    std::unique_ptr<Expr> left, right; std::vector<std::unique_ptr<Expr>> items;
};
struct Context {
    std::function<void(SourceSpan)> failure_origin;
    std::function<bool(const std::string&, SourceSpan, nift::RuntimeValue&, std::string&)> legacy_with_span;
    std::function<bool(const std::string&, nift::RuntimeValue&, std::string&)> resolve;
    std::function<bool(const std::string&, std::shared_ptr<const nift::RuntimeValue>&, std::string&)> resolve_ref;
    std::function<bool(const std::string&, nift::RuntimeValue&, std::string&)> legacy;
    std::function<bool(const std::string&, std::vector<nift::RuntimeValue>&&, nift::RuntimeValue&, std::string&)> call;
    std::function<bool(const nift::RuntimeValue&, const std::string&, std::vector<nift::RuntimeValue>&&, nift::RuntimeValue&, std::string&)> native_method;
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
    std::function<bool(const std::string&)> arg_is_location;
    // Value-only native calls never need the location probe, which may inspect
    // complex argument syntax before normal one-time argument evaluation.
    std::function<bool(const std::string&)> call_is_value_only;
    std::function<std::string(const nift::RuntimeValue&)> render;
};
enum class StmtKind { Block, Declaration, Assignment, CompoundAssignment, Increment, Expression, If, While, For, Break, Continue, Return, Function, Struct, Enum, Import, Export, Script, Legacy };
struct Stmt { StmtKind kind=StmtKind::Legacy; SourceSpan span{}; std::string text, name, op; int decl_type=0; std::unique_ptr<Expr> expr, target, condition, iterable; std::vector<std::string> params, bindings; std::string variadic_param; std::vector<std::unique_ptr<Stmt>> body; std::vector<std::unique_ptr<Expr>> call_args; };
struct StatementParseResult { std::unique_ptr<Stmt> stmt; std::string error; bool supported=false; };
struct ParseResult { std::unique_ptr<Expr> expr; std::string error; bool supported=false; };
enum class TemplateKind { Literal, Expression, Script, If, For, Fragment, Function, LegacyDirective };
struct TemplateNode { TemplateKind kind=TemplateKind::Literal; SourceSpan span{}; std::string text; std::unique_ptr<Expr> expr; std::vector<std::unique_ptr<TemplateNode>> children; };
struct TemplateParseResult { std::vector<std::unique_ptr<TemplateNode>> nodes; std::string error; bool supported=false; };
TemplateParseResult parse_template(const std::string& source);
StatementParseResult parse_statement(const std::string& source);
ParseResult parse_expression(const std::string& source);
ParseResult parse_expression_at(const std::string& source, std::size_t start, std::size_t length = std::string::npos);
void rebase(Expr& expression, std::size_t offset);
void rebase(Stmt& statement, std::size_t offset);
bool evaluate(const Expr& expr, Context& ctx, nift::RuntimeValue& out, std::string& error);
bool truthy(const nift::RuntimeValue& value);
void fold_constants(Expr& expr);
} // namespace nift::ast
