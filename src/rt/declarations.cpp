// Declaration statements of the Runtime 2.0 script parser (see
// declarations.h).
#include "rt/declarations.h"

#include <algorithm>

namespace nift::rt::detail {

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
    out.push_back(fn_signature(ast_, fn, p, pc, false));
    i = bc + 1;
    return Step::Done;
}

// Mirrors the @fn( handler (src/ParserTemplate.cpp) and
// parse_callable_parameters (src/ParserHelpers.cpp). A malformed signature is
// reported by legacy only when the declaration runs: Opaque.
NodeId fn_signature(Ast& ast, NodeId fn, std::uint32_t open, std::uint32_t close, bool method) {
    const std::string_view src = ast.source();
    const std::string_view inner = src.substr(open + 1, close - open - 1);
    const std::string_view sig = trim(inner);
    const std::uint32_t sig_begin = open + 1 + static_cast<std::uint32_t>(sig.data() - inner.data());
    const auto lp = sig.find('(');
    auto opaque = [&]() {
        const Node node = ast.at(fn);
        return make_opaque(ast, node.begin, node.end, OpaqueReason::LegacyRuntimeError);
    };
    if (lp == std::string_view::npos || sig.back() != ')') return opaque();
    const std::string_view name = trim(sig.substr(0, lp));
    if (!valid_identifier(name)) return opaque();
    const NodeId name_id = ast.add(NodeKind::Ident, sig_begin + static_cast<std::uint32_t>(name.data() - sig.data()),
                                    sig_begin + static_cast<std::uint32_t>(name.data() - sig.data() + name.size()));
    std::vector<NodeId> params;
    std::vector<std::string_view> names;
    const std::uint32_t pbegin = sig_begin + static_cast<std::uint32_t>(lp) + 1;
    const std::uint32_t pend = sig_begin + static_cast<std::uint32_t>(sig.size()) - 1;
    // `f( )`: parse_callable_parameters rejects a whitespace-only list.
    if (pend > pbegin && trim(src.substr(pbegin, pend - pbegin)).empty()) return opaque();
    if (pend > pbegin) {
        std::uint32_t s = pbegin;
        for (std::uint32_t k = pbegin; k <= pend; ++k) {
            if (k < pend && src[k] != ',') continue;
            const std::string_view raw = src.substr(s, k - s);
            std::string_view param = trim(raw);
            const bool variadic = param.size() >= 3 && param[0] == '.' && param[1] == '.' && param[2] == '.';
            if (variadic) param = trim(param.substr(3));
            const bool duplicate = std::find(names.begin(), names.end(), param) != names.end();
            if (!valid_identifier(param) || duplicate) return opaque();
            if (variadic && k != pend) return opaque();
            // Parse time only (a unit is parsed once, cached by content hash).
            if (method && param == "this") return opaque();  // rt-lint: allow-string-compare parse time
            names.push_back(param);
            const std::uint32_t pb = static_cast<std::uint32_t>(param.data() - src.data());
            const NodeId pid = ast.add(NodeKind::Param, pb, pb + static_cast<std::uint32_t>(param.size()));
            if (variadic) ast.at(pid).flags |= kFlagVariadic;
            params.push_back(pid);
            s = k + 1;
        }
    }
    ast.at(fn).a = name_id;
    ast.set_list(fn, params);
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
            const NodeId signed_method = fn_signature(ast_, method, p + 2, mhc, true);
            if (ast_.at(signed_method).kind == NodeKind::Opaque) return opaque();
            // A constructor (method named like the struct) must not be variadic
            // and may appear once; a repeated method name silently replaces the
            // earlier one in legacy. All three: Opaque.
            const std::string_view method_name = ast_.text(ast_.at(ast_.at(method).a));
            const bool ctor = method_name == ast_.text(ast_.at(ast_.at(decl).a));
            const std::uint32_t count = ast_.at(method).count;
            const NodeId* params = ast_.list(ast_.at(method));
            if (ctor && count && (ast_.at(params[count - 1]).flags & kFlagVariadic)) return opaque();
            for (NodeId m : members) {
                const Node& member = ast_.at(m);
                if (member.kind == NodeKind::FnDecl && ast_.text(ast_.at(member.a)) == method_name) return opaque();
            }
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
    return fn_signature(ast_, fn, open, pc, false);
}

} // namespace nift::rt::detail
