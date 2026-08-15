#include <minify/Minify.h>
#include "Json.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

namespace minify {
namespace {

bool ws(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

bool word_char(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    // Be deliberately conservative for UTF-8: non-ASCII bytes may belong to a
    // JavaScript/CSS identifier, so removing adjacent whitespace could merge
    // tokens even though this lightweight scanner does not decode Unicode IDs.
    return u >= 0x80 || std::isalnum(u) || c == '_' || c == '$' || c == '-' || c == '\\';
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool starts_ci(const std::string& s, std::size_t pos, const std::string& needle) {
    if (pos + needle.size() > s.size()) return false;
    for (std::size_t i = 0; i < needle.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(s[pos + i])) !=
            std::tolower(static_cast<unsigned char>(needle[i]))) return false;
    return true;
}

void emit_pending_space(std::string& out, bool& pending, char next) {
    if (!pending) return;
    if (!out.empty() && !ws(out.back()) && word_char(out.back()) && word_char(next))
        out.push_back(' ');
    pending = false;
}

} // namespace

bool json(const std::string& input, std::string& output, std::string& error) {
    json::Document document;
    if (!json::Document::parse(input, document, error)) {
        error = "invalid JSON: " + error;
        output.clear();
        return false;
    }

    output.clear();
    output.reserve(input.size());
    bool quoted = false;
    bool escaped = false;
    for (char c : input) {
        if (quoted) {
            output.push_back(c);
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') {
            quoted = true;
            output.push_back(c);
        } else if (!ws(c)) {
            output.push_back(c);
        }
    }
    error.clear();
    return true;
}

bool css(const std::string& input, std::string& output, std::string& error) {
    output.clear();
    output.reserve(input.size());
    bool pending_space = false;

    for (std::size_t i = 0; i < input.size();) {
        const char c = input[i];

        if (c == '\'' || c == '"') {
            emit_pending_space(output, pending_space, c);
            const char quote = c;
            output.push_back(c);
            ++i;
            bool escaped = false;
            while (i < input.size()) {
                const char q = input[i++];
                output.push_back(q);
                if (escaped) escaped = false;
                else if (q == '\\') escaped = true;
                else if (q == quote) break;
            }
            continue;
        }

        if (c == '/' && i + 1 < input.size() && input[i + 1] == '*') {
            const bool preserve = i + 2 < input.size() && input[i + 2] == '!';
            const auto end = input.find("*/", i + 2);
            if (end == std::string::npos) {
                error = "unterminated CSS comment";
                output.clear();
                return false;
            }
            if (preserve) {
                emit_pending_space(output, pending_space, '/');
                output.append(input, i, end + 2 - i);
            } else {
                pending_space = true;
            }
            i = end + 2;
            continue;
        }

        if (ws(c)) {
            pending_space = true;
            ++i;
            continue;
        }

        const bool punctuation = c == '{' || c == '}' || c == ':' ||
                                 c == ';' || c == ',';
        if (punctuation) {
            pending_space = false;
            while (!output.empty() && output.back() == ' ') output.pop_back();
            output.push_back(c);
        } else {
            // CSS math functions require whitespace around binary + and -.
            // Preserve authored whitespace adjacent to these operators rather
            // than trying to parse the full evolving CSS value grammar.
            if (pending_space && (c == '+' || c == '-') && !output.empty() && output.back() != ' ')
                output.push_back(' ');
            else if (pending_space && !output.empty() &&
                     (output.back() == '+' || output.back() == '-') && output.back() != ' ')
                output.push_back(' ');
            else
                emit_pending_space(output, pending_space, c);
            pending_space = false;
            output.push_back(c);
        }
        ++i;
    }

    while (!output.empty() && ws(output.back())) output.pop_back();
    error.clear();
    return true;
}

bool html(const std::string& input, std::string& output, std::string& error) {
    output.clear();
    output.reserve(input.size());

    bool pending_space = false;
    std::string raw_tag;

    for (std::size_t i = 0; i < input.size();) {
        if (!raw_tag.empty()) {
            const std::string close = "</" + raw_tag;
            std::size_t p = i;
            bool found = false;
            for (; p < input.size(); ++p) {
                if (!starts_ci(input, p, close)) continue;
                const std::size_t after = p + close.size();
                if (after < input.size() &&
                    (input[after] == '>' || ws(input[after]))) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                output.append(input, i, std::string::npos);
                i = input.size();
                break;
            }
            output.append(input, i, p - i);
            i = p;
            raw_tag.clear();
            continue;
        }

        if (input.compare(i, 4, "<!--") == 0) {
            const auto end = input.find("-->", i + 4);
            if (end == std::string::npos) {
                error = "unterminated HTML comment";
                output.clear();
                return false;
            }
            const bool preserve = starts_ci(input, i, "<!--[if") ||
                                  input.compare(i, 5, "<!--#") == 0 ||
                                  input.compare(i, 5, "<!--!") == 0;
            if (preserve) {
                if (pending_space && !output.empty()) output.push_back(' ');
                pending_space = false;
                output.append(input, i, end + 3 - i);
            } else {
                // Ordinary comments are zero-width. Preserve whitespace that
                // actually occurred around the comment, but do not invent any.
            }
            i = end + 3;
            continue;
        }

        if (input[i] == '<') {
            // Whitespace between elements is a real HTML text node and can
            // become observable when CSS changes display/layout. Collapse it,
            // but do not erase it simply because the next token is a tag.
            if (pending_space && !output.empty() && !ws(output.back()))
                output.push_back(' ');
            pending_space = false;

            std::size_t j = i + 1;
            bool quoted = false;
            char quote = 0;
            for (; j < input.size(); ++j) {
                const char c = input[j];
                if (quoted) {
                    if (c == '\\') ++j;
                    else if (c == quote) quoted = false;
                } else if (c == '\'' || c == '"') {
                    quoted = true; quote = c;
                } else if (c == '>') {
                    ++j;
                    break;
                }
            }
            if (j > input.size() || input[j - 1] != '>') {
                error = "unterminated HTML tag";
                output.clear();
                return false;
            }

            // Collapse whitespace inside tags, preserving quoted attribute values.
            bool tag_space = false;
            bool in_quote = false;
            char tag_quote = 0;
            for (std::size_t k = i; k < j; ++k) {
                char c = input[k];
                if (in_quote) {
                    output.push_back(c);
                    if (c == '\\' && k + 1 < j) output.push_back(input[++k]);
                    else if (c == tag_quote) in_quote = false;
                } else if (c == '\'' || c == '"') {
                    if (tag_space && !output.empty() && output.back() != '<' && output.back() != ' ')
                        output.push_back(' ');
                    tag_space = false;
                    in_quote = true; tag_quote = c; output.push_back(c);
                } else if (ws(c)) {
                    tag_space = true;
                } else {
                    if (tag_space && !output.empty() &&
                        output.back() != '<' && output.back() != '/' &&
                        c != '>' && c != '/') output.push_back(' ');
                    tag_space = false;
                    output.push_back(c);
                }
            }

            // Detect raw-text/preformatted elements from opening tags.
            std::size_t n = i + 1;
            while (n < j && ws(input[n])) ++n;
            if (n < j && input[n] != '/' && input[n] != '!' && input[n] != '?') {
                std::size_t e = n;
                while (e < j && (std::isalnum(static_cast<unsigned char>(input[e])) ||
                                 input[e] == '-' || input[e] == ':')) ++e;
                const std::string name = lower(input.substr(n, e - n));
                if (name == "pre" || name == "textarea" || name == "script" || name == "style")
                    raw_tag = name;
            }
            i = j;
            continue;
        }

        if (ws(input[i])) {
            pending_space = true;
            ++i;
            continue;
        }

        if (pending_space && !output.empty()) output.push_back(' ');
        pending_space = false;
        output.push_back(input[i++]);
    }

    while (!output.empty() && ws(output.back())) output.pop_back();
    while (!output.empty() && ws(output.front())) output.erase(output.begin());
    error.clear();
    return true;
}

// JavaScript minification is intentionally conservative. It removes comments
// and redundant horizontal whitespace, but preserves every significant newline
// so automatic-semicolon-insertion behavior cannot be changed.
bool javascript(const std::string& input, std::string& output, std::string& error) {
    output.clear();
    output.reserve(input.size());

    bool pending_space = false;
    bool pending_newline = false;
    bool can_start_regex = true;
    bool pending_control_paren = false;
    std::vector<bool> control_parens;
    std::vector<bool> block_braces;
    std::string last_token;
    bool pending_class_brace = false;
    bool pending_class_expression = false;
    bool pending_function_brace = false;
    bool pending_function_expression = false;
    bool pending_async_expression = false;

    auto emit_pending = [&](char next) {
        if (pending_newline) {
            // Keeping line terminators is intentionally conservative: ASI,
            // `return`, `throw`, postfix ++/-- and future syntax can depend on
            // their presence.
            if (!output.empty() && output.back() != '\n') output.push_back('\n');
        } else if (pending_space && !output.empty() &&
                   ((word_char(output.back()) && word_char(next)) ||
                    (output.back() == '+' && next == '+') ||
                    (output.back() == '-' && next == '-') ||
                    (output.back() == '/' && next == '/') ||
                    (std::isdigit(static_cast<unsigned char>(output.back())) && next == '.'))) {
            output.push_back(' ');
        }
        pending_space = pending_newline = false;
    };

    auto copy_quoted = [&](std::size_t& i, char quote) {
        emit_pending(quote);
        output.push_back(input[i++]);
        bool escaped = false;
        while (i < input.size()) {
            const char q = input[i++];
            output.push_back(q);
            if (escaped) escaped = false;
            else if (q == '\\') escaped = true;
            else if (q == quote) return true;
        }
        error = std::string("unterminated JavaScript ") +
                (quote == '`' ? "template literal" : "string literal");
        output.clear();
        return false;
    };

    static const std::unordered_set<std::string> control_keywords = {
        "if", "while", "for", "with", "switch", "catch"
    };
    static const std::unordered_set<std::string> expression_prefix_keywords = {
        "return", "throw", "case", "delete", "void", "typeof", "new",
        "in", "instanceof", "yield", "await", "else", "do"
    };

    for (std::size_t i = 0; i < input.size();) {
        const char c = input[i];

        if (ws(c)) {
            if (c == '\n' || c == '\r') pending_newline = true;
            else pending_space = true;
            ++i;
            continue;
        }

        // ASCII identifier/keyword token. Non-ASCII identifier bytes are kept
        // byte-for-byte by the generic path below.
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$') {
            const std::size_t begin = i++;
            while (i < input.size()) {
                const unsigned char u = static_cast<unsigned char>(input[i]);
                if (!std::isalnum(u) && input[i] != '_' && input[i] != '$') break;
                ++i;
            }
            const std::string word = input.substr(begin, i - begin);
            emit_pending(word.front());
            output += word;

            const bool was_pending_control_paren = pending_control_paren;
            pending_control_paren = control_keywords.count(word) != 0 ||
                                    (was_pending_control_paren && word == "await");
            if (word == "async") {
                pending_async_expression =
                    !(last_token.empty() || last_token == ";" || last_token == "}block" ||
                      last_token == "export" || last_token == "default");
            }
            if (word == "function") {
                pending_function_brace = true;
                pending_function_expression = last_token == "async"
                    ? pending_async_expression
                    : !(last_token.empty() || last_token == ";" || last_token == "}block" ||
                        last_token == "export" || last_token == "default");
            }
            if (word == "class") {
                pending_class_brace = true;
                pending_class_expression =
                    !(last_token.empty() || last_token == ";" || last_token == "}block" ||
                      last_token == "export" || last_token == "default");
            }
            can_start_regex = pending_control_paren ||
                              expression_prefix_keywords.count(word) != 0;
            last_token = word;
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            const std::size_t begin = i++;
            bool exponent = false;
            while (i < input.size()) {
                const unsigned char u = static_cast<unsigned char>(input[i]);
                const char q = input[i];
                if (std::isalnum(u) || q == '.' || q == '_') {
                    exponent = q == 'e' || q == 'E';
                    ++i;
                    continue;
                }
                if ((q == '+' || q == '-') && exponent) {
                    exponent = false;
                    ++i;
                    continue;
                }
                break;
            }
            emit_pending(input[begin]);
            output.append(input, begin, i - begin);
            pending_control_paren = false;
            can_start_regex = false;
            last_token = "value";
            continue;
        }

        if (c == '\'' || c == '"' || c == '`') {
            if (!copy_quoted(i, c)) return false;
            pending_control_paren = false;
            can_start_regex = false;
            last_token = "value";
            continue;
        }

        if (c == '(') {
            emit_pending(c);
            output.push_back(c);
            control_parens.push_back(pending_control_paren);
            pending_control_paren = false;
            can_start_regex = true;
            last_token = "(";
            ++i;
            continue;
        }

        if (c == ')') {
            emit_pending(c);
            output.push_back(c);
            const bool was_control = !control_parens.empty() && control_parens.back();
            if (!control_parens.empty()) control_parens.pop_back();
            pending_control_paren = false;
            // A statement can begin with a regex after if/while/for/etc.; a
            // normal function-call parenthesis instead ends an expression.
            can_start_regex = was_control;
            last_token = was_control ? ")control" : ")";
            ++i;
            continue;
        }

        // Comments are recognized before regex because // and /* cannot begin
        // a JavaScript regex literal.
        if (c == '/' && i + 1 < input.size() && input[i + 1] == '/') {
            i += 2;
            while (i < input.size() && input[i] != '\n' && input[i] != '\r') ++i;
            pending_newline = true;
            continue;
        }

        if (c == '/' && i + 1 < input.size() && input[i + 1] == '*') {
            const bool preserve = i + 2 < input.size() && input[i + 2] == '!';
            const auto close = input.find("*/", i + 2);
            if (close == std::string::npos) {
                error = "unterminated JavaScript block comment";
                output.clear();
                return false;
            }
            const bool had_newline = input.find('\n', i + 2) < close ||
                                     input.find('\r', i + 2) < close;
            if (preserve) {
                emit_pending('/');
                output.append(input, i, close + 2 - i);
            } else if (had_newline) {
                pending_newline = true;
            } else {
                pending_space = true;
            }
            i = close + 2;
            continue;
        }

        if (c == '/' && can_start_regex && i + 1 < input.size() &&
            (output.empty() || output.back() != '<')) {
            emit_pending(c);
            output.push_back(input[i++]);
            bool escaped = false;
            bool in_class = false;
            bool closed = false;
            while (i < input.size()) {
                const char q = input[i++];
                output.push_back(q);
                if (escaped) {
                    escaped = false;
                    continue;
                }
                if (q == '\\') {
                    escaped = true;
                    continue;
                }
                if (q == '[') in_class = true;
                else if (q == ']') in_class = false;
                else if (q == '/' && !in_class) {
                    closed = true;
                    break;
                } else if (q == '\n' || q == '\r') {
                    break;
                }
            }
            if (!closed) {
                error = "unterminated JavaScript regular expression";
                output.clear();
                return false;
            }
            while (i < input.size() &&
                   std::isalpha(static_cast<unsigned char>(input[i])))
                output.push_back(input[i++]);
            pending_control_paren = false;
            can_start_regex = false;
            last_token = "value";
            continue;
        }

        // Track whether braces belong to executable blocks or object
        // expressions. Slash after a block may begin a regex statement; slash
        // after an object literal is division.
        if (c == '{') {
            emit_pending(c);
            bool is_block = false;
            if (pending_class_brace || pending_function_brace ||
                last_token.empty() || last_token == ";" || last_token == "}block" ||
                last_token == ")" || last_token == ")control" ||
                last_token == "else" || last_token == "do" ||
                last_token == "try" || last_token == "catch" || last_token == "finally" ||
                last_token == "class") {
                is_block = true;
            } else if (last_token == ":" && (block_braces.empty() || block_braces.back())) {
                // A labelled statement (`label: { ... }`) closes like a block,
                // unlike an object property or ternary whose value is `{...}`.
                // Confirm that the current statement fragment before ':' is
                // only an identifier; this keeps `cond ? x : {}` expression-like.
                std::size_t colon = output.empty() ? 0 : output.size() - 1;
                std::size_t end = colon;
                while (end > 0 && ws(output[end - 1])) --end;
                std::size_t start = end;
                while (start > 0) {
                    const unsigned char ch = static_cast<unsigned char>(output[start - 1]);
                    if (!(std::isalnum(ch) || output[start - 1] == '_' || output[start - 1] == '$' || ch >= 0x80)) break;
                    --start;
                }
                bool identifier = end > start;
                // Labels may directly follow a control header: `if(x) label:{}`.
                // Object/ternary colons do not have a bare identifier in this
                // statement position.
                std::size_t before=start;
                while(before>0 && ws(output[before-1])) --before;
                const bool statement_position =
                    before==0 || output[before-1]==';' || output[before-1]=='{' ||
                    output[before-1]=='}' || output[before-1]==')';
                if (identifier && statement_position) is_block = true;
            }
            // For class expressions the body is syntactically a block, but
            // the closing brace yields an expression value, so slash afterward
            // must be interpreted like division rather than a regex statement.
            const bool expression_body =
                (pending_class_brace && pending_class_expression) ||
                (pending_function_brace && pending_function_expression);
            block_braces.push_back(expression_body ? false : is_block);
            output.push_back(c);
            pending_class_brace = false;
            pending_class_expression = false;
            pending_function_brace = false;
            pending_function_expression = false;
            pending_async_expression = false;
            pending_control_paren = false;
            can_start_regex = true;
            last_token = "{";
            ++i;
            continue;
        }

        if (c == '}') {
            emit_pending(c);
            output.push_back(c);
            const bool was_block = block_braces.empty() ? true : block_braces.back();
            if (!block_braces.empty()) block_braces.pop_back();
            pending_control_paren = false;
            can_start_regex = was_block;
            last_token = was_block ? "}block" : "}object";
            ++i;
            continue;
        }

        emit_pending(c);
        output.push_back(c);
        pending_control_paren = false;

        if (static_cast<unsigned char>(c) >= 0x80 || word_char(c) ||
            c == ']' || c == '.' || c == '\'' || c == '"' || c == '`') {
            can_start_regex = false;
        } else if (c == ';' || c == ',' || c == ':' || c == '[' ||
                   c == '=' || c == '!' || c == '?' || c == '&' ||
                   c == '|' || c == '+' || c == '-' || c == '*' || c == '%' ||
                   c == '<' || c == '>' || c == '/') {
            // A bare slash arriving here is division; the token following a
            // division operator begins an expression and may itself be regex.
            can_start_regex = true;
        }
        last_token.assign(1, c);
        ++i;
    }

