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
#include "rt/expr_parser.h"

namespace nift::rt::detail {
namespace {

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

} // namespace

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
    const Node& target = ast_.at(left);
    const Node& rhs = ast_.at(value);
    const std::string_view src = ast_.source();
    if (op == Op::None && rhs.kind == NodeKind::Lambda && target.end + 1 < src.size() &&
        src[target.end] == '=' && src[target.end + 1] == '(')
        return fail(OpaqueReason::LegacyRuntimeError);
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
NodeId ExprParser::lambda_assignment(NodeId left) {
    if(left==kNoNode)return left;
    const Op op=compound_op(tok_.kind);
    if(!is(Tok::Assign)&&op==Op::None)return left;
    if(!assignable(ast_.at(left).kind))return fail(OpaqueReason::LegacyRuntimeError);
    const auto begin=ast_.at(left).begin;
    advance();
    const NodeId right=unit();
    if(right==kNoNode)return right;
    return node2(op==Op::None?NodeKind::Assign:NodeKind::CompoundAssign,op,begin,left,right);
}

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
    const std::uint32_t right_begin = tok_.begin;
    const NodeId right = parse(right_assoc ? prec : prec + 1);
    if (right == kNoNode) return kNoNode;
    // Conditions group an unparenthesised `a != b == c` differently.
    const Node& r = ast_.at(right);
    if (options_.condition && prec == kEquality && r.kind == NodeKind::Binary && r.begin == right_begin &&
        (r.op == Op::Eq || r.op == Op::Ne))
        return fail(OpaqueReason::IrregularLegacyParse);
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
    if (next != ' ') {
        if (next == '(') {  // `await(...)`: the removed call form
            advance();
            name_base_ = true;
            collection_base_ = false;
            return postfix(leaf(NodeKind::Ident, token), token.begin);
        }
        return fail(OpaqueReason::LegacyRuntimeError);
    }
    advance();
    const DepthGuard guard(expression_depth(), kMaxExpressionDepth);
    if (guard.exceeded) return fail(OpaqueReason::NestingLimit);
    const NodeId operand = prefix();
    if (operand == kNoNode) return kNoNode;
    if (infix_prec(tok_.kind) != kNone || is(Tok::Increment) || is(Tok::Decrement))
        return fail(OpaqueReason::IrregularLegacyParse);
    return node1(NodeKind::Await, token.begin, operand);
}

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
