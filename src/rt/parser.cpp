// Statement layer of the Runtime 2.0 script parser. Each branch below mirrors
// one branch of Parser::translate_function_program (src/ParserScript.cpp) so
// that statement boundaries, accepted forms and parse-time diagnostics are
// identical to the legacy engine's. Comments name the legacy behaviour each
// rule reproduces.
#include "rt/parser.h"
#include "rt/parser_internal.h"

#include <algorithm>
#include <cctype>

namespace nift::rt {
namespace detail {

bool is_space(char c) { return is_ascii_space(c); }

std::uint32_t skip_space(std::string_view src, std::uint32_t p, std::uint32_t end) {
    while (p < end && is_space(src[p])) ++p;
    return p;
}

// Parser::trim_copy trims exactly " \t\r\n".
std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// include/nift/context.h valid_binding_identifier.
bool valid_identifier(std::string_view text) {
    if (text.empty() || !is_ident_start(text[0])) return false;
    return std::all_of(text.begin() + 1, text.end(), is_ident_char);
}

int& block_depth() {
    thread_local int depth = 0;
    return depth;
}

int& expression_depth() {
    thread_local int depth = 0;
    return depth;
}

NodeId make_opaque(Ast& ast, std::uint32_t begin, std::uint32_t end, OpaqueReason reason) {
    const NodeId id = ast.add(NodeKind::Opaque, begin, end);
    ast.at(id).reason = reason;
    return id;
}

namespace {

std::uint32_t find_in(std::string_view src, std::string_view what, std::uint32_t from, std::uint32_t end) {
    if (from > end) return end;
    const auto at = src.substr(0, end).find(what, from);
    return at == std::string_view::npos ? end : static_cast<std::uint32_t>(at);
}

bool starts(std::string_view src, std::uint32_t p, std::uint32_t end, std::string_view what) {
    return p + what.size() <= end && src.compare(p, what.size(), what) == 0;
}

} // namespace

// Parser::find_balanced (src/Parser.cpp) restricted to [open, end).
bool find_balanced(std::string_view src, std::uint32_t open, std::uint32_t end,
                   char open_char, char close_char, std::uint32_t& close) {
    if (open >= end || src[open] != open_char) return false;
    std::size_t depth = 0;
    bool quoted = false;
    char quote = 0;
    for (std::uint32_t i = open; i < end; ++i) {
        const char c = src[i];
        if (quoted) {
            if (c == '\\' && i + 1 < end) ++i;
            else if (c == quote) quoted = false;
            continue;
        }
        if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
        if (c == '`') {
            std::uint32_t run = 1;
            while (i + run < end && src[i + run] == '`') ++run;
            const std::uint32_t span_end = find_in(src, src.substr(i, run), i + run, end);
            if (span_end != end) i = span_end + run - 1;
            continue;
        }
        if (c == '@' && starts(src, i, end, "@/*")) {
            const std::uint32_t comment_end = find_in(src, "*/", i + 3, end);
            if (comment_end == end) return false;
            i = comment_end + 1;
            continue;
        }
        if (c == '<' && starts(src, i, end, "<!--")) {
            const std::uint32_t comment_end = find_in(src, "-->", i + 4, end);
            if (comment_end == end) return false;
            i = comment_end + 2;
            continue;
        }
        if (c == '@' && starts(src, i, end, "@//")) {
            const std::uint32_t line_end = find_in(src, "\n", i, end);
            if (line_end == end) return false;
            i = line_end;
            continue;
        }
        if (c == open_char) ++depth;
        else if (c == close_char && --depth == 0) { close = i; return true; }
    }
    return false;
}

namespace {

enum class Step { NoMatch, Done, Error };

// First bytes of every keyword/comment branch below; any other statement goes
// straight to the general statement rule.
bool may_start_keyword(char c) {
    switch (c) {
    case 'f': case 'e': case 's': case 'i': case '@': case '/': case 'w': case 't': case 'r': case 'b': case 'c':
        return true;
    default:
        return false;
    }
}

class StatementParser {
public:
    StatementParser(Ast& ast, Mode mode, ParseError& error)
        : ast_(ast), src_(ast.source()), mode_(mode), error_(error) {}
    bool block(std::uint32_t begin, std::uint32_t end, std::vector<NodeId>& out);

private:
    bool boundary(std::uint32_t p, std::uint32_t end, std::string_view kw) const;
    Step fail(const char* message, std::uint32_t offset);
    bool balanced(std::uint32_t open, std::uint32_t end, char a, char b, std::uint32_t& close) const {
        return find_balanced(src_, open, end, a, b, close);
    }
    NodeId body_block(std::uint32_t open, std::uint32_t close, bool& ok);
    NodeId expression(std::uint32_t begin, std::uint32_t end, bool ternary_ok, bool condition = false);

