#include "rt/lexer.h"

namespace nift::rt {

const char* token_name(Tok kind) {
    switch (kind) {
    case Tok::End: return "end"; case Tok::Bad: return "bad"; case Tok::Ident: return "identifier";
    case Tok::Number: return "number"; case Tok::String: return "string";
    case Tok::LParen: return "("; case Tok::RParen: return ")"; case Tok::LBracket: return "[";
    case Tok::RBracket: return "]"; case Tok::LBrace: return "{"; case Tok::RBrace: return "}";
    case Tok::Comma: return ","; case Tok::Colon: return ":"; case Tok::Semicolon: return ";";
    case Tok::Dot: return "."; case Tok::Ellipsis: return "..."; case Tok::Question: return "?";
    case Tok::QuestionDot: return "?."; case Tok::QuestionBracket: return "?["; case Tok::Coalesce: return "??";
    case Tok::Arrow: return "=>"; case Tok::Declare: return ":="; case Tok::Assign: return "=";
    case Tok::AddAssign: return "+="; case Tok::SubAssign: return "-="; case Tok::MulAssign: return "*=";
    case Tok::DivAssign: return "/="; case Tok::ModAssign: return "%="; case Tok::AndAssign: return "&=";
    case Tok::OrAssign: return "|="; case Tok::XorAssign: return "^="; case Tok::Increment: return "++";
    case Tok::Decrement: return "--";
    case Tok::Eq: return "==";  // rt-lint: allow-string-compare display name, nothing is compared
    case Tok::Ne: return "!=";  // rt-lint: allow-string-compare display name, nothing is compared
    case Tok::Lt: return "<"; case Tok::Le: return "<="; case Tok::Gt: return ">"; case Tok::Ge: return ">=";
    case Tok::AndAnd: return "&&"; case Tok::OrOr: return "||"; case Tok::Bang: return "!";
    case Tok::Plus: return "+"; case Tok::Minus: return "-"; case Tok::Star: return "*";
    case Tok::Slash: return "/"; case Tok::Percent: return "%"; case Tok::Dollar: return "$";
    case Tok::At: return "@"; case Tok::Hash: return "#"; case Tok::Other: return "other";
    }
    return "?";
}

Token Lexer::peek() {
    if (!has_peek_) { peeked_ = scan(); has_peek_ = true; }
    return peeked_;
}

Token Lexer::next() {
    if (has_peek_) { has_peek_ = false; return peeked_; }
    return scan();
}

Token Lexer::scan() {
    while (pos_ < end_ && is_ascii_space(src_[pos_])) ++pos_;
    const std::uint32_t start = pos_;
    if (pos_ >= end_) return {Tok::End, start, start};
    const char c = src_[pos_];
    if (c == '"' || c == '\'') return string_token(start);
    if (is_ascii_digit(c) ||
        (c == '.' && pos_ + 1 < end_ && is_ascii_digit(src_[pos_ + 1])))
        return number_token(start);
    if (is_ident_start(c)) {
        while (pos_ < end_ && is_ident_char(src_[pos_])) ++pos_;
        return {Tok::Ident, start, pos_};
    }
    return punct_token(start);
}

// Quote-aware scan identical to the legacy statement splitter: a backslash
// escapes the next byte, the same quote character ends the literal. An
// unterminated literal is a Bad token (legacy reports it only at run time).
Token Lexer::string_token(std::uint32_t start) {
    const char quote = src_[pos_++];
    while (pos_ < end_ && src_[pos_] != quote) {
        if (src_[pos_] == '\\' && pos_ + 1 < end_) ++pos_;
        ++pos_;
    }
    if (pos_ >= end_) return {Tok::Bad, start, end_};
    ++pos_;
    return {Tok::String, start, pos_};
}

// Numbers as the legacy evaluator accepts them: digits with an optional
// fraction (`5.`, `.5`) and exponent (`1.5e-2`). Hex, separators and
// suffixes are not numbers (they are malformed expressions at run time).
Token Lexer::number_token(std::uint32_t start) {
    auto digits = [&]() { while (pos_ < end_ && is_ascii_digit(src_[pos_])) ++pos_; };
    digits();
    const bool member_next = pos_ + 1 < end_ && (src_[pos_ + 1] == '.' || is_ident_start(src_[pos_ + 1]));
    if (pos_ < end_ && src_[pos_] == '.' && !member_next) { ++pos_; digits(); }
    if (pos_ < end_ && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
        std::uint32_t q = pos_ + 1;
        if (q < end_ && (src_[q] == '+' || src_[q] == '-')) ++q;
        if (q < end_ && is_ascii_digit(src_[q])) { pos_ = q; digits(); }
    }
    if (pos_ < end_ && is_ident_char(src_[pos_])) {
        while (pos_ < end_ && is_ident_char(src_[pos_])) ++pos_;
        return {Tok::Bad, start, pos_};
    }
    return {Tok::Number, start, pos_};
}

Token Lexer::punct_token(std::uint32_t start) {
    auto at = [&](std::uint32_t k) { return pos_ + k < end_ ? src_[pos_ + k] : '\0'; };
    auto emit = [&](Tok kind, std::uint32_t length) { pos_ += length; return Token{kind, start, pos_}; };
    const char c = at(0), d = at(1);
    switch (c) {
    case '(': return emit(Tok::LParen, 1); case ')': return emit(Tok::RParen, 1);
    case '[': return emit(Tok::LBracket, 1); case ']': return emit(Tok::RBracket, 1);
    case '{': return emit(Tok::LBrace, 1); case '}': return emit(Tok::RBrace, 1);
    case ',': return emit(Tok::Comma, 1); case ';': return emit(Tok::Semicolon, 1);
    case '$': return emit(Tok::Dollar, 1); case '@': return emit(Tok::At, 1); case '#': return emit(Tok::Hash, 1);
    case '.': return d == '.' && at(2) == '.' ? emit(Tok::Ellipsis, 3) : emit(Tok::Dot, 1);
    case ':': return d == '=' ? emit(Tok::Declare, 2) : emit(Tok::Colon, 1);
    case '?':
        if (d == '.') return emit(Tok::QuestionDot, 2);
        if (d == '[') return emit(Tok::QuestionBracket, 2);
        if (d == '?') return emit(Tok::Coalesce, 2);
        return emit(Tok::Question, 1);
    case '=': return d == '=' ? emit(Tok::Eq, 2) : d == '>' ? emit(Tok::Arrow, 2) : emit(Tok::Assign, 1);
    case '!': return d == '=' ? emit(Tok::Ne, 2) : emit(Tok::Bang, 1);
    case '<': return d == '=' ? emit(Tok::Le, 2) : emit(Tok::Lt, 1);
    case '>': return d == '=' ? emit(Tok::Ge, 2) : emit(Tok::Gt, 1);
    case '&': return d == '&' ? emit(Tok::AndAnd, 2) : d == '=' ? emit(Tok::AndAssign, 2) : emit(Tok::Other, 1);
    case '|': return d == '|' ? emit(Tok::OrOr, 2) : d == '=' ? emit(Tok::OrAssign, 2) : emit(Tok::Other, 1);
    case '^': return d == '=' ? emit(Tok::XorAssign, 2) : emit(Tok::Other, 1);
    case '+': return d == '+' ? emit(Tok::Increment, 2) : d == '=' ? emit(Tok::AddAssign, 2) : emit(Tok::Plus, 1);
    case '-': return d == '-' ? emit(Tok::Decrement, 2) : d == '=' ? emit(Tok::SubAssign, 2) : emit(Tok::Minus, 1);
    case '*': return d == '=' ? emit(Tok::MulAssign, 2) : emit(Tok::Star, 1);
    case '/': return d == '=' ? emit(Tok::DivAssign, 2) : emit(Tok::Slash, 1);
    case '%': return d == '=' ? emit(Tok::ModAssign, 2) : emit(Tok::Percent, 1);
    default: break;
    }
    // One byte (a UTF-8 lead or continuation byte is also one "other" byte).
    return emit(Tok::Other, 1);
}

} // namespace nift::rt
