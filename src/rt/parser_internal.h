#pragma once
// Internal interface between the statement layer (statement_parser.h) and the
// expression layer (expr_parser.h): modes, entry points, recursion bounds.
// Not part of the rt/ public surface.
#include "rt/ast.h"
#include "rt/lexer.h"
#include "rt/parser.h"
#include "rt/source_scan.h"

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

// Expression-layer entry points. Each returns kNoNode when the text is not a
// complete, regular expression/statement; the caller then emits Opaque.
struct ExprOptions {
    bool ternary_at_top = false;  // legacy parses `a ? b : c` regularly here
    // `if`/`else if`/`while` condition: legacy's condition evaluator reads
    // `=>` as an assignment, splits at comparisons inside braces and before
    // `??`, so lambdas, objects, `??` and `?:` are Opaque there.
    bool condition = false;
};
NodeId parse_expression_text(Ast& ast, std::uint32_t begin, std::uint32_t end, ExprOptions options,
                             OpaqueReason& why);
NodeId parse_statement_text(Ast& ast, std::uint32_t begin, std::uint32_t end, OpaqueReason& why);

NodeId make_opaque(Ast& ast, std::uint32_t begin, std::uint32_t end, OpaqueReason reason);

// Recursion bounds. Nesting beyond them becomes Opaque(NestingLimit): the unit
// falls back to legacy (D-001), which then translates and reports it itself.
// Legacy has no parse-time depth diagnostic, so none is invented here.
//  - Blocks: 256 keeps the whole parse (blocks plus the expression bound) well
//    inside a 1 MB stack (the Windows main thread), also unoptimised and
//    under ASan; tests/rt run it with `ulimit -s 1024`.
//  - Expressions: legacy's evaluator fails at depth 97 ("expression nesting
//    exceeds parser limit", ParserExpression.cpp:1879), one level per nested
//    operand, argument, element, prefix operator or postfix receiver. The
//    counter here grows at least as fast (each parse() level, prefix operator
//    and postfix link), and 64 leaves a margin for the levels legacy adds
//    around a unit (statement wrappers, call frames).
inline constexpr int kMaxBlockDepth = 256;
inline constexpr int kMaxExpressionDepth = 64;

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
