#include <minify/Minify.h>
#include "Json.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <functional>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace minify {
namespace {

bool ws(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

bool ascii_alpha(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool ascii_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

bool ascii_alnum(unsigned char c) {
    return ascii_alpha(c) || ascii_digit(c);
}

bool ascii_hex_digit(unsigned char c) {
    return ascii_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

std::string shortest_js_literal(const std::string& original,
                                std::initializer_list<std::string> alternatives) {
    const std::string* best = &original;
    for (const auto& candidate : alternatives) {
        if (candidate.size() < best->size()) best = &candidate;
    }
    return *best;
}

std::string shorten_js_integer(const std::string& token) {
    if (token.size() < 3 || token[0] != '0') return token;
    unsigned base = 0;
    if (token[1] == 'x' || token[1] == 'X') base = 16;
    else if (token[1] == 'b' || token[1] == 'B') base = 2;
    else if (token[1] == 'o' || token[1] == 'O') base = 8;
    else return token;

    std::uint64_t value = 0;
    for (std::size_t i = 2; i < token.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(token[i]);
        unsigned digit = 0;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return token;  // Separators, BigInt suffixes and malformed input.
        if (digit >= base || value > (UINT64_MAX - digit) / base) return token;
        value = value * base + digit;
    }
    const std::string decimal = std::to_string(value);
    return shortest_js_literal(token, {decimal});
}

std::string shorten_js_number(const std::string& token) {
    const std::string integer = shorten_js_integer(token);
    if (integer != token) return integer;
    if (token.size() > 2 && token[0] == '0' && token[1] == '.' &&
        ascii_digit(static_cast<unsigned char>(token[2])))
        return token.substr(1);
    return token;
}

std::string quote_js_string(const std::string& token, char target) {
    const char source = token.front();
    std::string result;
    result.reserve(token.size() + 2);
    result.push_back(target);
    for (std::size_t i = 1; i + 1 < token.size(); ++i) {
        const char c = token[i];
        if (c == '\\' && i + 2 < token.size()) {
            const char escaped = token[++i];
            if (escaped == '\'' || escaped == '"') {
                if (escaped == target) result.push_back('\\');
                result.push_back(escaped);
            } else {
                result.push_back('\\');
                result.push_back(escaped);
            }
        } else {
            if (c == target && target != source) result.push_back('\\');
            result.push_back(c);
        }
    }
    result.push_back(target);
    return result;
}

std::string shorten_js_string(const std::string& token) {
    const std::string single = quote_js_string(token, '\'');
    const std::string dual = quote_js_string(token, '"');
    return shortest_js_literal(token, {single, dual});
}

std::string shorten_js_boolean(const std::string& token, bool expression_safe) {
    if (!expression_safe) return token;
    return shortest_js_literal(token, {token == "true" ? "!0" : "!1"});
}

enum class JsTokenKind { Identifier, Punctuator, Number, String, Regex, Template };
enum class JsBraceKind { Unknown, Block, Object };

struct JsToken {
    JsTokenKind kind;
    std::string_view text;
    std::size_t begin;
    std::size_t end;
    JsBraceKind brace_kind = JsBraceKind::Unknown;
};

struct JsTokenRecorder {
    const std::string& source;
    std::vector<JsToken>& tokens;
    void record(JsTokenKind kind, std::size_t begin, std::size_t end,
                JsBraceKind brace_kind = JsBraceKind::Unknown) {
        tokens.push_back({kind, std::string_view(source.data() + begin, end - begin),
                          begin, end, brace_kind});
    }
};

struct JsReplacement {
    std::size_t begin;
    std::size_t end;
    std::string text;
};

struct JsRewriteResult {
    std::string text;
    bool valid = true;
    bool smaller = false;
};

bool js_rewrite_is_smaller(const std::vector<JsToken>& tokens,
                           std::size_t first, std::size_t last,
                           std::string_view replacement) {
    return first < tokens.size() && last < tokens.size() && first <= last &&
           replacement.size() < tokens[last].end - tokens[first].begin;
}

enum class JsValueKind { Unknown, Undefined, Null, Boolean, Number, BigInt, String };
enum class JsTruthiness { Unknown, Falsy, Truthy };
enum class JsInvocationKind { None, Call, OptionalCall, Construct };
enum class JsConversionKind { None, ToPrimitive, ToBoolean, ToNumber, ToNumeric,
                              ToString, ToPropertyKey, Compare };
enum class JsPropertyEffectKind { None, Read, Write, Delete, PrivateCheck, Computed, Optional };
enum class JsEffectKind { Pure, Read, Write, CallOrConstruct, MayThrow };
enum class JsCompletionKind { Normal, Return, Throw, Break, Continue };
struct JsEffectSummary {
    bool reads_state = false;
    bool writes_state = false;
    bool calls_user_code = false;
    bool may_throw = false;
    bool definitely_pure() const {
        return !reads_state && !writes_state && !calls_user_code && !may_throw;
    }
};

[[maybe_unused]] JsEffectSummary sequence_js_effects(JsEffectSummary left, const JsEffectSummary& right) {
    left.reads_state |= right.reads_state; left.writes_state |= right.writes_state;
    left.calls_user_code |= right.calls_user_code; left.may_throw |= right.may_throw;
    return left;
}

struct JsSemanticFacts {
    std::vector<JsValueKind> values;
    std::vector<JsEffectKind> effects;
    std::vector<JsCompletionKind> completions;
    std::vector<JsEffectSummary> effect_summaries;
    std::vector<JsTruthiness> truthiness;
    std::vector<JsInvocationKind> invocations;
    std::vector<JsConversionKind> conversions;
    std::vector<JsPropertyEffectKind> property_effects;
    std::vector<unsigned char> extended_effects;
};

JsSemanticFacts build_js_semantic_facts(const std::vector<JsToken>& tokens) {
    JsSemanticFacts facts;
    facts.values.resize(tokens.size(), JsValueKind::Unknown);
    facts.effects.resize(tokens.size(), JsEffectKind::MayThrow);
    facts.completions.resize(tokens.size(), JsCompletionKind::Normal);
    facts.effect_summaries.resize(tokens.size());
    facts.truthiness.resize(tokens.size(), JsTruthiness::Unknown);
    facts.invocations.resize(tokens.size(), JsInvocationKind::None);
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        const JsToken& token = tokens[index];
        if (token.kind == JsTokenKind::Number)
            facts.values[index] = !token.text.empty() && token.text.back() == 'n'
                ? JsValueKind::BigInt : JsValueKind::Number;
        else if (token.kind == JsTokenKind::String) facts.values[index] = JsValueKind::String;
        else if (token.text == "true" || token.text == "false") facts.values[index] = JsValueKind::Boolean;
        else if (token.text == "null") facts.values[index] = JsValueKind::Null;

        if (token.text == "false" || token.text == "null" ||
            token.text == "0" || token.text == "0n" || token.text == "''" || token.text == "\"\"")
            facts.truthiness[index] = JsTruthiness::Falsy;
        else if (token.text == "true" || token.kind == JsTokenKind::Regex ||
                 token.kind == JsTokenKind::String || token.kind == JsTokenKind::Template)
            facts.truthiness[index] = JsTruthiness::Truthy;

        if (facts.values[index] != JsValueKind::Unknown || token.kind == JsTokenKind::Regex ||
            token.kind == JsTokenKind::Template)
            facts.effects[index] = JsEffectKind::Pure;
        else if (token.kind == JsTokenKind::Identifier)
            facts.effects[index] = JsEffectKind::Read;
        else if (token.text == "=" || token.text == "+=" || token.text == "-=" ||
                 token.text == "*=" || token.text == "/=" || token.text == "%=" ||
                 token.text == "++" || token.text == "--")
            facts.effects[index] = JsEffectKind::Write;
        else if (token.text == "new" || (token.text == "(" && index &&
                 (tokens[index - 1].kind == JsTokenKind::Identifier ||
                  tokens[index - 1].text == ")" || tokens[index - 1].text == "]")))
            facts.effects[index] = JsEffectKind::CallOrConstruct;

        if (token.text == "return") facts.completions[index] = JsCompletionKind::Return;
        else if (token.text == "throw") facts.completions[index] = JsCompletionKind::Throw;
        else if (token.text == "break") facts.completions[index] = JsCompletionKind::Break;
        else if (token.text == "continue") facts.completions[index] = JsCompletionKind::Continue;

        JsEffectSummary& summary = facts.effect_summaries[index];
        if (facts.effects[index] == JsEffectKind::Read) summary.reads_state = true;
        else if (facts.effects[index] == JsEffectKind::Write) summary.writes_state = true;
        else if (facts.effects[index] == JsEffectKind::CallOrConstruct) {
            summary.calls_user_code = true;
            summary.may_throw = true;
        } else if (facts.effects[index] == JsEffectKind::MayThrow) {
            summary.may_throw = true;
        }
        if (token.text == "." || token.text == "[" || token.text == "in" ||
            token.text == "instanceof") {
            summary.reads_state = true;
            summary.calls_user_code = true;
            summary.may_throw = true;
        }
        if (token.text == "(" && index) {
            const std::string_view previous = tokens[index - 1].text;
            if (previous == "?.") facts.invocations[index] = JsInvocationKind::OptionalCall;
            else if (tokens[index - 1].kind == JsTokenKind::Identifier ||
                     previous == ")" || previous == "]")
                facts.invocations[index] = index >= 2 && tokens[index - 2].text == "new"
                    ? JsInvocationKind::Construct : JsInvocationKind::Call;
            if (facts.invocations[index] != JsInvocationKind::None) {
                summary.calls_user_code = true;
                summary.may_throw = true;
            }
        }
    }
    return facts;
}

void extend_js_semantic_facts(const std::vector<JsToken>& tokens, JsSemanticFacts& facts) {
    facts.conversions.resize(tokens.size(), JsConversionKind::None);
    facts.property_effects.resize(tokens.size(), JsPropertyEffectKind::None);
    facts.extended_effects.resize(tokens.size(), 0);
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        const JsToken& token = tokens[index];
        if (token.kind == JsTokenKind::Number) {
            const std::string_view number = token.text;
            facts.truthiness[index] = number == "0" || number == "0n" || number == "0.0"
                ? JsTruthiness::Falsy : JsTruthiness::Truthy;
        }
        if (token.text == "return" || token.text == "throw" || token.text == "break" ||
            token.text == "continue") facts.extended_effects[index] |= 16;
        if (token.text == "await" || token.text == "yield") facts.extended_effects[index] |= 8;
        if (token.text == "new" || token.text == "function" || token.text == "class")
            facts.extended_effects[index] |= 1;
        if (token.text == "for") facts.extended_effects[index] |= 4;
        if (token.text == "!" || token.text == "&&" || token.text == "||" || token.text == "?")
            facts.conversions[index] = JsConversionKind::ToBoolean;
        else if (token.text == "+" || token.text == "-") facts.conversions[index] = JsConversionKind::ToPrimitive;
        else if (token.text == "*" || token.text == "/" || token.text == "%" || token.text == "**")
            facts.conversions[index] = JsConversionKind::ToNumeric;
        else if (token.text == "<" || token.text == ">" || token.text == "<=" ||
                 token.text == ">=" || token.text == "==" || token.text == "!=")
            facts.conversions[index] = JsConversionKind::Compare;
        else if (token.text == "[" || token.text == ".") facts.conversions[index] = JsConversionKind::ToPropertyKey;
        if (token.text == ".") facts.property_effects[index] = JsPropertyEffectKind::Read;
        else if (token.text == "[") facts.property_effects[index] = JsPropertyEffectKind::Computed;
        else if (token.text == "?.") facts.property_effects[index] = JsPropertyEffectKind::Optional;
        else if (token.text == "delete") facts.property_effects[index] = JsPropertyEffectKind::Delete;
        else if (token.text == "#") facts.property_effects[index] = JsPropertyEffectKind::PrivateCheck;
    }
}

enum class JsSyntaxKind { Root, Parentheses, Brackets, Braces, Token };
enum class JsGroupRole { Root, Grouping, Arguments, Parameters, ArrayLiteral, ObjectLiteral, Block, Unknown };
enum class JsStatementKind {
    None, Block, Empty, Expression, Variable, If, For, While, DoWhile, Switch,
    Try, Catch, Finally, Label, Break, Continue, Return, Throw, Function, Class,
    Import, Export
};
enum class JsExpressionKind {
    None, Primary, Member, Call, New, Postfix, Unary, Exponentiation,
    Multiplicative, Additive, Shift, Relational, Equality, BitwiseAnd,
    BitwiseXor, BitwiseOr, LogicalAnd, LogicalOr, Nullish, Conditional,
    Assignment, Yield, Sequence, Arrow
};
enum class JsBindingPatternKind { None, Identifier, Object, Array, Rest, DefaultValue };
enum class JsFunctionContext {
    None, Ordinary, Async, Generator, AsyncGenerator, Arrow, Method, Getter,
    Setter, Constructor, Class, StaticBlock
};
enum class JsModuleRole {
    None, ImportKeyword, ImportBinding, ImportSource, DynamicImport,
    ExportKeyword, ExportLocal, ExportedName, ReExportSource, ExportAll
};
enum class JsIdentifierRole {
    Unknown, Binding, Reference, PropertyKey, ShorthandProperty, MemberProperty,
    Label, ImportExportName, PrivateName
};
struct JsSyntaxNode {
    JsSyntaxKind kind;
    JsGroupRole role;
    std::size_t first_token;
    std::size_t last_token;
    std::size_t parent;
};
struct JsConcreteSyntax {
    std::vector<JsSyntaxNode> nodes;
    std::vector<std::vector<std::size_t>> children;
    std::vector<std::size_t> node_at_token;
    std::vector<std::size_t> matching_token;
    std::vector<JsIdentifierRole> identifier_roles;
    std::vector<JsStatementKind> statement_kinds;
    std::vector<JsExpressionKind> expression_kinds;
    std::vector<unsigned char> expression_precedence;
    std::vector<JsBindingPatternKind> binding_patterns;
    std::vector<JsFunctionContext> function_contexts;
    std::vector<JsModuleRole> module_roles;
    std::vector<bool> parameter_initializer_tokens;
    bool balanced = true;
};

struct JsControlFlowGraph {
    std::vector<std::vector<std::size_t>> successors;
    std::vector<bool> abrupt;
    std::size_t entry = 0;
    std::size_t normal_exit = 0;
    std::size_t abrupt_exit = 0;
};

[[maybe_unused]] JsControlFlowGraph build_js_control_flow(const std::vector<JsToken>& tokens,
                                         const JsConcreteSyntax& syntax) {
    JsControlFlowGraph flow;
    flow.successors.resize(tokens.size());
    flow.abrupt.resize(tokens.size(), false);
    flow.normal_exit = tokens.size();
    flow.abrupt_exit = tokens.size() + 1;
    for (std::size_t token = 0; token < tokens.size(); ++token) {
        const JsStatementKind statement = token < syntax.statement_kinds.size()
            ? syntax.statement_kinds[token] : JsStatementKind::None;
        const bool abrupt = statement == JsStatementKind::Return ||
                            statement == JsStatementKind::Throw ||
                            statement == JsStatementKind::Break ||
                            statement == JsStatementKind::Continue;
        flow.abrupt[token] = abrupt;
        if (!abrupt && token + 1 < tokens.size()) flow.successors[token].push_back(token + 1);
        else if (!abrupt) flow.successors[token].push_back(flow.normal_exit);
        else flow.successors[token].push_back(flow.abrupt_exit);
        if ((statement == JsStatementKind::If || statement == JsStatementKind::For ||
             statement == JsStatementKind::While || statement == JsStatementKind::Switch) &&
            token + 1 < tokens.size() && tokens[token + 1].text == "(" &&
            syntax.matching_token[token + 1] < tokens.size()) {
            const std::size_t body = syntax.matching_token[token + 1] + 1;
            std::size_t body_end = body;
            if (body < tokens.size() && tokens[body].text == "{" &&
                syntax.matching_token[body] < tokens.size())
                body_end = syntax.matching_token[body];
            else
                while (body_end + 1 < tokens.size() && tokens[body_end].text != ";") ++body_end;
            const std::size_t after_body = body_end + 1;
            flow.successors[token].push_back(after_body < tokens.size()
                ? after_body : flow.normal_exit);
            if (statement == JsStatementKind::For || statement == JsStatementKind::While)
                flow.successors[body_end].push_back(token + 1);
        }
        if (statement == JsStatementKind::DoWhile && token + 1 < tokens.size()) {
            const std::size_t body = token + 1;
            std::size_t body_end = body;
            if (tokens[body].text == "{" && syntax.matching_token[body] < tokens.size())
                body_end = syntax.matching_token[body];
            while (body_end + 1 < tokens.size() && tokens[body_end + 1].text != "while") ++body_end;
            if (body_end + 1 < tokens.size()) flow.successors[body_end].push_back(body_end + 1);
        }
    }
    for (std::size_t token = 0; token < tokens.size(); ++token) {
        const std::string_view op = tokens[token].text;
        const bool paired = token + 1 < tokens.size() && tokens[token + 1].text == op &&
            (op == "&" || op == "|" || op == "?");
        const bool conditional = op == "?" && !paired;
        if (!paired && !conditional) continue;
        unsigned nested = 0;
        std::size_t colon = tokens.size(), boundary = tokens.size();
        for (std::size_t cursor = token + 1; cursor < tokens.size(); ++cursor) {
            const std::string_view text = tokens[cursor].text;
            if (text == "(" || text == "[" || text == "{") ++nested;
            else if (text == ")" || text == "]" || text == "}") {
                if (!nested) { boundary = cursor; break; }
                --nested;
            } else if (!nested && conditional && text == ":" && colon == tokens.size())
                colon = cursor;
            else if (!nested && (text == ";" || text == ",")) {
                boundary = cursor; break;
            }
        }
        if (conditional && colon < tokens.size()) flow.successors[token].push_back(colon + 1);
        else if (boundary < tokens.size()) flow.successors[token].push_back(boundary);
        else flow.successors[token].push_back(flow.normal_exit);
    }
    return flow;
}

std::string build_js_cfg_signature(const std::vector<JsToken>& tokens,
                                   const JsConcreteSyntax& syntax) {
    const JsControlFlowGraph flow = build_js_control_flow(tokens, syntax);
    std::string signature = "E" + std::to_string(flow.entry) + ";N" +
        std::to_string(flow.normal_exit) + ";X" + std::to_string(flow.abrupt_exit) + ";";
    for (std::size_t from = 0; from < flow.successors.size(); ++from)
        for (std::size_t to : flow.successors[from])
            signature += std::to_string(from) + ">" + std::to_string(to) + ";";
    return signature;
}

bool js_precedes_block(const std::vector<JsToken>& tokens, std::size_t open) {
    if (open == 0) return true;
    const std::string_view previous = tokens[open - 1].text;
    if (previous == ")") return true;
    if (previous == "else" || previous == "try" || previous == "finally" ||
        previous == "do" || previous == "=>") return true;
    if (open >= 2 && (tokens[open - 2].text == "class" ||
                      tokens[open - 2].text == "function" ||
                      tokens[open - 2].text == "catch")) return true;
    return false;
}

JsGroupRole js_group_role(const std::vector<JsToken>& tokens, JsSyntaxKind kind,
                          std::size_t open) {
    if (kind == JsSyntaxKind::Brackets) return JsGroupRole::ArrayLiteral;
    if (kind == JsSyntaxKind::Braces && tokens[open].brace_kind != JsBraceKind::Unknown)
        return tokens[open].brace_kind == JsBraceKind::Block
            ? JsGroupRole::Block : JsGroupRole::ObjectLiteral;
    if (kind == JsSyntaxKind::Braces)
        return js_precedes_block(tokens, open) ? JsGroupRole::Block : JsGroupRole::ObjectLiteral;
    if (kind != JsSyntaxKind::Parentheses) return JsGroupRole::Unknown;
    if (open && (tokens[open - 1].kind == JsTokenKind::Identifier ||
                 tokens[open - 1].text == ")" || tokens[open - 1].text == "]" ||
                 tokens[open - 1].text == "super" || tokens[open - 1].text == "import"))
        return JsGroupRole::Arguments;
    return JsGroupRole::Grouping;
}

JsConcreteSyntax build_js_concrete_syntax(const std::vector<JsToken>& tokens) {
    JsConcreteSyntax syntax;
    // At most one syntax node is created per token plus the root. Reserve the
    // complete outer storage up front: large bundles otherwise repeatedly move
    // both node records and child vectors while constructing the concrete tree.
    syntax.nodes.reserve(tokens.size() + 1);
    syntax.children.reserve(tokens.size() + 1);
    syntax.nodes.push_back({JsSyntaxKind::Root, JsGroupRole::Root, 0, tokens.size(), 0});
    syntax.children.emplace_back();
    syntax.node_at_token.resize(tokens.size(), 0);
    syntax.matching_token.resize(tokens.size(), tokens.size());
    syntax.identifier_roles.resize(tokens.size(), JsIdentifierRole::Unknown);
    syntax.statement_kinds.resize(tokens.size(), JsStatementKind::None);
    syntax.expression_kinds.resize(tokens.size(), JsExpressionKind::None);
    syntax.expression_precedence.resize(tokens.size(), 0);
    syntax.binding_patterns.resize(tokens.size(), JsBindingPatternKind::None);
    syntax.function_contexts.resize(tokens.size(), JsFunctionContext::None);
    syntax.module_roles.resize(tokens.size(), JsModuleRole::None);
    syntax.parameter_initializer_tokens.resize(tokens.size(), false);
    std::vector<std::size_t> groups;
    groups.reserve(64);
    groups.push_back(0);
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        const std::string_view text = tokens[index].text;
        JsSyntaxKind kind = text == "(" ? JsSyntaxKind::Parentheses
            : text == "[" ? JsSyntaxKind::Brackets
            : text == "{" ? JsSyntaxKind::Braces : JsSyntaxKind::Token;
        if (kind != JsSyntaxKind::Token) {
            const std::size_t parent = groups.back();
            syntax.nodes.push_back({kind, js_group_role(tokens, kind, index), index,
                                    tokens.size(), parent});
            syntax.children.emplace_back();
            syntax.children[parent].push_back(syntax.nodes.size() - 1);
            groups.push_back(syntax.nodes.size() - 1);
            syntax.node_at_token[index] = groups.back();
        } else if (text == ")" || text == "]" || text == "}") {
            const JsSyntaxKind expected = text == ")" ? JsSyntaxKind::Parentheses
                : text == "]" ? JsSyntaxKind::Brackets : JsSyntaxKind::Braces;
            if (groups.size() == 1 || syntax.nodes[groups.back()].kind != expected)
                syntax.balanced = false;
            else {
                const std::size_t group = groups.back();
                syntax.nodes[group].last_token = index + 1;
                syntax.node_at_token[index] = group;
                syntax.matching_token[syntax.nodes[group].first_token] = index;
                syntax.matching_token[index] = syntax.nodes[group].first_token;
                groups.pop_back();
            }
        } else {
            const std::size_t parent = groups.back();
            syntax.nodes.push_back({JsSyntaxKind::Token, JsGroupRole::Unknown,
                                    index, index + 1, parent});
            syntax.children.emplace_back();
            syntax.children[parent].push_back(syntax.nodes.size() - 1);
            syntax.node_at_token[index] = syntax.nodes.size() - 1;
        }
    }
    for (std::size_t function = 0; function < tokens.size(); ++function) {
        if (tokens[function].text != "function") continue;
        std::size_t open = function + 1;
        if (open < tokens.size() && tokens[open].text == "*") ++open;
        if (open < tokens.size() && tokens[open].kind == JsTokenKind::Identifier) ++open;
        if (open >= tokens.size() || tokens[open].text != "(" ||
            syntax.matching_token[open] >= tokens.size()) continue;
        const std::size_t close = syntax.matching_token[open];
        unsigned nested = 0;
        bool initializer = false;
        for (std::size_t cursor = open + 1; cursor < close; ++cursor) {
            const std::string_view text = tokens[cursor].text;
            if (text == "(" || text == "[" || text == "{") ++nested;
            else if ((text == ")" || text == "]" || text == "}") && nested) --nested;
            else if (!nested && text == "=") initializer = true;
            else if (!nested && text == ",") initializer = false;
            else if (initializer) syntax.parameter_initializer_tokens[cursor] = true;
        }
    }
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        if (tokens[index].text == "import") {
            syntax.module_roles[index] = JsModuleRole::ImportKeyword;
            if (index + 1 < tokens.size() && tokens[index + 1].text == "(") {
                syntax.module_roles[index] = JsModuleRole::DynamicImport;
                continue;
            }
            bool after_from = false;
            for (std::size_t cursor = index + 1; cursor < tokens.size() && tokens[cursor].text != ";"; ++cursor) {
                if (tokens[cursor].text == "from") after_from = true;
                else if (tokens[cursor].kind == JsTokenKind::String)
                    syntax.module_roles[cursor] = JsModuleRole::ImportSource;
                else if (!after_from && tokens[cursor].kind == JsTokenKind::Identifier &&
                         tokens[cursor].text != "as")
                    syntax.module_roles[cursor] = JsModuleRole::ImportBinding;
            }
        } else if (tokens[index].text == "export") {
            syntax.module_roles[index] = JsModuleRole::ExportKeyword;
            bool after_as = false, after_from = false;
            for (std::size_t cursor = index + 1; cursor < tokens.size() && tokens[cursor].text != ";"; ++cursor) {
                if (tokens[cursor].text == "*") syntax.module_roles[cursor] = JsModuleRole::ExportAll;
                else if (tokens[cursor].text == "as") after_as = true;
                else if (tokens[cursor].text == ",") after_as = false;
                else if (tokens[cursor].text == "from") after_from = true;
                else if (after_from && tokens[cursor].kind == JsTokenKind::String)
                    syntax.module_roles[cursor] = JsModuleRole::ReExportSource;
                else if (tokens[cursor].kind == JsTokenKind::Identifier)
                    syntax.module_roles[cursor] = after_as ? JsModuleRole::ExportedName : JsModuleRole::ExportLocal;
            }
        }
    }
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        if (tokens[index].text == "class") {
            syntax.function_contexts[index] = JsFunctionContext::Class;
            continue;
        }
        if (tokens[index].text == "function") {
            const bool async = index && tokens[index - 1].text == "async";
            const bool generator = index + 1 < tokens.size() && tokens[index + 1].text == "*";
            syntax.function_contexts[index] = async && generator ? JsFunctionContext::AsyncGenerator
                : async ? JsFunctionContext::Async
                : generator ? JsFunctionContext::Generator : JsFunctionContext::Ordinary;
            continue;
        }
        if (tokens[index].text == "=" && index + 1 < tokens.size() && tokens[index + 1].text == ">")
            syntax.function_contexts[index] = JsFunctionContext::Arrow;
        else if (tokens[index].text == "constructor" && index + 1 < tokens.size() &&
                 tokens[index + 1].text == "(")
            syntax.function_contexts[index] = JsFunctionContext::Constructor;
        else if ((tokens[index].text == "get" || tokens[index].text == "set") &&
                 index + 2 < tokens.size() && tokens[index + 2].text == "(")
            syntax.function_contexts[index] = tokens[index].text == "get"
                ? JsFunctionContext::Getter : JsFunctionContext::Setter;
        else if (tokens[index].text == "static" && index + 1 < tokens.size() &&
                 tokens[index + 1].text == "{")
            syntax.function_contexts[index] = JsFunctionContext::StaticBlock;
    }
    // Retain pattern boundaries independently of the renamer. This inventory
    // is intentionally non-mutating until binding/reference coverage is complete.
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        const std::string_view text = tokens[index].text;
        const bool declaration_start = index &&
            (tokens[index - 1].text == "var" || tokens[index - 1].text == "let" ||
             tokens[index - 1].text == "const" || tokens[index - 1].text == "catch");
        if (declaration_start && text == "{")
            syntax.binding_patterns[index] = JsBindingPatternKind::Object;
        else if (declaration_start && text == "[")
            syntax.binding_patterns[index] = JsBindingPatternKind::Array;
        else if (declaration_start && tokens[index].kind == JsTokenKind::Identifier)
            syntax.binding_patterns[index] = JsBindingPatternKind::Identifier;
        if (index >= 3 && tokens[index - 3].text == "." && tokens[index - 2].text == "." &&
            tokens[index - 1].text == "." && tokens[index].kind == JsTokenKind::Identifier)
            syntax.binding_patterns[index] = JsBindingPatternKind::Rest;
        if (text == "=" && index && syntax.binding_patterns[index - 1] != JsBindingPatternKind::None)
            syntax.binding_patterns[index] = JsBindingPatternKind::DefaultValue;
    }
    static const std::unordered_map<std::string_view,
        std::pair<JsExpressionKind, unsigned char>> operators = {
        {",", {JsExpressionKind::Sequence, 1}}, {"=", {JsExpressionKind::Assignment, 2}},
        {"+=", {JsExpressionKind::Assignment, 2}}, {"-=", {JsExpressionKind::Assignment, 2}},
        {"=>", {JsExpressionKind::Arrow, 2}}, {"?", {JsExpressionKind::Conditional, 3}},
        {"??", {JsExpressionKind::Nullish, 4}}, {"||", {JsExpressionKind::LogicalOr, 4}},
        {"&&", {JsExpressionKind::LogicalAnd, 5}}, {"|", {JsExpressionKind::BitwiseOr, 6}},
        {"^", {JsExpressionKind::BitwiseXor, 7}}, {"&", {JsExpressionKind::BitwiseAnd, 8}},
        {"==", {JsExpressionKind::Equality, 9}}, {"===", {JsExpressionKind::Equality, 9}},
        {"!=", {JsExpressionKind::Equality, 9}}, {"!==", {JsExpressionKind::Equality, 9}},
        {"<", {JsExpressionKind::Relational, 10}}, {">", {JsExpressionKind::Relational, 10}},
        {"<=", {JsExpressionKind::Relational, 10}}, {">=", {JsExpressionKind::Relational, 10}},
        {"in", {JsExpressionKind::Relational, 10}}, {"instanceof", {JsExpressionKind::Relational, 10}},
        {"<<", {JsExpressionKind::Shift, 11}}, {">>", {JsExpressionKind::Shift, 11}},
        {">>>", {JsExpressionKind::Shift, 11}}, {"+", {JsExpressionKind::Additive, 12}},
        {"-", {JsExpressionKind::Additive, 12}}, {"*", {JsExpressionKind::Multiplicative, 13}},
        {"/", {JsExpressionKind::Multiplicative, 13}}, {"%", {JsExpressionKind::Multiplicative, 13}},
        {"**", {JsExpressionKind::Exponentiation, 14}}, {".", {JsExpressionKind::Member, 18}},
        {"?.", {JsExpressionKind::Member, 18}}
    };
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        const auto found = operators.find(tokens[index].text);
        if (found != operators.end()) {
            syntax.expression_kinds[index] = found->second.first;
            syntax.expression_precedence[index] = found->second.second;
        } else if (tokens[index].kind == JsTokenKind::Number ||
                   tokens[index].kind == JsTokenKind::String ||
                   tokens[index].kind == JsTokenKind::Regex ||
                   tokens[index].kind == JsTokenKind::Template ||
                   tokens[index].kind == JsTokenKind::Identifier) {
            syntax.expression_kinds[index] = JsExpressionKind::Primary;
            syntax.expression_precedence[index] = 20;
        }
    }
    if (groups.size() != 1) syntax.balanced = false;
    static const std::unordered_map<std::string_view, JsStatementKind> statement_words = {
        {"var", JsStatementKind::Variable}, {"let", JsStatementKind::Variable},
        {"const", JsStatementKind::Variable}, {"if", JsStatementKind::If},
        {"for", JsStatementKind::For}, {"while", JsStatementKind::While},
        {"do", JsStatementKind::DoWhile}, {"switch", JsStatementKind::Switch},
        {"try", JsStatementKind::Try}, {"catch", JsStatementKind::Catch},
        {"finally", JsStatementKind::Finally}, {"break", JsStatementKind::Break},
        {"continue", JsStatementKind::Continue}, {"return", JsStatementKind::Return},
        {"throw", JsStatementKind::Throw}, {"function", JsStatementKind::Function},
        {"class", JsStatementKind::Class}, {"import", JsStatementKind::Import},
        {"export", JsStatementKind::Export}
    };
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        const auto statement = statement_words.find(tokens[index].text);
        if (statement != statement_words.end()) syntax.statement_kinds[index] = statement->second;
        else if (tokens[index].text == "{") syntax.statement_kinds[index] = JsStatementKind::Block;
        else if (tokens[index].text == ";") syntax.statement_kinds[index] = JsStatementKind::Empty;
        else if (tokens[index].kind == JsTokenKind::Identifier && index + 1 < tokens.size() &&
                 tokens[index + 1].text == ":")
            syntax.statement_kinds[index] = JsStatementKind::Label;
    }
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        if (tokens[index].kind != JsTokenKind::Identifier) continue;
        const std::string_view previous = index ? tokens[index - 1].text : std::string_view();
        const std::string_view next = index + 1 < tokens.size() ? tokens[index + 1].text : std::string_view();
        const std::size_t node = syntax.node_at_token[index];
        const std::size_t parent = node < syntax.nodes.size() ? syntax.nodes[node].parent : 0;
        const bool object_member = parent < syntax.nodes.size() &&
                                   syntax.nodes[parent].role == JsGroupRole::ObjectLiteral;
        const bool object_member_start = object_member &&
                                         (previous == "{" || previous == ",");
        const bool statement_label = next == ":" && !object_member &&
            (index == 0 || previous == ";" || previous == "{" || previous == "}");
        const bool method_key = next == "(" && index + 1 < syntax.matching_token.size() &&
            syntax.matching_token[index + 1] < tokens.size() &&
            syntax.matching_token[index + 1] + 1 < tokens.size() &&
            tokens[syntax.matching_token[index + 1] + 1].text == "{" &&
            previous != "function";
        JsIdentifierRole role = JsIdentifierRole::Reference;
        const bool spread = index >= 3 && tokens[index - 1].text == "." &&
                            tokens[index - 2].text == "." && tokens[index - 3].text == ".";
        if (syntax.module_roles[index] == JsModuleRole::ExportLocal)
            role = JsIdentifierRole::Reference;
        else if (syntax.module_roles[index] == JsModuleRole::ExportedName ||
                 syntax.module_roles[index] == JsModuleRole::ImportSource ||
                 syntax.module_roles[index] == JsModuleRole::ReExportSource)
            role = JsIdentifierRole::ImportExportName;
        else if (method_key) role = JsIdentifierRole::PropertyKey;
        else if (previous == "." && !spread) role = JsIdentifierRole::MemberProperty;
        else if (previous == "#") role = JsIdentifierRole::PrivateName;
        else if (next == ":" && object_member_start) role = JsIdentifierRole::PropertyKey;
        else if (previous == "break" || previous == "continue" || statement_label)
            role = JsIdentifierRole::Label;
        else if (previous == "import" || previous == "export" || previous == "as")
            role = JsIdentifierRole::ImportExportName;
        if (role == JsIdentifierRole::Reference && parent < syntax.nodes.size() &&
            syntax.nodes[parent].role == JsGroupRole::ObjectLiteral && object_member_start &&
            (next == "}" || next == ","))
            role = JsIdentifierRole::ShorthandProperty;
        syntax.identifier_roles[index] = role;
    }
    return syntax;
}