    while (!output.empty() && ws(output.back())) output.pop_back();
    error.clear();
    return true;
}


static bool minify_xml_like(const std::string& input, std::string& output,
                            std::string& error, bool svg_mode) {
    output.clear();
    output.reserve(input.size());

    for (std::size_t i = 0; i < input.size();) {
        if (input.compare(i, 4, "<!--") == 0) {
            const auto end = input.find("-->", i + 4);
            if (end == std::string::npos) {
                error = "unterminated XML comment";
                output.clear();
                return false;
            }
            i = end + 3;
            continue;
        }
        if (input.compare(i, 9, "<![CDATA[") == 0) {
            const auto end = input.find("]]>", i + 9);
            if (end == std::string::npos) {
                error = "unterminated CDATA section";
                output.clear();
                return false;
            }
            output.append(input, i, end + 3 - i);
            i = end + 3;
            continue;
        }
        if (input.compare(i, 2, "<?") == 0) {
            const auto end = input.find("?>", i + 2);
            if (end == std::string::npos) {
                error = "unterminated XML processing instruction";
                output.clear();
                return false;
            }
            // Processing-instruction data is application-defined; whitespace
            // inside it may be significant, so preserve the complete PI.
            output.append(input, i, end + 2 - i);
            i = end + 2;
            continue;
        }
        if (input[i] == '<') {
            std::size_t j = i + 1;
            bool quoted = false;
            char quote = 0;
            for (; j < input.size(); ++j) {
                char c = input[j];
                if (quoted) {
                    if (c == quote) quoted = false;
                } else if (c == '\'' || c == '"') {
                    quoted = true; quote = c;
                } else if (c == '>') { ++j; break; }
            }
            if (j > input.size() || j == 0 || input[j - 1] != '>') {
                error = "unterminated XML tag";
                output.clear();
                return false;
            }

            // Collapse insignificant whitespace inside markup, preserving quotes.
            bool ws_pending = false, in_quote = false;
            char q = 0;
            for (std::size_t k = i; k < j; ++k) {
                char c = input[k];
                if (in_quote) {
                    output.push_back(c);
                    if (c == q) in_quote = false;
                } else if (c == '\'' || c == '"') {
                    if (ws_pending && !output.empty() && output.back() != '<' && output.back() != ' ')
                        output.push_back(' ');
                    ws_pending = false; in_quote = true; q = c; output.push_back(c);
                } else if (ws(c)) {
                    ws_pending = true;
                } else {
                    if (ws_pending && !output.empty() && output.back() != '<' &&
                        output.back() != '/' && c != '>' && c != '/')
                        output.push_back(' ');
                    ws_pending = false;
                    output.push_back(c);
                }
            }
            i = j;
            continue;
        }

        // XML/SVG whitespace is text content unless a schema/consumer says
        // otherwise. Preserve it verbatim; even whitespace between tags may be
        // visible in mixed content or SVG <text>/<tspan>.
        if (ws(input[i])) {
            std::size_t j = i;
            while (j < input.size() && ws(input[j])) ++j;
            output.append(input, i, j - i);
            i = j;
            continue;
        }
        output.push_back(input[i++]);
    }

    (void)svg_mode;
    error.clear();
    return true;
}

bool xml(const std::string& input, std::string& output, std::string& error) {
    return minify_xml_like(input, output, error, false);
}

bool svg(const std::string& input, std::string& output, std::string& error) {
    return minify_xml_like(input, output, error, true);
}

static bool looks_like_jsx_start(const std::string& input, std::size_t i) {
    if (i + 1 >= input.size() || input[i] != '<') return false;
    const unsigned char n = static_cast<unsigned char>(input[i + 1]);
    return std::isalpha(n) || input[i + 1] == '>' || input[i + 1] == '/';
}

static bool looks_like_jsx_root_start(const std::string& input, std::size_t i) {
    if (!looks_like_jsx_start(input, i)) return false;
    if (input[i + 1] == '/' || input[i + 1] == '>') return true;

    std::size_t p = i;
    while (p > 0 && ws(input[p - 1])) --p;
    if (p == 0) return true;

    const char prev = input[p - 1];
    if (prev == '=' || prev == '(' || prev == '[' || prev == '{' || prev == ',' ||
        prev == ':' || prev == ';' || prev == '?' || prev == '!' || prev == '&' ||
        prev == '|' || prev == '+' || prev == '-' || prev == '*' || prev == '%' ||
        prev == '~' || prev == '^' || prev == '>') return true;

    // JSX can directly follow expression-introducing keywords. Ordinary compact
    // comparisons such as a<b and generic-looking calls such as foo<Bar>(x)
    // must remain JavaScript instead of being guessed as markup.
    std::size_t end = p;
    while (p > 0) {
        const unsigned char c = static_cast<unsigned char>(input[p - 1]);
        if (!(std::isalnum(c) || input[p - 1] == '_' || input[p - 1] == '$')) break;
        --p;
    }
    const std::string word = input.substr(p, end - p);
    return word == "return" || word == "yield" || word == "await" ||
           word == "case" || word == "throw";
}

static bool find_jsx_expression_end(const std::string& input, std::size_t start,
                                    std::size_t limit, std::size_t& end,
                                    std::string& error);
static bool looks_like_tsx_generic_arrow(const std::string& input,
                                         std::size_t start,
                                         std::size_t limit) {
    if (start >= limit || input[start] != '<') return false;
    std::size_t i = start + 1;
    std::size_t angle = 1;
    bool trailing_comma = false, saw_extends = false;
    bool quoted = false, escaped = false;
    char quote = 0;
    for (; i < limit; ++i) {
        const char c = input[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == quote) quoted = false;
            continue;
        }
        if (c == '\'' || c == '"' || c == '`') { quoted = true; quote = c; continue; }
        if (c == '<') { ++angle; continue; }
        if (c == '>') {
            if (i > start && input[i-1] == '=') continue;
            if (--angle == 0) break;
            continue;
        }
        if (angle == 1 && c == ',') trailing_comma = true;
    }
    if (i >= limit || angle != 0) return false;