    Step fn_decl(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out);
    Step enum_decl(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out);
    Step struct_decl(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out);
    Step export_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out);
    Step import_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out, bool& stop);
    Step comment(std::uint32_t& i, std::uint32_t end);
    Step while_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out);
    Step try_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out);
    Step for_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out);
    Step if_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out);
    Step else_chain(std::uint32_t& i, std::uint32_t end, NodeId head);
    Step return_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out);
    Step throw_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out);
    Step jump_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out, std::string_view kw, NodeKind kind);
    void general_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out);
    NodeId classify_statement(std::uint32_t start, std::uint32_t stmt_end);
    NodeId template_declaration(std::uint32_t start, std::uint32_t stmt_end);
    NodeId fn_signature(NodeId fn, std::uint32_t open, std::uint32_t close, bool method);
    NodeId for_header(std::uint32_t begin, std::uint32_t end);
    NodeId struct_body(NodeId decl, std::uint32_t begin, std::uint32_t end);

    Ast& ast_;
    std::string_view src_;
    Mode mode_;
    ParseError& error_;
};

// The translator's boundary(): keyword followed by end of region or a
// character that is neither alphanumeric nor '_'.
bool StatementParser::boundary(std::uint32_t p, std::uint32_t end, std::string_view kw) const {
    if (p >= end || src_[p] != kw[0] || !starts(src_, p, end, kw)) return false;
    const std::uint32_t after = p + static_cast<std::uint32_t>(kw.size());
    return after == end || !is_ident_char(src_[after]);
}

Step StatementParser::fail(const char* message, std::uint32_t offset) {
    error_.message = message;
    error_.offset = offset;
    return Step::Error;
}

// A nested block is translated as part of the enclosing one, so errors in it
// keep the enclosing mode.
NodeId StatementParser::body_block(std::uint32_t open, std::uint32_t close, bool& ok) {
    std::vector<NodeId> children;
    ok = block(open + 1, close, children);
    const NodeId id = ast_.add(NodeKind::Block, open, close + 1);
    ast_.set_list(id, children);
    return id;
}

NodeId StatementParser::expression(std::uint32_t begin, std::uint32_t end, bool ternary_ok, bool condition) {
    OpaqueReason why = OpaqueReason::None;
    ExprOptions options;
    options.ternary_at_top = ternary_ok;
    options.condition = condition;
    const NodeId id = parse_expression_text(ast_, begin, end, options, why);
    if (id != kNoNode) return id;
    return make_opaque(ast_, begin, end, why == OpaqueReason::None ? OpaqueReason::LegacyRuntimeError : why);
}

