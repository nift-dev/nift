#pragma once
// Internal interface between the statement layer (parser.cpp) and the
// expression layer (expr_parser.cpp). Not part of the rt/ public surface.
#include "rt/ast.h"
#include "rt/lexer.h"
#include "rt/parser.h"

#include <cstdint>
#include <vector>

namespace nift::rt::detail {

// How statement-structure errors are treated inside a region.
//  Strict:   the error is a parse-time diagnostic for the whole script (legacy
//            translates these regions before running anything).
//  Deferred: legacy translates the region only when it runs (lambda block
//            bodies, struct methods); the caller turns the region Opaque.
enum class Mode : std::uint8_t { Strict, Deferred };

// Parses the statements of src[begin, end) into `out`. Returns false and sets
// `error` on a statement-structure error.
bool parse_block(Ast& ast, std::uint32_t begin, std::uint32_t end, Mode mode,
                 std::vector<NodeId>& out, ParseError& error);

// Legacy Parser::find_balanced on src[open, end): quote, backtick-run, @/*,
// <!-- and @// aware. Sets `close` to the matching close character.
bool find_balanced(std::string_view src, std::uint32_t open, std::uint32_t end,
                   char open_char, char close_char, std::uint32_t& close);

bool is_space(char c);
std::uint32_t skip_space(std::string_view src, std::uint32_t p, std::uint32_t end);
bool valid_identifier(std::string_view text);
std::string_view trim(std::string_view text);

// Expression-layer entry points. Each returns kNoNode when the text is not a
// complete, regular expression/statement; the caller then emits Opaque.
struct ExprOptions {
    bool ternary_at_top = false;  // legacy parses `a ? b : c` regularly here
};
NodeId parse_expression_text(Ast& ast, std::uint32_t begin, std::uint32_t end, ExprOptions options,
                             OpaqueReason& why);
NodeId parse_statement_text(Ast& ast, std::uint32_t begin, std::uint32_t end, OpaqueReason& why);

NodeId make_opaque(Ast& ast, std::uint32_t begin, std::uint32_t end, OpaqueReason reason);

// Recursion bounds. Nesting beyond them becomes Opaque(NestingLimit): the unit
// falls back to legacy (D-001), which then translates and reports it itself.
// Legacy has no parse-time depth diagnostic (its translator overflows the
// stack near 10,000 nested blocks; its evaluator reports "expression nesting
// exceeds parser limit" at run time), so none is invented here.
inline constexpr int kMaxBlockDepth = 4096;
inline constexpr int kMaxExpressionDepth = 200;

// Depth counters shared by every parser instance on the thread (string
// interpolation and lambda bodies start nested parsers).
struct DepthGuard {
    DepthGuard(int& counter, int limit) : counter_(counter), exceeded(++counter > limit) {}
    ~DepthGuard() { --counter_; }
    DepthGuard(const DepthGuard&) = delete;
    DepthGuard& operator=(const DepthGuard&) = delete;
    int& counter_;
    const bool exceeded;
};
int& block_depth();
int& expression_depth();

} // namespace nift::rt::detail
