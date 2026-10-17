#pragma once
// Runtime 2.0 CP10: expression lexer for Nift scripts.
//
// The statement structure of a script (blocks, statement separators, comments)
// is recognised by rt/parser.cpp exactly as the legacy translator recognises
// it. This lexer only tokenises the text of one statement or expression; it
// never decides where statements end.
#include <cstdint>
#include <string_view>

namespace nift::rt {

enum class Tok : std::uint8_t {
    End, Bad, Ident, Number, String,
    LParen, RParen, LBracket, RBracket, LBrace, RBrace,
    Comma, Colon, Semicolon, Dot, Ellipsis, Question, QuestionDot, QuestionBracket, Coalesce,
    Arrow, Declare, Assign, AddAssign, SubAssign, MulAssign, DivAssign, ModAssign, AndAssign, OrAssign, XorAssign,
    Increment, Decrement,
    Eq, Ne, Lt, Le, Gt, Ge, AndAnd, OrOr, Bang, Plus, Minus, Star, Slash, Percent,
    Dollar, At, Hash, Other
};

struct Token {
    Tok kind = Tok::End;
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

const char* token_name(Tok kind);

// Tokenises src[begin, end). Whitespace (including newlines) separates
// tokens. Comment markers are not recognised here: legacy expression text has
// no comment grammar, so `//` lexes as two slashes and parses as an error.
class Lexer {
public:
    Lexer(std::string_view src, std::uint32_t begin, std::uint32_t end) : src_(src), pos_(begin), end_(end) {}
    Token next();
    Token peek();
    std::uint32_t position() const { return pos_; }
    void reset(std::uint32_t pos) { pos_ = pos; has_peek_ = false; }

private:
    Token scan();
    Token string_token(std::uint32_t start);
    Token number_token(std::uint32_t start);
    Token punct_token(std::uint32_t start);
    std::string_view src_;
    std::uint32_t pos_;
    std::uint32_t end_;
    Token peeked_{};
    bool has_peek_ = false;
};

// ASCII classification, identical to the <cctype> functions in the "C" locale
// the legacy engine runs in (it never calls setlocale).
inline bool is_ascii_space(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
inline bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }
inline bool is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
inline bool is_ident_char(char c) { return is_ident_start(c) || is_ascii_digit(c); }

} // namespace nift::rt