bool StatementParser::block(std::uint32_t begin, std::uint32_t end, std::vector<NodeId>& out) {
    const DepthGuard guard(block_depth(), kMaxBlockDepth);
    if (guard.exceeded) {
        out.push_back(make_opaque(ast_, begin, end, OpaqueReason::NestingLimit));
        return true;
    }
    std::uint32_t i = begin;
    while (i < end) {
        i = skip_space(src_, i, end);
        if (i >= end) break;
        if (!may_start_keyword(src_[i])) {
            general_stmt(i, end, out);
            continue;
        }
        bool stop = false;
        Step step = fn_decl(i, end, out);
        if (step == Step::NoMatch) step = enum_decl(i, end, out);
        if (step == Step::NoMatch) step = struct_decl(i, end, out);
        if (step == Step::NoMatch) step = export_stmt(i, end, out);
        if (step == Step::NoMatch) step = import_stmt(i, end, out, stop);
        if (step == Step::NoMatch) step = comment(i, end);
        if (step == Step::NoMatch) step = while_stmt(i, end, out);
        if (step == Step::NoMatch) step = try_stmt(i, end, out);
        if (step == Step::NoMatch) step = for_stmt(i, end, out);
        if (step == Step::NoMatch) step = if_stmt(i, end, out);
        if (step == Step::NoMatch) step = return_stmt(i, end, out);
        if (step == Step::NoMatch) step = throw_stmt(i, end, out);
        if (step == Step::NoMatch) step = jump_stmt(i, end, out, "break", NodeKind::Break);
        if (step == Step::NoMatch) step = jump_stmt(i, end, out, "continue", NodeKind::Continue);
        if (step == Step::Error) return false;
        if (stop) return true;
        if (step == Step::NoMatch) general_stmt(i, end, out);
    }
    return true;
}

// fn [async] (name(args)) { body }
Step StatementParser::fn_decl(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out) {
    if (!boundary(i, end, "fn")) return Step::NoMatch;
    std::uint32_t p = skip_space(src_, i + 2, end);
    bool async = false;
    if (starts(src_, p, end, "[async]")) { async = true; p = skip_space(src_, p + 7, end); }
    std::uint32_t pc = 0;
    if (p >= end || src_[p] != '(' || !balanced(p, end, '(', ')', pc)) return fail("fn requires '(name(args))'", i);
    const std::uint32_t bo = skip_space(src_, pc + 1, end);
    std::uint32_t bc = 0;
    if (bo >= end || src_[bo] != '{' || !balanced(bo, end, '{', '}', bc)) return fail("fn requires a block", i);
    bool ok = true;
    const NodeId body = body_block(bo, bc, ok);
    if (!ok) return Step::Error;
    NodeId fn = ast_.add(NodeKind::FnDecl, i, bc + 1);
    if (async) ast_.at(fn).flags |= kFlagAsync;
    ast_.at(fn).b = body;
    out.push_back(fn_signature(fn, p, pc, false));
    i = bc + 1;
    return Step::Done;
}

// Mirrors the @fn( handler (src/ParserTemplate.cpp) and
// parse_callable_parameters (src/ParserHelpers.cpp). A malformed signature is
// reported by legacy only when the declaration runs: Opaque.
NodeId StatementParser::fn_signature(NodeId fn, std::uint32_t open, std::uint32_t close, bool method) {
    const std::string_view inner = src_.substr(open + 1, close - open - 1);
    const std::string_view sig = trim(inner);
    const std::uint32_t sig_begin = open + 1 + static_cast<std::uint32_t>(sig.data() - inner.data());
    const auto lp = sig.find('(');
    auto opaque = [&]() {
        const Node node = ast_.at(fn);
        return make_opaque(ast_, node.begin, node.end, OpaqueReason::LegacyRuntimeError);
    };
    if (lp == std::string_view::npos || sig.back() != ')') return opaque();
    const std::string_view name = trim(sig.substr(0, lp));
    if (!valid_identifier(name)) return opaque();
    const NodeId name_id = ast_.add(NodeKind::Ident, sig_begin + static_cast<std::uint32_t>(name.data() - sig.data()),
                                    sig_begin + static_cast<std::uint32_t>(name.data() - sig.data() + name.size()));
    std::vector<NodeId> params;
    std::vector<std::string_view> names;
    const std::uint32_t pbegin = sig_begin + static_cast<std::uint32_t>(lp) + 1;
    const std::uint32_t pend = sig_begin + static_cast<std::uint32_t>(sig.size()) - 1;
    // `f( )`: parse_callable_parameters rejects a whitespace-only list.
    if (pend > pbegin && trim(src_.substr(pbegin, pend - pbegin)).empty()) return opaque();
    if (pend > pbegin) {
        std::uint32_t s = pbegin;
        for (std::uint32_t k = pbegin; k <= pend; ++k) {
            if (k < pend && src_[k] != ',') continue;
            const std::string_view raw = src_.substr(s, k - s);
            std::string_view param = trim(raw);
            const bool variadic = param.substr(0, 3) == "...";
            if (variadic) param = trim(param.substr(3));
            const bool duplicate = std::find(names.begin(), names.end(), param) != names.end();
            if (!valid_identifier(param) || duplicate) return opaque();
            if (variadic && k != pend) return opaque();
            if (method && param == "this") return opaque();
            names.push_back(param);
            const std::uint32_t pb = static_cast<std::uint32_t>(param.data() - src_.data());
            const NodeId pid = ast_.add(NodeKind::Param, pb, pb + static_cast<std::uint32_t>(param.size()));
            if (variadic) ast_.at(pid).flags |= kFlagVariadic;
            params.push_back(pid);
            s = k + 1;
        }
    }
    ast_.at(fn).a = name_id;
    ast_.set_list(fn, params);
    return fn;
}