enum class JsScopeKind { Script, Module, Function, Block, Class, Catch };
enum class JsBindingKind { Parameter, Var, Lexical, Function, Class, Catch, Import, Unknown };
struct JsBinding {
    std::string_view name;
    std::size_t token;
    std::size_t scope;
    JsBindingKind kind;
    std::vector<std::size_t> declaration_tokens;
};
enum class JsReferenceAccess { Read, Write, ReadWrite };
enum class JsEscapeKind { Local, Captured, Returned, Passed, Stored, Exported, Unknown };
struct JsReference {
    std::size_t token;
    std::size_t scope;
    std::size_t binding;
    std::size_t binding_scope;
    JsReferenceAccess access;
    bool captured;
    bool parameter_initializer;
    std::size_t capture_depth;
};
struct JsScope {
    JsScopeKind kind; std::size_t parent; std::size_t first_token; std::size_t last_token;
    bool dynamic_lookup = false; std::vector<std::size_t> bindings;
    bool descendant_dynamic_lookup = false;
};
struct JsScopeGraph {
    std::vector<JsScope> scopes;
    std::vector<JsBinding> bindings;
    std::vector<JsReference> references;
    std::vector<std::size_t> scope_at_token;
    std::vector<std::vector<std::size_t>> references_by_binding;
    std::vector<std::vector<std::size_t>> scope_children;
    std::vector<std::size_t> containing_function;
    std::vector<bool> control_flow_complex;
    std::vector<std::size_t> reference_at_token;
    std::vector<std::size_t> unresolved_references;
    std::vector<std::vector<std::size_t>> captures_by_function;
    std::vector<JsEscapeKind> binding_escape;
    struct BindingUseCount { std::size_t reads = 0, writes = 0, captures = 0; };
    std::vector<BindingUseCount> binding_uses;
};

bool js_function_is_declaration(const std::vector<JsToken>& tokens, std::size_t function) {
    if (function == 0) return true;
    std::size_t previous = function - 1;
    if (tokens[previous].text == "async" && previous) --previous;
    const std::string_view context = tokens[previous].text;
    if (context == "{" || context == "}" || context == ";" ||
        context == "export" || context == "default") return true;
    static const std::unordered_set<std::string_view> expression_prefixes = {
        "return","throw","case","delete","void","typeof","new","yield","await"
    };
    // Two ordinary identifiers cannot be adjacent in an expression. When a
    // function follows one, ASI has started a function declaration statement.
    return tokens[previous].kind == JsTokenKind::Identifier &&
           expression_prefixes.count(context) == 0;
}

JsScopeGraph build_js_scope_graph(const std::vector<JsToken>& tokens) {
    const bool module = std::any_of(tokens.begin(), tokens.end(), [](const JsToken& token) {
        return token.text == "import" || token.text == "export";
    });
    JsScopeGraph graph;
    // Scope/binding/reference counts are bounded by token count. Reserving a
    // conservative fraction avoids repeated large-vector relocation without
    // committing token-sized memory for the common case.
    const std::size_t structural_hint = tokens.size() / 8 + 8;
    graph.scopes.reserve(tokens.size() / 16 + 8);
    graph.bindings.reserve(structural_hint);
    graph.references.reserve(tokens.size() / 4 + 8);
    graph.unresolved_references.reserve(tokens.size() / 32 + 4);
    graph.scope_at_token.resize(tokens.size(), 0);
    graph.scopes.push_back({module ? JsScopeKind::Module : JsScopeKind::Script, 0, 0,
                            tokens.size(), false, {}});
    std::vector<std::size_t> scopes;
    scopes.reserve(64);
    scopes.push_back(0);
    std::vector<bool> brace_creates_scope;
    brace_creates_scope.reserve(64);
    std::vector<bool> binding_at_token(tokens.size(), false);
    // var/function redeclarations are the only declarations that coalesce.
    // Keep a per-scope index so large generated scopes do not linearly rescan
    // every preceding binding for each declaration.
    std::vector<std::unordered_map<std::string_view, std::size_t>>
        hoisted_bindings_by_name(1);
    JsScopeKind pending = JsScopeKind::Block;
    auto add_binding = [&](std::size_t scope, std::size_t token, JsBindingKind kind) {
        if (binding_at_token[token]) return;
        binding_at_token[token] = true;
        if (hoisted_bindings_by_name.size() <= scope)
            hoisted_bindings_by_name.resize(scope + 1);
        if (kind == JsBindingKind::Var || kind == JsBindingKind::Function) {
            auto& by_name = hoisted_bindings_by_name[scope];
            const auto found = by_name.find(tokens[token].text);
            if (found != by_name.end()) {
                graph.bindings[found->second].declaration_tokens.push_back(token);
                return;
            }
        }
        const std::size_t binding = graph.bindings.size();
        graph.bindings.push_back({tokens[token].text, token, scope, kind, {token}});
        graph.scopes[scope].bindings.push_back(binding);
        if (kind == JsBindingKind::Var || kind == JsBindingKind::Function)
            hoisted_bindings_by_name[scope].emplace(tokens[token].text, binding);
    };
    auto mark_dynamic_function = [&](std::size_t scope) {
        for (;;) {
            graph.scopes[scope].dynamic_lookup = true;
            if (graph.scopes[scope].kind == JsScopeKind::Function || scope == 0) break;
            scope = graph.scopes[scope].parent;
        }
    };
    const auto matching_close = [&](std::size_t open) {
        const std::string_view opening = tokens[open].text;
        const std::string_view closing = opening == "{" ? "}" : "]";
        std::size_t depth = 1;
        for (std::size_t token = open + 1; token < tokens.size(); ++token) {
            if (tokens[token].text == opening) ++depth;
            else if (tokens[token].text == closing && --depth == 0) return token;
        }
        return tokens.size();
    };
    std::function<void(std::size_t,std::size_t,std::size_t,JsBindingKind)> add_pattern_bindings;
    add_pattern_bindings = [&](std::size_t scope, std::size_t open,
                               std::size_t close, JsBindingKind kind) {
        const bool object = tokens[open].text == "{";
        for (std::size_t token = open + 1; token < close;) {
            if (tokens[token].text == ",") { ++token; continue; }
            if (token + 2 < close && tokens[token].text == "." &&
                tokens[token + 1].text == "." && tokens[token + 2].text == ".")
                token += 3;
            if (token >= close) break;
            if (tokens[token].text == "{" || tokens[token].text == "[") {
                const std::size_t nested_close = matching_close(token);
                if (nested_close >= close) break;
                add_pattern_bindings(scope, token, nested_close, kind);
                token = nested_close + 1;
            } else if (tokens[token].kind == JsTokenKind::Identifier) {
                if (object && token + 1 < close && tokens[token + 1].text == ":") {
                    token += 2;
                    if (token < close && (tokens[token].text == "{" || tokens[token].text == "[")) {
                        const std::size_t nested_close = matching_close(token);
                        if (nested_close >= close) break;
                        add_pattern_bindings(scope, token, nested_close, kind);
                        token = nested_close + 1;
                    } else if (token < close && tokens[token].kind == JsTokenKind::Identifier) {
                        graph.scope_at_token[token] = scope;
                        add_binding(scope, token, kind);
                        ++token;
                    }
                } else {
                    graph.scope_at_token[token] = scope;
                    add_binding(scope, token, kind);
                    ++token;
                }
            } else {
                ++token;
            }
            if (token < close && tokens[token].text == "=") {
                unsigned nested = 0;
                do {
                    ++token;
                    if (token >= close) break;
                    if (tokens[token].text == "(" || tokens[token].text == "[" || tokens[token].text == "{") ++nested;
                    else if ((tokens[token].text == ")" || tokens[token].text == "]" || tokens[token].text == "}") && nested) --nested;
                } while (nested || tokens[token].text != ",");
            }
            while (token < close && tokens[token].text != ",") ++token;
        }
    };
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        graph.scope_at_token[index] = scopes.back();
        const std::string_view text = tokens[index].text;
        if (text == "function") pending = JsScopeKind::Function;
        else if (text == "class") pending = JsScopeKind::Class;
        else if (text == "catch") pending = JsScopeKind::Catch;
        const bool bare_direct_eval = text == "eval" && index + 1 < tokens.size() &&
                                      tokens[index + 1].text == "(" &&
                                      (index == 0 || tokens[index - 1].text != ".");
        const bool parenthesized_direct_eval = text == "eval" && index &&
            tokens[index - 1].text == "(" && index + 2 < tokens.size() &&
            tokens[index + 1].text == ")" && tokens[index + 2].text == "(";
        const bool direct_eval = bare_direct_eval || parenthesized_direct_eval;
        if (direct_eval || text == "with") mark_dynamic_function(scopes.back());
        if (text == "{") {
            const bool parameter_pattern = pending == JsScopeKind::Function && index &&
                (tokens[index - 1].text == "(" || tokens[index - 1].text == ",");
            const bool creates_scope = !parameter_pattern &&
                                       tokens[index].brace_kind != JsBraceKind::Object;
            brace_creates_scope.push_back(creates_scope);
            if (!creates_scope) continue;
            const bool arrow_body = index >= 2 && tokens[index - 1].text == ">" &&
                                    tokens[index - 2].text == "=";
            bool method_body = false;
            if (!arrow_body && pending == JsScopeKind::Block && index &&
                tokens[index - 1].text == ")") {
                std::size_t open = index - 1, depth = 1;
                while (open && depth) {
                    --open;
                    if (tokens[open].text == ")") ++depth;
                    else if (tokens[open].text == "(") --depth;
                }
                const std::string_view before = open ? tokens[open - 1].text : std::string_view();
                method_body = before != "if" && before != "for" && before != "while" &&
                    before != "switch" && before != "catch" && before != "with" &&
                    before != "function";
            }
            const bool static_block = index && tokens[index - 1].text == "static";
            const JsScopeKind opening_kind = arrow_body || method_body || static_block
                ? JsScopeKind::Function : pending;
            graph.scopes.push_back({opening_kind, scopes.back(), index, tokens.size(), false, {}});
            scopes.push_back(graph.scopes.size() - 1);
            graph.scope_at_token[index] = scopes.back();
            if ((opening_kind == JsScopeKind::Function && index && tokens[index - 1].text == ")") ||
                arrow_body) {
                std::size_t close = arrow_body ? index - 3 : index - 1;
                if (arrow_body && tokens[close].text != ")") {
                    if (tokens[close].kind == JsTokenKind::Identifier) {
                        graph.scope_at_token[close] = scopes.back();
                        add_binding(scopes.back(), close, JsBindingKind::Parameter);
                    }
                    pending = JsScopeKind::Block;
                    continue;
                }
                std::size_t depth = 1, open = close;
                while (open && depth) { --open; if (tokens[open].text == ")") ++depth; else if (tokens[open].text == "(") --depth; }
                if (!depth) {
                    for (std::size_t p = open + 1; p < close; ++p)
                        graph.scope_at_token[p] = scopes.back();
                    unsigned nested = 0;
                    bool binding_position = true;
                    for (std::size_t p = open + 1; p < close; ++p) {
                        const std::string_view parameter = tokens[p].text;
                        if (binding_position && nested == 0) {
                            if (tokens[p].kind == JsTokenKind::Identifier)
                                add_binding(scopes.back(), p, JsBindingKind::Parameter);
                            else if (tokens[p].text == "{" || tokens[p].text == "[") {
                                const std::size_t pattern_close = matching_close(p);
                                if (pattern_close < close)
                                    add_pattern_bindings(scopes.back(), p, pattern_close,
                                                         JsBindingKind::Parameter);
                            }
                            else if (p + 3 < close && tokens[p].text == "." &&
                                     tokens[p + 1].text == "." && tokens[p + 2].text == "." &&
                                     tokens[p + 3].kind == JsTokenKind::Identifier) {
                                graph.scope_at_token[p + 3] = scopes.back();
                                add_binding(scopes.back(), p + 3, JsBindingKind::Parameter);
                            }
                            binding_position = false;
                        }
                        if (parameter == "(" || parameter == "[" || parameter == "{") ++nested;
                        else if ((parameter == ")" || parameter == "]" || parameter == "}") && nested) --nested;
                        else if (parameter == "," && nested == 0) binding_position = true;
                    }
                }
                if (!arrow_body) {
                    std::size_t function_token = open;
                    while (function_token && open - function_token < 5 &&
                           tokens[function_token].text != "function") --function_token;
                    if (tokens[function_token].text == "function" &&
                        !js_function_is_declaration(tokens, function_token)) {
                        std::size_t name = function_token + 1;
                        if (name < open && tokens[name].text == "*") ++name;
                        if (name < open && tokens[name].kind == JsTokenKind::Identifier)
                            add_binding(scopes.back(), name, JsBindingKind::Function);
                    }
                }
            } else if (pending == JsScopeKind::Catch && index && tokens[index - 1].text == ")") {
                std::size_t depth = 1, open = index - 1;
                while (open && depth) { --open; if (tokens[open].text == ")") ++depth; else if (tokens[open].text == "(") --depth; }
                if (!depth && open + 2 == index &&
                    tokens[open + 1].kind == JsTokenKind::Identifier)
                    add_binding(scopes.back(), open + 1, JsBindingKind::Catch);
                else if (!depth && open + 1 < index &&
                         (tokens[open + 1].text == "{" || tokens[open + 1].text == "[")) {
                    const std::size_t pattern_close = matching_close(open + 1);
                    if (pattern_close < index)
                        add_pattern_bindings(scopes.back(), open + 1, pattern_close,
                                             JsBindingKind::Catch);
                }
            }
            pending = JsScopeKind::Block;
        } else if (text == "}" && !brace_creates_scope.empty()) {
            const bool closes_scope = brace_creates_scope.back();
            brace_creates_scope.pop_back();
            if (closes_scope && scopes.size() > 1) {
                graph.scopes[scopes.back()].last_token = index + 1; scopes.pop_back();
            }
        } else if (index && tokens[index].kind == JsTokenKind::Identifier) {
            const std::string_view previous = tokens[index - 1].text;
            JsBindingKind kind = JsBindingKind::Unknown;
            if (previous == "var") kind = JsBindingKind::Var;
            else if (previous == "let" || previous == "const") kind = JsBindingKind::Lexical;
            else if (previous == "function" && js_function_is_declaration(tokens, index - 1))
                kind = JsBindingKind::Function;
            else if (previous == "class") kind = JsBindingKind::Class;
            else if (previous == "catch") kind = JsBindingKind::Catch;
            if (kind != JsBindingKind::Unknown) {
                std::size_t binding_scope = scopes.back();
                if (kind == JsBindingKind::Var) {
                    while (binding_scope && graph.scopes[binding_scope].kind != JsScopeKind::Function)
                        binding_scope = graph.scopes[binding_scope].parent;
                }
                add_binding(binding_scope, index, kind);
            }
        }
    }
    // Concise arrows do not have a brace at which the main scope stack can
    // open a function. Model the supported single-parameter form explicitly,
    // from the inside out, so captures and shadowing use the normal resolver.
    for (std::size_t arrow = tokens.size(); arrow-- > 1;) {
        if (tokens[arrow].text != ">" || tokens[arrow - 1].text != "=" ||
            arrow + 1 >= tokens.size() || tokens[arrow + 1].text == "{") continue;
        std::size_t parameter = tokens.size();
        bool supported_parameters = false;
        if (arrow >= 2 && tokens[arrow - 2].kind == JsTokenKind::Identifier)
            parameter = arrow - 2, supported_parameters = true;
        else if (arrow >= 4 && tokens[arrow - 2].text == ")" &&
                 tokens[arrow - 3].kind == JsTokenKind::Identifier &&
                 tokens[arrow - 4].text == "(")
            parameter = arrow - 3, supported_parameters = true;
        else if (arrow >= 3 && tokens[arrow - 2].text == ")" &&
                 tokens[arrow - 3].text == "(")
            supported_parameters = true;
        if (!supported_parameters) continue;
        const std::size_t parent = graph.scope_at_token[arrow];
        const std::size_t begin = arrow + 1;
        std::size_t end = tokens.size();
        unsigned parens = 0, brackets = 0, braces = 0;
        for (std::size_t token = begin; token < tokens.size(); ++token) {
            const std::string_view text = tokens[token].text;
            if (text == "(") ++parens;
            else if (text == ")") { if (!parens) { end = token; break; } --parens; }
            else if (text == "[") ++brackets;
            else if (text == "]") { if (!brackets) { end = token; break; } --brackets; }
            else if (text == "{") ++braces;
            else if (text == "}") { if (!braces) { end = token; break; } --braces; }
            else if (!parens && !brackets && !braces && (text == "," || text == ";")) {
                end = token; break;
            }
        }
        graph.scopes.push_back({JsScopeKind::Function, parent, begin, end, false, {}});
        const std::size_t scope = graph.scopes.size() - 1;
        if (parameter < tokens.size()) graph.scope_at_token[parameter] = scope;
        graph.scope_at_token[arrow - 1] = scope;
        graph.scope_at_token[arrow] = scope;
        for (std::size_t token = begin; token < end; ++token)
            if (graph.scope_at_token[token] == parent) graph.scope_at_token[token] = scope;
        for (std::size_t nested = 1; nested + 1 < graph.scopes.size(); ++nested)
            if (graph.scopes[nested].parent == parent &&
                graph.scopes[nested].first_token >= begin && graph.scopes[nested].last_token <= end)
                graph.scopes[nested].parent = scope;
        if (parameter < tokens.size()) add_binding(scope, parameter, JsBindingKind::Parameter);
    }
    // Discover every simple declarator, not only the first name after the
    // declaration keyword. Initializer commas inside delimiter groups are
    // skipped; destructuring patterns remain deliberately outside this pass.
    for (std::size_t keyword = 0; keyword < tokens.size(); ++keyword) {
        const std::string_view declaration = tokens[keyword].text;
        if (declaration != "var" && declaration != "let" && declaration != "const") continue;
        const JsBindingKind kind = declaration == "var" ? JsBindingKind::Var : JsBindingKind::Lexical;
        unsigned nested = 0;
        bool binding_position = true;
        for (std::size_t p = keyword + 1; p < tokens.size(); ++p) {
            const std::string_view text = tokens[p].text;
            if (nested == 0 && (text == ";" || text == "in" || text == "of" || text == ")")) break;
            if (binding_position && nested == 0) {
                if (tokens[p].kind == JsTokenKind::Identifier) {
                    std::size_t binding_scope = graph.scope_at_token[keyword];
                    if (kind == JsBindingKind::Var) {
                        while (binding_scope && graph.scopes[binding_scope].kind != JsScopeKind::Function)
                            binding_scope = graph.scopes[binding_scope].parent;
                    }
                    add_binding(binding_scope, p, kind);
                } else if (tokens[p].text == "{" || tokens[p].text == "[") {
                    std::size_t binding_scope = graph.scope_at_token[keyword];
                    if (kind == JsBindingKind::Var)
                        while (binding_scope && graph.scopes[binding_scope].kind != JsScopeKind::Function)
                            binding_scope = graph.scopes[binding_scope].parent;
                    const std::size_t pattern_close = matching_close(p);
                    if (pattern_close < tokens.size())
                        add_pattern_bindings(binding_scope, p, pattern_close, kind);
                }
                binding_position = false;
            }
            if (text == "(" || text == "[" || text == "{") ++nested;
            else if ((text == ")" || text == "]" || text == "}") && nested) --nested;
            else if (text == "," && nested == 0) binding_position = true;
        }
    }
    // Module imports declare local bindings, but imported and exported names
    // retain their externally observable spelling. Shorthand named imports are
    // represented as bindings too; the mangler deliberately leaves Import
    // bindings unchanged until it can print `import {name as short}`.
    for (std::size_t keyword = 0; keyword < tokens.size(); ++keyword) {
        if (tokens[keyword].text != "import" ||
            (keyword + 1 < tokens.size() && tokens[keyword + 1].text == "(")) continue;
        bool in_named = false, after_as = false, saw_default = false;
        for (std::size_t token = keyword + 1; token < tokens.size(); ++token) {
            const std::string_view text = tokens[token].text;
            if (text == ";" || text == "from" || tokens[token].kind == JsTokenKind::String) break;
            if (text == "{") { in_named = true; after_as = false; continue; }
            if (text == "}") { in_named = false; after_as = false; continue; }
            if (text == "as") { after_as = true; continue; }
            if (text == ",") { after_as = false; continue; }
            if (tokens[token].kind != JsTokenKind::Identifier) continue;
            const bool namespace_local = token && tokens[token - 1].text == "as" &&
                                         token >= 2 && tokens[token - 2].text == "*";
            const bool aliased_named_local = in_named && after_as;
            const bool shorthand_named_local = in_named &&
                (token + 1 >= tokens.size() || tokens[token + 1].text != "as");
            const bool default_local = !in_named && !saw_default && text != "type";
            if (namespace_local || aliased_named_local || shorthand_named_local || default_local) {
                graph.scope_at_token[token] = 0;
                add_binding(0, token, JsBindingKind::Import);
                if (default_local) saw_default = true;
            }
        }
    }
    graph.scope_children.resize(graph.scopes.size());
    graph.containing_function.resize(graph.scopes.size(), 0);
    graph.control_flow_complex.resize(graph.scopes.size(), false);
    for (std::size_t scope = 1; scope < graph.scopes.size(); ++scope) {
        const std::size_t parent = graph.scopes[scope].parent;
        graph.scope_children[parent].push_back(scope);
        graph.containing_function[scope] = graph.scopes[scope].kind == JsScopeKind::Function
            ? scope : graph.containing_function[parent];
    }
    for (std::size_t scope = 0; scope < graph.scopes.size(); ++scope) {
        if (!graph.scopes[scope].dynamic_lookup) continue;
        for (std::size_t ancestor = scope;; ancestor = graph.scopes[ancestor].parent) {
            graph.scopes[ancestor].descendant_dynamic_lookup = true;
            if (!ancestor) break;
        }
    }
    static const std::unordered_set<std::string_view> complex_flow_words = {
        "if", "else", "for", "while", "do", "switch", "case", "try", "catch",
        "finally", "throw", "break", "continue", "yield", "await", "function", "class"
    };
    for (std::size_t token = 0; token < tokens.size(); ++token) {
        const std::size_t function = graph.containing_function[graph.scope_at_token[token]];
        if (function && (complex_flow_words.count(tokens[token].text) ||
                         (tokens[token].text == "=" && token + 1 < tokens.size() &&
                          tokens[token + 1].text == ">")))
            graph.control_flow_complex[function] = true;
    }
    return graph;
}

