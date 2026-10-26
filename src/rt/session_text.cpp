#include "rt/session_text.h"
#include "rt/lexer.h"
#include "rt/unit_check.h"
#include "rt/infer.h"
#include "rt/parser.h"
#include "rt/compiler_eval.h"
#include <stdexcept>
#include <algorithm>

// rt-lint: cold-file immutable declaration linking, never executable history replay
namespace nift::rt {
SessionText session_text(const std::string& source, const std::string& path, const SessionCompileContext& context) {
    SessionText out;
    struct Entry { std::shared_ptr<const SessionDefinition> definition; std::string name; };
    std::unordered_map<std::string, std::vector<Entry>> available;
    std::unordered_set<std::string> declarations;
    Ast original(source);
    const auto parsed = parse_script(original);
    if (parsed.ok) for (std::uint32_t k = 0; k < original.at(parsed.root).count; ++k) {
        const auto& node = original.at(original.list(original.at(parsed.root))[k]);
        if (node.kind == NodeKind::FnDecl || node.kind == NodeKind::StructDecl)
            declarations.emplace(original.text(original.at(node.a)));
    }
    for (const auto& schema : context.structures)
        available[schema.first].push_back({schema.second, schema.first});
    std::unordered_map<const SessionDefinition*, std::string> schema_names;
    unsigned serial = 0;
    for (const auto& symbol : context.symbols) if (symbol.defined) {
        for (const auto& callable : session_callable_targets(symbol.callable_shape))
            available[symbol.name].push_back({callable.definition, callable.definition->name});
        if (!symbol.struct_definition) continue;
        auto& name = schema_names[symbol.struct_definition.get()];
        if (name.empty()) {
            name = symbol.struct_name;
            const auto current = context.structures.find(name);
            if (declarations.count(name) ||
                (name != symbol.struct_definition->name && source.find(name) != std::string::npos) ||
                (current != context.structures.end() && current->second != symbol.struct_definition)) {
                do { name = "__cp19_schema" + std::to_string(serial++); }
                while (source.find(name) != std::string::npos || available.count(name) ||
                    std::any_of(context.symbols.begin(), context.symbols.end(),
                        [&](const auto& other) { return other.name == name || other.struct_name == name; }));
            }
        }
        out.schema_names.emplace(symbol.name, name);
        available[symbol.name].push_back({symbol.struct_definition, name});
    }
    std::vector<Entry> selected;
    auto scan = [&](std::string_view text) {
        Lexer lexer(text, 0, static_cast<std::uint32_t>(text.size()));
        for (auto token = lexer.next(); token.kind != Tok::End; token = lexer.next()) {
            if (token.kind != Tok::Ident) continue;
            const auto found = available.find(std::string(text.substr(token.begin, token.end - token.begin)));
            if (found != available.end()) for (const auto& entry : found->second)
                if (!declarations.count(entry.name) &&
                    out.imports.emplace(entry.name, entry.definition).second) selected.push_back(entry);
        }
    };
    scan(source);
    if (parsed.ok) {
        const auto prior_nodes = original.size();
        normalize_session_callbacks(original, parsed.root, context);
        // Only statically proven Array callback strings were parsed into
        // these nodes. Ordinary strings never import executable definitions.
        for (NodeId id = static_cast<NodeId>(prior_nodes); id < original.size(); ++id)
            if (original.at(id).kind == NodeKind::Ident) scan(original.text(original.at(id)));
    }
    // Redefinition changes a registry entry seen by every retained alias.
    // Compile all reachable readers against it, including captured callables.
    bool redefining = false;
    for (const auto& symbol : context.symbols) redefining = redefining || declarations.count(symbol.name);
    if (redefining) for (const auto& symbol : context.symbols) scan(symbol.name);
    for (std::size_t k = 0; k < selected.size(); ++k) {
        const auto definition = selected[k].definition;
        scan(std::string_view(*definition->source).substr(definition->begin, definition->end - definition->begin));
    }
    auto generated = [&](std::string value) {
        out.text += value;
        out.pieces.push_back({path, std::make_shared<const std::string>(value), 0,
                              static_cast<std::uint32_t>(value.size())});
    };
    out.imports.clear();
    std::stable_partition(selected.begin(), selected.end(),
        [](const auto& item) { return item.definition->structure; });
    for (const auto& entry : selected) {
        const auto& definition = entry.definition;
        if (definition->lambda) {
            std::string wrapper = "__cp19_template";
            auto collision = [&](const std::string& name) {
                if (source.find(name) != std::string::npos) return true;
                for (const auto& symbol : context.symbols) if (symbol.name == name) return true;
                for (const auto& item : selected)
                    if (item.definition->source->find(name) != std::string::npos) return true;
                return false;
            };
            while (collision(wrapper) || out.imports.count(wrapper)) wrapper += "_";
            out.imports.emplace(wrapper, definition);
            generated("fn(" + wrapper + "()){");
            for (const auto& capture : definition->captures) generated(capture.name + ":=null;");
            generated("return ");
        } else out.imports.emplace(entry.name, definition);
        if (definition->structure && entry.name != definition->name) {
            Ast schema(*definition->source);
            const auto parsed_schema = parse_script_range(schema, definition->begin, definition->end);
            if (!parsed_schema.ok) throw std::logic_error("session schema source");
            const auto& node = schema.at(schema.list(schema.at(parsed_schema.root))[0]);
            std::vector<NodeId> names{node.a};
            for (std::uint32_t m = 0; m < node.count; ++m) {
                const auto& method = schema.at(schema.list(node)[m]);
                if (method.kind == NodeKind::FnDecl && schema.text(schema.at(method.a)) == definition->name)
                    names.push_back(method.a);
            }
            auto position = definition->begin;
            for (const auto id : names) {
                const auto& name = schema.at(id);
                out.text.append(*definition->source, position, name.begin - position);
                out.pieces.push_back({definition->path, definition->source, position, name.begin - position});
                out.text += entry.name;
                out.pieces.push_back({definition->path, definition->source, name.begin,
                                      static_cast<std::uint32_t>(entry.name.size())});
                position = name.end;
            }
            out.text.append(*definition->source, position, definition->end - position);
            out.pieces.push_back({definition->path, definition->source, position, definition->end - position});
        } else {
            out.text.append(*definition->source, definition->begin, definition->end - definition->begin);
            out.pieces.push_back({definition->path, definition->source, definition->begin,
                                 definition->end - definition->begin});
        }
        generated(definition->lambda ? ";}\n" : "\n");
    }
    if (out.text.size() + source.size() > 0xffffffffu) throw std::length_error("session source offset limit");
    out.unit_begin = static_cast<std::uint32_t>(out.text.size());
    out.text += source;
    out.pieces.push_back({path, std::make_shared<const std::string>(source), 0,
                         static_cast<std::uint32_t>(source.size())});
    return out;
}
void prepare_session_templates(const Ast& ast, NodeId root, bool parsed_ok, const SessionText& text,
                               const SessionCompileContext& current, SessionCompileContext& pending) {
    pending.source_owner = std::make_shared<const std::string>(text.text);
    pending.lambda_templates.clear(); pending.capture_seeds.clear(); pending.template_wrappers.clear();
    pending.callable_templates.clear(); pending.capture_callable_seeds.clear();
    pending.capture_contracts.clear(); pending.template_definitions.clear(); pending.const_captures.clear();
    if (parsed_ok) for (std::uint32_t k = 0; k < ast.at(root).count; ++k) {
        const auto& fn = ast.at(ast.list(ast.at(root))[k]);
        if (fn.kind != NodeKind::FnDecl) continue;
        const auto imported = text.imports.find(std::string(ast.text(ast.at(fn.a))));
        if (imported == text.imports.end() || !imported->second->lambda) continue;
        const auto& body = ast.at(fn.b);
        if (body.count != imported->second->captures.size() + 1)
            throw std::logic_error("session lambda template shape");
        const auto& tail = ast.at(ast.list(body)[body.count - 1]);
        if (tail.kind != NodeKind::Return || ast.at(tail.a).kind != NodeKind::Lambda)
            throw std::logic_error("session lambda template body");
        pending.template_wrappers.insert(ast.list(ast.at(root))[k]);
        pending.template_definitions.emplace(tail.a, imported->second);
        pending.callable_templates.emplace(imported->second.get(), tail.a);
        for (std::uint32_t c = 0; c + 1 < body.count; ++c) {
            const auto& declaration = ast.at(ast.list(body)[c]);
            pending.capture_seeds.emplace(declaration.b, imported->second->captures[c].shape);
            pending.capture_contracts.emplace(declaration.a, imported->second->captures[c].shape);
            if (imported->second->captures[c].const_array) pending.const_captures.insert(declaration.a);
            for (const auto& symbol : current.symbols)
                for (const auto& target : session_callable_targets(symbol.callable_shape))
                    if (target.definition == imported->second && c < target.captures.size()) {
                        const auto shape = target.captures[c];
                        if (!shape) continue;
                        // Multiple closure instances share a definition, so
                        // infer every possible callable captured at this slot.
                        auto& seed = pending.capture_callable_seeds[declaration.b];
                        if (!seed) seed = shape;
                        else seed = merge_session_callable_shapes(seed, shape);
                    }
        }
        for (const auto& symbol : current.symbols) if (symbol.definition == imported->second)
            pending.lambda_templates.emplace(symbol.name, tail.a);
    }
}
void session_definitions(const Ast& ast, NodeId root, const SessionText& text, const std::string& path,
                         const SessionCompileContext& pending, Program& program, std::uint64_t executions) {
    const auto plan = plan_unit(ast, root);
    for (std::size_t f = 0; f < plan.functions.size(); ++f) {
        const auto& fn = ast.at(plan.functions[f]);
        const auto name = std::string(ast.text(ast.at(fn.a)));
        const auto imported = text.imports.find(name);
        if (imported != text.imports.end()) {
            if (!imported->second->lambda) program.protos[f + 1]->session_definition = imported->second;
        }
        else if (fn.begin >= text.unit_begin) {
            auto definition = std::make_shared<SessionDefinition>();
            definition->name = name; definition->path = path; definition->source = text.pieces.back().source;
            definition->begin = fn.begin - text.unit_begin; definition->end = fn.end - text.unit_begin;
            program.protos[f + 1]->session_definition = std::move(definition);
        }
    }
    for (std::size_t p = 1; p < program.protos.size(); ++p) {
        auto& proto = program.protos[p];
        const auto& original = proto->session_definition;
        if (!original || !original->lambda || original->source != pending.source_owner) continue;
        auto definition = std::make_shared<SessionDefinition>(*original);
        std::uint32_t position = 0;
        bool mapped = false;
        for (const auto& piece : text.pieces) {
            if (definition->begin >= position && definition->end <= position + piece.length) {
                definition->begin = definition->begin - position + piece.source_begin;
                definition->end = definition->end - position + piece.source_begin;
                definition->source = piece.source; definition->path = piece.path; mapped = true; break;
            }
            position += piece.length;
        }
        if (!mapped) throw std::logic_error("session lambda source mapping");
        definition->name = "lambda:" + std::to_string(executions) + ":" + std::to_string(p);
        proto->session_definition = std::move(definition);
    }
}
} // namespace nift::rt