// enum Name { members }
Step StatementParser::enum_decl(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out) {
    if (!boundary(i, end, "enum")) return Step::NoMatch;
    std::uint32_t p = skip_space(src_, i + 4, end);
    const std::uint32_t ns = p;
    while (p < end && is_ident_char(src_[p])) ++p;
    const std::uint32_t name_end = p;
    p = skip_space(src_, p, end);
    std::uint32_t bc = 0;
    if (name_end == ns || p >= end || src_[p] != '{' || !balanced(p, end, '{', '}', bc))
        return fail("enum requires 'Name { members }'", i);
    const NodeId decl = ast_.add(NodeKind::EnumDecl, i, bc + 1);
    ast_.at(decl).a = ast_.add(NodeKind::Ident, ns, name_end);
    std::vector<NodeId> members;
    bool regular = true;
    std::uint32_t s = p + 1;
    for (std::uint32_t k = p + 1; k <= bc; ++k) {
        if (k < bc && src_[k] != ',' && src_[k] != '\n') continue;
        const std::string_view raw = src_.substr(s, k - s);
        const std::string_view member = trim(raw);
        if (!member.empty()) {
            if (!valid_identifier(member)) regular = false;
            for (NodeId seen : members)
                if (ast_.text(ast_.at(seen)) == member) regular = false;  // "invalid or duplicate enum member"
            const std::uint32_t mb = static_cast<std::uint32_t>(member.data() - src_.data());
            members.push_back(ast_.add(NodeKind::Ident, mb, mb + static_cast<std::uint32_t>(member.size())));
        }
        s = k + 1;
    }
    if (members.empty()) regular = false;  // "enum requires at least one member"
    ast_.set_list(decl, members);
    out.push_back(regular ? decl : make_opaque(ast_, i, bc + 1, OpaqueReason::LegacyRuntimeError));
    i = bc + 1;
    return Step::Done;
}

// struct (Name) { fields and methods }: the body is copied verbatim by the
// translator and only checked when the declaration runs.
Step StatementParser::struct_decl(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out) {
    if (!boundary(i, end, "struct")) return Step::NoMatch;
    const std::uint32_t p = skip_space(src_, i + 6, end);
    std::uint32_t pc = 0;
    if (p >= end || src_[p] != '(' || !balanced(p, end, '(', ')', pc)) return fail("struct requires '(name)'", i);
    const std::uint32_t bo = skip_space(src_, pc + 1, end);
    std::uint32_t bc = 0;
    if (bo >= end || src_[bo] != '{' || !balanced(bo, end, '{', '}', bc)) return fail("struct requires a block", i);
    const std::string_view inner = src_.substr(p + 1, pc - p - 1);
    const std::string_view name = trim(inner);
    if (!valid_identifier(name)) {
        out.push_back(make_opaque(ast_, i, bc + 1, OpaqueReason::LegacyRuntimeError));
    } else {
        const NodeId decl = ast_.add(NodeKind::StructDecl, i, bc + 1);
        const std::uint32_t nb = static_cast<std::uint32_t>(name.data() - src_.data());
        ast_.at(decl).a = ast_.add(NodeKind::Ident, nb, nb + static_cast<std::uint32_t>(name.size()));
        out.push_back(struct_body(decl, bo + 1, bc));
    }
    i = bc + 1;
    return Step::Done;
}

