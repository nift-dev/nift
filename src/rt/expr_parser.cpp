// Expression layer of the Runtime 2.0 script parser: a Pratt parser over the
// tokens of one statement or expression. Whatever it cannot place in the
// grammar below is returned as kNoNode with a reason, and the statement layer
// records it as an Opaque node (legacy evaluates it; D-001).
//
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

#include <algorithm>
#include <iterator>

namespace nift::rt::detail {
namespace {

// `true`, `false` and `null` are literals; every other word is a name.
NodeKind keyword_kind(std::string_view word) {
    if (word.size() == 4 && word[0] == 't' && word[1] == 'r' && word[2] == 'u' && word[3] == 'e') return NodeKind::True;
    if (word.size() == 4 && word[0] == 'n' && word[1] == 'u' && word[2] == 'l' && word[3] == 'l') return NodeKind::Null;
    if (word.size() == 5 && word[0] == 'f' && word.substr(1) == std::string_view("alse")) return NodeKind::False;
    return NodeKind::Ident;
}

enum Prec : int { kNone = 0, kTernary, kCoalesce, kOr, kAnd, kEquality, kRelational, kAdditive, kMultiplicative };

int infix_prec(Tok kind) {
    switch (kind) {
    case Tok::Question: return kTernary;
    case Tok::Coalesce: return kCoalesce;
    case Tok::OrOr: return kOr;
    case Tok::AndAnd: return kAnd;
    case Tok::Eq: case Tok::Ne: return kEquality;
    case Tok::Lt: case Tok::Le: case Tok::Gt: case Tok::Ge: return kRelational;
    case Tok::Plus: case Tok::Minus: return kAdditive;
    case Tok::Star: case Tok::Slash: case Tok::Percent: return kMultiplicative;
    default: return kNone;
    }
}

Op binary_op(Tok kind) {
    switch (kind) {
    case Tok::Plus: return Op::Add; case Tok::Minus: return Op::Sub; case Tok::Star: return Op::Mul;
    case Tok::Slash: return Op::Div; case Tok::Percent: return Op::Mod; case Tok::Eq: return Op::Eq;
    case Tok::Ne: return Op::Ne; case Tok::Lt: return Op::Lt; case Tok::Le: return Op::Le;
    case Tok::Gt: return Op::Gt; case Tok::Ge: return Op::Ge; case Tok::AndAnd: return Op::And;
    case Tok::OrOr: return Op::Or;
    default: return Op::None;
    }
}

Op compound_op(Tok kind) {
    switch (kind) {
    case Tok::AddAssign: return Op::AddAssign; case Tok::SubAssign: return Op::SubAssign;
    case Tok::MulAssign: return Op::MulAssign; case Tok::DivAssign: return Op::DivAssign;
    case Tok::ModAssign: return Op::ModAssign; case Tok::AndAssign: return Op::AndAssign;
    case Tok::OrAssign: return Op::OrAssign; case Tok::XorAssign: return Op::XorAssign;
    default: return Op::None;
    }
}

bool assignable(NodeKind kind) {
    return kind == NodeKind::Ident || kind == NodeKind::Member || kind == NodeKind::Index;
}

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
    bool callable_shape(NodeId callee, std::uint32_t last_dot) const;
    bool receiver_ok(std::uint32_t begin, std::uint32_t end) const;
    NodeId primary();
    NodeId primary_name(Token token);
    NodeId string_literal(Token token);
    NodeId sequence(NodeKind kind, Tok close, std::uint32_t begin, bool allow_spread);
    NodeId object(std::uint32_t begin);
    NodeId lambda_if_any(std::uint32_t begin, bool async);
    NodeId lambda_body(NodeId lambda);
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
};

NodeId ExprParser::node1(NodeKind kind, std::uint32_t begin, NodeId a) {
    const NodeId id = ast_.add(kind, begin, prev_end_);
    ast_.at(id).a = a;
    return id;
}

NodeId ExprParser::node2(NodeKind kind, Op op, std::uint32_t begin, NodeId a, NodeId b) {
    const NodeId id = ast_.add(kind, begin, prev_end_);
    Node& node = ast_.at(id);
    node.op = op;
    node.a = a;
    node.b = b;
    return id;
}

NodeId ExprParser::whole_expression() {
    const NodeId id = unit();
    if (id == kNoNode) return kNoNode;
    if (!is(Tok::End)) return fail(OpaqueReason::LegacyRuntimeError);
    return id;
}

// Statement content of the legacy `$[...]` wrapper: declarations,
// assignments, increments and expression statements.
NodeId ExprParser::whole_statement() {
    const std::uint32_t begin = tok_.begin;
    std::uint16_t decl_flags = 0;
    if (is_word("const") || is_word("immut")) {
        const Token word = tok_;
        Lexer probe = lex_;
        const Token name = probe.next();
        if (name.kind == Tok::Ident && probe.next().kind == Tok::Declare) {
            decl_flags = src_[word.begin] == 'c' ? kFlagConst : kFlagImmut;
            advance();
        }
    }
    // Legacy treats any unit text starting with "await " as an await of the
    // rest (ParserExpression.cpp:2363), before declarations are recognised.
    const bool await_text = is_word("await") && tok_.end < end_ && src_[tok_.end] == ' ';
    if (is(Tok::Ident) && !await_text) {
        Lexer probe = lex_;
        if (probe.next().kind == Tok::Declare) {
            const NodeId name = leaf(NodeKind::Ident, tok_);
            advance();
            advance();
            const NodeId value = unit();
            if (value == kNoNode) return kNoNode;
            if (!is(Tok::End)) return fail(OpaqueReason::LegacyRuntimeError);
            const NodeId id = node2(NodeKind::Declare, Op::None, begin, name, value);
            ast_.at(id).flags |= decl_flags;
            return id;
        }
    }
    if (decl_flags) return fail(OpaqueReason::LegacyRuntimeError);
    if (is(Tok::LBracket) || is(Tok::LBrace)) {
        const NodeId pattern = pattern_statement(begin);
        if (pattern != kNoNode || why_ != OpaqueReason::None) return pattern;
    }
    if (is(Tok::Increment) || is(Tok::Decrement)) {
        const Op op = is(Tok::Increment) ? Op::PreInc : Op::PreDec;
        advance();
        const std::uint32_t target_begin = tok_.begin;
        const NodeId target = postfix(primary(), target_begin);
        if (target == kNoNode || !assignable(ast_.at(target).kind) || !is(Tok::End))
            return fail(OpaqueReason::LegacyRuntimeError);
        const NodeId id = node1(NodeKind::IncDec, begin, target);
        ast_.at(id).op = op;
        return id;
    }
    const NodeId left = parse(kTernary);
    if (left == kNoNode) return kNoNode;
    if (is(Tok::End)) return node1(NodeKind::ExprStmt, begin, left);
    const bool target_ok = assignable(ast_.at(left).kind);
    if ((is(Tok::Increment) || is(Tok::Decrement)) && target_ok) {
        const Op op = is(Tok::Increment) ? Op::PostInc : Op::PostDec;
        const std::uint32_t stop = tok_.end;
        advance();
        if (!is(Tok::End)) return fail(OpaqueReason::LegacyRuntimeError);
        const NodeId id = node1(NodeKind::IncDec, begin, left);
        ast_.at(id).op = op;
        ast_.at(id).end = stop;
        return id;
    }
    const Op op = compound_op(tok_.kind);
    if (!target_ok || (!is(Tok::Assign) && op == Op::None)) return fail(OpaqueReason::LegacyRuntimeError);
    advance();
    const NodeId value = unit();
    if (value == kNoNode) return kNoNode;
    if (!is(Tok::End)) return fail(OpaqueReason::LegacyRuntimeError);
    return node2(op == Op::None ? NodeKind::Assign : NodeKind::CompoundAssign, op, begin, left, value);
}

// `[a, b] := value`, `{a, b} = value` (legacy CP208 flat destructuring).
// Returns kNoNode without a reason when the statement is not a pattern.
NodeId ExprParser::pattern_statement(std::uint32_t begin) {
    const Token open = tok_;
    const Lexer saved = lex_;
    const std::uint32_t saved_end = prev_end_;
    const Tok close = is(Tok::LBracket) ? Tok::RBracket : Tok::RBrace;
    std::vector<NodeId> names;
    advance();
    bool trailing_comma = false;
    while (is(Tok::Ident)) {
        names.push_back(leaf(NodeKind::Ident, tok_));
        advance();
        trailing_comma = false;
        if (!is(Tok::Comma)) break;
        advance();
        trailing_comma = true;
    }
    const Token close_token = tok_;
    if (names.empty() || !is(close)) { lex_ = saved; tok_ = open; prev_end_ = saved_end; return kNoNode; }
    // `[a,] := xs`: legacy "destructuring patterns support only flat identifiers".
    if (trailing_comma) return fail(OpaqueReason::LegacyRuntimeError);
    advance();
    if (!is(Tok::Declare) && !is(Tok::Assign)) { lex_ = saved; tok_ = open; prev_end_ = saved_end; return kNoNode; }
    const bool declaration = is(Tok::Declare);
    for (std::size_t i = 0; i < names.size() && declaration; ++i)
        for (std::size_t j = 0; j < i; ++j)
            if (ast_.text(ast_.at(names[i])) == ast_.text(ast_.at(names[j])))
                return fail(OpaqueReason::LegacyRuntimeError);
    advance();
    const NodeId pattern = ast_.add(NodeKind::Pattern, open.begin, close_token.end);
    if (close == Tok::RBrace) ast_.at(pattern).flags |= kFlagObjectPattern;
    ast_.set_list(pattern, names);
    const NodeId value = unit();
    if (value == kNoNode) return kNoNode;
    if (!is(Tok::End)) return fail(OpaqueReason::LegacyRuntimeError);
    return node2(declaration ? NodeKind::Declare : NodeKind::Assign, Op::None, begin, pattern, value);
}

// One evaluation unit of the legacy evaluator: an argument, element, index,
// parenthesised expression, right-hand side, condition or lambda body. Legacy
// applies a leading or trailing `++`/`--` to the whole unit text, so an
// increment is only regular when it is the entire unit; likewise a lambda
// (`=>` anywhere at depth 0 makes the whole unit a lambda) and `await`.
NodeId ExprParser::unit() {
    const std::uint32_t saved = unit_begin_;
    unit_begin_ = tok_.begin;
    const NodeId id = unit_body();
    unit_begin_ = saved;
    return id;
}

NodeId ExprParser::unit_body() {
    const std::uint32_t begin = tok_.begin;
    if (is(Tok::Increment) || is(Tok::Decrement)) {
        const Op op = is(Tok::Increment) ? Op::PreInc : Op::PreDec;
        advance();
        const std::uint32_t target_begin = tok_.begin;
        const NodeId target = postfix(primary(), target_begin);
        if (target == kNoNode) return kNoNode;
        if (!assignable(ast_.at(target).kind)) return fail(OpaqueReason::LegacyRuntimeError);
        if (!unit_end()) return fail(OpaqueReason::IrregularLegacyParse);
        const NodeId id = node1(NodeKind::IncDec, begin, target);
        ast_.at(id).op = op;
        return id;
    }
    const NodeId value = parse(kTernary);
    if (value == kNoNode || !(is(Tok::Increment) || is(Tok::Decrement))) return value;
    if (!assignable(ast_.at(value).kind)) return fail(OpaqueReason::LegacyRuntimeError);
    const Op op = is(Tok::Increment) ? Op::PostInc : Op::PostDec;
    advance();
    if (!unit_end()) return fail(OpaqueReason::IrregularLegacyParse);
    const NodeId id = node1(NodeKind::IncDec, begin, value);
    ast_.at(id).op = op;
    return id;
}

NodeId ExprParser::parse(int min_prec) {
    const DepthGuard guard(expression_depth(), kMaxExpressionDepth);
    if (guard.exceeded) return fail(OpaqueReason::NestingLimit);
    const std::uint32_t begin = tok_.begin;
    NodeId left = prefix();
    while (left != kNoNode) {
        const int prec = infix_prec(tok_.kind);
        if (prec == kNone || prec < min_prec) break;
        left = infix(left, prec, tok_.kind, begin);
    }
    return left;
}

NodeId ExprParser::infix(NodeId left, int prec, Tok kind, std::uint32_t begin) {
    advance();
    if (options_.condition && (kind == Tok::Question || kind == Tok::Coalesce))
        return fail(OpaqueReason::IrregularLegacyParse);
    if (kind == Tok::Question) {
        if (!ternary_allowed()) return fail(OpaqueReason::IrregularLegacyParse);
        const NodeId then = parse(kTernary);
        if (then == kNoNode) return kNoNode;
        if (!is(Tok::Colon)) return fail(OpaqueReason::LegacyRuntimeError);
        advance();
        const NodeId other = parse(kTernary);
        if (other == kNoNode) return kNoNode;
        const NodeId id = node2(NodeKind::Ternary, Op::None, begin, left, then);
        ast_.at(id).c = other;
        return id;
    }
    const bool right_assoc = prec == kCoalesce || prec == kEquality;
    const NodeId right = parse(right_assoc ? prec : prec + 1);
    if (right == kNoNode) return kNoNode;
    if (prec == kRelational && infix_prec(tok_.kind) == kRelational) return fail(OpaqueReason::LegacyRuntimeError);
    if (kind == Tok::Coalesce) return node2(NodeKind::Coalesce, Op::None, begin, left, right);
    const NodeKind node_kind = prec == kOr || prec == kAnd ? NodeKind::Logical : NodeKind::Binary;
    return node2(node_kind, binary_op(kind), begin, left, right);
}

NodeId ExprParser::prefix() {
    const Token token = tok_;
    Op op = Op::None;
    if (is(Tok::Bang)) op = Op::Not;
    else if (is(Tok::Minus)) op = Op::Neg;
    else if (is(Tok::Plus)) op = Op::Pos;
    if (op == Op::None && is_word("await")) return await_expression(token);
    if (op == Op::None) return postfix(primary(), token.begin);
    advance();
    const DepthGuard guard(expression_depth(), kMaxExpressionDepth);
    if (guard.exceeded) return fail(OpaqueReason::NestingLimit);
    const NodeId operand = prefix();
    if (operand == kNoNode) return kNoNode;
    // Legacy applies `!` after splitting at logical and equality operators but
    // before arithmetic (ParserExpression.cpp:4835), so `!1 + 1` is !(1 + 1).
    const int next = infix_prec(tok_.kind);
    if (op == Op::Not && (next >= kRelational || next == kCoalesce)) return fail(OpaqueReason::IrregularLegacyParse);
    const NodeId id = node1(NodeKind::Unary, token.begin, operand);
    ast_.at(id).op = op;
    return id;
}

// Legacy: a unit whose text starts with "await " (one space) awaits the whole
// rest of the unit (ParserExpression.cpp:2363), so `await f() + 1` is
// await(f() + 1); `await(x)` is the removed form (error at run time). Only
// `await <prefix operand>` ending the unit is regular; `await` alone is a name.
NodeId ExprParser::await_expression(Token token) {
    const char next = token.end < end_ ? src_[token.end] : '\0';
    Lexer probe = lex_;
    const Tok after = probe.peek().kind;
    const bool ends = after == Tok::End || after == Tok::Comma || after == Tok::RParen || after == Tok::RBracket ||
                      after == Tok::RBrace;
    if (ends) return postfix(primary(), token.begin);  // the name `await`
    if (next != ' ') return fail(OpaqueReason::LegacyRuntimeError);
    advance();
    const DepthGuard guard(expression_depth(), kMaxExpressionDepth);
    if (guard.exceeded) return fail(OpaqueReason::NestingLimit);
    const NodeId operand = prefix();
    if (operand == kNoNode) return kNoNode;
    if (infix_prec(tok_.kind) != kNone || is(Tok::Increment) || is(Tok::Decrement))
        return fail(OpaqueReason::IrregularLegacyParse);
    return node1(NodeKind::Await, token.begin, operand);
}

// Builtin methods the legacy evaluator applies to any receiver as a final
// `.method(...)` postfix (ParserExpression.cpp:3125, `known`).
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
//   receiver.known_method(args) for any receiver passing receiver_ok.
bool ExprParser::callable_shape(NodeId callee, std::uint32_t last_dot) const {
    const Node& node = ast_.at(callee);
    if (node.kind == NodeKind::Ident) return name_base_;
    if (node.kind == NodeKind::Member && known_method(ast_.text(ast_.at(node.b))) &&
        receiver_ok(node.begin, last_dot))
        return true;
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
            if (!callable_shape(node, last_dot)) return fail(OpaqueReason::LegacyRuntimeError);
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
            if (!name_base) {
                // Only `.known_method(` is regular on a non-name receiver.
                Lexer probe = lex_;
                const Token after = probe.peek();
                const bool called = after.kind == Tok::LParen && after.begin == tok_.end;
                if (kind != NodeKind::Member || !called || !known_method(src_.substr(tok_.begin, tok_.end - tok_.begin)))
                    return fail(OpaqueReason::LegacyRuntimeError);
            }
            const NodeId name = leaf(NodeKind::Ident, tok_);
            advance();
            node = node2(kind, Op::None, begin, node, name);
            continue;
        }
        if (!name_base) return fail(OpaqueReason::LegacyRuntimeError);
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

NodeId ExprParser::primary() {
    const Token token = tok_;
    name_base_ = false;
    switch (token.kind) {
    case Tok::Number: advance(); return leaf(NodeKind::Number, token);
    case Tok::String: advance(); return string_literal(token);
    case Tok::LBracket: advance(); return sequence(NodeKind::Array, Tok::RBracket, token.begin, false);
    case Tok::LBrace: advance(); return object(token.begin);
    case Tok::LParen: {
        const NodeId lambda = lambda_if_any(token.begin, false);
        if (lambda != kNoNode || why_ != OpaqueReason::None) return lambda;
        advance();
        ++nesting_;
        const NodeId inner = unit();
        --nesting_;
        if (inner == kNoNode) return kNoNode;
        if (!is(Tok::RParen)) return fail(OpaqueReason::LegacyRuntimeError);
        advance();
        name_base_ = false;
        return inner;
    }
    case Tok::Ident: return primary_name(token);
    default:
        return fail(OpaqueReason::LegacyRuntimeError);
    }
}

NodeId ExprParser::primary_name(Token token) {
    const std::string_view word = src_.substr(token.begin, token.end - token.begin);
    const char first = word[0];
    if (first == 'a' && word.size() == 5 && word.substr(1) == std::string_view("sync")) {
        Lexer probe = lex_;
        const Tok after = probe.peek().kind;
        if (after == Tok::LParen || after == Tok::Ident) {
            advance();
            const NodeId lambda = lambda_if_any(token.begin, true);
            if (lambda != kNoNode || why_ != OpaqueReason::None) return lambda;
            return fail(OpaqueReason::LegacyRuntimeError);
        }
    }
    if (first == 'a' && word.size() == 6 && word.substr(1) == std::string_view("tomic")) {
        // `atomic<int>(...)` / `atomic<bool>(...)`: the callee name includes the type.
        for (std::string_view type : {std::string_view("<int>("), std::string_view("<bool>(")}) {
            if (token.end + type.size() > end_ || src_.substr(token.end, type.size()) != type) continue;
            const std::uint32_t name_end = token.end + static_cast<std::uint32_t>(type.size()) - 1;
            resume_at(name_end);
            name_base_ = true;
            return ast_.add(NodeKind::Ident, token.begin, name_end);
        }
    }
    const NodeId lambda = lambda_if_any(token.begin, false);
    if (lambda != kNoNode || why_ != OpaqueReason::None) return lambda;
    advance();
    const NodeKind kind = keyword_kind(word);
    name_base_ = kind == NodeKind::Ident;
    return leaf(kind, token);
}

// Skips a balanced (...) group starting at the current '(' token. Leaves the
// current token on the token after ')'. Returns false if unbalanced.
bool ExprParser::skip_group() {
    int depth = 0;
    do {
        if (is(Tok::End) || is(Tok::Bad)) return false;
        if (is(Tok::LParen)) ++depth;
        else if (is(Tok::RParen)) --depth;
        advance();
    } while (depth > 0);
    return true;
}

// `(params) => body`, `name => body` (current token is '(' or the name).
// Returns kNoNode with no reason when the text is not a lambda. Legacy makes
// the whole unit a lambda when it contains `=>` at depth 0, so a lambda is
// regular only at the start of its unit.
NodeId ExprParser::lambda_if_any(std::uint32_t begin, bool async) {
    const Token start = tok_;
    const Lexer saved = lex_;
    const std::uint32_t saved_end = prev_end_;
    std::vector<NodeId> params;
    if (is(Tok::Ident)) {
        advance();
        if (!is(Tok::Arrow)) { lex_ = saved; tok_ = start; prev_end_ = saved_end; return kNoNode; }
        if (begin != unit_begin_) return fail(OpaqueReason::IrregularLegacyParse);
        params.push_back(leaf(NodeKind::Param, start));
    } else {
        if (!skip_group() || !is(Tok::Arrow)) { lex_ = saved; tok_ = start; prev_end_ = saved_end; return kNoNode; }
        if (begin != unit_begin_) return fail(OpaqueReason::IrregularLegacyParse);
        const Token arrow = tok_;
        lex_ = saved;
        tok_ = start;
        advance();
        // `( ) => 1`: legacy "lambda: malformed parameter list".
        if (is(Tok::RParen) && tok_.begin != start.end) return fail(OpaqueReason::LegacyRuntimeError);
        while (!is(Tok::RParen)) {
            const bool variadic = is(Tok::Ellipsis);
            if (variadic) advance();
            if (!is(Tok::Ident)) return fail(OpaqueReason::LegacyRuntimeError);
            const NodeId param = leaf(NodeKind::Param, tok_);
            if (variadic) ast_.at(param).flags |= kFlagVariadic;
            for (NodeId seen : params)
                if (ast_.text(ast_.at(seen)) == ast_.text(ast_.at(param)))
                    return fail(OpaqueReason::LegacyRuntimeError);
            params.push_back(param);
            advance();
            if (is(Tok::Comma) && !variadic) {
                advance();
                if (is(Tok::RParen)) return fail(OpaqueReason::LegacyRuntimeError);  // trailing comma
                continue;
            }
            if (!is(Tok::RParen)) return fail(OpaqueReason::LegacyRuntimeError);
        }
        advance();
        if (tok_.begin != arrow.begin) return fail(OpaqueReason::LegacyRuntimeError);
    }
    if (options_.condition) return fail(OpaqueReason::IrregularLegacyParse);
    advance();  // '=>'
    const NodeId lambda = ast_.add(NodeKind::Lambda, begin, begin);
    if (async) ast_.at(lambda).flags |= kFlagAsync;
    ast_.set_list(lambda, params);
    return lambda_body(lambda);
}

NodeId ExprParser::lambda_body(NodeId lambda) {
    if (is(Tok::LBrace)) {
        std::uint32_t close = 0;
        if (!find_balanced(src_, tok_.begin, end_, '{', '}', close)) return fail(OpaqueReason::LegacyRuntimeError);
        std::vector<NodeId> body;
        ParseError ignored;
        const std::uint32_t open = tok_.begin;
        // A block body starting with '<' is rendered as template text when the
        // lambda is called (ParserExpression.cpp:3522), not run as statements.
        const std::string_view text = trim(src_.substr(open + 1, close - open - 1));
        const bool rendered = !text.empty() && text.front() == '<';
        if (rendered) body.push_back(make_opaque(ast_, open + 1, close, OpaqueReason::TemplateText));
        const bool ok = rendered || parse_block(ast_, open + 1, close, Mode::Deferred, body, ignored);
        const NodeId block = ast_.add(NodeKind::Block, open, close + 1);
        ast_.set_list(block, body);
        resume_at(close + 1);
        // Legacy takes the body as a block only when the rest of the unit is
        // exactly `{...}`; `(x) => {a} + 1` is an expression body there.
        if (!unit_end()) return fail(OpaqueReason::IrregularLegacyParse);
        Node& node = ast_.at(lambda);
        node.flags |= kFlagBlockBody;
        node.b = block;
        node.end = close + 1;
        if (!ok) return make_opaque(ast_, node.begin, node.end, OpaqueReason::DeferredTranslation);
        return lambda;
    }
    const NodeId body = unit();
    if (body == kNoNode) return kNoNode;
    ast_.at(lambda).b = body;
    ast_.at(lambda).end = prev_end_;
    return lambda;
}

// A string literal that is a whole array element or object value is stored
// verbatim by legacy (no interpolation; JSON or parameter unescaping), unlike
// the same literal elsewhere: regular only without `$[` and backslashes.
bool verbatim_literal_ok(const Ast& ast, NodeId item) {
    const Node& node = ast.at(item);
    if (node.kind != NodeKind::String) return true;
    const std::string_view text = ast.text(node);
    return text.find('\\') == std::string_view::npos && text.find("$[") == std::string_view::npos;
}

// Call arguments or array elements up to `close` (current token is the first
// one after the opening bracket). A trailing comma is a legacy run-time error
// ("expression cannot be empty").
NodeId ExprParser::sequence(NodeKind kind, Tok close, std::uint32_t begin, bool allow_spread) {
    std::vector<NodeId> items;
    ++nesting_;
    while (!is(close)) {
        NodeId item = kNoNode;
        if (is(Tok::Ellipsis)) {
            if (!allow_spread) { --nesting_; return fail(OpaqueReason::LegacyRuntimeError); }
            const std::uint32_t at = tok_.begin;
            advance();
            const NodeId inner = parse(kTernary);
            if (inner != kNoNode) item = node1(NodeKind::Spread, at, inner);
        } else {
            item = unit();
        }
        if (item == kNoNode) { --nesting_; return kNoNode; }
        if (kind == NodeKind::Array && !verbatim_literal_ok(ast_, item)) {
            --nesting_;
            return fail(OpaqueReason::LegacyRuntimeError);
        }
        items.push_back(item);
        if (is(Tok::Comma)) {
            advance();
            if (is(close)) { --nesting_; return fail(OpaqueReason::LegacyRuntimeError); }
            continue;
        }
        if (!is(close)) { --nesting_; return fail(OpaqueReason::LegacyRuntimeError); }
    }
    --nesting_;
    const NodeId id = ast_.add(kind, begin, tok_.end);
    ast_.set_list(id, items);
    advance();
    return id;
}

// { "key": value, ... }: keys must be double-quoted (legacy run-time error
// otherwise). Escapes in keys, duplicate keys and a trailing comma are
// run-time errors or decoded differently by legacy's JSON fast path: Opaque.
NodeId ExprParser::object(std::uint32_t begin) {
    // A condition is split at comparison operators without regard to braces
    // (`if ({"k": a == b})` is malformed in legacy).
    if (options_.condition) return fail(OpaqueReason::IrregularLegacyParse);
    std::vector<NodeId> props;
    ++nesting_;
    auto bad = [&]() { --nesting_; return fail(OpaqueReason::LegacyRuntimeError); };
    while (!is(Tok::RBrace)) {
        if (!is(Tok::String) || src_[tok_.begin] != '"') return bad();
        const NodeId key = leaf(NodeKind::String, tok_);
        const std::string_view key_text = ast_.text(ast_.at(key));
        if (key_text.find('\\') != std::string_view::npos) return bad();
        for (NodeId seen : props)
            if (ast_.text(ast_.at(ast_.at(seen).a)) == key_text) return bad();
        advance();
        if (!is(Tok::Colon)) return bad();
        advance();
        const NodeId value = unit();
        if (value == kNoNode) { --nesting_; return kNoNode; }
        if (!verbatim_literal_ok(ast_, value)) return bad();
        const NodeId prop = node2(NodeKind::Property, Op::None, ast_.at(key).begin, key, value);
        props.push_back(prop);
        if (is(Tok::Comma)) {
            advance();
            if (is(Tok::RBrace)) return bad();
            continue;
        }
        if (!is(Tok::RBrace)) return bad();
    }
    --nesting_;
    const NodeId id = ast_.add(NodeKind::Object, begin, tok_.end);
    ast_.set_list(id, props);
    advance();
    return id;
}

// A string literal. `$[expr]` segments become child expressions between
// StringPart nodes; escapes are decoded later (semantics.md: every escape is
// accepted, an unknown one drops the backslash). Legacy decodes escapes first
// and then interpolates the decoded text, where `\$` is an escaped `$` and a
// `$[` segment ends at the first `]` that balances `[`s, quotes ignored
// (Parser::interpolate_parameter). A literal with both a backslash and a `$`,
// or whose segment end differs from the quote-aware one, is Opaque.
NodeId ExprParser::string_literal(Token token) {
    const NodeId id = leaf(NodeKind::String, token);
    if (src_[token.begin] == '\'') ast_.at(id).flags |= kFlagSingleQuote;
    const std::uint32_t body_end = token.end - 1;
    const std::string_view body = src_.substr(token.begin + 1, body_end - token.begin - 1);
    if (body.find('$') == std::string_view::npos) return id;
    if (body.find('\\') != std::string_view::npos)
        return make_opaque(ast_, token.begin, token.end, OpaqueReason::IrregularLegacyParse);
    std::vector<NodeId> parts;
    std::uint32_t text_begin = token.begin + 1;
    for (std::uint32_t k = token.begin + 1; k < body_end; ++k) {
        if (src_[k] != '$' || k + 1 >= body_end || src_[k + 1] != '[') continue;
        std::uint32_t close = 0, naive = k + 2;
        for (int depth = 0; naive < body_end; ++naive) {
            if (src_[naive] == '[') ++depth;
            else if (src_[naive] == ']' && depth-- == 0) break;
        }
        if (naive >= body_end || !find_balanced(src_, k + 1, body_end, '[', ']', close) || close != naive)
            return make_opaque(ast_, token.begin, token.end, OpaqueReason::LegacyRuntimeError);
        if (k > text_begin) parts.push_back(ast_.add(NodeKind::StringPart, text_begin, k));
        ExprOptions inner_options;
        inner_options.ternary_at_top = true;
        OpaqueReason why = OpaqueReason::None;
        const NodeId inner = parse_expression_text(ast_, k + 2, close, inner_options, why);
        if (inner == kNoNode) return make_opaque(ast_, token.begin, token.end, why);
        parts.push_back(inner);
        text_begin = close + 1;
        k = close;
    }
    if (parts.empty()) return id;
    if (text_begin < body_end) parts.push_back(ast_.add(NodeKind::StringPart, text_begin, body_end));
    ast_.set_list(id, parts);
    return id;
}

} // namespace

NodeId parse_expression_text(Ast& ast, std::uint32_t begin, std::uint32_t end, ExprOptions options,
                             OpaqueReason& why) {
    ExprParser parser(ast, begin, end, options);
    const NodeId id = parser.whole_expression();
    why = parser.reason();
    return id;
}

NodeId parse_statement_text(Ast& ast, std::uint32_t begin, std::uint32_t end, OpaqueReason& why) {
    ExprParser parser(ast, begin, end, ExprOptions{});
    const NodeId id = parser.whole_statement();
    why = parser.reason();
    return id;
}

} // namespace nift::rt::detail

namespace nift::rt {

NodeId parse_expression(Ast& ast, std::uint32_t begin, std::uint32_t end) {
    OpaqueReason why = OpaqueReason::None;
    detail::ExprOptions options;
    options.ternary_at_top = true;
    const NodeId id = detail::parse_expression_text(ast, begin, end, options, why);
    return id != kNoNode ? id : detail::make_opaque(ast, begin, end, why);
}

} // namespace nift::rt
