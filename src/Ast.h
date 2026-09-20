#pragma once
#include "Json.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace nift::ast {
struct SourceSpan { std::size_t begin=0, end=0; };
enum class Kind { Literal, Binding, Unary, Binary, Logical, Index, Member, Range, Call, Legacy };
struct Expr {
    Kind kind=Kind::Legacy; SourceSpan span{}; std::string text, op, name;
    json::Document literal;
    std::unique_ptr<Expr> left, right;
};
struct Context {
    std::function<bool(const std::string&, json::Document&, std::string&)> resolve;
    std::function<bool(const std::string&, json::Document&, std::string&)> legacy;
    std::function<std::string(const json::Document&)> render;
};
struct ParseResult { std::unique_ptr<Expr> expr; std::string error; bool supported=false; };
ParseResult parse_expression(const std::string& source);
bool evaluate(const Expr& expr, Context& ctx, json::Document& out, std::string& error);
bool truthy(const json::Document& value);
} // namespace nift::ast