// Mirrors the @struct( handler (src/ParserTemplate.cpp): members separated by
// top-level newlines or ';', `[private ]fn(name(args)) { ... }` methods and
// `[private ]name := initializer` fields. Anything else is a run-time error
// in legacy, so the whole declaration becomes Opaque.
NodeId StatementParser::struct_body(NodeId decl, std::uint32_t begin, std::uint32_t end) {
    std::vector<NodeId> members;
    const NodeId whole = decl;
    auto opaque = [&]() {
        const Node node = ast_.at(whole);
        return make_opaque(ast_, node.begin, node.end, OpaqueReason::StructBody);
    };
    std::uint32_t p = begin;
    while (p < end) {
        p = skip_space(src_, p, end);
        if (p >= end) break;
        bool priv = false;
        if (starts(src_, p, end, "private ")) { priv = true; p = skip_space(src_, p + 8, end); }
        if (starts(src_, p, end, "fn(")) {
            std::uint32_t mhc = 0, mbc = 0;
            if (!balanced(p + 2, end, '(', ')', mhc)) return opaque();
            const std::uint32_t mbo = skip_space(src_, mhc + 1, end);
            if (mbo >= end || src_[mbo] != '{' || !balanced(mbo, end, '{', '}', mbc)) return opaque();
            std::vector<NodeId> body;
            ParseError ignored;
            if (!parse_block(ast_, mbo + 1, mbc, Mode::Deferred, body, ignored)) return opaque();
            const NodeId method = ast_.add(NodeKind::FnDecl, p, mbc + 1);
            ast_.at(method).flags |= kFlagMethod | (priv ? kFlagPrivate : 0);
            const NodeId block_id = ast_.add(NodeKind::Block, mbo, mbc + 1);
            ast_.set_list(block_id, body);
            ast_.at(method).b = block_id;
            const NodeId signed_method = fn_signature(method, p + 2, mhc, true);
            if (ast_.at(signed_method).kind == NodeKind::Opaque) return opaque();
            // A constructor (method named like the struct) must not be variadic
            // and may appear once; a repeated method name silently replaces the
            // earlier one in legacy. All three: Opaque.
            const std::string_view method_name = ast_.text(ast_.at(ast_.at(method).a));
            const bool ctor = method_name == ast_.text(ast_.at(ast_.at(decl).a));
            const std::uint32_t count = ast_.at(method).count;
            if (ctor && count && (ast_.at(ast_.list(ast_.at(method))[count - 1]).flags & kFlagVariadic)) return opaque();
            for (NodeId m : members)
                if (ast_.at(m).kind == NodeKind::FnDecl && ast_.text(ast_.at(ast_.at(m).a)) == method_name) return opaque();
            members.push_back(signed_method);
            p = mbc + 1;
            continue;
        }
        std::uint32_t eol = p;
        int par = 0, br = 0, bc = 0;
        bool q = false;
        char qc = 0;
        for (; eol < end; ++eol) {
            const char ch = src_[eol];
            if (q) { if (ch == '\\') { ++eol; continue; } if (ch == qc) q = false; continue; }
            if (ch == '\'' || ch == '"') { q = true; qc = ch; continue; }
            if (ch == '(') ++par; else if (ch == ')') --par; else if (ch == '[') ++br; else if (ch == ']') --br;
            else if (ch == '{') ++bc; else if (ch == '}') --bc;
            else if (!par && !br && !bc && (ch == '\n' || ch == ';')) break;
        }
        const std::uint32_t line_end = std::min(eol, end);
        const std::string_view line = trim(src_.substr(p, line_end - p));
        const auto dp = line.find(":=");
        if (dp == std::string_view::npos) return opaque();
        const std::string_view field = trim(line.substr(0, dp));
        const std::string_view init = trim(line.substr(dp + 2));
        if (!valid_identifier(field) || init.empty()) return opaque();
        for (NodeId m : members) {
            const Node& node = ast_.at(m);
            if (node.kind == NodeKind::StructField && ast_.text(ast_.at(node.a)) == field) return opaque();
        }
        const std::uint32_t fb = static_cast<std::uint32_t>(field.data() - src_.data());
        const std::uint32_t ib = static_cast<std::uint32_t>(init.data() - src_.data());
        const NodeId member = ast_.add(NodeKind::StructField, p, line_end);
        ast_.at(member).flags |= priv ? kFlagPrivate : 0;
        ast_.at(member).a = ast_.add(NodeKind::Ident, fb, fb + static_cast<std::uint32_t>(field.size()));
        ast_.at(member).b = expression(ib, ib + static_cast<std::uint32_t>(init.size()), false);
        members.push_back(member);
        p = line_end < end ? line_end + 1 : line_end;
    }
    ast_.set_list(decl, members);
    return decl;
}