void resolve_js_references(JsScopeGraph& graph, const std::vector<JsToken>& tokens,
                           JsConcreteSyntax& syntax) {
    graph.references_by_binding.resize(graph.bindings.size());
    graph.reference_at_token.resize(tokens.size(), tokens.size());
    graph.captures_by_function.resize(graph.scopes.size());
    std::unordered_set<std::size_t> declarations;
    std::vector<std::unordered_map<std::string_view, std::size_t>> bindings_by_name(graph.scopes.size());
    for (const auto& binding : graph.bindings) {
        declarations.insert(binding.declaration_tokens.begin(), binding.declaration_tokens.end());
        const std::size_t binding_id = static_cast<std::size_t>(&binding - graph.bindings.data());
        bindings_by_name[binding.scope].emplace(binding.name, binding_id);
        for (std::size_t token : binding.declaration_tokens)
            if (token < syntax.identifier_roles.size())
                syntax.identifier_roles[token] = JsIdentifierRole::Binding;
    }
    static const std::unordered_set<std::string_view> non_references = {
        "break","case","catch","class","const","continue","debugger","default","delete","do","else","export","extends","false","finally","for","function","if","import","in","instanceof","let","new","null","return","static","super","switch","this","throw","true","try","typeof","var","void","while","with","yield","await"
    };
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        if (tokens[index].kind != JsTokenKind::Identifier || declarations.count(index) || non_references.count(tokens[index].text)) continue;
        const JsIdentifierRole role = index < syntax.identifier_roles.size()
            ? syntax.identifier_roles[index] : JsIdentifierRole::Unknown;
        if (role == JsIdentifierRole::PropertyKey || role == JsIdentifierRole::MemberProperty ||
            role == JsIdentifierRole::Label || role == JsIdentifierRole::ImportExportName ||
            role == JsIdentifierRole::PrivateName) continue;
        const std::size_t containing = graph.scope_at_token[index];
        std::size_t resolved = graph.bindings.size();
        std::size_t resolved_scope = graph.scopes.size();
        for (std::size_t scope = containing;; scope = graph.scopes[scope].parent) {
            const auto found = bindings_by_name[scope].find(tokens[index].text);
            if (found != bindings_by_name[scope].end()) {
                const JsBinding& candidate = graph.bindings[found->second];
                const bool invalid_parameter_lookup = syntax.parameter_initializer_tokens[index] &&
                    candidate.kind != JsBindingKind::Parameter && scope == containing;
                if (!invalid_parameter_lookup) {
                    resolved = found->second; resolved_scope = scope; break;
                }
            }
            if (!scope) break;
        }
        const std::string_view next = index + 1 < tokens.size() ? tokens[index + 1].text : std::string_view();
        const std::string_view previous = index ? tokens[index - 1].text : std::string_view();
        JsReferenceAccess access = JsReferenceAccess::Read;
        if (next == "=" && (index + 2 >= tokens.size() || tokens[index + 2].text != ">"))
            access = JsReferenceAccess::Write;
        else if ((next == "+" || next == "-") && index + 2 < tokens.size() &&
                 tokens[index + 2].text == next)
            access = JsReferenceAccess::ReadWrite;
        else if ((previous == "+" || previous == "-") && index >= 2 &&
                 tokens[index - 2].text == previous)
            access = JsReferenceAccess::ReadWrite;
        else if ((next == "+" || next == "-" || next == "*" || next == "/" || next == "%") &&
                 index + 2 < tokens.size() && tokens[index + 2].text == "=")
            access = JsReferenceAccess::ReadWrite;
        bool captured = false;
        std::size_t capture_depth = 0;
        if (resolved_scope < graph.scopes.size() && containing != resolved_scope) {
            for (std::size_t scope = containing; scope != resolved_scope && scope != 0;
                 scope = graph.scopes[scope].parent) {
                if (graph.scopes[scope].kind == JsScopeKind::Function) {
                    captured = true;
                    ++capture_depth;
                }
            }
        }
        graph.references.push_back({index, containing, resolved, resolved_scope, access, captured,
                                    syntax.parameter_initializer_tokens[index], capture_depth});
        graph.reference_at_token[index] = graph.references.size() - 1;
        if (index < syntax.identifier_roles.size() && role != JsIdentifierRole::ShorthandProperty)
            syntax.identifier_roles[index] = JsIdentifierRole::Reference;
        if (resolved < graph.bindings.size()) {
            graph.references_by_binding[resolved].push_back(graph.references.size() - 1);
            if (captured) {
                for (std::size_t scope = containing; scope != resolved_scope && scope;
                     scope = graph.scopes[scope].parent) {
                    if (graph.scopes[scope].kind != JsScopeKind::Function) continue;
                    auto& captures = graph.captures_by_function[scope];
                    if (std::find(captures.begin(), captures.end(), resolved) == captures.end())
                        captures.push_back(resolved);
                }
            }
        } else
            graph.unresolved_references.push_back(graph.references.size() - 1);
    }
    graph.binding_escape.resize(graph.bindings.size(), JsEscapeKind::Local);
    graph.binding_uses.resize(graph.bindings.size());
    for (std::size_t binding = 0; binding < graph.bindings.size(); ++binding) {
        JsEscapeKind escape = JsEscapeKind::Local;
        for (std::size_t reference_id : graph.references_by_binding[binding]) {
            const JsReference& reference = graph.references[reference_id];
            if (reference.access == JsReferenceAccess::Read) ++graph.binding_uses[binding].reads;
            else if (reference.access == JsReferenceAccess::Write) ++graph.binding_uses[binding].writes;
            else {
                ++graph.binding_uses[binding].reads;
                ++graph.binding_uses[binding].writes;
            }
            if (reference.captured) ++graph.binding_uses[binding].captures;
            if (reference.captured) { escape = JsEscapeKind::Captured; break; }
            const std::size_t token = reference.token;
            if (token && tokens[token - 1].text == "return") escape = JsEscapeKind::Returned;
            else if (token && tokens[token - 1].text == "export") escape = JsEscapeKind::Exported;
            else if (token && tokens[token - 1].text == "=") escape = JsEscapeKind::Stored;
            else if (token < syntax.node_at_token.size()) {
                const std::size_t node = syntax.node_at_token[token];
                const std::size_t parent = node < syntax.nodes.size()
                    ? syntax.nodes[node].parent : syntax.nodes.size();
                if (parent < syntax.nodes.size() &&
                    syntax.nodes[parent].role == JsGroupRole::Arguments)
                    escape = JsEscapeKind::Passed;
            }
        }
        graph.binding_escape[binding] = escape;
    }
}

std::string js_short_name(std::size_t index);
std::string js_short_name(std::size_t index, const std::string& first,
                          const std::string& continuation);
bool js_tokens_are_ordered(const std::string& source, const std::vector<JsToken>& tokens) {
    std::size_t cursor = 0;
    for (const auto& token : tokens) {
        if (token.begin < cursor || token.end < token.begin || token.end > source.size())
            return false;
        cursor = token.end;
    }
    return true;
}

std::string build_js_binding_signature(const std::vector<JsToken>& tokens,
                                       const JsConcreteSyntax& syntax,
                                       const JsScopeGraph& graph) {
    std::vector<std::size_t> binding_at(tokens.size(), graph.bindings.size());
    for (std::size_t binding = 0; binding < graph.bindings.size(); ++binding)
        for (std::size_t token : graph.bindings[binding].declaration_tokens)
            binding_at[token] = binding;
    std::string signature;
    signature.reserve(tokens.size() * 3);
    for (std::size_t token = 0; token < tokens.size(); ++token) {
        if (tokens[token].kind != JsTokenKind::Identifier) continue;
        if (binding_at[token] < graph.bindings.size()) {
            signature += "D" + std::to_string(binding_at[token]) + ";";
            continue;
        }
        const std::size_t reference = token < graph.reference_at_token.size()
            ? graph.reference_at_token[token] : tokens.size();
        if (reference < graph.references.size()) {
            const JsReference& occurrence = graph.references[reference];
            if (occurrence.binding < graph.bindings.size()) {
                signature += "R" + std::to_string(occurrence.binding);
                signature += occurrence.access == JsReferenceAccess::Read ? "r;"
                    : occurrence.access == JsReferenceAccess::Write ? "w;" : "x;";
            } else {
                signature += "U" + std::string(tokens[token].text) + ";";
            }
            continue;
        }
        const unsigned role = token < syntax.identifier_roles.size()
            ? static_cast<unsigned>(syntax.identifier_roles[token]) : 0;
        signature += "K" + std::to_string(role) + ":" + std::string(tokens[token].text) + ";";
    }
    for (std::size_t reference = 0; reference < graph.references.size(); ++reference) {
        const JsReference& value = graph.references[reference];
        signature += "J" + std::to_string(value.token) + ":";
        if (value.binding < graph.bindings.size())
            signature += std::to_string(value.binding);
        else
            signature += "U";
        signature += value.access == JsReferenceAccess::Read ? "r;"
            : value.access == JsReferenceAccess::Write ? "w;" : "x;";
    }
    return signature;
}

std::string build_js_effect_signature(const std::vector<JsToken>& tokens,
                                      const JsScopeGraph& graph,
                                      const JsSemanticFacts& facts,
                                      const JsConcreteSyntax& syntax) {
    std::string signature;
    for (std::size_t token = 0; token < tokens.size(); ++token) {
        const std::size_t reference = token < graph.reference_at_token.size()
            ? graph.reference_at_token[token] : tokens.size();
        if (reference < graph.references.size()) {
            const JsReference& occurrence = graph.references[reference];
            if (occurrence.access != JsReferenceAccess::Read) signature += "W";
            else signature += "R";
            signature += occurrence.binding < graph.bindings.size()
                ? std::to_string(occurrence.binding) : std::string(tokens[token].text);
            signature += ";";
        }
        if (token < facts.invocations.size() &&
            facts.invocations[token] != JsInvocationKind::None) {
            bool local = false;
            if (token && token - 1 < graph.reference_at_token.size()) {
                const std::size_t reference = graph.reference_at_token[token - 1];
                local = reference < graph.references.size() &&
                    graph.references[reference].binding < graph.bindings.size() &&
                    graph.bindings[graph.references[reference].binding].kind == JsBindingKind::Function;
            }
            signature += facts.invocations[token] == JsInvocationKind::Construct ? "N;"
                : local ? "L;" : "C;";
        }
        if (tokens[token].text == "." || tokens[token].text == "[") signature += "P;";
        if (tokens[token].text == "throw") signature += "T;";
        const unsigned char extended = facts.extended_effects[token];
        if (extended & 1) signature += "A;";
        if (extended & 2) signature += "M;";
        if (extended & 4) signature += "I;";
        if (extended & 8) signature += "S;";
        if (extended & 16) signature += "X;";
        if (facts.values[token] != JsValueKind::Unknown)
            signature += "V" + std::to_string(static_cast<unsigned>(facts.values[token])) + ";";
        if (facts.truthiness[token] != JsTruthiness::Unknown)
            signature += "Q" + std::to_string(static_cast<unsigned>(facts.truthiness[token])) + ";";
        if (facts.conversions[token] != JsConversionKind::None)
            signature += "Z" + std::to_string(static_cast<unsigned>(facts.conversions[token])) + ";";
        if (facts.property_effects[token] != JsPropertyEffectKind::None)
            signature += "Y" + std::to_string(static_cast<unsigned>(facts.property_effects[token])) + ";";
        const JsStatementKind statement = syntax.statement_kinds[token];
        if ((statement == JsStatementKind::If || statement == JsStatementKind::While ||
             statement == JsStatementKind::For) && token + 2 < tokens.size() &&
            tokens[token + 1].text == "(") {
            const JsTruthiness condition = facts.truthiness[token + 2];
            signature += "B" + std::to_string(token + 2) + ":" +
                (condition == JsTruthiness::Truthy ? "T" :
                 condition == JsTruthiness::Falsy ? "F" : "?") + ";";
        }
    }
    return signature;
}

std::string build_js_ir_signature(const std::vector<JsToken>& tokens,
                                  const JsConcreteSyntax& syntax,
                                  const JsScopeGraph& graph) {
    std::string signature;
    signature.reserve(syntax.nodes.size() * 12);
    for (std::size_t id = 0; id < syntax.nodes.size(); ++id) {
        const JsSyntaxNode& node = syntax.nodes[id];
        signature += "N" + std::to_string(id) + ":" +
            std::to_string(static_cast<unsigned>(node.kind)) + ":" +
            std::to_string(node.first_token) + "-" +
            std::to_string(node.last_token) + ":P" +
            std::to_string(node.parent) + ";";
    }
    signature += "T" + std::to_string(tokens.size()) + ";";
    for (std::size_t token = 0; token < tokens.size(); ++token)
        if (syntax.expression_kinds[token] != JsExpressionKind::None)
            signature += "E" + std::to_string(token) + ":" +
                std::to_string(static_cast<unsigned>(syntax.expression_kinds[token])) +
                ":" + std::to_string(syntax.expression_precedence[token]) + ";";
    for (std::size_t token = 0; token < tokens.size(); ++token) {
        const JsIdentifierRole role = syntax.identifier_roles[token];
        if (tokens[token].kind == JsTokenKind::Identifier)
            signature += "K" + std::to_string(token) + ":" +
                std::to_string(static_cast<unsigned>(role)) + ";";
        if (role == JsIdentifierRole::MemberProperty || role == JsIdentifierRole::PropertyKey)
            signature += "P" + std::to_string(token) + ":" +
                std::to_string(static_cast<unsigned>(role)) + ";";
    }
    for (std::size_t id = 0; id < syntax.nodes.size(); ++id)
        if (syntax.nodes[id].role == JsGroupRole::Arguments)
            signature += "A" + std::to_string(id) + ";";
        else if (syntax.nodes[id].role == JsGroupRole::ObjectLiteral)
            signature += "O" + std::to_string(id) + ";";
        else if (syntax.nodes[id].role == JsGroupRole::ArrayLiteral)
            signature += "R" + std::to_string(id) + ";";
    for (std::size_t token = 0; token < tokens.size(); ++token) {
        if (tokens[token].kind == JsTokenKind::Template)
            signature += "G" + std::to_string(token) + ";";
        if (token >= 2 && tokens[token - 2].text == "." &&
            tokens[token - 1].text == "." && tokens[token].text == ".")
            signature += "D" + std::to_string(token - 2) + ";";
    }
    for (std::size_t token = 0; token < tokens.size(); ++token)
        if (syntax.statement_kinds[token] != JsStatementKind::None)
            signature += "S" + std::to_string(token) + ":" +
                std::to_string(static_cast<unsigned>(syntax.statement_kinds[token])) + ";";
    for (std::size_t token = 0; token < tokens.size(); ++token) {
        if (syntax.function_contexts[token] != JsFunctionContext::None)
            signature += "F" + std::to_string(token) + ":" +
                std::to_string(static_cast<unsigned>(syntax.function_contexts[token])) + ";";
        if (syntax.parameter_initializer_tokens[token])
            signature += "I" + std::to_string(token) + ";";
        if (syntax.binding_patterns[token] != JsBindingPatternKind::None)
            signature += "H" + std::to_string(token) + ":" +
                std::to_string(static_cast<unsigned>(syntax.binding_patterns[token])) + ";";
    }
    for (std::size_t token = 0; token < tokens.size(); ++token)
        if (syntax.module_roles[token] != JsModuleRole::None)
            signature += "M" + std::to_string(token) + ":" +
                std::to_string(static_cast<unsigned>(syntax.module_roles[token])) + ";";
    for (std::size_t scope = 0; scope < graph.scopes.size(); ++scope) {
        const JsScope& value = graph.scopes[scope];
        signature += "C" + std::to_string(scope) + ":" +
            std::to_string(static_cast<unsigned>(value.kind)) + ":" +
            std::to_string(value.parent) + ":" + std::to_string(value.first_token) +
            "-" + std::to_string(value.last_token) + ";";
        if (graph.scopes[scope].dynamic_lookup || graph.scopes[scope].descendant_dynamic_lookup)
            signature += "B" + std::to_string(scope) + ":" +
                (graph.scopes[scope].dynamic_lookup ? "D" : "d") + ";";
    }
    return signature;
}

std::vector<std::unordered_set<std::string_view>> build_js_nested_name_barriers(
    const std::vector<JsToken>& tokens, const JsScopeGraph& graph) {
    std::vector<std::unordered_set<std::string_view>> barriers(graph.scopes.size());
    for (const JsBinding& binding : graph.bindings) {
        const std::size_t owner = graph.containing_function[binding.scope];
        for (std::size_t scope = graph.scopes[owner].parent; scope; scope = graph.scopes[scope].parent)
            if (graph.scopes[scope].kind == JsScopeKind::Function)
                barriers[scope].insert(binding.name);
    }
    for (std::size_t reference : graph.unresolved_references) {
        const JsReference& occurrence = graph.references[reference];
        const std::size_t owner = graph.containing_function[occurrence.scope];
        for (std::size_t scope = graph.scopes[owner].parent; scope; scope = graph.scopes[scope].parent)
            if (graph.scopes[scope].kind == JsScopeKind::Function)
                barriers[scope].insert(tokens[occurrence.token].text);
    }
    return barriers;
}

