#pragma once
#include "Json.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nift::ast {
struct SourceSpan { std::size_t begin=0, end=0; };
enum class Kind { Literal, Binding, Unary, Binary, Logical, Coalesce, Index, Member, SafeIndex, SafeMember, Range, Call, Lambda, Array, Object, Legacy };
struct Expr {
    Kind kind=Kind::Legacy; SourceSpan span{}; std::string text, op, name; std::vector<std::string> params;
    json::Document literal;
    std::unique_ptr<Expr> left, right; std::vector<std::unique_ptr<Expr>> items;
};
struct Context {
    std::function<bool(const std::string&, json::Document&, std::string&)> resolve;
    std::function<bool(const std::string&, std::shared_ptr<const json::Document>&, std::string&)> resolve_ref;
    std::function<bool(const std::string&, json::Document&, std::string&)> legacy;
    std::function<std::string(const json::Document&)> render;
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
bool evaluate(const Expr& expr, Context& ctx, json::Document& out, std::string& error);
bool truthy(const json::Document& value);
void fold_constants(Expr& expr);
} // namespace nift::ast