// export(binding)[;]
Step StatementParser::export_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out) {
    if (!boundary(i, end, "export")) return Step::NoMatch;
    const std::uint32_t p = skip_space(src_, i + 6, end);
    std::uint32_t pc = 0;
    if (p >= end || src_[p] != '(' || !balanced(p, end, '(', ')', pc)) return fail("export requires '(binding)'", i);
    const NodeId node = ast_.add(NodeKind::Export, i, pc + 1);
    const std::string_view name = trim(src_.substr(p + 1, pc - p - 1));
    if (valid_identifier(name)) {
        const std::uint32_t nb = static_cast<std::uint32_t>(name.data() - src_.data());
        ast_.at(node).a = ast_.add(NodeKind::Ident, nb, nb + static_cast<std::uint32_t>(name.size()));
        out.push_back(node);
    } else {
        out.push_back(make_opaque(ast_, i, pc + 1, OpaqueReason::LegacyRuntimeError));
    }
    i = pc + 1;
    if (i < end && src_[i] == ';') ++i;
    return Step::Done;
}

// import(args)[;]. Without a balanced ')' the translator copies the rest of
// the region verbatim and stops translating it.
Step StatementParser::import_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out, bool& stop) {
    if (!boundary(i, end, "import")) return Step::NoMatch;
    const std::uint32_t p = skip_space(src_, i + 6, end);
    if (p >= end || src_[p] != '(') return Step::NoMatch;
    std::uint32_t pc = 0;
    if (!balanced(p, end, '(', ')', pc)) {
        out.push_back(make_opaque(ast_, i, end, OpaqueReason::TemplateText));
        stop = true;
        return Step::Done;
    }
    const NodeId node = ast_.add(NodeKind::Import, i, pc + 1);
    ast_.at(node).a = expression(p + 1, pc, true);
    out.push_back(node);
    i = pc + 1;
    if (i < end && src_[i] == ';') ++i;
    return Step::Done;
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
    std::uint32_t e = i + 6;
    bool quoted = false;
    char quote = 0;
    int par = 0, br = 0, bc = 0;
    for (; e < end; ++e) {
        const char c = src_[e];
        if (quoted) { if (c == '\\' && e + 1 < end) ++e; else if (c == quote) quoted = false; continue; }
        if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
        if (c == '(') ++par; else if (c == ')') --par; else if (c == '[') ++br; else if (c == ']') --br;
        else if (c == '{') ++bc; else if (c == '}') --bc;
        if (!par && !br && !bc && (c == ';' || c == '\n')) break;
    }
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

// Any other statement: text up to ';' or newline at bracket depth 0 outside
// quotes. Depth may go negative, which suppresses splitting until it returns
// to zero, exactly as in the translator.
void StatementParser::general_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out) {
    const std::uint32_t start = i;
    bool quoted = false;
    char quote = 0;
    int par = 0, br = 0, bc = 0;
    for (; i < end; ++i) {
        const char c = src_[i];
        if (quoted) { if (c == '\\' && i + 1 < end) ++i; else if (c == quote) quoted = false; continue; }
        if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
        if (c == '(') ++par; else if (c == ')') --par; else if (c == '[') ++br; else if (c == ']') --br;
        else if (c == '{') ++bc; else if (c == '}') --bc;
        if (!par && !br && !bc && (c == ';' || c == '\n')) break;
    }
    const std::uint32_t stop = std::min(i, end);
    const std::string_view stmt = trim(src_.substr(start, stop - start));
    if (!stmt.empty()) out.push_back(classify_statement(start, start + static_cast<std::uint32_t>(stmt.size())));
    if (i < end) ++i;
}