struct JsBindingInterferenceGraph {
    const JsScopeGraph& graph;
    std::vector<std::size_t> live_begin;
    std::vector<std::size_t> live_end;

    explicit JsBindingInterferenceGraph(const JsScopeGraph& value)
        : graph(value), live_begin(value.bindings.size()), live_end(value.bindings.size()) {
        for (std::size_t binding = 0; binding < value.bindings.size(); ++binding) {
            std::size_t begin = value.bindings[binding].token;
            std::size_t end = begin;
            bool captured = false;
            for (std::size_t reference : value.references_by_binding[binding]) {
                begin = std::min(begin, value.references[reference].token);
                end = std::max(end, value.references[reference].token);
                captured = captured || value.references[reference].captured;
            }
            live_begin[binding] = captured ? 0 : begin;
            live_end[binding] = captured ? std::numeric_limits<std::size_t>::max() : end;
        }
    }

    bool conflicts(std::size_t left, std::size_t right) const {
        const JsBinding& a = graph.bindings[left];
        const JsBinding& b = graph.bindings[right];
        if (graph.containing_function[a.scope] != graph.containing_function[b.scope]) return false;
        const JsScope& a_scope = graph.scopes[a.scope];
        const JsScope& b_scope = graph.scopes[b.scope];
        const bool reusable_kinds =
            (a.kind == JsBindingKind::Lexical || a.kind == JsBindingKind::Catch) &&
            (b.kind == JsBindingKind::Lexical || b.kind == JsBindingKind::Catch);
        const bool disjoint = a_scope.last_token <= b_scope.first_token ||
                              b_scope.last_token <= a_scope.first_token;
        if (reusable_kinds && disjoint) return false;
        const std::size_t function = graph.containing_function[a.scope];
        if (a.kind == JsBindingKind::Var && b.kind == JsBindingKind::Var && function &&
            !graph.control_flow_complex[function] &&
            !graph.scopes[function].dynamic_lookup &&
            !graph.scopes[function].descendant_dynamic_lookup &&
            !graph.references_by_binding[left].empty() &&
            !graph.references_by_binding[right].empty()) {
            if (live_end[left] < live_begin[right] || live_end[right] < live_begin[left])
                return false;
        }
        return true;
    }
};

struct JsNameAlphabet { std::string first; std::string continuation; };

struct JsMangleCoverage {
    std::size_t bindings = 0;
    std::size_t references = 0;
    std::size_t unresolved = 0;
    std::size_t eligible = 0;
    std::size_t short_names = 0;
    std::size_t top_level = 0;
    std::size_t unsupported_kind = 0;
    std::size_t duplicate = 0;
    std::size_t dynamic_scope = 0;
    std::size_t arguments = 0;
    std::size_t classes = 0;
    std::size_t concise_arrows = 0;
    std::size_t methods = 0;
    std::size_t catch_patterns = 0;
    std::size_t eligible_bytes = 0;
    std::size_t top_level_bytes = 0;
    std::size_t unsupported_kind_bytes = 0;
    std::size_t duplicate_bytes = 0;
    std::size_t dynamic_scope_bytes = 0;
    std::size_t arguments_bytes = 0;
    std::size_t classes_bytes = 0;
    std::size_t concise_arrows_bytes = 0;
    std::size_t methods_bytes = 0;
    std::size_t catch_patterns_bytes = 0;
};

JsMangleCoverage analyze_js_mangle_coverage(const std::vector<JsToken>& tokens,
                                             const JsScopeGraph& graph) {
    JsMangleCoverage result;
    result.bindings = graph.bindings.size();
    result.references = graph.references.size();
    result.unresolved = graph.unresolved_references.size();
    std::vector<std::unordered_map<std::string_view, std::size_t>> counts(graph.scopes.size());
    for (const JsBinding& binding : graph.bindings) ++counts[binding.scope][binding.name];
    std::vector<unsigned> barriers(graph.scopes.size(), 0);
    for (std::size_t unit = 1; unit < graph.scopes.size(); ++unit) {
        const JsScope& function = graph.scopes[unit];
        if (function.kind != JsScopeKind::Function) continue;
        if (function.dynamic_lookup || function.descendant_dynamic_lookup) barriers[unit] |= 1;
        for (std::size_t token = function.first_token; token < function.last_token; ++token) {
            if (graph.containing_function[graph.scope_at_token[token]] != unit) continue;
            const std::string_view text = tokens[token].text;
            if (text == "arguments") barriers[unit] |= 2;
            else if (text == "class") barriers[unit] |= 4;
            else if (text == "=" && token + 1 < function.last_token &&
                     tokens[token + 1].text == ">" &&
                     (token + 2 >= function.last_token || tokens[token + 2].text != "{"))
                barriers[unit] |= 8;
            if (token != function.first_token && text == "{" &&
                tokens[token].brace_kind == JsBraceKind::Block && token &&
                tokens[token - 1].text == ")") {
                std::size_t open = token - 1, depth = 1;
                while (open && depth) { --open; if (tokens[open].text == ")") ++depth; else if (tokens[open].text == "(") --depth; }
                const std::string_view before = open ? tokens[open - 1].text : std::string_view();
                bool function_parameters = false;
                for (std::size_t lookback = open; lookback && open - lookback < 4; --lookback)
                    if (tokens[lookback - 1].text == "function") function_parameters = true;
                if (before != "if" && before != "for" && before != "while" &&
                    before != "switch" && before != "catch" && before != "with" &&
                    !function_parameters) barriers[unit] |= 16;
            }
        }
    }
    static const std::unordered_set<std::string_view> reserved = {
        "await","break","case","catch","class","const","continue","debugger",
        "default","delete","do","else","enum","eval","export","extends","false",
        "finally","for","function","if","implements","import","in","instanceof",
        "interface","let","new","null","package","private","protected","public",
        "return","static","super","switch","this","throw","true","try","typeof",
        "var","void","while","with","yield","arguments"
    };
    for (const JsBinding& binding : graph.bindings) {
        const std::size_t unit = graph.containing_function[binding.scope];
        const std::size_t binding_id = static_cast<std::size_t>(&binding - graph.bindings.data());
        const std::size_t occurrences = binding.declaration_tokens.size() +
            graph.references_by_binding[binding_id].size();
        const std::size_t potential_bytes = binding.name.size() > 1
            ? (binding.name.size() - 1) * occurrences : 0;
        if (!unit) { ++result.top_level; result.top_level_bytes += potential_bytes; continue; }
        const unsigned barrier = barriers[unit];
        if (barrier) {
            if (barrier & 1) { ++result.dynamic_scope; result.dynamic_scope_bytes += potential_bytes; }
            else if (barrier & 2) { ++result.arguments; result.arguments_bytes += potential_bytes; }
            else if (barrier & 4) { ++result.classes; result.classes_bytes += potential_bytes; }
            else if (barrier & 8) { ++result.concise_arrows; result.concise_arrows_bytes += potential_bytes; }
            else if (barrier & 16) { ++result.methods; result.methods_bytes += potential_bytes; }
            else { ++result.catch_patterns; result.catch_patterns_bytes += potential_bytes; }
            continue;
        }
        const bool supported = binding.kind == JsBindingKind::Parameter ||
            binding.kind == JsBindingKind::Var || binding.kind == JsBindingKind::Lexical ||
            binding.kind == JsBindingKind::Catch;
        if (!supported || reserved.count(binding.name)) {
            ++result.unsupported_kind; result.unsupported_kind_bytes += potential_bytes; continue;
        }
        if (counts[binding.scope][binding.name] != 1) {
            ++result.duplicate; result.duplicate_bytes += potential_bytes; continue;
        }
        if (binding.name.size() <= 2) { ++result.short_names; continue; }
        ++result.eligible; result.eligible_bytes += potential_bytes;
    }
    return result;
}

std::string build_js_mangle_report(const std::vector<JsToken>& tokens,
                                   const JsScopeGraph& graph) {
    const JsMangleCoverage value = analyze_js_mangle_coverage(tokens, graph);
    return "bindings\t" + std::to_string(value.bindings) +
        "\treferences\t" + std::to_string(value.references) +
        "\tunresolved\t" + std::to_string(value.unresolved) +
        "\teligible\t" + std::to_string(value.eligible) +
        "\tshort\t" + std::to_string(value.short_names) +
        "\ttop-level\t" + std::to_string(value.top_level) +
        "\tunsupported-kind\t" + std::to_string(value.unsupported_kind) +
        "\tduplicate\t" + std::to_string(value.duplicate) +
        "\tdynamic\t" + std::to_string(value.dynamic_scope) +
        "\targuments\t" + std::to_string(value.arguments) +
        "\tclass\t" + std::to_string(value.classes) +
        "\tconcise-arrow\t" + std::to_string(value.concise_arrows) +
        "\tmethod\t" + std::to_string(value.methods) +
        "\tcatch-pattern\t" + std::to_string(value.catch_patterns) +
        "\teligible-bytes\t" + std::to_string(value.eligible_bytes) +
        "\ttop-level-bytes\t" + std::to_string(value.top_level_bytes) +
        "\tunsupported-kind-bytes\t" + std::to_string(value.unsupported_kind_bytes) +
        "\tduplicate-bytes\t" + std::to_string(value.duplicate_bytes) +
        "\tdynamic-bytes\t" + std::to_string(value.dynamic_scope_bytes) +
        "\targuments-bytes\t" + std::to_string(value.arguments_bytes) +
        "\tclass-bytes\t" + std::to_string(value.classes_bytes) +
        "\tconcise-arrow-bytes\t" + std::to_string(value.concise_arrows_bytes) +
        "\tmethod-bytes\t" + std::to_string(value.methods_bytes) +
        "\tcatch-pattern-bytes\t" + std::to_string(value.catch_patterns_bytes);
}

JsNameAlphabet build_js_frequency_alphabet(const std::vector<JsToken>& tokens,
                                            std::size_t begin, std::size_t end,
                                            bool worthwhile) {
    const std::string default_first = "$_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const std::string default_continuation = default_first + "0123456789";
    if (!worthwhile) return {default_first, default_continuation};
    std::unordered_map<char, std::size_t> frequency;
    for (std::size_t token = begin; token < end; ++token)
        for (char character : tokens[token].text)
            if (default_continuation.find(character) != std::string::npos)
                ++frequency[character];
    auto ranked = [&](std::string alphabet) {
        std::stable_sort(alphabet.begin(), alphabet.end(), [&](char left, char right) {
            return frequency[left] > frequency[right];
        });
        return alphabet;
    };
    return {ranked(default_first), ranked(default_continuation)};
}

bool js_identifier_is_shorthand(const std::vector<JsToken>& tokens,
                                const JsConcreteSyntax& syntax,
                                std::size_t token,
                                bool binding_declaration = false) {
    if (token >= syntax.identifier_roles.size()) return false;
    if (syntax.identifier_roles[token] == JsIdentifierRole::ShorthandProperty) return true;
    if (!token || token + 1 >= tokens.size() ||
        (tokens[token - 1].text != "{" && tokens[token - 1].text != ",") ||
        (tokens[token + 1].text != "}" && tokens[token + 1].text != ",")) return false;
    if (token < syntax.binding_patterns.size() &&
        syntax.binding_patterns[token] != JsBindingPatternKind::None)
        return true;
    unsigned parens = 0, brackets = 0, braces = 0;
    for (std::size_t cursor = token; cursor-- > 0;) {
        const std::string_view text = tokens[cursor].text;
        if (text == ")") ++parens;
        else if (text == "]") ++brackets;
        else if (text == "}") ++braces;
        else if (text == "(") {
            if (parens) --parens;
            else if (!brackets && !braces) return false;
        } else if (text == "[") {
            if (brackets) --brackets;
            else if (!parens && !braces) return false;
        } else if (text == "{") {
            if (braces) --braces;
            else if (!parens && !brackets) {
                if (binding_declaration) {
                    for (std::size_t between = cursor + 1; between < token; ++between)
                        if (tokens[between].text == "var" || tokens[between].text == "let" ||
                            tokens[between].text == "const" || tokens[between].text == "function" ||
                            tokens[between].text == ";") return false;
                    return true;
                }
                return tokens[cursor].brace_kind != JsBraceKind::Block ||
                       (cursor && tokens[cursor - 1].text == "(");
            }
        }
    }
    return false;
}

std::vector<JsReplacement> plan_safe_js_parameter_renaming(
    const std::vector<JsToken>& tokens, const JsConcreteSyntax& syntax,
    const JsScopeGraph& graph, bool mangle_top_level = false) {
    std::vector<JsReplacement> replacements;
    // For-await heads need a dedicated lexical/destructuring scope model.
    // Until then, fail closed for the whole compilation unit rather than let
    // a synthetic nested unit bypass the containing function's barrier.
    for (std::size_t token = 0; token + 1 < tokens.size(); ++token)
        if (tokens[token].text == "for" && tokens[token + 1].text == "await")
            return replacements;
    // Dynamic import and source-phase import introduce contextual grammar that
    // the lightweight scope builder does not model yet. In particular, a
    // nested arrow used as a promise callback can otherwise have its parameter
    // declaration renamed while a reference is classified outside that scope.
    // Keep compression enabled, but fail closed for identifier mangling.
    for (std::size_t token = 0; token + 1 < tokens.size(); ++token)
        if (tokens[token].text == "import" &&
            (tokens[token + 1].text == "(" || tokens[token + 1].text == "."))
            return replacements;
    static const std::unordered_set<std::string_view> reserved = {
        "await","break","case","catch","class","const","continue","debugger",
        "default","delete","do","else","enum","eval","export","extends","false",
        "finally","for","function","if","implements","import","in","instanceof",
        "interface","let","new","null","package","private","protected","public",
        "return","static","super","switch","this","throw","true","try","typeof",
        "var","void","while","with","yield","arguments"
    };
    std::vector<std::unordered_map<std::string_view, std::size_t>> binding_counts(graph.scopes.size());
    for (const JsBinding& binding : graph.bindings)
        ++binding_counts[binding.scope][binding.name];
    const std::size_t no_unit = graph.scopes.size();
    std::vector<std::size_t> unit_of_scope(graph.scopes.size(), no_unit);
    if (mangle_top_level && !graph.scopes.empty() &&
        graph.scopes[0].kind == JsScopeKind::Script)
        unit_of_scope[0] = 0;
    // Scopes are created parent-before-child. The owning function can therefore
    // be propagated in one pass instead of walking every ancestor chain again
    // for every scope in the mangler hot path.
    for (std::size_t scope = 1; scope < graph.scopes.size(); ++scope) {
        if (graph.scopes[scope].kind == JsScopeKind::Function)
            unit_of_scope[scope] = scope;
        else
            unit_of_scope[scope] = unit_of_scope[graph.scopes[scope].parent];
    }
    std::vector<std::vector<std::size_t>> bindings_by_unit(graph.scopes.size());
    std::vector<std::vector<std::size_t>> references_by_unit(graph.scopes.size());
    std::vector<bool> unit_dynamic(graph.scopes.size(), false);
    for (std::size_t scope = 1; scope < graph.scopes.size(); ++scope)
        if (unit_of_scope[scope] < graph.scopes.size()) {
            if (graph.scopes[scope].dynamic_lookup) unit_dynamic[unit_of_scope[scope]] = true;
        }
    for (std::size_t binding = 0; binding < graph.bindings.size(); ++binding) {
        const std::size_t unit = unit_of_scope[graph.bindings[binding].scope];
        if (unit < graph.scopes.size()) bindings_by_unit[unit].push_back(binding);
    }
    std::vector<std::unordered_map<std::string_view, std::size_t>> unit_name_counts(graph.scopes.size());
    for (std::size_t unit = 0; unit < bindings_by_unit.size(); ++unit)
        for (std::size_t binding : bindings_by_unit[unit])
            ++unit_name_counts[unit][graph.bindings[binding].name];
    for (std::size_t reference = 0; reference < graph.references.size(); ++reference) {
        const std::size_t unit = unit_of_scope[graph.references[reference].scope];
        if (unit < graph.scopes.size()) references_by_unit[unit].push_back(reference);
    }
    const auto nested_name_barriers = build_js_nested_name_barriers(tokens, graph);
    std::unordered_set<std::string_view> unresolved_names;
    for (std::size_t reference : graph.unresolved_references)
        if (reference < graph.references.size() &&
            graph.references[reference].token < tokens.size())
            unresolved_names.insert(tokens[graph.references[reference].token].text);
    std::unordered_set<std::string_view> external_top_level_names;
    if (!mangle_top_level)
        for (std::size_t token = 0; token + 1 < tokens.size(); ++token) {
            if (graph.scope_at_token[token] != 0 ||
                (tokens[token].text != "function" && tokens[token].text != "class")) continue;
            const bool declaration_position = token == 0 || tokens[token - 1].text == ";" ||
                tokens[token - 1].text == "}" || tokens[token - 1].text == "export" ||
                tokens[token - 1].text == "default" ||
                (tokens[token - 1].text == "async" &&
                 (token == 1 || tokens[token - 2].text == ";" || tokens[token - 2].text == "}"));
            if (!declaration_position) continue;
            std::size_t name = token + 1;
            if (tokens[name].text == "*" && name + 1 < tokens.size()) ++name;
            if (tokens[name].kind == JsTokenKind::Identifier)
                external_top_level_names.insert(tokens[name].text);
        }
    std::vector<bool> class_heritage_tokens(tokens.size(), false);
    for (std::size_t start = 0; start < tokens.size(); ++start) {
        if (tokens[start].text != "class") continue;
        bool heritage = false;
        unsigned parens = 0, brackets = 0, braces = 0;
        for (std::size_t token = start + 1; token < tokens.size(); ++token) {
            const std::string_view text = tokens[token].text;
            if (!parens && !brackets && !braces && text == "extends") {
                heritage = true;
                continue;
            }
            if (!parens && !brackets && !braces && text == "{") break;
            if (heritage) class_heritage_tokens[token] = true;
            if (text == "(") ++parens;
            else if (text == ")" && parens) --parens;
            else if (text == "[") ++brackets;
            else if (text == "]" && brackets) --brackets;
            else if (text == "{") ++braces;
            else if (text == "}" && braces) --braces;
        }
    }
    std::vector<std::string> coordinated_names(graph.bindings.size());
    // Synthetic concise-arrow scopes are appended after brace-backed child
    // scopes are discovered. Process function units by lexical depth rather
    // than storage order so every parent's coordinated names exist before a
    // child allocates from its namespace.
    std::vector<std::size_t> unit_order;
    unit_order.reserve(graph.scopes.size());
    for (std::size_t unit = 0; unit < graph.scopes.size(); ++unit)
        if ((unit == 0 && mangle_top_level &&
             graph.scopes[unit].kind == JsScopeKind::Script) ||
            graph.scopes[unit].kind == JsScopeKind::Function)
            unit_order.push_back(unit);
    std::vector<std::size_t> scope_depth(graph.scopes.size(), 0);
    for (std::size_t scope = 1; scope < graph.scopes.size(); ++scope)
        scope_depth[scope] = scope_depth[graph.scopes[scope].parent] + 1;
    std::stable_sort(unit_order.begin(), unit_order.end(),
        [&](std::size_t left, std::size_t right) {
            const std::size_t left_depth = scope_depth[left];
            const std::size_t right_depth = scope_depth[right];
            if (left_depth != right_depth) return left_depth < right_depth;
            return graph.scopes[left].first_token < graph.scopes[right].first_token;
        });
    const JsBindingInterferenceGraph binding_interference(graph);
    for (std::size_t unit : unit_order) {
        const JsScope& function = graph.scopes[unit];
        const bool top_level_script = unit == 0 && mangle_top_level &&
                                      function.kind == JsScopeKind::Script;
        if ((!top_level_script && function.kind != JsScopeKind::Function) ||
            function.last_token > tokens.size() || (!top_level_script && !function.first_token)) continue;

        bool unsafe = unit_dynamic[unit] || function.descendant_dynamic_lookup;
        bool uses_arguments = false;
        for (std::size_t i = function.first_token; i < function.last_token && !unsafe; ++i) {
            if (tokens[i].text == "for" && i + 1 < function.last_token &&
                tokens[i + 1].text == "await") unsafe = true;
            if (unit_of_scope[graph.scope_at_token[i]] != unit) continue;
            if (tokens[i].text == "arguments") uses_arguments = true;
            // A block following a non-control parameter list is an object/class
            // method body, which the lightweight scope builder cannot yet model.
            if (i != function.first_token && tokens[i].text == "{" &&
                tokens[i].brace_kind == JsBraceKind::Block && i &&
                tokens[i - 1].text == ")") {
                std::size_t open = i - 1, depth = 1;
                while (open && depth) { --open; if (tokens[open].text == ")") ++depth; else if (tokens[open].text == "(") --depth; }
                const std::string_view before = open ? tokens[open - 1].text : std::string_view();
                bool function_parameters = false;
                for (std::size_t lookback = open; lookback && open - lookback < 4; --lookback)
                    if (tokens[lookback - 1].text == "function") function_parameters = true;
                if (before != "if" && before != "for" && before != "while" &&
                    before != "switch" && before != "catch" && before != "with" &&
                    !function_parameters) unsafe = true;
            }
        }
        if (unsafe) continue;

        std::vector<std::size_t> eligible;
        std::unordered_set<std::string_view> occupied = reserved;
        occupied.insert(nested_name_barriers[unit].begin(), nested_name_barriers[unit].end());
        // Derive occupied captured names directly from the unit's resolved
        // references as well as the convenience capture inventory. Synthetic
        // arrow scopes can make the latter incomplete even though resolution
        // itself correctly points at the outer binding.
        for (std::size_t reference_id : references_by_unit[unit]) {
            if (reference_id >= graph.references.size()) continue;
            const std::size_t binding = graph.references[reference_id].binding;
            if (binding >= graph.bindings.size() ||
                unit_of_scope[graph.bindings[binding].scope] == unit) continue;
            if (binding < coordinated_names.size() && !coordinated_names[binding].empty())
                occupied.insert(coordinated_names[binding]);
            else
                occupied.insert(graph.bindings[binding].name);
        }
        // A concise-arrow scope can sit between two brace-backed function
        // units. Until capture edges through every such synthetic scope are
        // complete, reserve all ancestor-unit spellings in the child. This
        // prevents a child local from colliding with an outer binding that is
        // referenced by a deeper callback.
        const std::size_t unit_begin = graph.scopes[unit].first_token;
        const bool arrow_unit = unit_begin >= 3 &&
            tokens[unit_begin - 1].text == ">" && tokens[unit_begin - 2].text == "=";
        if (arrow_unit)
            for (std::size_t ancestor = 0; ancestor < graph.bindings.size(); ++ancestor) {
                const std::size_t scope = graph.bindings[ancestor].scope;
                if (scope >= graph.scopes.size() || scope == unit ||
                    graph.scopes[scope].first_token > function.first_token ||
                    graph.scopes[scope].last_token < function.last_token) continue;
                if (ancestor < coordinated_names.size() &&
                    !coordinated_names[ancestor].empty())
                    occupied.insert(coordinated_names[ancestor]);
                else
                    occupied.insert(graph.bindings[ancestor].name);
            }
        for (std::size_t captured : graph.captures_by_function[unit]) {
            if (captured >= graph.bindings.size()) continue;
            if (captured < coordinated_names.size() && !coordinated_names[captured].empty())
                occupied.insert(coordinated_names[captured]);
            else
                occupied.insert(graph.bindings[captured].name);
        }
        for (std::size_t binding : bindings_by_unit[unit]) {
            const JsBinding& candidate = graph.bindings[binding];
            bool referenced_from_opaque_class = false;
            for (std::size_t declaration : candidate.declaration_tokens)
                if (declaration < class_heritage_tokens.size() &&
                    class_heritage_tokens[declaration]) {
                    referenced_from_opaque_class = true;
                    break;
                }
            for (std::size_t reference_id : graph.references_by_binding[binding]) {
                if (graph.references[reference_id].token < class_heritage_tokens.size() &&
                    class_heritage_tokens[graph.references[reference_id].token]) {
                    referenced_from_opaque_class = true;
                    break;
                }
                std::size_t scope = graph.references[reference_id].scope;
                while (scope != unit && scope) {
                    if (graph.scopes[scope].kind == JsScopeKind::Class) {
                        referenced_from_opaque_class = true;
                        break;
                    }
                    scope = graph.scopes[scope].parent;
                }
                if (referenced_from_opaque_class) break;
            }
            const bool supported = candidate.kind == JsBindingKind::Parameter ||
                                   candidate.kind == JsBindingKind::Var ||
                                   candidate.kind == JsBindingKind::Lexical ||
                                   candidate.kind == JsBindingKind::Catch ||
                                   candidate.kind == JsBindingKind::Function ||
                                   candidate.kind == JsBindingKind::Class;
            const std::size_t duplicates = binding_counts[candidate.scope][candidate.name];
            const bool arguments_alias = uses_arguments &&
                                         candidate.kind == JsBindingKind::Parameter;
            const bool async_arrow_prefix = candidate.name == "async" &&
                candidate.token + 1 < tokens.size() && tokens[candidate.token + 1].text == "(" &&
                syntax.matching_token[candidate.token + 1] < tokens.size() &&
                syntax.matching_token[candidate.token + 1] + 2 < tokens.size() &&
                tokens[syntax.matching_token[candidate.token + 1] + 1].text == "=" &&
                tokens[syntax.matching_token[candidate.token + 1] + 2].text == ">";
            if (supported && duplicates == 1 && !reserved.count(candidate.name) &&
                candidate.name.size() > 2 && !referenced_from_opaque_class && !arguments_alias &&
                !async_arrow_prefix && !unresolved_names.count(candidate.name) &&
                !external_top_level_names.count(candidate.name) &&
                unit_name_counts[unit][candidate.name] == 1)
                eligible.push_back(binding);
            else
                occupied.insert(candidate.name);
        }
        for (std::size_t reference_id : references_by_unit[unit]) {
            const JsReference& reference = graph.references[reference_id];
            if (reference.binding >= graph.bindings.size() ||
                reference.binding_scope >= graph.scopes.size() ||
                unit_of_scope[reference.binding_scope] != unit)
                occupied.insert(tokens[reference.token].text);
        }
        const auto estimated_saving = [&](std::size_t binding) {
            const JsBinding& value = graph.bindings[binding];
            const std::size_t occurrences = value.declaration_tokens.size() +
                                            graph.references_by_binding[binding].size();
            return (value.name.size() - 1) * occurrences;
        };
        const auto more_profitable = [&](std::size_t left, std::size_t right) {
            if (eligible.size() <= 54)
                return graph.references_by_binding[left].size() >
                       graph.references_by_binding[right].size();
            const std::size_t left_saving = estimated_saving(left);
            const std::size_t right_saving = estimated_saving(right);
            if (left_saving != right_saving) return left_saving > right_saving;
            return graph.bindings[left].token < graph.bindings[right].token;
        };
        if (eligible.size() == 2) {
            if (more_profitable(eligible[1], eligible[0]))
                std::swap(eligible[0], eligible[1]);
        } else if (eligible.size() > 2) {
            std::stable_sort(eligible.begin(), eligible.end(), more_profitable);
        }
        const JsNameAlphabet alphabet = build_js_frequency_alphabet(
            tokens, function.first_token, function.last_token, eligible.size() > 54);

        // Candidate indices are a perfect, allocation-free key for generated
        // names in this unit. Most bindings reuse one of the first few names.
        std::vector<std::vector<std::size_t>> allocations_by_candidate;
        for (std::size_t binding_id : eligible) {
            const JsBinding& binding = graph.bindings[binding_id];
            std::string replacement;
            std::size_t selected_candidate = 0;
            for (std::size_t candidate_index = 0;; ++candidate_index) {
                replacement = js_short_name(candidate_index, alphabet.first,
                                            alphabet.continuation);
                if (occupied.count(replacement)) continue;
                bool conflicts = false;
                if (candidate_index < allocations_by_candidate.size())
                    for (std::size_t allocated : allocations_by_candidate[candidate_index]) {
                    if (binding_interference.conflicts(binding_id, allocated)) {
                        conflicts = true;
                        break;
                    }
                }
                if (!conflicts) { selected_candidate = candidate_index; break; }
            }
            std::size_t ordinary = 1, shorthand = 0;
            for (std::size_t reference_id : graph.references_by_binding[binding_id]) {
                const JsReference& reference = graph.references[reference_id];
                if (js_identifier_is_shorthand(tokens, syntax, reference.token))
                    ++shorthand;
                else
                    ++ordinary;
            }
            const std::size_t old_size = (ordinary + shorthand) * binding.name.size();
            const std::size_t new_size = ordinary * replacement.size() +
                                         shorthand * (binding.name.size() + 1 + replacement.size());
            if (new_size >= old_size) { occupied.insert(binding.name); continue; }
            for (std::size_t token : binding.declaration_tokens) {
                const bool object_pattern_shorthand =
                    js_identifier_is_shorthand(tokens, syntax, token, true);
                replacements.push_back({tokens[token].begin, tokens[token].end,
                    object_pattern_shorthand ? std::string(binding.name) + ":" + replacement
                                             : replacement});
            }
            for (std::size_t reference_id : graph.references_by_binding[binding_id]) {
                const JsReference& reference = graph.references[reference_id];
                const bool shorthand = js_identifier_is_shorthand(tokens, syntax, reference.token);
                replacements.push_back({tokens[reference.token].begin, tokens[reference.token].end,
                    shorthand ? std::string(binding.name) + ":" + replacement : replacement});
            }
            if (selected_candidate >= allocations_by_candidate.size())
                allocations_by_candidate.resize(selected_candidate + 1);
            allocations_by_candidate[selected_candidate].push_back(binding_id);
            coordinated_names[binding_id] = replacement;
        }
    }
    return replacements;
}

