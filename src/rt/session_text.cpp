#include "rt/session_text.h"
#include "rt/lexer.h"
#include <stdexcept>

// rt-lint: cold-file immutable declaration linking, never executable history replay
namespace nift::rt {
SessionText session_text(const std::string& source, const std::string& path, const SessionCompileContext& context) {
    SessionText out;
    std::unordered_map<std::string, std::shared_ptr<const SessionDefinition>> available;
    for (const auto& symbol : context.symbols)
        if (symbol.defined && symbol.definition) available.emplace(symbol.name, symbol.definition);
    std::vector<std::shared_ptr<const SessionDefinition>> selected;
    auto scan = [&](std::string_view text) {
        Lexer lexer(text, 0, static_cast<std::uint32_t>(text.size()));
        for (auto token = lexer.next(); token.kind != Tok::End; token = lexer.next()) {
            if (token.kind != Tok::Ident) continue;
            const auto found = available.find(std::string(text.substr(token.begin, token.end - token.begin)));
            if (found != available.end() && out.imports.emplace(found->second->name, found->second).second)
                selected.push_back(found->second);
        }
    };
    scan(source);
    for (std::size_t k = 0; k < selected.size(); ++k) {
        const auto definition = selected[k];
        scan(std::string_view(*definition->source).substr(definition->begin, definition->end - definition->begin));
    }
    for (const auto& definition : selected) {
        out.text.append(*definition->source, definition->begin, definition->end - definition->begin);
        out.text += '\n';
        out.pieces.push_back({definition->path, definition->source, definition->begin,
                              definition->end - definition->begin + 1});
    }
    if (out.text.size() + source.size() > 0xffffffffu) throw std::length_error("session source offset limit");
    out.unit_begin = static_cast<std::uint32_t>(out.text.size());
    out.text += source;
    out.pieces.push_back({path, std::make_shared<const std::string>(source), 0,
                         static_cast<std::uint32_t>(source.size())});
    return out;
}
} // namespace nift::rt