    const std::string head = input.substr(start + 1, i - start - 1);
    if (head.find("extends") != std::string::npos) saw_extends = true;
    if (!trailing_comma && !saw_extends) return false; // `<T>` is JSX-ambiguous in TSX.

    std::size_t p = i + 1;
    while (p < limit && ws(input[p])) ++p;
    if (p >= limit || input[p] != '(') return false;
    int paren = 0;
    bool q = false, esc = false; char qc = 0;
    for (; p < limit; ++p) {
        const char c = input[p];
        if (q) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == qc) q = false;
            continue;
        }
        if (c == '\'' || c == '"' || c == '`') { q = true; qc = c; continue; }
        if (c == '(') ++paren;
        else if (c == ')' && --paren == 0) { ++p; break; }
    }
    if (paren != 0) return false;
    while (p < limit && ws(input[p])) ++p;
    // Optional return type before the arrow.
    if (p < limit && input[p] == ':') {
        ++p; int a=0,b=0,c=0;
        for (; p + 1 < limit; ++p) {
            const char x=input[p];
            if(x=='<')++a; else if(x=='>'&&a)--a;
            else if(x=='[')++b; else if(x==']'&&b)--b;
            else if(x=='{')++c; else if(x=='}'&&c)--c;
            if(!a&&!b&&!c&&input[p]=='='&&input[p+1]=='>') break;
        }
    }
    while (p < limit && ws(input[p])) ++p;
    return p + 1 < limit && input[p] == '=' && input[p+1] == '>';
}