[[maybe_unused]] std::vector<JsReplacement> plan_js_concise_arrow_renaming(
    const std::vector<JsToken>& tokens, const JsConcreteSyntax& syntax) {
    std::vector<JsReplacement> replacements;
    static const std::unordered_set<std::string_view> reserved = {
        "await","break","case","catch","class","const","continue","debugger",
        "default","delete","do","else","enum","eval","export","extends","false",
        "finally","for","function","if","implements","import","in","instanceof",
        "interface","let","new","null","package","private","protected","public",
        "return","static","super","switch","this","throw","true","try","typeof",
        "var","void","while","with","yield","arguments"
    };
    for (std::size_t arrow = 1; arrow + 2 < tokens.size(); ++arrow) {
        if (tokens[arrow].text != "=" || tokens[arrow + 1].text != ">") continue;
        std::size_t parameter = tokens.size();
        bool parenthesized_parameter = false;
        if (tokens[arrow - 1].kind == JsTokenKind::Identifier) {
            parameter = arrow - 1;
        } else if (arrow >= 3 && tokens[arrow - 1].text == ")" &&
                   tokens[arrow - 2].kind == JsTokenKind::Identifier &&
                   tokens[arrow - 3].text == "(") {
            parameter = arrow - 2;
            parenthesized_parameter = true;
        }
        if (parameter == tokens.size() || tokens[parameter].text.size() <= 2 ||
            reserved.count(tokens[parameter].text)) continue;

        const std::size_t body_begin = arrow + 2;
        if (tokens[body_begin].text == "{") continue;
        std::size_t body_end = tokens.size();
        unsigned parens = 0, brackets = 0, braces = 0;
        bool unsafe = false;
        for (std::size_t i = body_begin; i < tokens.size(); ++i) {
            const std::string_view text = tokens[i].text;
            if (text == "(") ++parens;
            else if (text == ")") { if (!parens) { body_end = i; break; } --parens; }
            else if (text == "[") ++brackets;
            else if (text == "]") { if (!brackets) { body_end = i; break; } --brackets; }
            else if (text == "{") ++braces;
            else if (text == "}") { if (!braces) { body_end = i; break; } --braces; }
            else if (!parens && !brackets && !braces && (text == "," || text == ";")) {
                body_end = i;
                break;
            }
            if (text == "function" || text == "class" || text == "eval" ||
                text == "with" || text == "arguments" ||
                (text == "=" && i + 1 < tokens.size() && tokens[i + 1].text == ">"))
                unsafe = true;
        }
        if (unsafe || body_end <= body_begin) continue;

        std::unordered_set<std::string_view> occupied = reserved;
        for (std::size_t i = body_begin; i < body_end; ++i)
            if (tokens[i].kind == JsTokenKind::Identifier &&
                tokens[i].text != tokens[parameter].text)
                occupied.insert(tokens[i].text);
        std::string replacement;
        for (std::size_t next = 0;; ++next) {
            replacement = js_short_name(next);
            if (!occupied.count(replacement)) break;
        }

        std::vector<std::size_t> references;
        std::size_t shorthand_references = 0;
        for (std::size_t i = body_begin; i < body_end; ++i) {
            if (tokens[i].text != tokens[parameter].text) continue;
            const JsIdentifierRole role = i < syntax.identifier_roles.size()
                ? syntax.identifier_roles[i] : JsIdentifierRole::Unknown;
            if (role == JsIdentifierRole::PropertyKey ||
                role == JsIdentifierRole::MemberProperty ||
                role == JsIdentifierRole::Label ||
                role == JsIdentifierRole::ImportExportName ||
                role == JsIdentifierRole::PrivateName) continue;
            const bool arrow_object = body_begin + 1 < body_end &&
                                      tokens[body_begin].text == "(" &&
                                      tokens[body_begin + 1].text == "{";
            const bool shorthand = arrow_object &&
                (tokens[i - 1].text == "{" || tokens[i - 1].text == ",") &&
                i + 1 < body_end &&
                (tokens[i + 1].text == "}" || tokens[i + 1].text == ",");
            if (shorthand) ++shorthand_references;
            references.push_back(i);
        }
        const std::size_t old_size = (references.size() + 1) * tokens[parameter].text.size();
        const std::size_t new_size = (references.size() + 1) * replacement.size() +
                                     shorthand_references * (tokens[parameter].text.size() + 1);
        if (references.empty() || new_size >= old_size) continue;
        if (parenthesized_parameter) {
            replacements.push_back({tokens[arrow - 3].begin, tokens[arrow - 3].end, ""});
            replacements.push_back({tokens[arrow - 1].begin, tokens[arrow - 1].end, ""});
        }
        replacements.push_back({tokens[parameter].begin, tokens[parameter].end, replacement});
        for (std::size_t reference : references) {
            const bool arrow_object = body_begin + 1 < body_end &&
                                      tokens[body_begin].text == "(" &&
                                      tokens[body_begin + 1].text == "{";
            const bool shorthand = arrow_object &&
                (tokens[reference - 1].text == "{" || tokens[reference - 1].text == ",") &&
                reference + 1 < body_end &&
                (tokens[reference + 1].text == "}" || tokens[reference + 1].text == ",");
            replacements.push_back({tokens[reference].begin, tokens[reference].end,
                shorthand ? std::string(tokens[parameter].text) + ":" + replacement
                          : replacement});
        }
        arrow = body_end ? body_end - 1 : arrow;
    }
    return replacements;
}

std::vector<JsReplacement> plan_js_single_arrow_parentheses(
    const std::vector<JsToken>& tokens) {
    std::vector<JsReplacement> replacements;
    for (std::size_t open = 0; open + 4 < tokens.size(); ++open) {
        if (tokens[open].text != "(" ||
            tokens[open + 1].kind != JsTokenKind::Identifier ||
            tokens[open + 2].text != ")" || tokens[open + 3].text != "=" ||
            tokens[open + 4].text != ">") continue;
        replacements.push_back({tokens[open].begin, tokens[open].end, ""});
        replacements.push_back({tokens[open + 2].begin, tokens[open + 2].end, ""});
        open += 4;
    }
    return replacements;
}

std::vector<JsReplacement> plan_js_redundant_primary_parentheses(
    const std::vector<JsToken>& tokens, const JsConcreteSyntax& syntax) {
    std::vector<JsReplacement> replacements;
    static const std::unordered_set<std::string_view> safe_before = {
        "return", "throw", "case", "=", ",", ":", "?",
        "[", "{", "+", "-", "*", "/", "%", "**", "&&", "||", "??",
        "&", "|", "^", "!", "~", "<", ">", "<=", ">=", "==", "!=",
        "===", "!==", "=>", "in", "instanceof", "typeof", "void", "delete"
    };
    static const std::unordered_set<std::string_view> safe_after = {
        ";", ",", ":", "?", "]", "}", ")", "+", "-", "*", "/", "%",
        "**", "&&", "||", "??", "&", "|", "^", "<", ">", "<=", ">=",
        "==", "!=", "===", "!==", "in", "instanceof"
    };
    static const std::unordered_set<std::string_view> word_before = {
        "return", "throw", "case", "in", "instanceof",
        "typeof", "void", "delete"
    };
    for (std::size_t open = 1; open + 2 < tokens.size(); ++open) {
        if (tokens[open].text != "(" || syntax.matching_token[open] != open + 2)
            continue;
        const JsToken& value = tokens[open + 1];
        if (value.kind != JsTokenKind::Identifier && value.kind != JsTokenKind::Number &&
            value.kind != JsTokenKind::String && value.kind != JsTokenKind::Regex &&
            value.kind != JsTokenKind::Template) continue;
        if (value.text == "eval" || value.text == "yield" || value.text == "await" ||
            value.text == "async") continue;
        if (!safe_before.count(tokens[open - 1].text)) continue;
        if (open >= 2 && tokens[open - 2].text == "." &&
            (tokens[open - 1].text == "return" || tokens[open - 1].text == "throw" ||
             tokens[open - 1].text == "yield" || tokens[open - 1].text == "await" ||
             tokens[open - 1].text == "case" || tokens[open - 1].text == "typeof" ||
             tokens[open - 1].text == "void" || tokens[open - 1].text == "delete"))
            continue;
        const std::size_t close = open + 2;
        if (close + 1 < tokens.size() && !safe_after.count(tokens[close + 1].text))
            continue;
        if (value.kind == JsTokenKind::Number && close + 1 < tokens.size() &&
            tokens[close + 1].text == ".") continue;
        replacements.push_back({tokens[open].begin, tokens[open].end,
                                word_before.count(tokens[open - 1].text) ? " " : ""});
        replacements.push_back({tokens[close].begin, tokens[close].end, ""});
        open = close;
    }
    return replacements;
}

std::vector<JsReplacement> plan_js_redundant_shorthand_properties(
    const std::vector<JsToken>& tokens, const JsConcreteSyntax& syntax) {
    std::vector<JsReplacement> replacements;
    for (std::size_t key = 1; key + 2 < tokens.size(); ++key) {
        const std::size_t value = key + 2;
        if (tokens[key].kind != JsTokenKind::Identifier || tokens[key + 1].text != ":" ||
            tokens[value].kind != JsTokenKind::Identifier ||
            tokens[key].text != tokens[value].text) continue;
        if (value + 1 >= tokens.size() ||
            (tokens[value + 1].text != "," && tokens[value + 1].text != "}")) continue;
        if (key >= syntax.identifier_roles.size() ||
            syntax.identifier_roles[key] != JsIdentifierRole::PropertyKey) continue;
        const std::size_t node = syntax.node_at_token[key];
        const std::size_t parent = node < syntax.nodes.size() ? syntax.nodes[node].parent : 0;
        if (parent >= syntax.nodes.size() ||
            syntax.nodes[parent].role != JsGroupRole::ObjectLiteral) continue;
        replacements.push_back({tokens[key + 1].begin, tokens[value].end, ""});
        key = value;
    }
    return replacements;
}

[[maybe_unused]] std::vector<JsReplacement> plan_js_simple_destructuring_parameters(
    const std::vector<JsToken>& tokens, const JsConcreteSyntax& syntax) {
    std::vector<JsReplacement> replacements;
    static const std::unordered_set<std::string_view> reserved = {
        "await","break","case","catch","class","const","continue","debugger",
        "default","delete","do","else","enum","eval","export","extends","false",
        "finally","for","function","if","implements","import","in","instanceof",
        "interface","let","new","null","package","private","protected","public",
        "return","static","super","switch","this","throw","true","try","typeof",
        "var","void","while","with","yield","arguments"
    };
    for (std::size_t function = 0; function + 8 < tokens.size(); ++function) {
        if (tokens[function].text != "function") continue;
        std::size_t open = function + 1;
        if (tokens[open].text == "*") ++open;
        if (open < tokens.size() && tokens[open].kind == JsTokenKind::Identifier) ++open;
        if (open + 5 >= tokens.size() || tokens[open].text != "(") continue;
        const bool object_pattern = tokens[open + 1].text == "{";
        const bool array_pattern = tokens[open + 1].text == "[";
        if ((!object_pattern && !array_pattern) ||
            tokens[open + 2].kind != JsTokenKind::Identifier ||
            tokens[open + 3].text != (object_pattern ? "}" : "]") ||
            tokens[open + 4].text != ")" || tokens[open + 5].text != "{") continue;
        const std::size_t parameter = open + 2;
        if (tokens[parameter].text.size() <= 2 || reserved.count(tokens[parameter].text))
            continue;
        const std::size_t body_close = syntax.matching_token[open + 5];
        if (body_close >= tokens.size()) continue;

        bool unsafe = false;
        std::unordered_set<std::string_view> occupied = reserved;
        for (std::size_t i = open + 6; i < body_close; ++i) {
            if (tokens[i].text == "function" || tokens[i].text == "class" ||
                tokens[i].text == "eval" || tokens[i].text == "with" ||
                tokens[i].text == "arguments" ||
                (tokens[i].text == "=" && i + 1 < body_close && tokens[i + 1].text == ">"))
                unsafe = true;
            if (tokens[i].kind == JsTokenKind::Identifier &&
                tokens[i].text != tokens[parameter].text)
                occupied.insert(tokens[i].text);
        }
        if (unsafe) continue;
        std::string replacement;
        for (std::size_t next = 0;; ++next) {
            replacement = js_short_name(next);
            if (!occupied.count(replacement)) break;
        }

        std::vector<std::pair<std::size_t, bool>> references;
        for (std::size_t i = open + 6; i < body_close; ++i) {
            if (tokens[i].text != tokens[parameter].text) continue;
            const JsIdentifierRole role = i < syntax.identifier_roles.size()
                ? syntax.identifier_roles[i] : JsIdentifierRole::Unknown;
            if (role == JsIdentifierRole::PropertyKey || role == JsIdentifierRole::MemberProperty ||
                role == JsIdentifierRole::Label || role == JsIdentifierRole::ImportExportName ||
                role == JsIdentifierRole::PrivateName) continue;
            const bool shorthand = role == JsIdentifierRole::ShorthandProperty;
            references.push_back({i, shorthand});
        }
        const std::size_t old_size = (references.size() + 1) * tokens[parameter].text.size();
        const std::size_t expansion = object_pattern ? tokens[parameter].text.size() + 1 : 0;
        std::size_t new_size = (references.size() + 1) * replacement.size() + expansion;
        for (const auto& reference : references)
            if (reference.second) new_size += tokens[parameter].text.size() + 1;
        if (references.empty() || new_size >= old_size) continue;

        replacements.push_back({tokens[parameter].begin, tokens[parameter].end,
            object_pattern ? std::string(tokens[parameter].text) + ":" + replacement
                           : replacement});
        for (const auto& reference : references)
            replacements.push_back({tokens[reference.first].begin, tokens[reference.first].end,
                reference.second ? std::string(tokens[parameter].text) + ":" + replacement
                                 : replacement});
        function = body_close;
    }
    return replacements;
}

JsRewriteResult apply_js_replacements(const std::string& source,
                                      std::vector<JsReplacement> replacements){
    std::sort(replacements.begin(),replacements.end(),[](const auto& a,const auto& b){return a.begin<b.begin;});
    std::string result;result.reserve(source.size());std::size_t cursor=0;
    for(const auto& r:replacements){
        if(r.begin<cursor||r.end<r.begin||r.end>source.size())
            return {source, false, false};
        result.append(source,cursor,r.begin-cursor);result+=r.text;cursor=r.end;
    }
    result.append(source,cursor,source.size()-cursor);
    if(result.size()>=source.size())return {source,true,false};
    return {std::move(result),true,true};
}

