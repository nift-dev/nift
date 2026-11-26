#include "rt/module_data.h"

#include "rt/module_link.h"
#include "rt/resolver.h"

#include <charconv>

// rt-lint: cold-file compile-time validation of literal package data
namespace nift::rt {
bool imported_module_data(const UnitPlan& plan, const Node& call) {
    return plan.linked && plan.linked->locate(call.begin).module > 0;
}

std::optional<std::string> module_ascii_literal(const Ast& ast, const Node& call) {
    if (call.kind != NodeKind::Call || call.count != 1) return {};
    const Node& decode = ast.at(call.a);
    if (decode.kind != NodeKind::Member || ast.text(ast.at(decode.b)) != "decode") return {};
    const Node& encoding = ast.at(ast.list(call)[0]);
    if (encoding.kind != NodeKind::String || ast.text(encoding) != "\"utf-8\"") return {};
    const Node& bytes = ast.at(decode.a);
    if (bytes.kind != NodeKind::Call || bytes.count != 1) return {};
    const Node& constructor = ast.at(bytes.a);
    if (constructor.kind != NodeKind::Ident || ast.text(constructor) != "bytes") return {};
    const Node& array = ast.at(ast.list(bytes)[0]);
    if (array.kind != NodeKind::Array) return {};
    const auto src = ast.source();
    if (decode.end >= call.end || bytes.end >= decode.end || constructor.end >= bytes.end ||
        src[decode.end] != '(' || src[bytes.end] != '.' || src[constructor.end] != '(')
        return {};
    for (const Node* sequence : {&call, &bytes, &array}) {
        auto end = sequence->end - 1;
        while (end > sequence->begin && (src[end - 1] == ' ' || src[end - 1] == '\t' ||
               src[end - 1] == '\n' || src[end - 1] == '\r')) --end;
        if (end > sequence->begin && src[end - 1] == ',') return {};
    }
    std::string literal;
    literal.reserve(array.count);
    for (std::uint32_t k = 0; k < array.count; ++k) {
        const Node& item = ast.at(ast.list(array)[k]);
        if (item.kind != NodeKind::Number) return {};
        const auto raw = ast.text(item);
        if (raw.size() > 1 && raw.front() == '0') return {};
        unsigned octet = 0;
        const auto end = raw.data() + raw.size();
        // 0x1f can introduce a tagged legacy handle.
        const auto value = std::from_chars(raw.data(), end, octet);
        if (value.ec != std::errc{} || value.ptr != end || octet > 127 || octet == 31) return {};
        literal.push_back(static_cast<char>(octet));
    }
    return literal;
}
}