static bool find_nested_jsx_end(const std::string& input, std::size_t start,
                                std::size_t limit, std::size_t& end,
                                std::string& error) {
    std::size_t p = start;
    std::size_t depth = 0;
    bool started = false;
    while (p < limit) {
        if (input[p] == '<' && looks_like_jsx_start(input, p)) {
            const bool closing = p + 1 < limit && input[p + 1] == '/';
            std::size_t j = p + 1;
            bool quoted = false, escaped = false;
            char quote = 0;
            std::size_t tag_angles = 0;
            while (j < limit) {
                const char c = input[j];
                if (quoted) {
                    if (escaped) escaped = false;
                    else if (c == '\\') escaped = true;
                    else if (c == quote) quoted = false;
                    ++j; continue;
                }
                if (c == '\'' || c == '"') { quoted = true; quote = c; ++j; continue; }
                if (c == '{') {
                    std::size_t q = 0;
                    if (!find_jsx_expression_end(input, j + 1, limit, q, error)) return false;
                    j = q; continue;
                }
                if (c == '<') { ++tag_angles; ++j; continue; }
                if (c == '>') {
                    // `=>` may occur inside a generic JSX type argument, e.g.
                    // `<Comp<(x:number)=>string> ... />`; the arrow's `>` is
                    // not a generic closer.
                    if (tag_angles && j > p && input[j-1] == '=') { ++j; continue; }
                    if (tag_angles) { --tag_angles; ++j; continue; }
                    ++j; break;
                }
                ++j;
            }
            if (j > limit || j == 0 || input[j - 1] != '>') { error = "unterminated nested JSX tag"; return false; }
            std::size_t k = j >= 2 ? j - 2 : 0;
            while (k > p && ws(input[k])) --k;
            const bool self_closing = input[k] == '/';
            if (!started) { started = true; depth = self_closing ? 0 : 1; }
            else if (closing) { if (depth) --depth; }
            else if (!self_closing) ++depth;
            p = j;
            if (started && depth == 0) { end = p; return true; }
            continue;
        }
        if (input[p] == '{') {
            std::size_t q = 0;
            if (!find_jsx_expression_end(input, p + 1, limit, q, error)) return false;
            p = q; continue;
        }
        ++p;
    }
    error = "unterminated nested JSX element";
    return false;
}