bool parse_small_decimal(std::string_view text, std::uint64_t& value) {
    if (text.empty()) return false;
    value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        const unsigned digit = static_cast<unsigned>(c - '0');
        if (value > (9007199254740991ULL - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return true;
}

std::vector<JsReplacement> plan_js_constant_folding(const std::vector<JsToken>& tokens) {
    std::vector<JsReplacement> replacements;
    for (std::size_t i = 0; i + 4 < tokens.size(); ++i) {
        if (tokens[i].text != "(" || tokens[i + 1].kind != JsTokenKind::Number ||
            tokens[i + 3].kind != JsTokenKind::Number || tokens[i + 4].text != ")") continue;
        std::uint64_t left = 0, right = 0, result = 0;
        if (!parse_small_decimal(tokens[i + 1].text, left) ||
            !parse_small_decimal(tokens[i + 3].text, right)) continue;
        const std::string_view op = tokens[i + 2].text;
        if (op == "+") {
            if (left > 9007199254740991ULL - right) continue;
            result = left + right;
        } else if (op == "-") {
            if (left < right) continue;
            result = left - right;
        } else if (op == "*") {
            if (right && left > 9007199254740991ULL / right) continue;
            result = left * right;
        } else if (op == "/") {
            if (!right || left % right) continue;
            result = left / right;
        } else if (op == "%") {
            if (!right) continue;
            result = left % right;
        } else if (op == "&" || op == "|" || op == "^") {
            const std::uint32_t lhs = static_cast<std::uint32_t>(left);
            const std::uint32_t rhs = static_cast<std::uint32_t>(right);
            const std::uint32_t bits = op == "&" ? lhs & rhs : op == "|" ? lhs | rhs : lhs ^ rhs;
            const std::int64_t signed_result = bits < 0x80000000U
                ? static_cast<std::int64_t>(bits)
                : static_cast<std::int64_t>(bits) - 0x100000000LL;
            const std::string folded = std::to_string(signed_result);
            if (folded.size() < tokens[i + 3].end - tokens[i + 1].begin)
                replacements.push_back({tokens[i + 1].begin, tokens[i + 3].end, folded});
            i += 4;
            continue;
        } else if (op == "<" || op == ">") {
            const bool comparison = op == "<" ? left < right : left > right;
            const std::string folded = comparison ? "!0" : "!1";
            if (js_rewrite_is_smaller(tokens, i + 1, i + 3, folded))
                replacements.push_back({tokens[i + 1].begin, tokens[i + 3].end, folded});
            i += 4;
            continue;
        } else continue;
        const std::string folded = std::to_string(result);
        if (js_rewrite_is_smaller(tokens, i + 1, i + 3, folded))
            replacements.push_back({tokens[i + 1].begin, tokens[i + 3].end, folded});
        i += 4;
    }
    return replacements;
}

std::vector<JsReplacement> plan_js_undefined_literals(
    const std::vector<JsToken>& tokens, const JsScopeGraph& graph,
    const JsConcreteSyntax& syntax) {
    std::vector<JsReplacement> replacements;
    for (const JsReference& reference : graph.references) {
        if (reference.token >= tokens.size() || tokens[reference.token].text != "undefined" ||
            reference.binding < graph.bindings.size() ||
            reference.access != JsReferenceAccess::Read) continue;
        const JsIdentifierRole role = reference.token < syntax.identifier_roles.size()
            ? syntax.identifier_roles[reference.token] : JsIdentifierRole::Unknown;
        if (role != JsIdentifierRole::Reference) continue;
        const std::string_view next = reference.token + 1 < tokens.size()
            ? tokens[reference.token + 1].text : std::string_view();
        const bool member_or_call = next == "." || next == "[" || next == "(" ||
                                    next == "`" || next == "?";
        replacements.push_back({tokens[reference.token].begin, tokens[reference.token].end,
                                member_or_call ? "(void 0)" : "void 0"});
    }
    return replacements;
}

std::vector<JsReplacement> plan_js_constant_conditionals(
    const std::vector<JsToken>& tokens, const JsSemanticFacts& facts) {
    std::vector<JsReplacement> replacements;
    static const std::unordered_set<std::string_view> safe_prefixes = {
        "(", "[", "{", "=", ",", ":", ";", "return", "throw", "case"
    };
    for (std::size_t i = 0; i + 4 < tokens.size(); ++i) {
        if ((tokens[i].text != "true" && tokens[i].text != "false") ||
            tokens[i + 1].text != "?" || tokens[i + 3].text != ":") continue;
        if (i && safe_prefixes.count(tokens[i - 1].text) == 0) continue;
        if (facts.values[i + 2] == JsValueKind::Unknown ||
            facts.values[i + 4] == JsValueKind::Unknown) continue;
        const JsToken& selected = tokens[i].text == "true" ? tokens[i + 2] : tokens[i + 4];
        if (selected.text.size() < tokens[i + 4].end - tokens[i].begin)
            replacements.push_back({tokens[i].begin, tokens[i + 4].end,
                                    std::string(selected.text)});
        i += 4;
    }
    return replacements;
}

std::vector<JsReplacement> plan_js_constant_logical_expressions(
    const std::vector<JsToken>& tokens, const JsSemanticFacts& facts) {
    std::vector<JsReplacement> replacements;
    static const std::unordered_set<std::string_view> safe_prefixes = {
        "(", "[", "{", "=", ",", ":", ";", "return", "throw", "case"
    };
    static const std::unordered_set<std::string_view> expression_ends = {
        ";", ",", ")", "]", "}", ":"
    };
    for (std::size_t i = 0; i + 3 < tokens.size(); ++i) {
        const bool value = tokens[i].text == "true";
        if (!value && tokens[i].text != "false") continue;
        const bool and_operator = tokens[i + 1].text == "&" && tokens[i + 2].text == "&";
        const bool or_operator = tokens[i + 1].text == "|" && tokens[i + 2].text == "|";
        const bool nullish_operator = tokens[i + 1].text == "?" && tokens[i + 2].text == "?";
        if (!and_operator && !or_operator && !nullish_operator) continue;
        if (i && !safe_prefixes.count(tokens[i - 1].text)) continue;
        if (i + 4 < tokens.size() && !expression_ends.count(tokens[i + 4].text)) continue;
        const JsToken& right = tokens[i + 3];
        if (facts.values[i + 3] == JsValueKind::Unknown &&
            right.kind != JsTokenKind::Identifier) continue;
        const bool select_right = (and_operator && value) || (or_operator && !value);
        const JsToken& selected = select_right ? right : tokens[i];
        if (selected.text.size() < tokens[i + 3].end - tokens[i].begin)
            replacements.push_back({tokens[i].begin, tokens[i + 3].end,
                                    std::string(selected.text)});
        i += 3;
    }
    return replacements;
}

std::vector<JsReplacement> plan_js_unreachable_debuggers(
    const std::vector<JsToken>& tokens, const JsSemanticFacts& facts) {
    std::vector<JsReplacement> replacements;
    for (std::size_t i = 0; i + 4 < tokens.size(); ++i) {
        if (facts.completions[i] != JsCompletionKind::Return) continue;
        std::size_t terminator = i + 1;
        while (terminator < tokens.size() && tokens[terminator].text != ";" &&
               tokens[terminator].text != "}") ++terminator;
        if (terminator + 2 >= tokens.size() || tokens[terminator].text != ";" ||
            tokens[terminator + 1].text != "debugger" || tokens[terminator + 2].text != ";") continue;
        replacements.push_back({tokens[terminator + 1].begin, tokens[terminator + 2].end, ""});
        i = terminator + 2;
    }
    return replacements;
}

std::vector<JsReplacement> plan_js_unreachable_literal_statements(
    const std::vector<JsToken>& tokens, const JsSemanticFacts& facts,
    const JsConcreteSyntax& syntax) {
    std::vector<JsReplacement> replacements;
    for (std::size_t i = 0; i + 3 < tokens.size(); ++i) {
        if (facts.completions[i] != JsCompletionKind::Return &&
            facts.completions[i] != JsCompletionKind::Throw) continue;
        std::size_t terminator = i + 1;
        while (terminator < tokens.size() && tokens[terminator].text != ";" &&
               tokens[terminator].text != "}") ++terminator;
        if (terminator + 2 >= tokens.size() || tokens[terminator].text != ";" ||
            tokens[terminator + 2].text != ";" ||
            facts.values[terminator + 1] == JsValueKind::Unknown) continue;
        const std::size_t first_node = syntax.node_at_token[terminator];
        const std::size_t second_node = syntax.node_at_token[terminator + 1];
        if (syntax.nodes[first_node].parent != syntax.nodes[second_node].parent) continue;
        replacements.push_back({tokens[terminator + 1].begin,
                                tokens[terminator + 2].end, ""});
        i = terminator + 2;
    }
    return replacements;
}

std::vector<JsReplacement> plan_js_return_conditionals(const std::vector<JsToken>& tokens) {
    std::vector<JsReplacement> replacements;
    for (std::size_t i = 0; i + 9 < tokens.size(); ++i) {
        if (tokens[i].text != "if" || tokens[i + 1].text != "(" ||
            tokens[i + 3].text != ")" || tokens[i + 4].text != "return" ||
            tokens[i + 6].text != ";") continue;
        const bool with_else = tokens[i + 7].text == "else";
        const std::size_t second_return = with_else ? i + 8 : i + 7;
        if (second_return + 2 >= tokens.size() ||
            tokens[second_return].text != "return" ||
            tokens[second_return + 2].text != ";") continue;
        const auto simple_value = [](const JsToken& token) {
            return token.kind == JsTokenKind::Identifier ||
                   token.kind == JsTokenKind::Number || token.kind == JsTokenKind::String;
        };
        if (!simple_value(tokens[i + 2]) || !simple_value(tokens[i + 5]) ||
            !simple_value(tokens[second_return + 1])) continue;
        std::string compressed = "return ";
        compressed.append(tokens[i + 2].text);
        compressed.push_back('?');
        compressed.append(tokens[i + 5].text);
        compressed.push_back(':');
        compressed.append(tokens[second_return + 1].text);
        compressed.push_back(';');
        if (compressed.size() < tokens[second_return + 2].end - tokens[i].begin)
            replacements.push_back({tokens[i].begin, tokens[second_return + 2].end,
                                    std::move(compressed)});
        i = second_return + 2;
    }
    return replacements;
}

std::vector<JsReplacement> plan_js_compound_assignments(const std::vector<JsToken>& tokens,
                                                         const JsScopeGraph& graph) {
    std::vector<JsReplacement> replacements;
    std::vector<std::size_t> binding_scope(tokens.size(), tokens.size());
    for (const auto& ref : graph.references) binding_scope[ref.token] = ref.binding_scope;
    for (std::size_t i = 0; i + 4 < tokens.size(); ++i) {
        if (tokens[i].kind != JsTokenKind::Identifier || tokens[i + 1].text != "=" ||
            tokens[i + 2].text != tokens[i].text) continue;
        const std::string_view op = tokens[i + 3].text;
        if (op != "+" && op != "-" && op != "*" && op != "/" && op != "%") continue;
        const std::size_t left_scope = binding_scope[i], read_scope = binding_scope[i + 2];
        if (left_scope >= graph.scopes.size() || left_scope != read_scope ||
            graph.scopes[left_scope].dynamic_lookup) continue;
        const std::string compressed = std::string(tokens[i].text) + std::string(op) + "=";
        if (compressed.size() < tokens[i + 3].end - tokens[i].begin)
            replacements.push_back({tokens[i].begin, tokens[i + 3].end, compressed});
        i += 3;
    }
    return replacements;
}

std::vector<JsReplacement> plan_js_var_declaration_joins(const std::vector<JsToken>& tokens) {
    std::vector<JsReplacement> replacements;
    for (std::size_t declaration = 0; declaration + 4 < tokens.size(); ++declaration) {
        if (tokens[declaration].text != "var" ||
            tokens[declaration + 1].kind != JsTokenKind::Identifier) continue;
        // A declaration in a for/for-in/for-of header is not a statement
        // boundary. Scanning onward from it can otherwise consume the loop
        // body and join an unrelated declaration after the loop.
        if (declaration && tokens[declaration - 1].text == "(") continue;
        unsigned parens = 0, brackets = 0, braces = 0;
        for (std::size_t end = declaration + 2; end + 2 < tokens.size(); ++end) {
            const std::string_view text = tokens[end].text;
            if (text == "(") ++parens;
            else if (text == ")" && parens) --parens;
            else if (text == "[") ++brackets;
            else if (text == "]" && brackets) --brackets;
            else if (text == "{") ++braces;
            else if (text == "}" && braces) --braces;
            if (parens || brackets || braces || text != ";") continue;
            if (tokens[end + 1].text == "var" &&
                tokens[end + 2].kind == JsTokenKind::Identifier) {
                replacements.push_back({tokens[end].begin, tokens[end + 1].end, ","});
                declaration = end + 1;
            }
            break;
        }
    }
    return replacements;
}

std::vector<JsReplacement> plan_js_unused_empty_vars(
    const std::vector<JsToken>& tokens, const JsScopeGraph& graph,
    const JsConcreteSyntax& syntax) {
    std::vector<JsReplacement> replacements;
    std::unordered_set<std::string_view> unresolved_names;
    unresolved_names.reserve(graph.unresolved_references.size());
    for (std::size_t reference : graph.unresolved_references)
        unresolved_names.insert(tokens[graph.references[reference].token].text);
    for (std::size_t binding = 0; binding < graph.bindings.size(); ++binding) {
        const JsBinding& candidate = graph.bindings[binding];
        if (candidate.kind != JsBindingKind::Var || candidate.scope >= graph.scopes.size() ||
            graph.scopes[candidate.scope].kind != JsScopeKind::Function ||
            graph.scopes[candidate.scope].dynamic_lookup ||
            graph.scopes[candidate.scope].descendant_dynamic_lookup ||
            candidate.declaration_tokens.size() != 1 ||
            graph.binding_uses[binding].reads || graph.binding_uses[binding].writes ||
            graph.binding_uses[binding].captures || candidate.token == 0 ||
            candidate.token + 1 >= tokens.size() ||
            tokens[candidate.token - 1].text != "var" ||
            tokens[candidate.token + 1].text != ";") continue;
        if (unresolved_names.count(candidate.name)) continue;
        const std::size_t node = syntax.node_at_token[candidate.token - 1];
        const std::size_t parent = node < syntax.nodes.size()
            ? syntax.nodes[node].parent : syntax.nodes.size();
        if (parent >= syntax.nodes.size() || syntax.nodes[parent].role != JsGroupRole::Block)
            continue;
        replacements.push_back({tokens[candidate.token - 1].begin,
                                tokens[candidate.token + 1].end, ""});
    }
    return replacements;
}

std::vector<JsReplacement> plan_js_adjacent_dead_var_stores(
    const std::vector<JsToken>& tokens, const JsScopeGraph& graph,
    const JsSemanticFacts& facts) {
    std::vector<JsReplacement> replacements;
    for (std::size_t token = 0; token + 7 < tokens.size(); ++token) {
        if (tokens[token].kind != JsTokenKind::Identifier || tokens[token + 1].text != "=" ||
            facts.values[token + 2] == JsValueKind::Unknown || tokens[token + 3].text != ";" ||
            tokens[token + 4].kind != JsTokenKind::Identifier || tokens[token + 5].text != "=" ||
            facts.values[token + 6] == JsValueKind::Unknown || tokens[token + 7].text != ";")
            continue;
        const std::size_t first_ref = graph.reference_at_token[token];
        const std::size_t second_ref = graph.reference_at_token[token + 4];
        if (first_ref >= graph.references.size() || second_ref >= graph.references.size()) continue;
        const JsReference& first = graph.references[first_ref];
        const JsReference& second = graph.references[second_ref];
        if (first.access != JsReferenceAccess::Write ||
            second.access != JsReferenceAccess::Write || first.binding != second.binding ||
            first.binding >= graph.bindings.size()) continue;
        const JsBinding& binding = graph.bindings[first.binding];
        if (binding.kind != JsBindingKind::Var ||
            graph.scopes[binding.scope].dynamic_lookup ||
            graph.scopes[binding.scope].descendant_dynamic_lookup ||
            graph.binding_uses[first.binding].captures) continue;
        replacements.push_back({tokens[token].begin, tokens[token + 3].end, ""});
        token += 3;
    }
    return replacements;
}

std::vector<JsReplacement> plan_js_literal_iifes(
    const std::vector<JsToken>& tokens, const JsSemanticFacts& facts) {
    std::vector<JsReplacement> replacements;
    for (std::size_t i = 0; i + 11 < tokens.size(); ++i) {
        if (tokens[i].text != "(" || tokens[i + 1].text != "function" ||
            tokens[i + 2].text != "(" || tokens[i + 3].text != ")" ||
            tokens[i + 4].text != "{" || tokens[i + 5].text != "return" ||
            tokens[i + 7].text != ";" || tokens[i + 8].text != "}" ||
            tokens[i + 9].text != ")" || tokens[i + 10].text != "(" ||
            tokens[i + 11].text != ")") continue;
        const JsToken& value = tokens[i + 6];
        if (facts.values[i + 6] == JsValueKind::Unknown) continue;
        if (value.text.size() < tokens[i + 11].end - tokens[i].begin)
            replacements.push_back({tokens[i].begin, tokens[i + 11].end,
                                    std::string(value.text)});
        i += 11;
    }
    return replacements;
}

std::vector<JsReplacement> plan_js_property_mangling(
    const std::vector<JsToken>& tokens, const std::vector<std::string>& allowlist) {
    std::vector<JsReplacement> replacements;
    std::unordered_set<std::string_view> occupied;
    for (const auto& token : tokens) if (token.kind == JsTokenKind::Identifier) occupied.insert(token.text);
    std::vector<std::pair<std::string_view, std::string>> names;
    std::size_t next_name = 0;
    for (const auto& property : allowlist) {
        if (property.empty() || occupied.count(property) == 0) continue;
        if (std::any_of(names.begin(), names.end(), [&](const auto& pair){ return pair.first == property; })) continue;
        std::string replacement;
        do replacement = js_short_name(next_name++); while (occupied.count(replacement));
        if (replacement.size() >= property.size()) continue;
        occupied.insert(replacement);
        names.push_back({property, replacement});
    }
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].kind != JsTokenKind::Identifier) continue;
        const auto found = std::find_if(names.begin(), names.end(), [&](const auto& pair){ return pair.first == tokens[i].text; });
        if (found == names.end()) continue;
        const std::string_view previous = i ? tokens[i - 1].text : std::string_view();
        const std::string_view next = i + 1 < tokens.size() ? tokens[i + 1].text : std::string_view();
        const bool dot_access = previous == ".";
        const bool literal_key = next == ":" && (previous == "{" || previous == ",");
        if (dot_access || literal_key)
            replacements.push_back({tokens[i].begin, tokens[i].end, found->second});
    }
    return replacements;
}

std::size_t matching_js_token(const std::vector<JsToken>& tokens,
                              std::size_t open, const char* left,
                              const char* right) {
    std::size_t depth = 0;
    for (std::size_t i = open; i < tokens.size(); ++i) {
        if (tokens[i].text == left) ++depth;
        else if (tokens[i].text == right && --depth == 0) return i;
    }
    return tokens.size();
}

std::string js_short_name(std::size_t index) {
    return js_short_name(index, "$_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ",
                         "$_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
}

std::string js_short_name(std::size_t index, const std::string& first,
                          const std::string& continuation) {
    std::size_t length = 1;
    std::size_t capacity = first.size();
    while (index >= capacity) {
        index -= capacity;
        ++length;
        if (capacity > std::numeric_limits<std::size_t>::max() / continuation.size()) break;
        capacity *= continuation.size();
    }
    std::size_t suffix_capacity = 1;
    for (std::size_t position = 1; position < length; ++position)
        suffix_capacity *= continuation.size();
    std::string name(length, '$');
    name[0] = first[(index / suffix_capacity) % first.size()];
    index %= suffix_capacity;
    for (std::size_t position = 1; position < length; ++position) {
        suffix_capacity /= continuation.size();
        name[position] = continuation[(index / suffix_capacity) % continuation.size()];
        index %= suffix_capacity;
    }
    return name;
}

[[maybe_unused]] std::vector<JsReplacement> plan_js_parameter_mangling(
    const std::vector<JsToken>& tokens) {
    std::vector<JsReplacement> replacements;
    static const std::unordered_set<std::string_view> unsafe_scope_words = {
        "eval", "with", "arguments", "catch", "class", "let", "const"
    };
    static const std::unordered_set<std::string_view> reserved_words = {
        "await", "break", "case", "catch", "class", "const", "continue",
        "debugger", "default", "delete", "do", "else", "enum", "export",
        "extends", "false", "finally", "for", "function", "if", "import",
        "in", "instanceof", "let", "new", "null", "return", "static",
        "super", "switch", "this", "throw", "true", "try", "typeof",
        "var", "void", "while", "with", "yield"
    };

    for (std::size_t f = 0; f < tokens.size(); ++f) {
        if (tokens[f].text != "function") continue;
        std::size_t cursor = f + 1;
        if (cursor < tokens.size() && tokens[cursor].text == "*") ++cursor;
        if (cursor < tokens.size() && tokens[cursor].text != "(") ++cursor;
        if (cursor >= tokens.size() || tokens[cursor].text != "(") continue;
        const std::size_t close_params = matching_js_token(tokens, cursor, "(", ")");
        if (close_params == tokens.size() || close_params + 1 >= tokens.size() ||
            tokens[close_params + 1].text != "{") continue;
        const std::size_t close_body =
            matching_js_token(tokens, close_params + 1, "{", "}");
        if (close_body == tokens.size()) continue;

        std::vector<std::string_view> params;
        bool simple_params = true;
        for (std::size_t p = cursor + 1; p < close_params; ++p) {
            if ((p - cursor) % 2 == 1) {
                const std::string_view name = tokens[p].text;
                if (name.empty() || reserved_words.count(name) != 0 ||
                    !(ascii_alpha(static_cast<unsigned char>(name[0])) ||
                      name[0] == '_' || name[0] == '$')) {
                    simple_params = false;
                    break;
                }
                params.push_back(name);
            } else if (tokens[p].text != ",") {
                simple_params = false;
                break;
            }
        }
        if (!simple_params || params.empty()) continue;

        bool unsafe_scope = false;
        std::unordered_set<std::string_view> occupied;
        for (const auto& param : params) occupied.insert(param);
        for (std::size_t p = close_params + 2; p < close_body; ++p) {
            occupied.insert(tokens[p].text);
            if (unsafe_scope_words.count(tokens[p].text) != 0 ||
                tokens[p].text == "function" ||
                (tokens[p].text == "=" && p + 1 < close_body &&
                 tokens[p + 1].text == ">")) {
                unsafe_scope = true;
            }
        }
        if (unsafe_scope) continue;

        std::size_t next_name = 0;
        std::unordered_set<std::string_view> handled;
        for (std::size_t parameter = 0; parameter < params.size(); ++parameter) {
            const std::string_view original = params[parameter];
            if (!handled.insert(original).second) continue;
            std::string replacement;
            do replacement = js_short_name(next_name++);
            while (occupied.count(replacement) != 0 ||
                   reserved_words.count(replacement) != 0);
            if (replacement.size() >= original.size()) continue;

            bool shorthand = false;
            for (std::size_t p = close_params + 2; p < close_body; ++p) {
                if (tokens[p].text != original) continue;
                const std::string_view previous = p ? tokens[p - 1].text : std::string_view();
                const std::string_view next = p + 1 < tokens.size()
                    ? tokens[p + 1].text : std::string_view();
                if ((previous == "{" || previous == ",") &&
                    (next == "}" || next == ",")) shorthand = true;
            }
            if (shorthand) continue;

            for (std::size_t declaration = 0; declaration < params.size(); ++declaration) {
                if (params[declaration] == original) {
                    const JsToken& token = tokens[cursor + 1 + declaration * 2];
                    replacements.push_back({token.begin, token.end, replacement});
                }
            }
            for (std::size_t p = close_params + 2; p < close_body; ++p) {
                if (tokens[p].text != original) continue;
                const std::string_view previous = p ? tokens[p - 1].text : std::string_view();
                const std::string_view next = p + 1 < tokens.size()
                    ? tokens[p + 1].text : std::string_view();
                if (previous == "." || previous == "#" || previous == "break" ||
                    previous == "continue" || next == ":" ||
                    (next == "(" && (previous == "{" || previous == "," ||
                                     previous == "get" || previous == "set" ||
                                     previous == "async" || previous == "*"))) continue;
                replacements.push_back(
                    {tokens[p].begin, tokens[p].end, replacement});
            }
            occupied.insert(replacement);
        }
        f = close_body;
    }
    return replacements;
}

bool js_line_terminator_in(const std::string& input, std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end; ++i) {
        if (input[i] == '\n' || input[i] == '\r') return true;
        if (i + 2 < end && static_cast<unsigned char>(input[i]) == 0xe2 &&
            static_cast<unsigned char>(input[i + 1]) == 0x80 &&
            (static_cast<unsigned char>(input[i + 2]) == 0xa8 ||
             static_cast<unsigned char>(input[i + 2]) == 0xa9)) return true;
    }
    return false;
}

bool copy_template_literal(const std::string& input, std::size_t& i,
                           std::string& output, std::string& error,
                           JsTokenRecorder* recorder = nullptr) {
    struct Frame {
        bool expression;
        std::size_t braces;
        bool can_start_regex;
        std::size_t segment_begin;
    };
    std::vector<Frame> stack;
    const std::size_t template_begin = i;
    output.push_back(input[i++]);
    stack.push_back({false, 0, true, template_begin});
    while (i < input.size()) {
        char c = input[i++];
        output.push_back(c);
        Frame& frame = stack.back();
        if (!frame.expression) {
            if (c == '\\' && i < input.size()) output.push_back(input[i++]);
            else if (c == '`') {
                if (recorder) recorder->record(JsTokenKind::Template, frame.segment_begin, i);
                stack.pop_back();
                if (stack.empty()) return true;
            } else if (c == '$' && i < input.size() && input[i] == '{') {
                output.push_back(input[i++]);
                if (recorder) recorder->record(JsTokenKind::Template, frame.segment_begin, i);
                frame.expression = true;
                frame.braces = 1;
                frame.can_start_regex = true;
            }
            continue;
        }
        if (ws(c)) {
            // Formatting whitespace between tokens preserves the regex
            // context; only the surrounding tokens change it.
            continue;
        }
        if (c == '\'' || c == '"') {
            const std::size_t begin = i - 1;
            const char quote = c;
            bool escaped = false;
            while (i < input.size()) {
                char q = input[i++]; output.push_back(q);
                if (escaped) escaped = false;
                else if (q == '\\') escaped = true;
                else if (q == quote) break;
            }
            if (recorder) recorder->record(JsTokenKind::String, begin, i);
            frame.can_start_regex = false;
        } else if (c == '`') {
            stack.push_back({false, 0, true, i - 1});
        } else if (c == '/' && i < input.size() && input[i] == '/') {
            output.push_back(input[i++]);
            while (i < input.size()) { char q=input[i++]; output.push_back(q); if (q=='\n'||q=='\r') break; }
            frame.can_start_regex = true;
        } else if (c == '/' && i < input.size() && input[i] == '*') {
            output.push_back(input[i++]);
            while (i < input.size()) { char q=input[i++]; output.push_back(q); if (q=='*'&&i<input.size()&&input[i]=='/') { output.push_back(input[i++]); break; } }
            // A block comment leaves the regex context unchanged.
        } else if (c == '/' && frame.can_start_regex) {
            const std::size_t begin = i - 1;
            // A regular-expression literal is copied verbatim so that `//`,
            // `{`, `}` and backticks inside it cannot corrupt expression or
            // template frame state.
            bool escaped = false, in_class = false, closed = false;
            while (i < input.size()) {
                char q = input[i++]; output.push_back(q);
                if (escaped) { escaped = false; continue; }
                if (q == '\\') { escaped = true; continue; }
                if (q == '[') in_class = true;
                else if (q == ']') in_class = false;
                else if (q == '/' && !in_class) { closed = true; break; }
                else if (q == '\n' || q == '\r') break;
            }
            if (!closed) { error = "unterminated JavaScript regular expression"; output.clear(); return false; }
            while (i < input.size() && ascii_alpha(static_cast<unsigned char>(input[i])))
                output.push_back(input[i++]);
            if (recorder) recorder->record(JsTokenKind::Regex, begin, i);
            frame.can_start_regex = false;
        } else if (ascii_alpha(static_cast<unsigned char>(c)) || c == '_' || c == '$') {
            const std::size_t begin = i - 1;
            while (i < input.size() &&
                   (ascii_alnum(static_cast<unsigned char>(input[i])) ||
                    input[i] == '_' || input[i] == '$')) {
                output.push_back(input[i]);
                ++i;
            }
            static const std::unordered_set<std::string> prefix_words = {
                "return","throw","case","delete","void","typeof","new",
                "in","instanceof","yield","await","else","do"
            };
            frame.can_start_regex =
                prefix_words.count(input.substr(begin, i - begin)) != 0;
            if (recorder) recorder->record(JsTokenKind::Identifier, begin, i);
        } else if (ascii_digit(static_cast<unsigned char>(c))) {
            const std::size_t begin = i - 1;
            bool exponent = false;
            while (i < input.size()) {
                const unsigned char u = static_cast<unsigned char>(input[i]);
                const char q = input[i];
                if (ascii_alnum(u) || q == '.' || q == '_') {
                    exponent = q == 'e' || q == 'E';
                    output.push_back(q);
                    ++i;
                    continue;
                }
                if ((q == '+' || q == '-') && exponent) {
                    exponent = false;
                    output.push_back(q);
                    ++i;
                    continue;
                }
                break;
            }
            if (recorder) recorder->record(JsTokenKind::Number, begin, i);
            frame.can_start_regex = false;
        } else if (c == '{') {
            if (recorder) recorder->record(JsTokenKind::Punctuator, i - 1, i);
            ++frame.braces;
            frame.can_start_regex = true;
        } else if (c == '}' && --frame.braces == 0) {
            frame.expression = false;
            frame.can_start_regex = false;
            frame.segment_begin = i - 1;
        } else if (c == ';' || c == ',' || c == ':' || c == '(' || c == '[' ||
                   c == '=' || c == '!' || c == '?' || c == '&' || c == '|' ||
                   c == '+' || c == '-' || c == '*' || c == '%' || c == '<' ||
                   c == '>' || c == '~' || c == '^') {
            if (recorder) recorder->record(JsTokenKind::Punctuator, i - 1, i);
            frame.can_start_regex = true;
        } else {
            if (recorder) recorder->record(JsTokenKind::Punctuator, i - 1, i);
            frame.can_start_regex = false;
        }
    }
    error = "unterminated JavaScript template literal";
    output.clear();
    return false;
}

