// Control-flow statements of the Runtime 2.0 script parser (see
// control_flow.h).
#include "rt/control_flow.h"

#include <algorithm>

namespace nift::rt::detail {

bool may_start_keyword(char c) {
    switch (c) {
    case 'f': case 'e': case 's': case 'i': case '@': case '/': case 'w': case 't': case 'r': case 'b': case 'c':
        return true;
    default:
        return false;
    }
}

// @// and // to end of line; @/* */ and /* */ (an unterminated one is a
// parse-time error at its start).
Step StatementParser::comment(std::uint32_t& i, std::uint32_t end) {
    if (starts(src_, i, end, "@//") || starts(src_, i, end, "//")) {
        i = find_in(src_, "\n", i, end);
        return Step::Done;
    }
    if (starts(src_, i, end, "@/*")) {
        const std::uint32_t close = find_in(src_, "*/", i + 3, end);
        if (close == end) return fail("open comment '@/*' has no close '*/'", i);
        i = close + 2;
        return Step::Done;
    }
    if (starts(src_, i, end, "/*")) {
        const std::uint32_t close = find_in(src_, "*/", i + 2, end);
        if (close == end) return fail("open comment '/*' has no close '*/'", i);
        i = close + 2;
        return Step::Done;
    }
    return Step::NoMatch;
}

// while (cond) { body }
Step StatementParser::while_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out) {
    if (!boundary(i, end, "while")) return Step::NoMatch;
    const std::uint32_t p = skip_space(src_, i + 5, end);
    std::uint32_t pc = 0;
    if (p >= end || src_[p] != '(' || !balanced(p, end, '(', ')', pc))
        return fail("function while requires '(...)'", i);
    const std::uint32_t bo = skip_space(src_, pc + 1, end);
    std::uint32_t bc = 0;
    if (bo >= end || src_[bo] != '{' || !balanced(bo, end, '{', '}', bc))
        return fail("function while requires a block", i);
    bool ok = true;
    const NodeId body = body_block(bo, bc, ok);
    if (!ok) return Step::Error;
    const NodeId node = ast_.add(NodeKind::While, i, bc + 1);
    ast_.at(node).a = expression(p + 1, pc, false, true);
    ast_.at(node).b = body;
    out.push_back(node);
    i = bc + 1;
    return Step::Done;
}

// try { } catch (name) { }. `try` not followed by '{' is an ordinary statement.
Step StatementParser::try_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out) {
    if (!boundary(i, end, "try")) return Step::NoMatch;
    const std::uint32_t bo = skip_space(src_, i + 3, end);
    if (bo >= end || src_[bo] != '{') return Step::NoMatch;
    std::uint32_t bc = 0;
    if (!balanced(bo, end, '{', '}', bc)) return fail("try requires a balanced block", i);
    std::uint32_t cp = skip_space(src_, bc + 1, end);
    if (!boundary(cp, end, "catch")) return fail("try requires catch(identifier)", i);
    cp = skip_space(src_, cp + 5, end);
    std::uint32_t cc = 0;
    if (cp >= end || src_[cp] != '(' || !balanced(cp, end, '(', ')', cc))
        return fail("catch requires '(identifier)'", i);
    const std::string_view binding = trim(src_.substr(cp + 1, cc - cp - 1));
    if (!valid_identifier(binding)) return fail("catch requires one valid identifier", i);
    const std::uint32_t cbo = skip_space(src_, cc + 1, end);
    std::uint32_t cbc = 0;
    if (cbo >= end || src_[cbo] != '{' || !balanced(cbo, end, '{', '}', cbc))
        return fail("catch(identifier) requires a block", i);
    bool ok = true;
    const NodeId try_body = body_block(bo, bc, ok);
    if (!ok) return Step::Error;
    const NodeId catch_body = body_block(cbo, cbc, ok);
    if (!ok) return Step::Error;
    const NodeId node = ast_.add(NodeKind::Try, i, cbc + 1);
    const std::uint32_t nb = static_cast<std::uint32_t>(binding.data() - src_.data());
    ast_.at(node).a = try_body;
    ast_.at(node).b = ast_.add(NodeKind::Ident, nb, nb + static_cast<std::uint32_t>(binding.size()));
    ast_.at(node).c = catch_body;
    out.push_back(node);
    i = cbc + 1;
    return Step::Done;
}

