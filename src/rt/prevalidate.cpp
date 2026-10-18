#include "rt/prevalidate.h"

#include "rt/ast.h"
#include "rt/lexer.h"
#include "rt/parser.h"

#include <map>
#include <vector>

// rt-lint: cold-file runs once per unit before execution; never on a hot path

namespace nift::rt {

namespace {

constexpr const char* kSyntax = "native.syntax_error";
constexpr const char* kTranslation = "native.translation_error";

bool is_open(Tok k) { return k == Tok::LParen || k == Tok::LBracket || k == Tok::LBrace; }
bool is_close(Tok k) { return k == Tok::RParen || k == Tok::RBracket || k == Tok::RBrace; }
Tok partner(Tok open) {
    return open == Tok::LParen ? Tok::RParen : open == Tok::LBracket ? Tok::RBracket : Tok::RBrace;
}

// R4: an operator that needs a right operand; legacy fails ("expression cannot
// be empty" / "unknown value or malformed expression") when one is followed
// by a closer, a comma or the end of the statement. Deliberately excluded
// (legacy accepts or the reading is ambiguous): `||`, `??`, `?`, `:`, the
// relational operators (template-like text), `=>`.
bool needs_right_operand(Tok k) {
    switch (k) {
    case Tok::Plus: case Tok::Minus: case Tok::Star: case Tok::Slash: case Tok::Percent:
    case Tok::Eq: case Tok::Ne: case Tok::AndAnd: case Tok::Dot:
        return true;
    default:
        return false;
    }
}

// R5: a binary-only operator where an operand must start (after an opener, a
// comma, `:=`/`=`/compound assignment, or at the start of the statement).
// Unary-capable `+ - !` are excluded (`x := + 2` is accepted by legacy).
bool binary_only(Tok k) {
    switch (k) {
    case Tok::Star: case Tok::Slash: case Tok::Percent: case Tok::Eq: case Tok::Ne:
    case Tok::AndAnd: case Tok::OrOr: case Tok::Coalesce:
        return true;
    default:
        return false;
    }
}

bool starts_operand(Tok k) {
    switch (k) {
    case Tok::Comma: case Tok::Declare: case Tok::Assign: case Tok::AddAssign: case Tok::SubAssign:
    case Tok::MulAssign: case Tok::DivAssign: case Tok::ModAssign: case Tok::AndAssign: case Tok::OrAssign:
    case Tok::XorAssign:
        return true;
    default:
        return is_open(k);
    }
}

struct Finding {
    bool found = false;
    std::uint32_t offset = 0;
    std::string message;
    const char* code = kSyntax;
    const char* rule = "";
    void take(std::uint32_t at, std::string text, const char* which, const char* with_code = kSyntax) {
        if (found && offset <= at) return;
        found = true;
        offset = at;
        message = std::move(text);
        rule = which;
        code = with_code;
    }
};

// Regions inside an Opaque statement that are parsed (or kept) on their own:
// blocks (their statements have their own nodes, comments are recognised
// there) and Opaque nodes of other reasons (template text, struct bodies).
// They are skipped as a single operand.
using Skips = std::map<std::uint32_t, std::uint32_t>;

std::string_view rest_of(std::string_view src, std::uint32_t from, std::uint32_t end) {
    std::string_view text = src.substr(from, end - from);
    while (!text.empty() && is_ascii_space(text.back())) text.remove_suffix(1);
    return text;
}

// Legacy reports an unbalanced statement as "unknown value or malformed
// expression: <text from the offending bracket to the end of the statement>"
// at that bracket; the same text is used here (D-022 #3 changes only when it
// is reported).
std::string malformed(std::string_view src, std::uint32_t at, std::uint32_t end) {
    return "unknown value or malformed expression: " + std::string(rest_of(src, at, end));
}

// R1/R2 first: an unterminated literal or unbalanced brackets decide the
// statement before any operand rule (legacy fails on the bracket).
bool check_balance(std::string_view src, std::uint32_t begin, std::uint32_t end, const Skips& skips,
                   Finding& out) {
    struct Open {
        Token token;
        std::uint32_t anchor;  // legacy's position: the callee for a call or index, else the bracket
    };
    Lexer lex(src, begin, end);
    std::vector<Open> stack;
    std::uint32_t first_multiline_string = end, operand_start = begin;
    bool operand_expected = true;
    for (;;) {
        const Token t = lex.next();
        const auto skip = skips.find(t.begin);
        if (t.kind != Tok::End && skip != skips.end() && skip->second <= end && skip->first > begin) {
            lex.reset(skip->second);
            if (operand_expected) operand_start = t.begin;
            operand_expected = false;
            continue;
        }
        if (t.kind == Tok::String && first_multiline_string == end &&
            src.substr(t.begin, t.end - t.begin).find('\n') != std::string_view::npos)
            first_multiline_string = t.begin;
        if (t.kind == Tok::Bad) {
            if (src[t.begin] == '"' || src[t.begin] == '\'') {
                // Quotes pair up across lines (as legacy reads them), so the
                // literal left open is usually the first one spanning a line.
                out.take(first_multiline_string < t.begin ? first_multiline_string : t.begin,
                         "unterminated string literal", "R1-unterminated-string");
                return true;
            }
            if (lex.position() < t.end) lex.reset(t.end);  // \v, \f: the lexer does not advance
            if (operand_expected) operand_start = t.begin;
            operand_expected = false;
            continue;
        }
        if (t.kind == Tok::End) {
            if (stack.empty()) return false;
            const std::uint32_t at = stack.front().anchor;
            out.take(at, malformed(src, at, end), "R2-unclosed");
            return true;
        }
        if (is_open(t.kind)) {
            const bool call = !operand_expected && t.kind != Tok::LBrace;
            stack.push_back({t, call ? operand_start : t.begin});
            if (operand_expected) operand_start = t.begin;
        } else if (is_close(t.kind)) {
            if (stack.empty()) {
                const std::uint32_t at = operand_expected ? t.begin : operand_start;
                out.take(at, malformed(src, at, end), "R2-stray-closer");
                return true;
            }
            if (partner(stack.back().token.kind) != t.kind) {
                const std::uint32_t at = stack.back().anchor;
                out.take(at, malformed(src, at, end), "R2-mismatched");
                return true;
            }
            operand_start = stack.back().anchor;  // the bracketed operand (or call) as a whole
            stack.pop_back();
        } else if (operand_expected) {
            operand_start = t.begin;
        }
        operand_expected = starts_operand(t.kind) || (t.kind != Tok::Dot && needs_right_operand(t.kind)) ||
                           binary_only(t.kind) || t.kind == Tok::Bang || t.kind == Tok::Lt ||
                           t.kind == Tok::Le || t.kind == Tok::Gt || t.kind == Tok::Ge ||
                           t.kind == Tok::Question || t.kind == Tok::Colon || t.kind == Tok::Arrow;
    }
}

// Where legacy reports an operand error (it evaluates element by element):
// a call argument at the callee (print: at the argument), an array element or
// a parenthesised operand at its own start, a statement value after `:=`/`=`.
struct Frame {
    Tok kind;
    std::uint32_t open = 0;
    std::uint32_t callee = 0;  // start of the called operand, for calls and indexes
    bool call = false;
    bool print = false;
    std::uint32_t element = 0;  // start of the current element
};

std::uint32_t operand_anchor(const std::vector<Frame>& stack, std::uint32_t value_start, bool empty_element) {
    if (stack.empty()) return value_start;
    const Frame& f = stack.back();
    if (f.call && !f.print) return f.callee;
    if (empty_element && f.kind != Tok::LParen) return f.open;
    return f.element;
}

void check_region(std::string_view src, std::uint32_t begin, std::uint32_t end, const Skips& skips,
                  Finding& out) {
    if (check_balance(src, begin, end, skips, out)) return;
    Lexer lex(src, begin, end);
    std::vector<Frame> stack;
    Tok prev = Tok::End;  // Tok::End = start of the statement
    std::uint32_t value_start = begin, operand_start = begin;
    bool operand_expected = true, element_pending = false;
    for (;;) {
        const Token t = lex.next();
        const auto skip = skips.find(t.begin);
        const bool skipped = t.kind != Tok::End && skip != skips.end() && skip->second <= end && skip->first > begin;
        if (skipped || t.kind == Tok::Bad) {
            if (skipped) lex.reset(skip->second);
            else if (lex.position() < t.end) lex.reset(t.end);
            if (operand_expected) operand_start = t.begin;
            if (element_pending && !stack.empty()) stack.back().element = t.begin;
            element_pending = false;
            prev = Tok::Ident; operand_expected = false;
            continue;
        }
        if (element_pending && !stack.empty() && t.kind != Tok::End) stack.back().element = t.begin;
        element_pending = false;
        if ((t.kind == Tok::End || is_close(t.kind) || t.kind == Tok::Comma) && needs_right_operand(prev)) {
            const std::uint32_t at = operand_anchor(stack, value_start, false);
            // `a.` is a malformed member path for legacy, `a +` an empty operand
            out.take(at, prev == Tok::Dot ? malformed(src, at, end) : "expression cannot be empty",
                     "R4-missing-operand");
            return;
        }
        if (t.kind == Tok::End) return;
        if (operand_expected && binary_only(t.kind)) {
            out.take(operand_anchor(stack, value_start, false), "expression cannot be empty",
                     "R5-missing-left-operand");
            return;
        }
        if (t.kind == Tok::Comma && !stack.empty() && (prev == Tok::Comma || is_open(prev))) {
            if (stack.back().kind == Tok::LParen && !stack.back().call && is_open(prev))
                out.take(t.begin, malformed(src, t.begin, end), "R3-empty-element");  // `(,1)`
            else
                out.take(operand_anchor(stack, value_start, true), "expression cannot be empty", "R3-empty-element");
            return;
        }
        if (is_close(t.kind)) {
            if (prev == Tok::Comma) {
                const bool object = stack.back().kind == Tok::LBrace;
                out.take(operand_anchor(stack, value_start, true),
                         object ? "object literal: trailing comma" : "expression cannot be empty", "R3-empty-element");
                return;
            }
            stack.pop_back();
        } else if (is_open(t.kind)) {
            Frame f{t.kind, t.begin, operand_start, false, false, t.begin};
            f.call = !operand_expected && t.kind != Tok::LBrace;
            const std::string_view callee = src.substr(operand_start, t.begin - operand_start);
            f.print = f.call && callee == "print";  // rt-lint: allow-string-compare cold path, once per call
            if (operand_expected) operand_start = t.begin;
            stack.push_back(f);
            element_pending = true;
        } else if (t.kind == Tok::Comma) {
            element_pending = true;
        } else if (operand_expected) {
            operand_start = t.begin;
        }
        if (t.kind == Tok::Declare || t.kind == Tok::Assign) {
            const Token next = lex.peek();
            value_start = next.begin;
        }
        operand_expected = starts_operand(t.kind);
        prev = t.kind;
    }
}

} // namespace

Prevalidation prevalidate(std::string_view source) {
    Prevalidation result;
    Ast ast(source);
    const ParseResult parsed = parse_script(ast);
    if (!parsed.ok) {
        result.ok = false;
        result.offset = parsed.error.offset;
        result.message = parsed.error.message;
        result.code = kTranslation;
        result.rule = "translator";
        return result;
    }
    Finding finding;
    for (const auto& deferred : ast.deferred_errors())
        finding.take(deferred.offset, deferred.message, "deferred-translator", kTranslation);
    Skips skips;
    for (NodeId id = 0; id < ast.size(); ++id) {
        const Node& node = ast.at(id);
        const bool other_opaque = node.kind == NodeKind::Opaque && node.reason != OpaqueReason::LegacyRuntimeError;
        if (node.kind == NodeKind::Block || other_opaque) {
            auto& end = skips[node.begin];
            if (node.end > end) end = node.end;
        }
    }
    for (NodeId id = 0; id < ast.size(); ++id) {
        const Node& node = ast.at(id);
        if (node.kind == NodeKind::Opaque && node.reason == OpaqueReason::LegacyRuntimeError)
            check_region(source, node.begin, node.end, skips, finding);
    }
    if (finding.found) {
        result.ok = false;
        result.offset = finding.offset;
        result.message = std::move(finding.message);
        result.code = finding.code;
        result.rule = finding.rule;
    }
    return result;
}

} // namespace nift::rt
