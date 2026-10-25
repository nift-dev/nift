#pragma once
// Expression layer of the Runtime 2.0 script parser: the Pratt parser class
// shared by expr_parser.cpp (statements, units, operators), expr_postfix.cpp
// (postfix links and call shapes) and expr_literals.cpp (primaries, literals,
// lambdas). Internal to src/rt.
// Precedence, lowest first (legacy src/Ast.cpp and src/ParserExpression.cpp,
// confirmed by the paren oracle in tests/runtime2/rt_parser_corpus.py):
//   ?:  (right)      only inside brackets/arguments, or at the top of
//                    return/throw/import; elsewhere legacy parses it irregularly
//   ??  (right)
//   ||  (left)
//   &&  (left)
//   == != (right)
//   < <= > >= (one comparison; a chain is a legacy run-time error)
//   + - (left)
//   * / % (left)
//   prefix ! - + await
//   postfix call, .name, [index], ?.name, ?[index]
#include "rt/parser_internal.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace nift::rt::detail {

enum Prec : int { kNone = 0, kTernary, kCoalesce, kOr, kAnd, kEquality, kRelational, kAdditive, kMultiplicative };

class ExprParser {
public:
    ExprParser(Ast& ast, std::uint32_t begin, std::uint32_t end, ExprOptions options)
        : ast_(ast), src_(ast.source()), lex_(src_, begin, end), end_(end), options_(options) {
        advance();
        unit_begin_ = tok_.begin;
    }
    NodeId whole_expression();
    NodeId whole_statement();
    OpaqueReason reason() const { return why_; }

private:
    void advance() {
        prev_end_ = tok_.end;
        tok_ = lex_.next();
    }
    // Repositions the lexer at `pos` (the byte after text consumed out of band).
    void resume_at(std::uint32_t pos) {
        lex_.reset(pos);
        tok_ = lex_.next();
        prev_end_ = pos;
    }
    bool is(Tok kind) const { return tok_.kind == kind; }
    bool is_word(std::string_view word) const {
        return tok_.kind == Tok::Ident && src_.substr(tok_.begin, tok_.end - tok_.begin) == word;
    }
    NodeId fail(OpaqueReason why) {
        if (why_ == OpaqueReason::None) why_ = why;
        return kNoNode;
    }
    bool ternary_allowed() const { return nesting_ > 0 || options_.ternary_at_top; }

    NodeId parse(int min_prec);
    NodeId unit();
    NodeId unit_body();
    bool unit_end() const {
        return is(Tok::End) || is(Tok::Comma) || is(Tok::RParen) || is(Tok::RBracket) || is(Tok::RBrace);
    }
    NodeId pattern_statement(std::uint32_t begin);
    NodeId infix(NodeId left, int prec, Tok kind, std::uint32_t begin);
    NodeId prefix();
    NodeId await_expression(Token token);
    NodeId postfix(NodeId node, std::uint32_t begin);
    NodeId call(NodeId callee, std::uint32_t begin);
    bool callable_shape(NodeId callee, std::uint32_t last_dot, bool empty_call) const;
    bool receiver_ok(std::uint32_t begin, std::uint32_t end) const;
    NodeId primary();
    NodeId primary_name(Token token);
    NodeId string_literal(Token token);
    NodeId sequence(NodeKind kind, Tok close, std::uint32_t begin, bool allow_spread);
    NodeId object(std::uint32_t begin);
    NodeId lambda_if_any(std::uint32_t begin, bool async);
    NodeId lambda_body(NodeId lambda);
    NodeId lambda_assignment(NodeId left);
    bool skip_group();
    NodeId leaf(NodeKind kind, Token token) { return ast_.add(kind, token.begin, token.end); }
    NodeId node1(NodeKind kind, std::uint32_t begin, NodeId a);
    NodeId node2(NodeKind kind, Op op, std::uint32_t begin, NodeId a, NodeId b);

    Ast& ast_;
    std::string_view src_;
    Lexer lex_;
    std::uint32_t end_;
    ExprOptions options_;
    Token tok_{};
    int nesting_ = 0;
    OpaqueReason why_ = OpaqueReason::None;
    // End of the last consumed token: node spans end here, so a node whose
    // last operand was parenthesised covers the closing parenthesis.
    std::uint32_t prev_end_ = 0;
    // First byte of the evaluation unit being parsed (see unit()).
    std::uint32_t unit_begin_ = 0;
    // What the last primary() was: a plain name (postfix chains allowed) or
    // anything else (literal, collection, parenthesised expression, lambda).
    bool name_base_ = false;
    // The last primary() was an array or object literal (member and index
    // links are regular on it, calls only as known or presentation methods).
    bool collection_base_ = false;
};

} // namespace nift::rt::detail