static bool find_jsx_expression_end(const std::string& input, std::size_t start,
                                    std::size_t limit, std::size_t& end,
                                    std::string& error) {
    std::size_t braces = 1;
    bool can_start_regex = true;
    for (std::size_t i = start; i < limit;) {
        const char c = input[i];

        // Track JavaScript keywords that put the scanner back into an
        // expression-prefix position. This matters for nested JSX after
        // `return`, `yield`, `await`, etc. inside a JSX expression block.
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$') {
            const std::size_t begin = i++;
            while (i < limit) {
                const unsigned char u = static_cast<unsigned char>(input[i]);
                if (!std::isalnum(u) && input[i] != '_' && input[i] != '$') break;
                ++i;
            }
            const std::string word = input.substr(begin, i - begin);
            static const std::unordered_set<std::string> prefix_words = {
                "return","throw","case","delete","void","typeof","new",
                "in","instanceof","yield","await","else","do"
            };
            can_start_regex = prefix_words.count(word) != 0;
            continue;
        }

        if (c == '\'' || c == '"' || c == '`') {
            const char quote = c;
            ++i;
            bool escaped = false;
            bool closed = false;
            while (i < limit) {
                const char q = input[i++];
                if (escaped) escaped = false;
                else if (q == '\\') escaped = true;
                else if (q == quote) { closed = true; break; }
            }
            if (!closed) { error = "unterminated string/template in JSX expression"; return false; }
            can_start_regex = false;
            continue;
        }

        if (c == '<' && can_start_regex && looks_like_jsx_start(input, i)) {
            if (looks_like_tsx_generic_arrow(input, i, limit)) {
                // TypeScript generic arrow syntax is JavaScript-expression
                // context for Minify++; do not reinterpret `<T,>` as markup.
                ++i;
                can_start_regex = true;
                continue;
            }
            std::size_t jsx_end = 0;
            if (!find_nested_jsx_end(input, i, limit, jsx_end, error)) return false;
            i = jsx_end;
            can_start_regex = false;
            continue;
        }

        if (c == '/' && i + 1 < limit && input[i + 1] == '/') {
            i += 2;
            while (i < limit && input[i] != '\n' && input[i] != '\r') ++i;
            can_start_regex = true;
            continue;
        }
        if (c == '/' && i + 1 < limit && input[i + 1] == '*') {
            const auto close = input.find("*/", i + 2);
            if (close == std::string::npos || close >= limit) {
                error = "unterminated JavaScript block comment in JSX expression";
                return false;
            }
            i = close + 2;
            continue;
        }
        if (c == '/' && can_start_regex) {
            ++i;
            bool escaped = false, in_class = false, closed = false;
            while (i < limit) {
                const char q = input[i++];
                if (escaped) { escaped = false; continue; }
                if (q == '\\') { escaped = true; continue; }
                if (q == '[') in_class = true;
                else if (q == ']') in_class = false;
                else if (q == '/' && !in_class) { closed = true; break; }
                else if (q == '\n' || q == '\r') break;
            }
            if (!closed) { error = "unterminated JavaScript regular expression in JSX expression"; return false; }
            while (i < limit && std::isalpha(static_cast<unsigned char>(input[i]))) ++i;
            can_start_regex = false;
            continue;
        }

        if (c == '{') { ++braces; can_start_regex = true; ++i; continue; }
        if (c == '}') {
            if (--braces == 0) { end = i + 1; return true; }
            can_start_regex = false;
            ++i;
            continue;
        }
        if (ws(c)) { ++i; continue; }

        if (word_char(c) || c == ')' || c == ']' || c == '.') can_start_regex = false;
        else if (c == ';' || c == ',' || c == ':' || c == '(' || c == '[' ||
                 c == '=' || c == '!' || c == '?' || c == '&' || c == '|' ||
                 c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
                 c == '<' || c == '>') can_start_regex = true;
        ++i;
    }
    error = "unterminated JSX expression";
    return false;
}

