#include "rt/compiler_prepared_rules.h"

#include "rt/prepared_parse.h"

#include <vector>

namespace nift::rt::detail {

namespace {

bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool word(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// [open, close] enclose a balanced text (quotes respected).
bool balanced_between(std::string_view src, std::uint32_t open, std::uint32_t close) {
    int depth = 0;
    char quote = 0;
    for (std::uint32_t i = open + 1; i < close; ++i) {
        const char c = src[i];
        if (quote) {
            if (c == '\\') ++i;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') quote = c;
        else if (c == '(') ++depth;
        else if (c == ')' && --depth < 0) return false;
    }
    return depth == 0 && !quote;
}

struct Span {
    std::uint32_t begin, end;
};

// Legacy's prepared parser gives a parenthesised expression the span of its
// parentheses, and an operator node the span of its operands.
Span legacy_span(const Ast& ast, const Node& n) {
    Span s{n.begin, n.end};
    switch (n.kind) {
        case NodeKind::Binary: case NodeKind::Logical: case NodeKind::Coalesce:
            s.begin = legacy_span(ast, ast.at(n.a)).begin;
            s.end = legacy_span(ast, ast.at(n.b)).end;
            break;
        case NodeKind::Ternary:
            s.begin = legacy_span(ast, ast.at(n.a)).begin;
            s.end = legacy_span(ast, ast.at(n.c)).end;
            break;
        case NodeKind::Index: case NodeKind::Member: s.begin = legacy_span(ast, ast.at(n.a)).begin; break;
        case NodeKind::Call:
            if (ast.at(n.a).kind == NodeKind::Member) s.begin = legacy_span(ast, ast.at(ast.at(n.a).a)).begin;
            break;
        case NodeKind::Unary: s.end = legacy_span(ast, ast.at(n.a)).end; break;
        default: break;
    }
    const std::string_view src = ast.source();
    while (true) {
        std::uint32_t p = s.begin;
        while (p > 0 && space(src[p - 1])) --p;
        if (p == 0 || src[p - 1] != '(') break;
        const std::uint32_t open = p - 1;
        std::uint32_t q = open;
        while (q > 0 && space(src[q - 1])) --q;
        if (q > 0 && (word(src[q - 1]) || src[q - 1] == ')' || src[q - 1] == ']')) {
            // A call, `if (`/`while (`: not a group; `return (` is one.
            const std::string_view before = q >= 6 ? src.substr(q - 6, 6) : std::string_view();
            const bool keyword = before == "return";  // rt-lint: allow-string-compare compile time
            const bool after_return = keyword && (q == 6 || !word(src[q - 7]));
            if (!after_return) break;
        }
        std::uint32_t r = s.end;
        while (r < src.size() && space(src[r])) ++r;
        if (r >= src.size() || src[r] != ')' || !balanced_between(src, open, r)) break;
        s.begin = open;
        s.end = r + 1;
    }
    return s;
}

} // namespace

std::uint32_t UnitCompiler::prepared_begin(const Node& node) const { return legacy_span(ast_, node).begin; }
std::uint32_t UnitCompiler::prepared_end(const Node& node) const { return legacy_span(ast_, node).end; }

// Does legacy's prepared evaluator run this statement (parse_statement
// accepts it), or its textual one (StmtKind::Legacy)?
bool UnitCompiler::statement_preparable(const Node& node) const {
    const auto accepts = [this](NodeId id) {
        if (id == kNoNode) return true;
        const Node& n = ast_.at(id);
        const std::uint32_t b = prepared_begin(n);
        return legacy_prepared_parse_accepts(ast_.source().substr(b, prepared_end(n) - b));
    };
    switch (node.kind) {
        case NodeKind::Declare: case NodeKind::Assign: case NodeKind::CompoundAssign: return accepts(node.b);
        case NodeKind::If: case NodeKind::While: return accepts(node.a);
        case NodeKind::Return: return (node.flags & kFlagBare) || accepts(node.a);
        case NodeKind::ExprStmt: {
            const Node& e = ast_.at(node.a);
            if (!accepts(node.a)) return false;
            if (e.kind != NodeKind::Call) return true;
            const NodeId* args = ast_.list(e);
            // A rejected argument sends the call to the textual path.
            for (std::uint32_t k = 0; k < e.count; ++k)
                if (!accepts(args[k])) return false;
            return true;
        }
        default: return true;
    }
}

// Legacy prepares a loop body when the loop starts. An `if`/`while`
// condition or a `return` expression its prepared parser rejects makes it
// abandon preparing that whole body (the statements then run textually).
// Nested loops prepare themselves when reached, so their bodies are not
// part of this decision.
bool UnitCompiler::loop_body_abandons_preparation(NodeId body) const {
    if (body == kNoNode) return false;
    std::vector<NodeId> stack{body};
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        if (id == kNoNode) continue;
        const Node& n = ast_.at(id);
        if (n.kind == NodeKind::Lambda || n.kind == NodeKind::FnDecl) continue;
        if (n.kind == NodeKind::While || n.kind == NodeKind::For) continue;
        if ((n.kind == NodeKind::If || n.kind == NodeKind::Return) && !statement_preparable(n)) return true;
        for (NodeId child : {n.a, n.b, n.c}) stack.push_back(child);
        const NodeId* list = ast_.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back(list[k]);
    }
    return false;
}

} // namespace nift::rt::detail
