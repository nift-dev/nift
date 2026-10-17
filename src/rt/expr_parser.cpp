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

namespace nift::rt::detail {
namespace {

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
    }
    NodeId whole_expression();
    NodeId whole_statement();
    OpaqueReason reason() const { return why_; }

private:
    void advance() { tok_ = lex_.next(); }
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
    bool unit_end() const {
        return is(Tok::End) || is(Tok::Comma) || is(Tok::RParen) || is(Tok::RBracket) || is(Tok::RBrace);
    }
    NodeId pattern_statement(std::uint32_t begin);
    NodeId infix(NodeId left, int prec, Tok kind, std::uint32_t begin);
    NodeId prefix();
    NodeId postfix(NodeId node);
    NodeId primary();
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
};

NodeId ExprParser::node1(NodeKind kind, std::uint32_t begin, NodeId a) {
    const NodeId id = ast_.add(kind, begin, ast_.at(a).end);
    ast_.at(id).a = a;
    return id;
}

NodeId ExprParser::node2(NodeKind kind, Op op, std::uint32_t begin, NodeId a, NodeId b) {
    const NodeId id = ast_.add(kind, begin, ast_.at(b).end);
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
    if (is(Tok::Ident)) {
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
        const NodeId target = postfix(primary());
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
    const Tok close = is(Tok::LBracket) ? Tok::RBracket : Tok::RBrace;
    std::vector<NodeId> names;
    advance();
    while (is(Tok::Ident)) {
        names.push_back(leaf(NodeKind::Ident, tok_));
        advance();
        if (!is(Tok::Comma)) break;
        advance();
    }
    const Token close_token = tok_;
    if (names.empty() || !is(close)) { lex_ = saved; tok_ = open; return kNoNode; }
    advance();
    if (!is(Tok::Declare) && !is(Tok::Assign)) { lex_ = saved; tok_ = open; return kNoNode; }
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
// increment is only regular when it is the entire unit.
NodeId ExprParser::unit() {
    const std::uint32_t begin = tok_.begin;
    if (is(Tok::Increment) || is(Tok::Decrement)) {
        const Op op = is(Tok::Increment) ? Op::PreInc : Op::PreDec;
        advance();
        const NodeId target = postfix(primary());
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
    const std::uint32_t stop = tok_.end;
    advance();
    if (!unit_end()) return fail(OpaqueReason::IrregularLegacyParse);
    const NodeId id = node1(NodeKind::IncDec, begin, value);
    ast_.at(id).op = op;
    ast_.at(id).end = stop;
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
        ast_.at(id).end = ast_.at(other).end;
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
    if (op != Op::None || is_word("await")) {
        Lexer probe = lex_;
        const Tok after = probe.peek().kind;
        if (op == Op::None && (after == Tok::End || infix_prec(after) != kNone || after == Tok::Assign))
            return postfix(primary());  // `await` used as a name
        advance();
        const DepthGuard guard(expression_depth(), kMaxExpressionDepth);
        if (guard.exceeded) return fail(OpaqueReason::NestingLimit);
        const NodeId operand = prefix();
        if (operand == kNoNode) return kNoNode;
        if (op == Op::None) return node1(NodeKind::Await, token.begin, operand);
        const NodeId id = node1(NodeKind::Unary, token.begin, operand);
        ast_.at(id).op = op;
        return id;
    }
    return postfix(primary());
}

NodeId ExprParser::postfix(NodeId node) {
    while (node != kNoNode) {
        const std::uint32_t begin = ast_.at(node).begin;
        if (is(Tok::LParen)) {
            const Token open = tok_;
            advance();
            const NodeId args = sequence(NodeKind::Call, Tok::RParen, open.begin, true);
            if (args == kNoNode) return kNoNode;
            ast_.at(args).a = node;
            ast_.at(args).begin = begin;
            node = args;
        } else if (is(Tok::Dot) || is(Tok::QuestionDot)) {
            const NodeKind kind = is(Tok::Dot) ? NodeKind::Member : NodeKind::SafeMember;
            advance();
            if (!is(Tok::Ident)) return fail(OpaqueReason::LegacyRuntimeError);
            node = node2(kind, Op::None, begin, node, leaf(NodeKind::Ident, tok_));
            advance();
        } else if (is(Tok::LBracket) || is(Tok::QuestionBracket)) {
            const NodeKind kind = is(Tok::LBracket) ? NodeKind::Index : NodeKind::SafeIndex;
            advance();
            ++nesting_;
            const NodeId index = unit();
            --nesting_;
            if (index == kNoNode) return kNoNode;
            if (!is(Tok::RBracket)) return fail(OpaqueReason::LegacyRuntimeError);
            node = node2(kind, Op::None, begin, node, index);
            ast_.at(node).end = tok_.end;
            advance();
        } else {
            break;
        }
    }
    return node;
}

NodeId ExprParser::primary() {
    const Token token = tok_;
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
        return inner;
    }
    case Tok::Ident: {
        const std::string_view word = src_.substr(token.begin, token.end - token.begin);
        if (word == "async") {
            Lexer probe = lex_;
            const Tok after = probe.peek().kind;
            if (after == Tok::LParen || after == Tok::Ident) {
                advance();
                const NodeId lambda = lambda_if_any(token.begin, true);
                if (lambda != kNoNode || why_ != OpaqueReason::None) return lambda;
                return fail(OpaqueReason::LegacyRuntimeError);
            }
        }
        if (word == "atomic") {
            // `atomic<int>(...)` / `atomic<bool>(...)`: the callee name includes the type.
            for (std::string_view type : {"<int>(", "<bool>("}) {
                if (src_.substr(token.end, type.size()) != type || token.end + type.size() > end_) continue;
                const std::uint32_t name_end = token.end + static_cast<std::uint32_t>(type.size()) - 1;
                lex_.reset(name_end);
                advance();
                return ast_.add(NodeKind::Ident, token.begin, name_end);
            }
        }
        const NodeId lambda = lambda_if_any(token.begin, false);
        if (lambda != kNoNode || why_ != OpaqueReason::None) return lambda;
        advance();
        if (word == "true") return leaf(NodeKind::True, token);
        if (word == "false") return leaf(NodeKind::False, token);
        if (word == "null") return leaf(NodeKind::Null, token);
        return leaf(NodeKind::Ident, token);
    }
    default:
        return fail(OpaqueReason::LegacyRuntimeError);
    }
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
// Returns kNoNode with no reason when the text is not a lambda.
NodeId ExprParser::lambda_if_any(std::uint32_t begin, bool async) {
    const Token start = tok_;
    const Lexer saved = lex_;
    std::vector<NodeId> params;
    if (is(Tok::Ident)) {
        advance();
        if (!is(Tok::Arrow)) { lex_ = saved; tok_ = start; return kNoNode; }
        params.push_back(leaf(NodeKind::Param, start));
    } else {
        if (!skip_group() || !is(Tok::Arrow)) { lex_ = saved; tok_ = start; return kNoNode; }
        const Token arrow = tok_;
        lex_ = saved;
        tok_ = start;
        advance();
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
            if (is(Tok::Comma) && !variadic) { advance(); continue; }
            if (!is(Tok::RParen)) return fail(OpaqueReason::LegacyRuntimeError);
        }
        advance();
        if (tok_.begin != arrow.begin) return fail(OpaqueReason::LegacyRuntimeError);
    }
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
        // A block body starting with '<' is rendered as template text when the
        // lambda is called (ParserExpression.cpp:3522), not run as statements.
        const std::string_view text = trim(src_.substr(tok_.begin + 1, close - tok_.begin - 1));
        const bool rendered = !text.empty() && text.front() == '<';
        if (rendered) body.push_back(make_opaque(ast_, tok_.begin + 1, close, OpaqueReason::TemplateText));
        const bool ok = rendered || parse_block(ast_, tok_.begin + 1, close, Mode::Deferred, body, ignored);
        const NodeId block = ast_.add(NodeKind::Block, tok_.begin, close + 1);
        ast_.set_list(block, body);
        lex_.reset(close + 1);
        advance();
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
    ast_.at(lambda).end = ast_.at(body).end;
    return lambda;
}

// Call arguments or array elements up to `close` (current token is the first
// one after the opening bracket).
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
        items.push_back(item);
        if (is(Tok::Comma)) { advance(); if (is(close)) break; continue; }
        if (!is(close)) { --nesting_; return fail(OpaqueReason::LegacyRuntimeError); }
    }
    --nesting_;
    const NodeId id = ast_.add(kind, begin, tok_.end);
    ast_.set_list(id, items);
    advance();
    return id;
}