// for (binding : iterable) { body }
Step StatementParser::for_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out) {
    if (!boundary(i, end, "for")) return Step::NoMatch;
    const std::uint32_t p = skip_space(src_, i + 3, end);
    std::uint32_t pc = 0;
    if (p >= end || src_[p] != '(' || !balanced(p, end, '(', ')', pc))
        return fail("function for requires '(...)'", i);
    const std::uint32_t bo = skip_space(src_, pc + 1, end);
    std::uint32_t bc = 0;
    if (bo >= end || src_[bo] != '{' || !balanced(bo, end, '{', '}', bc))
        return fail("function for requires a block", i);
    bool ok = true;
    const NodeId body = body_block(bo, bc, ok);
    if (!ok) return Step::Error;
    const NodeId header = for_header(p + 1, pc);
    if (header == kNoNode) {
        out.push_back(make_opaque(ast_, i, bc + 1, OpaqueReason::LegacyRuntimeError));
    } else {
        const NodeId node = ast_.add(NodeKind::For, i, bc + 1);
        ast_.at(node).a = header;
        ast_.at(node).b = body;
        out.push_back(node);
    }
    i = bc + 1;
    return Step::Done;
}

// `name : iterable` or `(key, value) : iterable`, split at the first ':' at
// bracket depth 0 outside quotes. Other headers (C-style loops, sort clauses)
// are evaluated by legacy at run time: kNoNode makes the loop Opaque.
NodeId StatementParser::for_header(std::uint32_t begin, std::uint32_t end) {
    int depth = 0;
    bool q = false;
    char qc = 0;
    std::uint32_t sep = end;
    for (std::uint32_t k = begin; k < end; ++k) {
        const char c = src_[k];
        if (q) { if (c == '\\') ++k; else if (c == qc) q = false; continue; }
        if (c == '\'' || c == '"') { q = true; qc = c; }
        else if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') --depth;
        else if (c == ':' && depth == 0) {
            if (k + 1 < end && src_[k + 1] == '=') return kNoNode;
            sep = k;
            break;
        }
    }
    if (sep == end) return kNoNode;
    const std::string_view lhs = trim(src_.substr(begin, sep - begin));
    const NodeId binding = ast_.add(NodeKind::ForBinding, begin, sep);
    std::vector<NodeId> names;
    auto add_name = [&](std::string_view name) {
        if (!valid_identifier(name)) return false;
        const std::uint32_t nb = static_cast<std::uint32_t>(name.data() - src_.data());
        names.push_back(ast_.add(NodeKind::Ident, nb, nb + static_cast<std::uint32_t>(name.size())));
        return true;
    };
    if (!lhs.empty() && lhs.front() == '(' && lhs.back() == ')') {
        const std::string_view inner = lhs.substr(1, lhs.size() - 2);
        const auto comma = inner.find(',');
        if (comma == std::string_view::npos || inner.find(',', comma + 1) != std::string_view::npos) return kNoNode;
        if (!add_name(trim(inner.substr(0, comma))) || !add_name(trim(inner.substr(comma + 1)))) return kNoNode;
        ast_.at(binding).flags |= kFlagPairBinding;
    } else if (!add_name(lhs)) {
        return kNoNode;
    }
    ast_.set_list(binding, names);
    const std::string_view rhs = trim(src_.substr(sep + 1, end - sep - 1));
    if (rhs.empty()) return kNoNode;
    const std::uint32_t rb = static_cast<std::uint32_t>(rhs.data() - src_.data());
    ast_.at(binding).a = expression(rb, rb + static_cast<std::uint32_t>(rhs.size()), false);
    return binding;
}

// if (cond) { } [else if (cond) { }]* [else { }]
Step StatementParser::if_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out) {
    if (!boundary(i, end, "if")) return Step::NoMatch;
    const std::uint32_t p = skip_space(src_, i + 2, end);
    if (p >= end || src_[p] != '(') return fail("function if requires '(...)'", i);
    std::uint32_t pc = 0;
    if (!balanced(p, end, '(', ')', pc)) return fail("function if has no matching ')'", i);
    const std::uint32_t bo = skip_space(src_, pc + 1, end);
    std::uint32_t bc = 0;
    if (bo >= end || src_[bo] != '{' || !balanced(bo, end, '{', '}', bc))
        return fail("function if requires a block", i);
    bool ok = true;
    const NodeId body = body_block(bo, bc, ok);
    if (!ok) return Step::Error;
    const NodeId node = ast_.add(NodeKind::If, i, bc + 1);
    ast_.at(node).a = expression(p + 1, pc, false, true);
    ast_.at(node).b = body;
    out.push_back(node);
    i = bc + 1;
    return else_chain(i, end, node);
}

