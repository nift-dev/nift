// Primaries of the Runtime 2.0 expression parser (see expr_literals.h).
#include "rt/expr_literals.h"

namespace nift::rt::detail {

NodeKind keyword_kind(std::string_view word) {
    if (word.size() == 4 && word[0] == 't' && word[1] == 'r' && word[2] == 'u' && word[3] == 'e') return NodeKind::True;
    if (word.size() == 4 && word[0] == 'n' && word[1] == 'u' && word[2] == 'l' && word[3] == 'l') return NodeKind::Null;
    if (word.size() == 5 && word[0] == 'f' && word.substr(1) == std::string_view("alse")) return NodeKind::False;
    return NodeKind::Ident;
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

} // namespace nift::rt::detail