// `@fn(name(args)) { }`, `@fn[async](...) { }`, `@struct(Name) { }`,
// `@import(...)` and `$[statement]` written directly in a script are copied
// verbatim by the translator and handled by the template engine
// (src/ParserTemplate.cpp) when the statement runs. When the statement is
// exactly one such form it means the same as the script form (the script
// forms translate to these), except that an `@fn` body is translated only
// when called. Anything else stays template text (kNoNode).
NodeId StatementParser::template_declaration(std::uint32_t start, std::uint32_t stmt_end) {
    std::uint32_t open = 0, close = 0;
    if ((starts(src_, start, stmt_end, "@import(") || starts(src_, start, stmt_end, "$[")) &&
        balanced(src_[start] == '@' ? start + 7 : start + 1, stmt_end, src_[start] == '@' ? '(' : '[',
                 src_[start] == '@' ? ')' : ']', close) &&
        close + 1 == stmt_end) {
        const std::uint32_t inner = src_[start] == '@' ? start + 8 : start + 2;
        if (src_[start] == '$') {
            // `$[statement]` is exactly what the translator makes of `statement`.
            OpaqueReason why = OpaqueReason::None;
            const NodeId id = parse_statement_text(ast_, inner, close, why);
            return id != kNoNode ? id : kNoNode;
        }
        const NodeId node = ast_.add(NodeKind::Import, start, stmt_end);
        ast_.at(node).flags |= kFlagTemplateImport;  // "@import ..." messages differ from "import ..."
        ast_.at(node).a = expression(inner, close, true);
        return node;
    }
    bool async = false, is_struct = false;
    if (starts(src_, start, stmt_end, "@fn(")) open = start + 3;
    else if (starts(src_, start, stmt_end, "@fn[async](")) { open = start + 10; async = true; }
    else if (starts(src_, start, stmt_end, "@struct(")) { open = start + 7; is_struct = true; }
    else return kNoNode;
    std::uint32_t pc = 0, bc = 0;
    if (!balanced(open, stmt_end, '(', ')', pc)) return kNoNode;
    const std::uint32_t bo = skip_space(src_, pc + 1, stmt_end);
    if (bo >= stmt_end || src_[bo] != '{' || !balanced(bo, stmt_end, '{', '}', bc) || bc + 1 != stmt_end)
        return kNoNode;
    if (is_struct) {
        const std::string_view name = trim(src_.substr(open + 1, pc - open - 1));
        if (!valid_identifier(name)) return make_opaque(ast_, start, stmt_end, OpaqueReason::LegacyRuntimeError);
        const NodeId decl = ast_.add(NodeKind::StructDecl, start, stmt_end);
        const std::uint32_t nb = static_cast<std::uint32_t>(name.data() - src_.data());
        ast_.at(decl).a = ast_.add(NodeKind::Ident, nb, nb + static_cast<std::uint32_t>(name.size()));
        return struct_body(decl, bo + 1, bc);
    }
    std::vector<NodeId> body;
    ParseError ignored;
    if (!parse_block(ast_, bo + 1, bc, Mode::Deferred, body, ignored))
        return make_opaque(ast_, start, stmt_end, OpaqueReason::DeferredTranslation);
    const NodeId block_id = ast_.add(NodeKind::Block, bo, bc + 1);
    ast_.set_list(block_id, body);
    const NodeId fn = ast_.add(NodeKind::FnDecl, start, stmt_end);
    ast_.at(fn).flags |= kFlagDeferredBody | (async ? kFlagAsync : 0);
    ast_.at(fn).b = block_id;
    return fn_signature(fn, open, pc, false);
}