bool word_char(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    // Be deliberately conservative for UTF-8: non-ASCII bytes may belong to a
    // JavaScript/CSS identifier, so removing adjacent whitespace could merge
    // tokens even though this lightweight scanner does not decode Unicode IDs.
    return u >= 0x80 || ascii_alnum(u) || c == '_' || c == '$' || c == '-' || c == '\\';
}

bool js_needs_separator(char left, char right, bool after_regex,
                        bool preserve_jsx_boundary) {
    const unsigned char next = static_cast<unsigned char>(right);
    return (word_char(left) && word_char(right)) ||
           (left == '+' && right == '+') ||
           (left == '-' && right == '-') ||
           (left == '/' && (right == '/' || right == '*')) ||
           (after_regex && (ascii_alpha(next) || right == '_' || right == '$' ||
                            next >= 0x80)) ||
           (left == '*' && right == '/') ||
           (preserve_jsx_boundary && left == '/' && right == '>') ||
           (preserve_jsx_boundary && left == '<' &&
            (ascii_alpha(next) || right == '>' || right == '/')) ||
           (ascii_digit(static_cast<unsigned char>(left)) && right == '.');
}

enum class JsLineTerminatorAction {
    DropAfterDelimiter,
    PreserveRestrictedProduction,
    PreserveExpressionContinuation,
    PreserveAsiBoundary
};

enum class JsSemicolonRole { EmptyControlBody, RequiredEmptyStatement, Delimiter };

JsSemicolonRole classify_js_semicolon(const std::string& previous_token) {
    if (previous_token == ")control") return JsSemicolonRole::EmptyControlBody;
    if (previous_token == ":" || previous_token == "{" ||
        previous_token == ";" || previous_token == "else" ||
        previous_token == "do") return JsSemicolonRole::RequiredEmptyStatement;
    return JsSemicolonRole::Delimiter;
}

bool js_semicolon_may_be_elided(const std::string& previous_token) {
    return classify_js_semicolon(previous_token) == JsSemicolonRole::Delimiter;
}

JsLineTerminatorAction classify_js_line_terminator(char left,
                                                   const std::string& previous_token,
                                                   char next) {
    if (left == ';' || left == '{')
        return JsLineTerminatorAction::DropAfterDelimiter;
    static const std::unordered_set<std::string> restricted = {
        "return", "throw", "break", "continue", "yield", "await", "async"
    };
    if (restricted.count(previous_token) != 0 || next == '+' || next == '-')
        return JsLineTerminatorAction::PreserveRestrictedProduction;
    if (next == '(' || next == '[' || next == '/' || next == '`' || next == '.')
        return JsLineTerminatorAction::PreserveExpressionContinuation;
    return JsLineTerminatorAction::PreserveAsiBoundary;
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

bool css_needs_space(char left, char right) {
    if ((word_char(left) && word_char(right)) ||
        (left == '/' && right == '*') || (left == '*' && right == '/')) return true;

    // In CSS nesting, authored whitespace after '&' is the descendant
    // combinator. Preserve it before a following compound selector.
    if (left == '&' && (word_char(right) || right == '.' || right == '#' ||
                        right == '[' || right == '*' || right == ':' || right == '\\')) return true;

    // The nesting selector can also appear on the right side of a descendant
    // combinator (`.ancestor &`). Joining it to the preceding selector changes
    // the compound selector just as surely as joining `& .child` does.
    if (right == '&' && (word_char(left) || left == ')' || left == ']' || left == '*')) return true;

    // A leading decimal, class/ID/attribute selector, universal selector or
    // parenthesized construct can begin a distinct CSS token even though it is
    // not word-like. Joining it to the previous token can invalidate a value
    // list or turn a descendant selector into a compound selector.
    if ((word_char(left) || left == '*' || left == '\'' || left == '"') &&
        (right == '.' || right == '#' || right == '[' || right == '(' || right == '*')) return true;

    // Function/attribute/percentage results followed by another value token
    // require separation (for example transform lists and color percentages).
    if ((left == ')' || left == ']' || left == '%' || left == '*' ||
         left == '\'' || left == '"') &&
        (word_char(right) || right == '.' || right == '#' || right == '[' ||
         right == '(' || right == '*' || right == '\'' || right == '"')) return true;

    // Adjacent strings are separate component values, as are identifiers or
    // functions followed by a string (font-family and generated-content lists
    // are common examples). Concatenating them is not CSS token compaction.
    if ((word_char(left) || left == ')' || left == ']' || left == '%') &&
        (right == '\'' || right == '"')) return true;

    return false;
}

void emit_pending_css_space(std::string& out, bool& pending, char next) {
    if (!pending) return;
    if (!out.empty() && !ws(out.back()) && css_needs_space(out.back(), next)) out.push_back(' ');
    pending = false;
}

bool css_colon_precedes_rule_block(const std::string& input, std::size_t colon) {
    std::size_t parens = 0;
    std::size_t brackets = 0;
    bool quoted = false;
    bool escaped = false;
    char quote = 0;
    for (std::size_t i = colon + 1; i < input.size(); ++i) {
        const char c = input[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == quote) quoted = false;
            continue;
        }
        if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
        if (c == '/' && i + 1 < input.size() && input[i + 1] == '*') {
            const auto end = input.find("*/", i + 2);
            if (end == std::string::npos) return false;
            i = end + 1;
            continue;
        }
        if (c == '(') { ++parens; continue; }
        if (c == ')' && parens) { --parens; continue; }
        if (c == '[') { ++brackets; continue; }
        if (c == ']' && brackets) { --brackets; continue; }
        if (parens || brackets) continue;
        if (c == '{') return true;
        if (c == ';' || c == '}') return false;
    }
    return false;
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
    bool preserve_final_bad_string_newline = false;
    std::size_t bracket_depth = 0;
    std::vector<bool> brace_is_component_value;
    bool just_closed_component_value = false;
    // CSS escape whitespace tracking. A hex escape (backslash + 1..6 hex
    // digits) consumes one trailing whitespace as its terminator, and an
    // escaped whitespace character is an identifier character, not removable
    // formatting. Collapsing the whitespace run after either merges what a
    // browser tokenizes as separate identifiers into a single identifier.
    bool in_hex_escape = false;
    unsigned hex_escape_digits = 0;
    bool escaped_ws = false;

    for (std::size_t i = 0; i < input.size();) {
        const char c = input[i];

        if (c == '\'' || c == '"') {
            emit_pending_css_space(output, pending_space, c);
            in_hex_escape = false;
            hex_escape_digits = 0;
            escaped_ws = false;
            const char quote = c;
            output.push_back(c);
            ++i;
            bool escaped = false;
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
                if (q == quote) break;
                // CSS Syntax recovers an unescaped newline as a bad-string
                // token rather than making the whole stylesheet invalid. The
                // newline itself is significant because it terminates the
                // token, so preserve it and resume scanning after it.
                if (q == '\n' || q == '\r' || q == '\f') {
                    preserve_final_bad_string_newline = i == input.size();
                    break;
                }
            }
            // EOF also terminates a CSS string token with a parse error but
            // without rejecting the stylesheet. Preserve the recoverable
            // source rather than turning browser-accepted CSS into a Minify++
            // hard error.
            continue;
        }

        // A CSS escape consumes the following code point. Copy it as a unit
        // so escaped whitespace is not mistaken for removable formatting.
        if (c == '\\' && i + 1 < input.size()) {
            emit_pending_css_space(output, pending_space, c);
            output.push_back(c);
            const char escaped = input[i + 1];
            output.push_back(escaped);
            if (escaped == ' ' || escaped == '\t' || escaped == '\f') {
                // An escaped whitespace character is an identifier code point.
                escaped_ws = true;
                in_hex_escape = false;
                hex_escape_digits = 0;
            } else if (ascii_hex_digit(static_cast<unsigned char>(escaped))) {
                // Hex escape: track following digits so a trailing whitespace
                // terminator and any separator can be preserved.
                in_hex_escape = true;
                hex_escape_digits = 1;
                escaped_ws = false;
            } else {
                in_hex_escape = false;
                hex_escape_digits = 0;
                escaped_ws = false;
            }
            i += 2;
            continue;
        }

        if (c == '/' && i + 1 < input.size() && input[i + 1] == '*') {
            in_hex_escape = false;
            hex_escape_digits = 0;
            escaped_ws = false;
            const bool preserve = i + 2 < input.size() && input[i + 2] == '!';
            const auto end = input.find("*/", i + 2);
            if (end == std::string::npos) {
                // CSS comments are implicitly closed by EOF. Ordinary
                // comments can therefore be removed to EOF; preserved license
                // comments remain byte-for-byte intact and are likewise
                // browser-recoverable without a closing delimiter.
                if (preserve) {
                    emit_pending_css_space(output, pending_space, '/');
                    output.append(input, i, std::string::npos);
                } else {
                    pending_space = true;
                }
                i = input.size();
                continue;
            }
            if (preserve) {
                emit_pending_css_space(output, pending_space, '/');
                output.append(input, i, end + 2 - i);
            } else {
                pending_space = true;
            }
            i = end + 2;
            continue;
        }

        if (ws(c)) {
            if (in_hex_escape || escaped_ws) {
                // A hex escape consumes the first whitespace as its terminator;
                // any further whitespace is a real token separator. An escaped
                // whitespace character is itself an identifier character, so
                // the whitespace following it is a separator. Collapsing this
                // run to a single space would merge distinct browser tokens.
                std::size_t run = 1;
                while (i + run < input.size() && ws(input[i + run])) ++run;
                if (in_hex_escape) {
                    output.push_back(' ');
                    if (run >= 2) output.push_back(' ');
                } else {
                    output.push_back(' ');
                }
                pending_space = false;
                in_hex_escape = false;
                hex_escape_digits = 0;
                escaped_ws = false;
                i += run;
                continue;
            }
            pending_space = true;
            ++i;
            continue;
        }

        const bool punctuation = c == '{' || c == '}' || c == ':' ||
                                 c == ';' || c == ',';
        if (punctuation) {
            in_hex_escape = false;
            hex_escape_digits = 0;
            escaped_ws = false;
            // Whitespace before ':' can distinguish a descendant pseudo-class
            // selector (`.a :hover`) from a compound selector (`.a:hover`).
            // Preserve authored spacing universally rather than guessing
            // declaration context in this intentionally lightweight scanner.
            const bool preserve_before_colon = c == ':' && pending_space &&
                                               css_colon_precedes_rule_block(input, i) &&
                                               !output.empty() && output.back() != ' ';
            const bool preserve_before_block = c == '{' && pending_space &&
                                               !output.empty() && output.back() == ')';
            pending_space = false;
            while (!output.empty() && output.back() == ' ') output.pop_back();
            if (preserve_before_colon || preserve_before_block) output.push_back(' ');
            const bool opens_component_value = c == '{' && !output.empty() && output.back() == ':';
            output.push_back(c);
            if (c == '{') {
                brace_is_component_value.push_back(opens_component_value);
                just_closed_component_value = false;
            } else if (c == '}') {
                just_closed_component_value = !brace_is_component_value.empty() && brace_is_component_value.back();
                if (!brace_is_component_value.empty()) brace_is_component_value.pop_back();
            } else {
                just_closed_component_value = false;
            }
        } else {
            if (in_hex_escape) {
                if (ascii_hex_digit(static_cast<unsigned char>(c)) && hex_escape_digits < 6) {
                    ++hex_escape_digits;
                } else {
                    in_hex_escape = false;
                    hex_escape_digits = 0;
                }
            }
            escaped_ws = false;
            if (pending_space && just_closed_component_value && !output.empty() && output.back() == '}') {
                output.push_back(' ');
                pending_space = false;
            }

            // Whitespace around an attribute-selector namespace separator can
            // distinguish a valid selector from an invalid one.
            if (pending_space && bracket_depth != 0 &&
                (c == '|' || (!output.empty() && output.back() == '|'))) {
                if (!output.empty() && output.back() != ' ') output.push_back(' ');
                pending_space = false;
            }

            // CSS math functions require whitespace around binary + and -.
            // Preserve authored whitespace adjacent to these operators rather
            // than trying to parse the full evolving CSS value grammar.
            if (pending_space && (c == '+' || c == '-') && !output.empty() && output.back() != ' ')
                output.push_back(' ');
            else if (pending_space && !output.empty() &&
                     (output.back() == '+' || output.back() == '-') && output.back() != ' ')
                output.push_back(' ');
            else
                emit_pending_css_space(output, pending_space, c);
            pending_space = false;
            output.push_back(c);
            if (c == '[') ++bracket_depth;
            else if (c == ']' && bracket_depth != 0) --bracket_depth;
            just_closed_component_value = false;
        }
        ++i;
    }

    if (!preserve_final_bad_string_newline) {
        // An escaped trailing whitespace is an identifier character and must
        // survive the trim; only removable formatting whitespace is dropped.
        while (!output.empty() && ws(output.back()) &&
               !(output.size() >= 2 && output[output.size() - 2] == '\\')) {
            output.pop_back();
        }
    }
    error.clear();
    return true;
}

static bool follows_attribute_equals(const std::string& input, std::size_t pos,
                                     std::size_t tag_start) {
    while (pos > tag_start + 1 && ws(input[pos - 1])) --pos;
    return pos > tag_start + 1 && input[pos - 1] == '=';
}

static bool tag_has_preserved_whitespace_style(const std::string& tag) {
    std::string compact;
    compact.reserve(tag.size());
    for (unsigned char c : tag)
        if (!ws(static_cast<char>(c))) compact.push_back(static_cast<char>(std::tolower(c)));
    const auto p = compact.find("white-space:");
    if (p == std::string::npos) return false;
    const auto value = p + 12;
    return compact.compare(value, 3, "pre") == 0 ||
           compact.compare(value, 12, "break-spaces") == 0;
}

