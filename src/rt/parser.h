#pragma once
// Runtime 2.0 CP10: parser for Nift scripts (.f files, -e programs).
//
// Two layers:
//  1. Statement structure. Blocks, statement boundaries, comments and the
//     keyword forms (fn, enum, struct, export, import, while, try/catch, for,
//     if/else, return, throw, break, continue) are recognised exactly as the
//     legacy translator (Parser::translate_function_program) recognises them,
//     with the same parse-time diagnostics at the same positions.
//  2. Expressions and simple statements, parsed with a Pratt parser. Text the
//     legacy engine only rejects when it runs, or parses irregularly, becomes
//     an Opaque node instead of a parse error, so no program that legacy
//     accepts is rejected here (D-001: such a unit stays on the legacy engine).
#include "rt/ast.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace nift::rt {

// A script syntax error. Legacy reports these from the translator with code
// native.translation_error (DiagnosticCode::NativeTranslationError, Fatal) as
// "<file>:<line>:<column>: <message>"; every ParseError carries that code.
struct ParseError {
    static constexpr const char* kCode = "native.translation_error";
    std::string message;
    std::uint32_t offset = 0;  // byte offset of the reported position
};

struct ParseResult {
    NodeId root = kNoNode;
    bool ok = true;
    ParseError error;
};

struct Location {
    std::size_t line = 0;
    std::size_t column = 0;  // 1-based byte column, as legacy diagnostics
};

// Parses a whole script. `ast` must have been constructed over the same text.
ParseResult parse_script(Ast& ast);

// Parses one expression (used by tests and later front ends).
NodeId parse_expression(Ast& ast, std::uint32_t begin, std::uint32_t end);

Location locate(std::string_view source, std::uint32_t offset);

} // namespace nift::rt