bool jsx(const std::string& input, std::string& output, std::string& error) {
    output.clear();
    output.reserve(input.size());

    std::size_t i = 0;
    std::size_t js_start = 0;
    auto flush_js = [&](std::size_t end) -> bool {
        if (end <= js_start) return true;
        std::string part, e;
        if (!javascript(input.substr(js_start, end - js_start), part, e)) {
            error = e;
            return false;
        }
        output += part;
        return true;
    };

    while (i < input.size()) {
        // Skip JavaScript regex literals before looking for JSX roots. Without
        // this, `<`/`>` inside a regex character class (for example
        // `/[{}<>]/`) can be mistaken for a JSX fragment.
        if (input[i] == '/' && i + 1 < input.size() &&
            input[i + 1] != '/' && input[i + 1] != '*') {
            std::size_t p = i;
            while (p > js_start && ws(input[p - 1])) --p;
            bool regex_here = p == js_start;
            if (!regex_here && p > js_start) {
                const char prev = input[p - 1];
                regex_here = prev == '=' || prev == '(' || prev == '[' ||
                             prev == '{' || prev == ',' || prev == ':' ||
                             prev == ';' || prev == '?' || prev == '!' ||
                             prev == '&' || prev == '|' || prev == '+' ||
                             prev == '-' || prev == '*' || prev == '%' ||
                             prev == '~' || prev == '^' || prev == '>';
                if (!regex_here &&
                    (std::isalpha(static_cast<unsigned char>(prev)) ||
                     prev == '_' || prev == '$')) {
                    std::size_t end = p;
                    while (p > js_start) {
                        const unsigned char c =
                            static_cast<unsigned char>(input[p - 1]);
                        if (!(std::isalnum(c) || input[p - 1] == '_' ||
                              input[p - 1] == '$')) break;
                        --p;
                    }
                    const std::string word = input.substr(p, end - p);
                    regex_here = word == "return" || word == "throw" ||
                                 word == "case" || word == "yield" ||
                                 word == "await";
                }
            }
            if (regex_here) {
                ++i;
                bool escaped = false, in_class = false;
                while (i < input.size()) {
                    const char c = input[i++];
                    if (escaped) { escaped = false; continue; }
                    if (c == '\\') { escaped = true; continue; }
                    if (c == '[') in_class = true;
                    else if (c == ']') in_class = false;
                    else if (c == '/' && !in_class) break;
                    else if (c == '\n' || c == '\r') break;
                }
                while (i < input.size() &&
                       std::isalpha(static_cast<unsigned char>(input[i]))) ++i;
                continue;
            }
        }

        // Respect quoted JS before deciding that '<' begins JSX.
        if (input[i] == '\'' || input[i] == '"' || input[i] == '`') {
            char q = input[i++];
            bool esc = false;
            while (i < input.size()) {
                char c = input[i++];
                if (esc) esc = false;
                else if (c == '\\') esc = true;
                else if (c == q) break;
            }
            continue;
        }
        if (input[i] == '<' && looks_like_tsx_generic_arrow(input, i, input.size())) {
            // Recursive JSX minification also sees the JavaScript/TSX expression
            // itself. Do not mistake a valid `<T,>(...) =>` generic arrow for
            // the root of a JSX region.
            ++i;
            continue;
        }
        if (!looks_like_jsx_root_start(input, i)) { ++i; continue; }

        if (!flush_js(i)) return false;

        // Copy one JSX region conservatively. Markup/text are preserved except
        // formatting whitespace inside tags; {...} expressions are recursively
        // minified with the JS scanner.
        std::size_t depth = 0;
        std::size_t p = i;
        bool started = false;
        while (p < input.size()) {
            if (input[p] == '<' && looks_like_jsx_start(input, p)) {
                bool closing = p + 1 < input.size() && input[p + 1] == '/';
                bool fragment_close = p + 2 < input.size() && input[p + 1] == '/' && input[p + 2] == '>';
                std::size_t j = p + 1;
                bool quoted = false, escaped = false;
                char q = 0;
                std::size_t attribute_braces = 0;
                std::size_t tag_angles = 0;
                for (; j < input.size(); ++j) {
                    const char c = input[j];
                    if (quoted) {
                        if (escaped) escaped = false;
                        else if (c == '\\') escaped = true;
                        else if (c == q) quoted = false;
                        continue;
                    }
                    if (c == '\'' || c == '"' || (attribute_braces && c == '`')) {
                        quoted = true; q = c; continue;
                    }
                    if (c == '{') { ++attribute_braces; continue; }
                    if (c == '}' && attribute_braces) { --attribute_braces; continue; }
                    if (attribute_braces == 0 && c == '<') { ++tag_angles; continue; }
                    if (c == '>' && attribute_braces == 0) {
                        if (tag_angles && j > p && input[j-1] == '=') continue;
                        if (tag_angles) { --tag_angles; continue; }
                        ++j; break;
                    }
                }
                if (j > input.size() || input[j - 1] != '>') {
                    error = "unterminated JSX tag"; output.clear(); return false;
                }
                const bool self_closing = j >= 2 && input[j - 2] == '/';
                // Preserve JSX tag spelling, but minify JavaScript expressions
                // inside attribute braces.
                {
                    bool attr_quote = false;
                    char attr_q = 0;
                    for (std::size_t k = p; k < j;) {
                        const char tc = input[k];
                        if (attr_quote) {
                            output.push_back(tc);
                            if (tc == attr_q) attr_quote = false;
                            ++k;
                            continue;
                        }
                        if (tc == '\'' || tc == '"') {
                            attr_quote = true; attr_q = tc; output.push_back(tc); ++k; continue;
                        }
                        if (tc == '{') {
                            std::size_t qpos = 0;
                            std::string scan_error;
                            if (!find_jsx_expression_end(input, k + 1, j, qpos, scan_error)) {
                                error = scan_error; output.clear(); return false;
                            }
                            std::string expr, e;
                            if (!jsx(input.substr(k + 1, qpos - k - 2), expr, e)) {
                                error = e; output.clear(); return false;
                            }
                            output.push_back('{'); output += expr; output.push_back('}');
                            k = qpos;
                            continue;
                        }
                        output.push_back(tc);
                        ++k;
                    }
                }
                if (!started) {
                    started = true;
                    depth = self_closing ? 0 : 1;
                } else if (closing || fragment_close) {
                    if (depth) --depth;
                } else if (!self_closing) {
                    ++depth;
                }
                p = j;
                if (started && depth == 0) break;
                continue;
            }
            if (input[p] == '{') {
                std::size_t j = 0;
                std::string scan_error;
                if (!find_jsx_expression_end(input, p + 1, input.size(), j, scan_error)) {
                    error = scan_error; output.clear(); return false;
                }
                std::string expr, e;
                if (!jsx(input.substr(p + 1, j - p - 2), expr, e)) {
                    error = e; output.clear(); return false;
                }
                output.push_back('{'); output += expr; output.push_back('}');
                p = j;
                continue;
            }
            output.push_back(input[p++]); // preserve JSX text exactly
        }
        if (!started || depth != 0) {
            error = "unterminated JSX element";
            output.clear();
            return false;
        }
        i = p;
        js_start = i;
    }

    if (!flush_js(input.size())) return false;
    error.clear();
    return true;
}

bool format_for_extension(const std::string& extension, Format& format) {
    std::string ext = lower(extension);
    if (!ext.empty() && ext.front() != '.') ext.insert(ext.begin(), '.');
    if (ext == ".html" || ext == ".htm") { format = Format::Html; return true; }
    if (ext == ".css") { format = Format::Css; return true; }
    if (ext == ".js" || ext == ".mjs" || ext == ".cjs") { format = Format::JavaScript; return true; }
    if (ext == ".jsx") { format = Format::Jsx; return true; }
    if (ext == ".json") { format = Format::Json; return true; }
    if (ext == ".xml") { format = Format::Xml; return true; }
    if (ext == ".svg") { format = Format::Svg; return true; }
    return false;
}

bool run(Format format, const std::string& input, std::string& output, std::string& error) {
    switch (format) {
        case Format::Html: return html(input, output, error);
        case Format::Css: return css(input, output, error);
        case Format::JavaScript: return javascript(input, output, error);
        case Format::Jsx: return jsx(input, output, error);
        case Format::Json: return json(input, output, error);
        case Format::Xml: return xml(input, output, error);
        case Format::Svg: return svg(input, output, error);
    }
    error = "unknown minification format";
    output.clear();
    return false;
}

} // namespace minify