bool html(const std::string& input, std::string& output, std::string& error) {
    output.clear();
    output.reserve(input.size());

    bool pending_space = false;
    std::string raw_tag;
    bool raw_tag_can_nest = false;
    std::size_t raw_tag_depth = 0;
    bool preserve_final_whitespace = false;

    for (std::size_t i = 0; i < input.size();) {
        if (!raw_tag.empty()) {
            const std::string close = "</" + raw_tag;
            std::size_t p = i;
            bool found = false;
            for (; p < input.size(); ++p) {
                if (raw_tag_can_nest && starts_ci(input, p, "<" + raw_tag)) {
                    const std::size_t after = p + raw_tag.size() + 1;
                    if (after < input.size() && (input[after] == '>' || input[after] == '/' || ws(input[after])))
                        ++raw_tag_depth;
                }
                if (starts_ci(input, p, close)) {
                    const std::size_t after = p + close.size();
                    if (after < input.size() && (input[after] == '>' || ws(input[after]))) {
                        if (raw_tag_can_nest && raw_tag_depth > 1) {
                            --raw_tag_depth;
                        } else {
                            found = true;
                            break;
                        }
                    }
                }
            }
            if (!found) {
                output.append(input, i, std::string::npos);
                preserve_final_whitespace = true;
                i = input.size();
                break;
            }
            output.append(input, i, p - i);
            i = p;
            raw_tag.clear();
            raw_tag_can_nest = false;
            raw_tag_depth = 0;
            continue;
        }

        if (input.compare(i, 4, "<!--") == 0) {
            const auto end = input.find("-->", i + 4);
            if (end == std::string::npos) {
                // HTML recovers an ordinary comment at EOF. Preserve special
                // conditional/SSI forms; an ordinary trailing comment is
                // removable just like a terminated ordinary comment.
                const bool preserve = starts_ci(input, i, "<!--[if") ||
                                      input.compare(i, 5, "<!--#") == 0 ||
                                      input.compare(i, 5, "<!--!") == 0;
                if (preserve) output.append(input, i, std::string::npos);
                i = input.size();
                break;
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
            // A second '<' cannot belong to the current tag opener. Browsers
            // emit the first one as text and reconsume the second, which may
            // begin a real raw-text element such as <<script>.
            if (i + 1 < input.size() && input[i + 1] == '<') {
                if (pending_space && !output.empty()) output.push_back(' ');
                pending_space = false;
                output.push_back('<');
                ++i;
                continue;
            }
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
                } else if ((c == '\'' || c == '"') && follows_attribute_equals(input, j, i)) {
                    // A quote only opens a quoted attribute value immediately
                    // after '='. Browsers keep a stray quote in an unquoted
                    // value as data, even though it is a parse error.
                    quoted = true; quote = c;
                } else if (c == '>') {
                    ++j;
                    break;
                }
            }
            if (quoted || j > input.size() || input[j - 1] != '>') {
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
                } else if ((c == '\'' || c == '"') && follows_attribute_equals(input, k, i)) {
                    if (tag_space && !output.empty() && output.back() != '<' && output.back() != ' ')
                        output.push_back(' ');
                    tag_space = false;
                    in_quote = true; tag_quote = c; output.push_back(c);
                } else if (ws(c)) {
                    tag_space = true;
                } else {
                    const bool after_tag_prefix = !output.empty() &&
                        (output.back() == '<' ||
                         (output.back() == '/' && output.size() >= 2 && output[output.size() - 2] == '<'));
                    if (tag_space && !output.empty() &&
                        (after_tag_prefix || c != '>'))
                        output.push_back(' ');
                    tag_space = false;
                    output.push_back(c);
                }
            }

            // Detect raw-text/preformatted elements from opening tags.
            std::size_t n = i + 1;
            while (n < j && ws(input[n])) ++n;
            if (n < j && input[n] != '/' && input[n] != '!' && input[n] != '?') {
                std::size_t e = n;
                while (e < j && (ascii_alnum(static_cast<unsigned char>(input[e])) ||
                                 input[e] == '-' || input[e] == ':')) ++e;
                const std::string name = lower(input.substr(n, e - n));
                if (name == "pre" || name == "textarea" || name == "script" ||
                    name == "style" || name == "xmp" || name == "listing" ||
                    name == "iframe") {
                    raw_tag = name;
                } else if (name == "plaintext") {
                    raw_tag = name; // no closing tag exists; preserve to EOF
                } else if (name == "svg" || name == "math") {
                    // Foreign-content parsing has different script and
                    // self-closing rules. Preserve the subtree verbatim.
                    raw_tag = name;
                    raw_tag_can_nest = true;
                    raw_tag_depth = 1;
                } else if (!name.empty() && tag_has_preserved_whitespace_style(input.substr(i, j - i))) {
                    raw_tag = name;
                    raw_tag_can_nest = true;
                    raw_tag_depth = 1;
                }
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

    if (!preserve_final_whitespace)
        while (!output.empty() && ws(output.back())) output.pop_back();
    while (!output.empty() && ws(output.front())) output.erase(output.begin());
    error.clear();
    return true;
}

// JavaScript minification is intentionally conservative. It removes comments,
// redundant horizontal whitespace, and line terminators at boundaries which
// already carry an unambiguous statement/block delimiter. Other significant
// line terminators remain available to automatic semicolon insertion.
struct JsDiagnostics {
    std::string* binding = nullptr;
    std::string* effect = nullptr;
    std::string* ir = nullptr;
    std::string* cfg = nullptr;
    std::string* mangle = nullptr;
};

#if defined(__GNUC__) || defined(__clang__)
__attribute__((cold, noinline))
#endif
void populate_js_diagnostics(const std::vector<JsToken>& tokens,
                             const JsConcreteSyntax& syntax,
                             const JsScopeGraph& scopes,
                             JsSemanticFacts& facts,
                             JsDiagnostics& diagnostics) {
    if (diagnostics.effect) extend_js_semantic_facts(tokens, facts);
    if (diagnostics.binding)
        *diagnostics.binding = build_js_binding_signature(tokens, syntax, scopes);
    if (diagnostics.effect)
        *diagnostics.effect = build_js_effect_signature(tokens, scopes, facts, syntax);
    if (diagnostics.ir)
        *diagnostics.ir = build_js_ir_signature(tokens, syntax, scopes);
    if (diagnostics.cfg)
        *diagnostics.cfg = build_js_cfg_signature(tokens, syntax);
    if (diagnostics.mangle)
        *diagnostics.mangle = build_js_mangle_report(tokens, scopes);
}

static bool minify_javascript(const std::string& input, std::string& output,
                              std::string& error, bool preserve_jsx_boundaries,
                              bool collect_tokens = false, bool structured_rewrite = false,
                              bool aggressive_rewrite = false,
                              bool mangle_top_level = false,
                              const std::vector<std::string>* property_allowlist = nullptr,
                              const std::vector<JavaScriptOptimizationPass>* disabled_passes = nullptr,
                              JsDiagnostics* diagnostics = nullptr,
                              unsigned aggressive_round = 0,
                              bool binding_rename_done = false) {
    output.clear();
    output.reserve(input.size());

    const bool profile_stages = std::getenv("MINIFY_PROFILE_STAGES") != nullptr;
    const auto scan_started = std::chrono::steady_clock::now();
    bool scan_profile_emitted = false;
    const auto emit_stage_profile = [&](const char* stage, auto started) {
        if (!profile_stages) return;
        const auto ended = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(ended - started).count();
        std::cerr << "minify_stage round=" << aggressive_round << " bytes=" << input.size()
                  << " stage=" << stage << " ms=" << ms << '\n';
    };
    const auto emit_scan_profile = [&]() {
        if (scan_profile_emitted) return;
        emit_stage_profile("scan_emit", scan_started);
        scan_profile_emitted = true;
    };

    bool pending_space = false;
    bool pending_newline = false;
    bool regex_boundary = false;
    bool can_start_regex = true;
    bool pending_control_paren = false;
    std::vector<bool> control_parens;
    std::vector<bool> block_braces;
    control_parens.reserve(64);
    block_braces.reserve(64);
    std::string last_token;
    [[maybe_unused]] std::vector<JsToken> tokens;
    // Structured and aggressive policy requests exercise the same non-mutating
    // inventory while those modes intentionally retain conservative output.
    // Positions refer to the source, never to a partially rewritten output.
    if (collect_tokens) tokens.reserve(input.size() / 4);
    JsTokenRecorder token_recorder{input, tokens};
    JsTokenRecorder* recorder = collect_tokens ? &token_recorder : nullptr;
    auto record_token = [&](JsTokenKind kind, std::size_t begin, std::size_t end,
                            JsBraceKind brace_kind = JsBraceKind::Unknown) {
        if (recorder) recorder->record(kind, begin, end, brace_kind);
    };
    std::string before_semicolon_token;
    bool pending_class_brace = false;
    bool pending_class_expression = false;
    bool pending_function_brace = false;
    bool pending_arrow_brace = false;
    bool pending_function_expression = false;
    bool pending_async_expression = false;

    auto emit_pending = [&](char next) {
        if (pending_newline) {
            JsLineTerminatorAction action = output.empty()
                ? JsLineTerminatorAction::PreserveAsiBoundary
                : classify_js_line_terminator(output.back(), last_token, next);
            // Once the structural scanner has proved that `}` closes a
            // statement block (rather than an object, class expression or
            // function expression), the following token cannot continue the
            // preceding expression. The line terminator carries no ASI value.
            if (structured_rewrite && last_token == "}block")
                action = JsLineTerminatorAction::DropAfterDelimiter;
            // A terminator after an explicit semicolon, an opening brace, or a
            // opening brace cannot supply ASI semantics. A closing brace is
            // not enough: it may close an async/function/class expression,
            // and the following line can depend on ASI. Preserve every other
            // newline, including all boundaries following `}`.
            if (action != JsLineTerminatorAction::DropAfterDelimiter &&
                !output.empty() && output.back() != '\n')
                output.push_back('\n');
        } else if (pending_space && !output.empty() &&
                   js_needs_separator(output.back(), next, regex_boundary,
                                      preserve_jsx_boundaries)) {
            output.push_back(' ');
        }
        pending_space = pending_newline = false;
        regex_boundary = false;
    };

    auto copy_quoted = [&](std::size_t& i, char quote) {
        emit_pending(quote);
        const std::size_t begin = i++;
        bool escaped = false;
        while (i < input.size()) {
            const char q = input[i++];
            if (escaped) escaped = false;
            else if (q == '\\') escaped = true;
            else if (q == quote) {
                const std::string token = input.substr(begin, i - begin);
                output += preserve_jsx_boundaries ? token : shorten_js_string(token);
                return true;
            }
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
    static const std::unordered_set<std::string> boolean_expression_prefixes = {
        "(", "[", "=", "!", "?", ":", "&", "|", "+", "-", "*", "%",
        "<", ">", "/", "return", "throw", "case", "delete", "void",
        "typeof", "in", "instanceof", "yield", "await"
    };

    auto next_nontrivia = [&](std::size_t position) {
        while (position < input.size()) {
            if (ws(input[position])) {
                ++position;
            } else if (position + 1 < input.size() && input[position] == '/' &&
                       input[position + 1] == '/') {
                position += 2;
                while (position < input.size() && input[position] != '\n' &&
                       input[position] != '\r') ++position;
            } else if (position + 1 < input.size() && input[position] == '/' &&
                       input[position + 1] == '*') {
                const auto close = input.find("*/", position + 2);
                if (close == std::string::npos) return input.size();
                position = close + 2;
            } else {
                break;
            }
        }
        return position;
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
        if (ascii_alpha(static_cast<unsigned char>(c)) || c == '_' || c == '$') {
            const std::size_t begin = i++;
            while (i < input.size()) {
                const unsigned char u = static_cast<unsigned char>(input[i]);
                if (!ascii_alnum(u) && input[i] != '_' && input[i] != '$') break;
                ++i;
            }
            const std::string word = input.substr(begin, i - begin);
            const bool boolean_literal = word == "true" || word == "false";
            const std::size_t following = boolean_literal ? next_nontrivia(i) : input.size();
            const char following_char = following < input.size() ? input[following] : '\0';
            const bool binds_more_tightly =
                following_char == '.' || following_char == '[' ||
                following_char == '(' || following_char == '`' ||
                (following_char == '?' && following + 1 < input.size() &&
                 input[following + 1] == '.');
            const bool boolean_expression =
                boolean_literal && !preserve_jsx_boundaries &&
                !binds_more_tightly && last_token != "new" &&
                (last_token.empty() || boolean_expression_prefixes.count(last_token) != 0);
            emit_pending(boolean_expression ? '!' : word.front());
            record_token(JsTokenKind::Identifier, begin, i);
            if (boolean_expression) output += shorten_js_boolean(word, true);
            else {
                output += word;
            }

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
            can_start_regex = boolean_expression
                ? false
                : pending_control_paren || expression_prefix_keywords.count(word) != 0;
            last_token = boolean_expression ? "value" : word;
            continue;
        }

        if (ascii_digit(static_cast<unsigned char>(c))) {
            const std::size_t begin = i++;
            bool exponent = false;
            while (i < input.size()) {
                const unsigned char u = static_cast<unsigned char>(input[i]);
                const char q = input[i];
                if (ascii_alnum(u) || q == '.' || q == '_') {
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
            const std::string raw_number = input.substr(begin, i - begin);
            const std::string number = preserve_jsx_boundaries
                ? raw_number : shorten_js_number(raw_number);
            emit_pending(number.front());
            output += number;
            record_token(JsTokenKind::Number, begin, i);
            pending_control_paren = false;
            can_start_regex = false;
            last_token = "value";
            continue;
        }

        if (c == '`') {
            emit_pending(c);
            if (!copy_template_literal(input, i, output, error, recorder)) return false;
            pending_control_paren = false;
            can_start_regex = false;
            last_token = "value";
            continue;
        }

        if (c == '\'' || c == '"') {
            const std::size_t begin = i;
            if (!copy_quoted(i, c)) return false;
            record_token(JsTokenKind::String, begin, i);
            pending_control_paren = false;
            can_start_regex = false;
            last_token = "value";
            continue;
        }

        if (c == '(') {
            emit_pending(c);
            output.push_back(c);
            record_token(JsTokenKind::Punctuator, i, i + 1);
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
            record_token(JsTokenKind::Punctuator, i, i + 1);
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
            const bool had_newline = js_line_terminator_in(input, i + 2, close);
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

        std::size_t jsx_close = i + 1;
        while (preserve_jsx_boundaries && jsx_close < input.size() && ws(input[jsx_close]))
            ++jsx_close;
        const bool jsx_self_close = preserve_jsx_boundaries &&
                                    jsx_close < input.size() && input[jsx_close] == '>';
        if (c == '/' && can_start_regex && i + 1 < input.size() &&
            (output.empty() || output.back() != '<') &&
            !jsx_self_close) {
            const std::size_t begin = i;
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
                   ascii_alpha(static_cast<unsigned char>(input[i])))
                output.push_back(input[i++]);
            record_token(JsTokenKind::Regex, begin, i);
            pending_control_paren = false;
            can_start_regex = false;
            last_token = "value";
            pending_space = true;
            regex_boundary = true;
            continue;
        }

        // Track whether braces belong to executable blocks or object
        // expressions. Slash after a block may begin a regex statement; slash
        // after an object literal is division.
        if (c == '{') {
            emit_pending(c);
            bool is_block = false;
            if (pending_class_brace || pending_function_brace || pending_arrow_brace ||
                last_token.empty() || last_token == ";" || last_token == "}block" ||
                last_token == ")" || last_token == ")control" ||
                last_token == "else" || last_token == "do" ||
                last_token == "try" || last_token == "catch" || last_token == "finally" ||
                last_token == "class" || last_token == "static") {
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
                    if (!(ascii_alnum(ch) || output[start - 1] == '_' || output[start - 1] == '$' || ch >= 0x80)) break;
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
                (pending_function_brace && pending_function_expression) ||
                pending_arrow_brace;
            block_braces.push_back(expression_body ? false : is_block);
            output.push_back(c);
            record_token(JsTokenKind::Punctuator, i, i + 1,
                         is_block ? JsBraceKind::Block : JsBraceKind::Object);
            pending_class_brace = false;
            pending_class_expression = false;
            pending_function_brace = false;
            pending_function_expression = false;
            pending_arrow_brace = false;
            pending_async_expression = false;
            pending_control_paren = false;
            can_start_regex = true;
            last_token = "{";
            ++i;
            continue;
        }

        if (c == '}') {
            emit_pending(c);
            // A statement terminator is implied before a closing brace. Keep
            // semicolons which form an empty control/labelled statement; all
            // other immediately preceding semicolons are redundant.
            if (!preserve_jsx_boundaries && !output.empty() && output.back() == ';' &&
                js_semicolon_may_be_elided(before_semicolon_token)) {
                output.pop_back();
            }
            output.push_back(c);
            record_token(JsTokenKind::Punctuator, i, i + 1);
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
        record_token(JsTokenKind::Punctuator, i, i + 1);
        if (preserve_jsx_boundaries && c == '/' && i + 1 < input.size() &&
            input[i + 1] == '>') output.push_back(' ');
        pending_control_paren = false;
        pending_arrow_brace = c == '>' && last_token == "=";

        if (c == ';') before_semicolon_token = last_token;

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
    if (!preserve_jsx_boundaries && !output.empty() && output.back() == ';' &&
        js_semicolon_may_be_elided(before_semicolon_token)) {
        output.pop_back();
    }
    if (recorder) {
        emit_scan_profile();
        auto stage_started = std::chrono::steady_clock::now();
        JsConcreteSyntax syntax = build_js_concrete_syntax(tokens);
        emit_stage_profile("concrete_syntax", stage_started);
        stage_started = std::chrono::steady_clock::now();
        JsScopeGraph scopes = build_js_scope_graph(tokens);
        emit_stage_profile("scope_graph", stage_started);
        stage_started = std::chrono::steady_clock::now();
        resolve_js_references(scopes, tokens, syntax);
        emit_stage_profile("reference_resolution", stage_started);
        JsSemanticFacts facts;
        if (aggressive_rewrite || diagnostics) {
            stage_started = std::chrono::steady_clock::now();
            facts = build_js_semantic_facts(tokens);
            emit_stage_profile("semantic_facts", stage_started);
        }
        if (diagnostics) populate_js_diagnostics(tokens, syntax, scopes, facts, *diagnostics);
        const bool ordered_tokens = js_tokens_are_ordered(input, tokens);
        if (structured_rewrite && syntax.balanced && ordered_tokens) {
            const auto pass_enabled = [&](JavaScriptOptimizationPass pass) {
                return !disabled_passes || std::find(disabled_passes->begin(),
                    disabled_passes->end(), pass) == disabled_passes->end();
            };
            const bool binding_rename_enabled =
                pass_enabled(JavaScriptOptimizationPass::BindingRename) && !binding_rename_done;
            auto replacements = binding_rename_enabled
                ? plan_safe_js_parameter_renaming(tokens, syntax, scopes, mangle_top_level)
                : std::vector<JsReplacement>{};
            const bool renamed_bindings_this_round = !replacements.empty();
            if (pass_enabled(JavaScriptOptimizationPass::BindingRename)) {
                auto arrow_parentheses = plan_js_single_arrow_parentheses(tokens);
                replacements.insert(replacements.end(), arrow_parentheses.begin(),
                                    arrow_parentheses.end());
                auto primary_parentheses = plan_js_redundant_primary_parentheses(tokens, syntax);
                replacements.insert(replacements.end(), primary_parentheses.begin(),
                                    primary_parentheses.end());
                // Shorthand restoration can target the value token of a binding
                // replacement. Apply it only after renaming has converged so the
                // rewrite batch remains non-overlapping and fail-closed.
                if (replacements.empty()) {
                    auto shorthand_properties = plan_js_redundant_shorthand_properties(tokens, syntax);
                    replacements.insert(replacements.end(), shorthand_properties.begin(),
                                        shorthand_properties.end());
                }
            }
            if (!replacements.empty()) {
                const JsRewriteResult rewritten = apply_js_replacements(input, std::move(replacements));
                if (!rewritten.valid || !rewritten.smaller) { error.clear(); return true; }
                return minify_javascript(rewritten.text, output, error, preserve_jsx_boundaries,
                                         aggressive_rewrite, aggressive_rewrite, aggressive_rewrite,
                                         mangle_top_level, property_allowlist, disabled_passes,
                                         nullptr, aggressive_round,
                                         binding_rename_done || renamed_bindings_this_round);
            }
            constexpr unsigned aggressive_round_budget = 7;
            if (aggressive_rewrite && aggressive_round < aggressive_round_budget) {
                auto append = [&](std::vector<JsReplacement> more) {
                    replacements.insert(replacements.end(), more.begin(), more.end());
                };
                if (pass_enabled(JavaScriptOptimizationPass::ConstantFold)) {
                    append(plan_js_constant_folding(tokens));
                    append(plan_js_undefined_literals(tokens, scopes, syntax));
                }
                if (pass_enabled(JavaScriptOptimizationPass::ConstantConditional)) {
                    append(plan_js_constant_conditionals(tokens, facts));
                    append(plan_js_constant_logical_expressions(tokens, facts));
                }
                if (pass_enabled(JavaScriptOptimizationPass::UnreachableCode)) {
                    append(plan_js_unreachable_debuggers(tokens, facts));
                    append(plan_js_unreachable_literal_statements(tokens, facts, syntax));
                    append(plan_js_return_conditionals(tokens));
                }
                if (pass_enabled(JavaScriptOptimizationPass::CompoundAssignment)) append(plan_js_compound_assignments(tokens, scopes));
                auto unused_vars = pass_enabled(JavaScriptOptimizationPass::UnusedBinding)
                    ? plan_js_unused_empty_vars(tokens, scopes, syntax)
                    : std::vector<JsReplacement>{};
                if (!unused_vars.empty()) append(std::move(unused_vars));
                else if (pass_enabled(JavaScriptOptimizationPass::DeclarationJoin))
                    append(plan_js_var_declaration_joins(tokens));
                if (pass_enabled(JavaScriptOptimizationPass::DeadStore))
                    append(plan_js_adjacent_dead_var_stores(tokens, scopes, facts));
                if (pass_enabled(JavaScriptOptimizationPass::LiteralIife)) append(plan_js_literal_iifes(tokens, facts));
                if (property_allowlist && pass_enabled(JavaScriptOptimizationPass::PropertyMangle))
                    append(plan_js_property_mangling(tokens, *property_allowlist));
                if (!replacements.empty()) {
                    const JsRewriteResult rewritten = apply_js_replacements(input, std::move(replacements));
                    if (!rewritten.valid || !rewritten.smaller) { error.clear(); return true; }
                    return minify_javascript(rewritten.text, output, error, preserve_jsx_boundaries,
                                             true, true, true, mangle_top_level, property_allowlist,
                                             disabled_passes, nullptr, aggressive_round + 1,
                                             binding_rename_done);
                }
            }
        }
    }
    emit_scan_profile();
    error.clear();
    return true;
}

bool javascript(const std::string& input, std::string& output, std::string& error) {
    return minify_javascript(input, output, error, false);
}

bool javascript(const std::string& input, std::string& output, std::string& error,
                const Options& options) {
    return minify_javascript(input, output, error, false,
                             options.optimization != OptimizationLevel::Conservative,
                             options.optimization != OptimizationLevel::Conservative,
                             options.optimization == OptimizationLevel::Aggressive,
                             options.mangle_top_level,
                             options.optimization == OptimizationLevel::Aggressive
                                 ? &options.property_mangle_allowlist : nullptr,
                             &options.disabled_javascript_passes);
}

bool javascript_binding_signature(const std::string& input, std::string& signature,
                                  std::string& error) {
    std::string ignored;
    signature.clear();
    JsDiagnostics diagnostics{&signature, nullptr, nullptr, nullptr};
    return minify_javascript(input, ignored, error, false, true, false, false, false,
                             nullptr, nullptr, &diagnostics);
}

bool javascript_mangle_report(const std::string& input, std::string& report,
                              std::string& error) {
    std::string ignored;
    report.clear();
    JsDiagnostics diagnostics{nullptr, nullptr, nullptr, nullptr, &report};
    return minify_javascript(input, ignored, error, false, true, false, false, false,
                             nullptr, nullptr, &diagnostics);
}

bool javascript_effect_signature(const std::string& input, std::string& signature,
                                 std::string& error) {
    std::string ignored;
    signature.clear();
    JsDiagnostics diagnostics{nullptr, &signature, nullptr, nullptr};
    return minify_javascript(input, ignored, error, false, true, false, false, false,
                             nullptr, nullptr, &diagnostics);
}

bool javascript_ir_signature(const std::string& input, std::string& signature,
                             std::string& error) {
    std::string ignored;
    signature.clear();
    JsDiagnostics diagnostics{nullptr, nullptr, &signature, nullptr};
    return minify_javascript(input, ignored, error, false, true, false, false, false,
                             nullptr, nullptr, &diagnostics);
}

bool javascript_cfg_signature(const std::string& input, std::string& signature,
                              std::string& error) {
    std::string ignored;
    signature.clear();
    JsDiagnostics diagnostics{nullptr, nullptr, nullptr, &signature};
    return minify_javascript(input, ignored, error, false, true, false, false, false,
                             nullptr, nullptr, &diagnostics);
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
            if (quoted || j > input.size() || j == 0 || input[j - 1] != '>') {
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
                    const bool after_tag_prefix = !output.empty() &&
                        (output.back() == '<' ||
                         (output.back() == '/' && output.size() >= 2 && output[output.size() - 2] == '<'));
                    if (ws_pending && !output.empty() &&
                        (after_tag_prefix || (output.back() != '/' && c != '>' && c != '/')))
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
    return ascii_alpha(n) || input[i + 1] == '>' || input[i + 1] == '/';
}

static bool looks_like_jsx_root_start(const std::string& input, std::size_t i) {
    if (!looks_like_jsx_start(input, i)) return false;
    // A closing tag can only belong to a root already being copied. Treating
    // it as a fresh root makes ordinary standalone JSX expressions appear
    // unterminated when their opening tag followed an ASI boundary.
    if (input[i + 1] == '/') return false;
    if (input[i + 1] == '>') return true;

    std::size_t p = i;
    bool crossed_line = false;
    while (p > 0 && ws(input[p - 1])) {
        crossed_line = crossed_line || input[p - 1] == '\n' ||
                       input[p - 1] == '\r';
        --p;
    }
    if (p == 0) return true;
    if (crossed_line && (input[p - 1] == '}' || input[p - 1] == ')' ||
                         input[p - 1] == '\'' || input[p - 1] == '"')) return true;
    if (crossed_line) {
        const auto line_start_pos = input.rfind('\n', p - 1);
        std::size_t line_start =
            line_start_pos == std::string::npos ? 0 : line_start_pos + 1;
        while (line_start < p && ws(input[line_start])) ++line_start;
        if (line_start + 1 < p && input[line_start] == '/' &&
            input[line_start + 1] == '/') return true;
    }

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
        if (!(ascii_alnum(c) || input[p - 1] == '_' || input[p - 1] == '$')) break;
        --p;
    }
    const std::string word = input.substr(p, end - p);
    return word == "return" || word == "yield" || word == "await" ||
           word == "default" ||
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
                if (c == '/' && j + 1 < limit && input[j + 1] == '/') {
                    j += 2;
                    while (j < limit && input[j] != '\n' && input[j] != '\r') ++j;
                    continue;
                }
                if (c == '/' && j + 1 < limit && input[j + 1] == '*') {
                    const auto close = input.find("*/", j + 2);
                    if (close == std::string::npos || close >= limit) {
                        error = "unterminated comment in nested JSX tag";
                        return false;
                    }
                    j = close + 2;
                    continue;
                }
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
            if (quoted || j > limit || j == 0 || input[j - 1] != '>') { error = "unterminated nested JSX tag"; return false; }
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
        if (ascii_alpha(static_cast<unsigned char>(c)) || c == '_' || c == '$') {
            const std::size_t begin = i++;
            while (i < limit) {
                const unsigned char u = static_cast<unsigned char>(input[i]);
                if (!ascii_alnum(u) && input[i] != '_' && input[i] != '$') break;
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

        if (c == '`') {
            // Consume the template literal as a unit so nested ${...}
            // expressions and regular-expression literals inside them (which
            // may contain backticks, braces or `//`-lookalikes) cannot corrupt
            // the enclosing brace scan.
            std::string sink;
            if (!copy_template_literal(input, i, sink, error)) return false;
            can_start_regex = false;
            continue;
        }
        if (c == '\'' || c == '"') {
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
            if (!closed) { error = "unterminated string in JSX expression"; return false; }
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
            while (i < limit && ascii_alpha(static_cast<unsigned char>(input[i]))) ++i;
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

static bool minify_jsx(const std::string& input, std::string& output,
                       std::string& error, bool collect_tokens,
                       bool structured_expressions = false,
                       bool inside_expression = false,
                       bool aggressive_expressions = false,
                       const std::vector<JavaScriptOptimizationPass>* disabled_passes = nullptr) {
    output.clear();
    output.reserve(input.size());

    std::size_t i = 0;
    std::size_t js_start = 0;
    auto flush_js = [&](std::size_t end) -> bool {
        if (end <= js_start) return true;
        std::string part, e;
        if (!minify_javascript(input.substr(js_start, end - js_start), part, e,
                               true, collect_tokens, structured_expressions && inside_expression,
                               aggressive_expressions && inside_expression, false, nullptr,
                               disabled_passes)) {
            error = e;
            return false;
        }
        output += part;
        return true;
    };

    while (i < input.size()) {
        // JSX-looking bytes inside JavaScript comments are not markup roots.
        // Leave the comment itself for the JavaScript minifier in flush_js(),
        // but advance this outer root finder past its contents.
        if (input[i] == '/' && i + 1 < input.size() && input[i + 1] == '/') {
            i += 2;
            while (i < input.size() && input[i] != '\n' && input[i] != '\r') ++i;
            continue;
        }
        if (input[i] == '/' && i + 1 < input.size() && input[i + 1] == '*') {
            const auto close = input.find("*/", i + 2);
            if (close == std::string::npos) {
                error = "unterminated JavaScript block comment";
                output.clear();
                return false;
            }
            i = close + 2;
            continue;
        }

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
                    (ascii_alpha(static_cast<unsigned char>(prev)) ||
                     prev == '_' || prev == '$')) {
                    std::size_t end = p;
                    while (p > js_start) {
                        const unsigned char c =
                            static_cast<unsigned char>(input[p - 1]);
                        if (!(ascii_alnum(c) || input[p - 1] == '_' ||
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
                       ascii_alpha(static_cast<unsigned char>(input[i]))) ++i;
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

        bool root_follows_line = false;
        for (std::size_t k = i; k > js_start && ws(input[k - 1]); --k)
            root_follows_line = root_follows_line ||
                                input[k - 1] == '\n' || input[k - 1] == '\r';
        if (!flush_js(i)) return false;
        if (root_follows_line) output.push_back('\n');

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
                    if (c == '/' && j + 1 < input.size() && input[j + 1] == '/') {
                        ++j;
                        while (j + 1 < input.size() &&
                               input[j + 1] != '\n' && input[j + 1] != '\r') ++j;
                        continue;
                    }
                    if (c == '/' && j + 1 < input.size() && input[j + 1] == '*') {
                        const auto close = input.find("*/", j + 2);
                        if (close == std::string::npos) {
                            error = "unterminated comment in JSX tag";
                            output.clear();
                            return false;
                        }
                        j = close + 1;
                        continue;
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
                if (quoted || j > input.size() || input[j - 1] != '>') {
                    error = "unterminated JSX tag"; output.clear(); return false;
                }
                const bool self_closing = j >= 2 && input[j - 2] == '/';
                // Preserve JSX tag spelling, but minify JavaScript expressions
                // inside attribute braces.
                {
                    bool attr_quote = false;
                    bool attr_escaped = false;
                    char attr_q = 0;
                    for (std::size_t k = p; k < j;) {
                        const char tc = input[k];
                        if (attr_quote) {
                            output.push_back(tc);
                            if (attr_escaped) attr_escaped = false;
                            else if (tc == '\\') attr_escaped = true;
                            else if (tc == attr_q) attr_quote = false;
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
                            if (!minify_jsx(input.substr(k + 1, qpos - k - 2), expr, e,
                                            collect_tokens, structured_expressions, true,
                                            aggressive_expressions, disabled_passes)) {
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
                if (!minify_jsx(input.substr(p + 1, j - p - 2), expr, e,
                                collect_tokens, structured_expressions, true,
                                aggressive_expressions, disabled_passes)) {
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
        // The JavaScript minifier receives the suffix after this JSX region as
        // a separate fragment. Preserve a line terminator from the boundary so
        // ASI still separates a JSX expression from a following declaration,
        // export, or expression statement.
        std::size_t boundary = p;
        bool had_line_terminator = false;
        while (boundary < input.size()) {
            if (ws(input[boundary])) {
                had_line_terminator = had_line_terminator ||
                                      input[boundary] == '\n' ||
                                      input[boundary] == '\r';
                ++boundary;
                continue;
            }
            if (boundary + 1 < input.size() && input[boundary] == '/' &&
                input[boundary + 1] == '/') {
                boundary += 2;
                while (boundary < input.size() &&
                       input[boundary] != '\n' && input[boundary] != '\r') ++boundary;
                continue;
            }
            if (boundary + 1 < input.size() && input[boundary] == '/' &&
                input[boundary + 1] == '*') {
                const auto close = input.find("*/", boundary + 2);
                if (close == std::string::npos) break;
                for (std::size_t k = boundary; k < close + 2; ++k)
                    had_line_terminator = had_line_terminator ||
                                          input[k] == '\n' || input[k] == '\r';
                boundary = close + 2;
                continue;
            }
            break;
        }
        if (had_line_terminator) output.push_back('\n');
    }

    if (!flush_js(input.size())) return false;
    error.clear();
    return true;
}

bool jsx(const std::string& input, std::string& output, std::string& error) {
    return minify_jsx(input, output, error, false, false, false, false, nullptr);
}

bool jsx(const std::string& input, std::string& output, std::string& error,
         const Options& options) {
    return minify_jsx(input, output, error,
                      options.optimization != OptimizationLevel::Conservative,
                      options.structured_jsx_expressions, false,
                      options.optimization == OptimizationLevel::Aggressive &&
                          options.structured_jsx_expressions,
                      &options.disabled_javascript_passes);
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
    return run(format, input, output, error, Options{});
}

bool run(Format format, const std::string& input, std::string& output,
         std::string& error, const Options& options) {
    switch (format) {
        case Format::Html: return html(input, output, error);
        case Format::Css: return css(input, output, error);
        case Format::JavaScript: return javascript(input, output, error, options);
        case Format::Jsx: return jsx(input, output, error, options);
        case Format::Json: return json(input, output, error);
        case Format::Xml: return xml(input, output, error);
        case Format::Svg: return svg(input, output, error);
    }
    error = "unknown minification format";
    output.clear();
    return false;
}

} // namespace minify
