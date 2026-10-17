#pragma once
// Statement layer of the Runtime 2.0 script parser: one class mirroring
// Parser::translate_function_program (src/ParserScript.cpp) branch by branch,
// so statement boundaries, accepted forms and parse-time diagnostics are the
// legacy engine's. Dispatch is in statement_parser.cpp, declarations in
// declarations.cpp, control flow in control_flow.cpp.
#include "rt/parser_internal.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace nift::rt::detail {

enum class Step { NoMatch, Done, Error };

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
    NodeId for_header(std::uint32_t begin, std::uint32_t end);
    NodeId struct_body(NodeId decl, std::uint32_t begin, std::uint32_t end);

    Ast& ast_;
    std::string_view src_;
    Mode mode_;
    ParseError& error_;
};

} // namespace nift::rt::detail
