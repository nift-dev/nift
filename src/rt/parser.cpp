// Runtime 2.0 script parser: public entry points (see parser.h) and the
// state shared by the statement and expression layers.
#include "rt/parser.h"

#include "rt/parser_internal.h"

#include <algorithm>

namespace nift::rt {
namespace detail {

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