// The translator reports else-chain errors at its current position, which is
// the byte after the previous block's '}'.
Step StatementParser::else_chain(std::uint32_t& i, std::uint32_t end, NodeId head) {
    NodeId tail = head;
    while (true) {
        std::uint32_t e = skip_space(src_, i, end);
        if (!boundary(e, end, "else")) break;
        e = skip_space(src_, e + 4, end);
        if (boundary(e, end, "if")) {
            const std::uint32_t q = skip_space(src_, e + 2, end);
            std::uint32_t qc = 0;
            if (q >= end || src_[q] != '(' || !balanced(q, end, '(', ')', qc))
                return fail("function else if malformed", i);
            const std::uint32_t eb = skip_space(src_, qc + 1, end);
            std::uint32_t ec = 0;
            if (eb >= end || src_[eb] != '{' || !balanced(eb, end, '{', '}', ec))
                return fail("function else if requires block", i);
            bool ok = true;
            const NodeId body = body_block(eb, ec, ok);
            if (!ok) return Step::Error;
            const NodeId elif = ast_.add(NodeKind::If, e, ec + 1);
            ast_.at(elif).flags |= kFlagElseIf;
            ast_.at(elif).a = expression(q + 1, qc, false, true);
            ast_.at(elif).b = body;
            ast_.at(tail).c = elif;
            ast_.at(head).end = ec + 1;
            tail = elif;
            i = ec + 1;
            continue;
        }
        std::uint32_t ec = 0;
        if (e >= end || src_[e] != '{' || !balanced(e, end, '{', '}', ec))
            return fail("function else requires block", i);
        bool ok = true;
        const NodeId body = body_block(e, ec, ok);
        if (!ok) return Step::Error;
        ast_.at(tail).c = body;
        ast_.at(head).end = ec + 1;
        i = ec + 1;
        break;
    }
    return Step::Done;
}

// return [expr] up to the first newline or ';' outside quotes. Brackets do
// not extend it (a multi-line return expression is a run-time error).
Step StatementParser::return_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out) {
    if (!boundary(i, end, "return")) return Step::NoMatch;
    std::uint32_t e = i + 6;
    bool quoted = false;
    char quote = 0;
    while (e < end && src_[e] != '\n' && src_[e] != ';') {
        const char c = src_[e];
        if (quoted) { if (c == '\\' && e + 1 < end) ++e; else if (c == quote) quoted = false; }
        else if (c == '\'' || c == '"') { quoted = true; quote = c; }
        ++e;
    }
    const std::string_view inner = src_.substr(i + 6, e - (i + 6));
    const std::string_view expr = trim(inner);
    const NodeId node = ast_.add(NodeKind::Return, i, e);
    if (expr.empty()) {
        ast_.at(node).flags |= kFlagBare;
    } else {
        const std::uint32_t xb = static_cast<std::uint32_t>(expr.data() - src_.data());
        ast_.at(node).a = expression(xb, xb + static_cast<std::uint32_t>(expr.size()), true);
    }
    out.push_back(node);
    i = e < end ? e + 1 : e;
    return Step::Done;
}

// throw <expr>: only when `throw` is followed by whitespace and the rest of
// the region does not start with an assignment operator (then `throw` is a
// binding name). The expression extends to ';' or newline at depth 0.
Step StatementParser::throw_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out) {
    if (!boundary(i, end, "throw") || !(i + 5 < end && is_space(src_[i + 5]))) return Step::NoMatch;
    const std::string_view rest = trim(src_.substr(i + 5, end - (i + 5)));
    for (std::string_view op : {":=", "=", "+=", "-=", "*=", "/=", "%="})
        if (rest.substr(0, op.size()) == op) return Step::NoMatch;
    const std::uint32_t e = statement_end(src_, i + 6, end);
    const std::uint32_t stop = std::min(e, end);
    const std::string_view inner = src_.substr(i + 5, stop - (i + 5));
    const std::string_view expr = trim(inner);
    if (expr.empty()) return fail("throw requires an Error expression", i);
    const NodeId node = ast_.add(NodeKind::Throw, i, stop);
    const std::uint32_t xb = static_cast<std::uint32_t>(expr.data() - src_.data());
    ast_.at(node).a = expression(xb, xb + static_cast<std::uint32_t>(expr.size()), true);
    out.push_back(node);
    i = e < end ? e + 1 : e;
    return Step::Done;
}

// break / continue followed only by spaces and then end, ';' or newline.
Step StatementParser::jump_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out,
                                std::string_view kw, NodeKind kind) {
    if (!boundary(i, end, kw)) return Step::NoMatch;
    std::uint32_t e = i + static_cast<std::uint32_t>(kw.size());
    while (e < end && is_space(src_[e]) && src_[e] != '\n') ++e;
    if (!(e == end || src_[e] == ';' || src_[e] == '\n')) return Step::NoMatch;
    out.push_back(ast_.add(kind, i, i + static_cast<std::uint32_t>(kw.size())));
    i = e < end ? e + 1 : e;
    return Step::Done;
}

} // namespace nift::rt::detail
