// Postfix links of the Runtime 2.0 expression parser (see expr_postfix.h).
#include "rt/expr_postfix.h"

#include <algorithm>
#include <iterator>

namespace nift::rt::detail {

bool known_method(std::string_view name) {
    static constexpr std::string_view kKnown[] = {
        "abs", "all", "any", "append", "cat", "ceil", "chunk", "close", "compare_exchange", "contains", "copy",
        "count", "count_by", "cwd", "decode", "done", "drop", "elapsed", "empty", "encode", "ends_with", "entries",
        "env", "eof", "exchange", "exists", "fetch_add", "fetch_sub", "filter", "find", "find_index", "first",
        "flatten", "floor", "flush", "from_entries", "get", "group_by", "group_by_each", "has", "indexOf",
        "index_by", "index_of", "insert", "insert_after", "insert_before", "join", "keys", "last", "last_index_of",
        "length", "load", "lock", "locked", "map", "max", "max_by", "merge", "merge_deep", "min", "min_by",
        "modified", "move", "omit", "open", "partition", "path", "pause", "paused", "pick", "pipe", "prepend",
        "read", "read_all", "read_all_bytes", "read_bytes", "read_line", "read_val", "reduce", "remove", "replace",
        "replace_once", "reset", "resume", "revert", "round", "run", "running", "save", "seek", "set", "size",
        "slice", "sort_by", "split", "start", "starts_with", "status", "stderr", "stdin", "stdout", "stop", "store",
        "substr", "sum", "take", "tell", "to_double", "to_int", "to_lower", "to_string", "to_upper", "trim",
        "trim_end", "trim_start", "try_lock", "unique", "unique_by", "unlock", "values", "write", "write_bytes",
        "write_line", "write_val"};
    return std::binary_search(std::begin(kKnown), std::end(kKnown), name);
}

bool presentation_method(std::string_view name) {
    static constexpr std::string_view kPresentation[] = {"highlight", "prettify", "stringify"};
    return std::binary_search(std::begin(kPresentation), std::end(kPresentation), name);
}

// The receiver test of legacy final_postfix: no ":=" and no operator
// character at depth 0 outside quotes.
bool ExprParser::receiver_ok(std::uint32_t begin, std::uint32_t end) const {
    const std::string_view text = src_.substr(begin, end - begin);
    if (text.find(":=") != std::string_view::npos) return false;
    bool quoted = false;
    char quote = 0;
    int depth = 0;
    for (std::size_t k = 0; k < text.size(); ++k) {
        const char c = text[k];
        if (quoted) {
            if (c == '\\') ++k;
            else if (c == quote) quoted = false;
            continue;
        }
        if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') --depth;
        else if (depth == 0 && std::string_view("+-*/%<>=!&|?:,").find(c) != std::string_view::npos) return false;
    }
    return true;
}

// Call shapes the legacy evaluator dispatches (everything else is "unknown
// value or malformed expression" or depends on the receiver's run-time type):
//   name(args), name.member...member(args) (also with ?.), and
//   receiver.known_method(args) and receiver.presentation_method() for any
//   receiver passing receiver_ok.
bool ExprParser::callable_shape(NodeId callee, std::uint32_t last_dot, bool empty_call) const {
    const Node& node = ast_.at(callee);
    if (node.kind == NodeKind::Ident) return name_base_;
    if (node.kind == NodeKind::Member && receiver_ok(node.begin, last_dot)) {
        const std::string_view method = ast_.text(ast_.at(node.b));
        if (known_method(method) || (empty_call && presentation_method(method))) return true;
    }
    if (!name_base_) return false;
    NodeId link = callee;
    while (ast_.at(link).kind == NodeKind::Member || ast_.at(link).kind == NodeKind::SafeMember) link = ast_.at(link).a;
    return ast_.at(link).kind == NodeKind::Ident;
}

// Postfix links: call, .name, [index], ?.name, ?[index]. Regular only without
// whitespace before the link or after the dot (legacy rejects `x [0]`,
// `o . a`, `o?. a`), on a plain name (literals, collections and parenthesised
// expressions take only a known method call), with an index that contains no
// brackets (legacy matches them naively), and with a callable_shape callee.
// Each link counts toward the nesting bound (legacy evaluates the receiver
// one level deeper).
NodeId ExprParser::postfix(NodeId node, std::uint32_t begin) {
    if (node == kNoNode) return kNoNode;
    const bool name_base = name_base_;
    const bool member_base = name_base || collection_base_;
    // `true.length()`, `1e2[0]`: legacy has no postfix on these literals.
    const NodeKind head = ast_.at(node).kind;
    const bool scalar_head = head == NodeKind::Number || head == NodeKind::True || head == NodeKind::False ||
                             head == NodeKind::Null;
    int links = 0;
    struct Release {
        int& count;
        ~Release() { expression_depth() -= count; }
    } release{links};
    std::uint32_t last_dot = 0;
    bool safe = false;
    while (is(Tok::LParen) || is(Tok::Dot) || is(Tok::QuestionDot) || is(Tok::LBracket) || is(Tok::QuestionBracket)) {
        if (tok_.begin != prev_end_ || scalar_head) return fail(OpaqueReason::LegacyRuntimeError);
        ++links;
        if (++expression_depth() > kMaxExpressionDepth) return fail(OpaqueReason::NestingLimit);
        if (is(Tok::LParen)) {
            name_base_ = name_base;
            Lexer probe = lex_;
            const Token after = probe.peek();
            const bool empty_call = after.kind == Tok::RParen && after.begin == tok_.end;
            if (!callable_shape(node, last_dot, empty_call)) return fail(OpaqueReason::LegacyRuntimeError);
            node = call(node, begin);
            if (node == kNoNode) return kNoNode;
            continue;
        }
        if (is(Tok::Dot) || is(Tok::QuestionDot)) {
            const NodeKind kind = is(Tok::Dot) ? NodeKind::Member : NodeKind::SafeMember;
            safe = safe || kind == NodeKind::SafeMember;
            last_dot = tok_.begin;
            advance();
            if (!is(Tok::Ident) || tok_.begin != prev_end_) return fail(OpaqueReason::LegacyRuntimeError);
            if (!member_base) {
                // Only a known or presentation method call is regular on a
                // literal or parenthesised receiver.
                Lexer probe = lex_;
                const Token after = probe.next();
                const Token close = probe.next();
                const bool called = after.kind == Tok::LParen && after.begin == tok_.end;
                const bool empty = called && close.kind == Tok::RParen && close.begin == after.end;
                const std::string_view method = src_.substr(tok_.begin, tok_.end - tok_.begin);
                const bool regular = known_method(method) || (empty && presentation_method(method));
                if (kind != NodeKind::Member || !called || !regular) return fail(OpaqueReason::LegacyRuntimeError);
            }
            const NodeId name = leaf(NodeKind::Ident, tok_);
            advance();
            node = node2(kind, Op::None, begin, node, name);
            continue;
        }
        if (!member_base) return fail(OpaqueReason::LegacyRuntimeError);
        const NodeKind kind = is(Tok::LBracket) ? NodeKind::Index : NodeKind::SafeIndex;
        safe = safe || kind == NodeKind::SafeIndex;
        const std::uint32_t open_end = tok_.end;
        advance();
        ++nesting_;
        const NodeId index = unit();
        --nesting_;
        if (index == kNoNode) return kNoNode;
        if (!is(Tok::RBracket)) return fail(OpaqueReason::LegacyRuntimeError);
        const std::string_view inner = src_.substr(open_end, tok_.begin - open_end);
        if (inner.find_first_of("[]") != std::string_view::npos) return fail(OpaqueReason::LegacyRuntimeError);
        advance();
        node = node2(kind, Op::None, begin, node, index);
    }
    // With `?.`/`?[` in the chain legacy's operator splitting misreads the
    // unit (`!n?[0]`, `1 <= o?.k` differ from their parenthesised forms):
    // regular only as a whole unit.
    if (safe && (begin != unit_begin_ || !unit_end())) return fail(OpaqueReason::IrregularLegacyParse);
    return node;
}

NodeId ExprParser::call(NodeId callee, std::uint32_t begin) {
    const Token open = tok_;
    advance();
    // `h( )`: legacy "expression cannot be empty" (a whitespace-only list).
    if (is(Tok::RParen) && tok_.begin != open.end) return fail(OpaqueReason::LegacyRuntimeError);
    const NodeId args = sequence(NodeKind::Call, Tok::RParen, open.begin, true);
    if (args == kNoNode) return kNoNode;
    ast_.at(args).a = callee;
    ast_.at(args).begin = begin;
    return args;
}

} // namespace nift::rt::detail