// The translator's choice for a general statement: shebang comment,
// verbatim template text, `await` expression, external command, or `$[...]`.
NodeId StatementParser::classify_statement(std::uint32_t start, std::uint32_t stmt_end) {
    const std::string_view stmt = src_.substr(start, stmt_end - start);
    if (stmt.size() >= 2 && stmt[0] == '#' && stmt[1] == '!') {
        // The translator emits `@//` + statement: only its first line is a
        // comment, the rest runs as template text.
        if (stmt.find('\n') == std::string_view::npos) return ast_.add(NodeKind::Comment, start, stmt_end);
        const NodeId id = ast_.add(NodeKind::TemplateStmt, start, stmt_end);
        ast_.at(id).a = make_opaque(ast_, start, stmt_end, OpaqueReason::TemplateText);
        return id;
    }
    if (stmt[0] == '@' || stmt[0] == '$') {
        const NodeId declaration = template_declaration(start, stmt_end);
        if (declaration != kNoNode) return declaration;
    }
    if (stmt[0] == '@' || stmt[0] == '$') {
        const NodeId id = ast_.add(NodeKind::TemplateStmt, start, stmt_end);
        ast_.at(id).a = make_opaque(ast_, start, stmt_end, OpaqueReason::TemplateText);
        return id;
    }
    auto statement = [&]() {
        OpaqueReason why = OpaqueReason::None;
        const NodeId id = parse_statement_text(ast_, start, stmt_end, why);
        if (id != kNoNode) return id;
        return make_opaque(ast_, start, stmt_end, why == OpaqueReason::None ? OpaqueReason::LegacyRuntimeError : why);
    };
    if (stmt.substr(0, 6) == "await ") return statement();
    const auto f0 = stmt.find_first_of(" \t");
    const std::string_view first = f0 == std::string_view::npos ? stmt : stmt.substr(0, f0);
    const bool path_first = first.find('/') != std::string_view::npos;
    const bool multi_token = f0 != std::string_view::npos;
    bool assignment_form = first.find('=') != std::string_view::npos;
    if (!assignment_form && multi_token) {
        const std::string_view rest = trim(stmt.substr(f0));
        assignment_form = rest.substr(0, 2) == ":=" || (rest.substr(0, 1) == "=" && rest.size() > 1 && rest[1] != '=');
    }
    bool command = (path_first || multi_token) && !assignment_form;
    bool quoted = false, saw_delim = false;
    char quote = 0;
    int pp = 0, bb = 0, cc = 0;
    for (std::size_t k = 0; k < stmt.size() && command; ++k) {
        const char c = stmt[k];
        if (quoted) { if (c == '\\') ++k; else if (c == quote) quoted = false; continue; }
        if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
        if (c == '(') ++pp; else if (c == ')') --pp; else if (c == '[') ++bb; else if (c == ']') --bb;
        else if (c == '{') ++cc; else if (c == '}') --cc;
        else if ((c == '=' || c == ':') && !pp && !bb && !cc) { command = false; continue; }
        else continue;
        saw_delim = true;
    }
    if (command && !saw_delim && !pp && !bb && !cc) return ast_.add(NodeKind::Command, start, stmt_end);
    return statement();
}

} // namespace

bool parse_block(Ast& ast, std::uint32_t begin, std::uint32_t end, Mode mode,
                 std::vector<NodeId>& out, ParseError& error) {
    StatementParser parser(ast, mode, error);
    return parser.block(begin, end, out);
}

} // namespace detail

ParseResult parse_script(Ast& ast) {
    ParseResult result;
    std::vector<NodeId> statements;
    const auto size = static_cast<std::uint32_t>(ast.source().size());
    ast.reserve(size / 3 + 16);
    result.ok = detail::parse_block(ast, 0, size, detail::Mode::Strict, statements, result.error);
    if (!result.ok) return result;
    result.root = ast.add(NodeKind::Program, 0, size);
    ast.set_list(result.root, statements);
    return result;
}

Location locate(std::string_view source, std::uint32_t offset) {
    offset = std::min<std::uint32_t>(offset, static_cast<std::uint32_t>(source.size()));
    Location location{1, 1};
    std::uint32_t line_start = 0;
    for (std::uint32_t k = 0; k < offset; ++k) {
        if (source[k] == '\n') { ++location.line; line_start = k + 1; }
    }
    location.column = offset - line_start + 1;
    return location;
}

} // namespace nift::rt
