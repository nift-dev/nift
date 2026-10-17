// Statement dispatch of the Runtime 2.0 script parser (see
// statement_parser.h). Comments name the legacy behaviour each rule
// reproduces.
#include "rt/statement_parser.h"

#include "rt/control_flow.h"

#include <algorithm>

namespace nift::rt::detail {

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

// Any other statement: text up to ';' or newline at bracket depth 0 outside
// quotes. Depth may go negative, which suppresses splitting until it returns
// to zero, exactly as in the translator.
void StatementParser::general_stmt(std::uint32_t& i, std::uint32_t end, std::vector<NodeId>& out) {
    const std::uint32_t start = i;
    i = statement_end(src_, i, end);
    const std::uint32_t stop = std::min(i, end);
    const std::string_view stmt = trim(src_.substr(start, stop - start));
    if (!stmt.empty()) out.push_back(classify_statement(start, start + static_cast<std::uint32_t>(stmt.size())));
    if (i < end) ++i;
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
    // Parse time only (a unit is parsed once, cached by content hash).
    if (stmt.substr(0, 6) == "await ") return statement();  // rt-lint: allow-string-compare parse time
    const auto f0 = stmt.find_first_of(" \t");
    const std::string_view first = f0 == std::string_view::npos ? stmt : stmt.substr(0, f0);
    const bool path_first = first.find('/') != std::string_view::npos;
    const bool multi_token = f0 != std::string_view::npos;
    bool assignment_form = first.find('=') != std::string_view::npos;
    if (!assignment_form && multi_token) {
        const std::string_view rest = trim(stmt.substr(f0));
        const bool declares = rest.size() >= 2 && rest[0] == ':' && rest[1] == '=';
        assignment_form = declares || (!rest.empty() && rest[0] == '=' && rest.size() > 1 && rest[1] != '=');
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

bool parse_block(Ast& ast, std::uint32_t begin, std::uint32_t end, Mode mode,
                 std::vector<NodeId>& out, ParseError& error) {
    StatementParser parser(ast, mode, error);
    return parser.block(begin, end, out);
}

} // namespace nift::rt::detail