// { "key": value, ... }: keys must be double-quoted (legacy run-time error
// otherwise).
NodeId ExprParser::object(std::uint32_t begin) {
    std::vector<NodeId> props;
    ++nesting_;
    while (!is(Tok::RBrace)) {
        if (!is(Tok::String) || src_[tok_.begin] != '"') { --nesting_; return fail(OpaqueReason::LegacyRuntimeError); }
        const NodeId key = leaf(NodeKind::String, tok_);
        advance();
        if (!is(Tok::Colon)) { --nesting_; return fail(OpaqueReason::LegacyRuntimeError); }
        advance();
        const NodeId value = unit();
        if (value == kNoNode) { --nesting_; return kNoNode; }
        const NodeId prop = node2(NodeKind::Property, Op::None, ast_.at(key).begin, key, value);
        props.push_back(prop);
        if (is(Tok::Comma)) { advance(); continue; }
        if (!is(Tok::RBrace)) { --nesting_; return fail(OpaqueReason::LegacyRuntimeError); }
    }
    --nesting_;
    const NodeId id = ast_.add(NodeKind::Object, begin, tok_.end);
    ast_.set_list(id, props);
    advance();
    return id;
}

// A string literal. `$[expr]` segments become child expressions between
// StringPart nodes; escapes are decoded later (semantics.md: every escape is
// accepted, an unknown one drops the backslash).
NodeId ExprParser::string_literal(Token token) {
    const NodeId id = leaf(NodeKind::String, token);
    if (src_[token.begin] == '\'') ast_.at(id).flags |= kFlagSingleQuote;
    const std::uint32_t body_end = token.end - 1;
    std::vector<NodeId> parts;
    std::uint32_t text_begin = token.begin + 1;
    for (std::uint32_t k = token.begin + 1; k < body_end; ++k) {
        if (src_[k] == '\\') { ++k; continue; }
        if (src_[k] != '$' || k + 1 >= body_end || src_[k + 1] != '[') continue;
        std::uint32_t close = 0;
        if (!find_balanced(src_, k + 1, body_end, '[', ']', close))
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
