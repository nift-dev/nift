#include "rt/link_diagnostic.h"

#include "rt/compiler.h"
#include "rt/compiler_coro_admit.h"
#include "rt/parser.h"
#include "rt/session_invocation.h"

// rt-lint: cold-file refusal reporting, once per refused unit

namespace nift::rt {

namespace {

struct Where {
    std::filesystem::path path;
    const std::string* source = nullptr;
};

Where unit_of(const LinkedUnit& link, std::size_t module, const std::filesystem::path& source_path,
              const std::string& source) {
    if (module == 0) return {source_path, &source};
    return {link.graph.modules[module]->path, &link.graph.modules[module]->source};
}

// `path:line:column` of a recorded offset.
std::string at(const LinkedUnit& link, std::size_t module, std::uint32_t offset,
               const std::filesystem::path& source_path, const std::string& source) {
    const Where w = unit_of(link, module, source_path, source);
    const Location l = locate(*w.source, offset);
    return w.path.generic_string() + ':' + std::to_string(l.line) + ':' + std::to_string(l.column);
}

// `offset` lies in top-level code legacy runs unconditionally when it gets
// there: not in a function, lambda or struct body, a branch, a loop, a `try`
// or a short-circuit operand (review of 2f37ed36: such errors never outrank).
bool unconditional_at(const Ast& ast, NodeId root, std::uint32_t offset) {
    if (root == kNoNode) return false;
    NodeId at = root;
    while (true) {
        const Node& n = ast.at(at);
        if (at != root) {
            switch (n.kind) {
                case NodeKind::FnDecl: case NodeKind::Lambda: case NodeKind::StructDecl: case NodeKind::If:
                case NodeKind::While: case NodeKind::For: case NodeKind::Try: case NodeKind::Ternary:
                case NodeKind::Logical: case NodeKind::Coalesce: case NodeKind::SafeMember: case NodeKind::SafeIndex:
                    return false;
                default: break;
            }
        }
        NodeId next = kNoNode;
        const auto consider = [&](NodeId child) {
            if (child == kNoNode || next != kNoNode) return;
            const Node& c = ast.at(child);
            if (c.begin <= offset && offset < c.end) next = child;
        };
        for (NodeId child : {n.a, n.b, n.c}) consider(child);
        const NodeId* list = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) consider(list[k]);
        if (next == kNoNode) return at != root;
        at = next;
    }
}

} // namespace

bool earlier_compile_error(const LinkedUnit& link, const std::filesystem::path& source_path,
                           const std::string& source, std::vector<InvocationBinding>* invocation,
                           detail::Stats& stats, std::filesystem::path& report_path) {
    if (!link.program_error || !link.prefix_link || !link.prefix_ast) return false;
    const LinkedUnit& pre = *link.prefix_link;
    Ast linked_ast(pre.linked ? std::string_view(pre.text) : std::string_view());
    NodeId root = link.prefix_root;
    if (pre.linked) {
        const ParseResult relinked = parse_script(linked_ast);
        if (!relinked.ok) return false;
        root = relinked.root;
    }
    const Ast& ast = pre.linked ? linked_ast : *link.prefix_ast;
    CompileLimits limits;
    limits.module_names = pre.module_names;
    limits.link = &pre;
    cooperative_switches(limits);
    limits.invocation = invocation;
    if (invocation)
        locate_invocation_shadows(*invocation, ast, root,
                                  [&](std::uint32_t at) { return !pre.linked || pre.locate(at).module == 0; });
    const CompileResult compiled = compile_unit(ast, root, limits);
    const std::string& r = compiled.reason;
    if (compiled.program || !compiled.positioned ||
        (r.rfind("static-error:", 0) != 0 && r.rfind("invocation-assign:", 0) != 0))
        return false;
    const LinkedUnit::Location where = pre.linked ? pre.locate(compiled.offset)
                                                  : LinkedUnit::Location{0, compiled.offset};
    const Ast& unit = where.module == 0 ? *link.prefix_ast : *pre.graph.modules[where.module]->ast;
    const NodeId unit_root = where.module == 0 ? link.prefix_root : pre.graph.modules[where.module]->root;
#if !(defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 21)  // test-only (packages/mutants.sh)
    if (!unconditional_at(unit, unit_root, static_cast<std::uint32_t>(where.offset))) return false;
#endif
    (void)unit;
    (void)unit_root;
    const Where w = where.module == 0 ? Where{source_path, &source}
                                      : Where{pre.graph.modules[where.module]->path,
                                              &pre.graph.modules[where.module]->source};
    report_path = w.path;
    const Location l = locate(*w.source, static_cast<std::uint32_t>(where.offset));
    stats.reason = pre.display(r);
    stats.line = l.line;
    stats.column = l.column;
    stats.positioned = true;
    return true;
}

void describe_link_refusal(const LinkedUnit& link, const std::filesystem::path& source_path, const std::string& source,
                           detail::Stats& stats, std::filesystem::path& report_path) {
    stats.reason = link.reason;
    const Where w = unit_of(link, link.where, source_path, source);
    report_path = w.path;
    const Location l = locate(*w.source, link.offset);
    stats.line = l.line;
    stats.column = l.column;
    stats.positioned = link.positioned;  // set by the link/graph site that recorded the offset
    if (link.program_error) {
        const GraphError& e = link.graph.error;
        stats.program_message = e.message;
        stats.program_note = "note: the program was not executed: its imports are resolved before it runs, so no "
                             "part of it ran\n";
        if (e.return_note)
            stats.program_note += "note: the `return` is at " +
                                  at(link, e.return_where, e.return_offset, source_path, source) + "\n";
        if (e.early_return)
            stats.program_note += "note: a top-level `return` at " +
                                  at(link, e.early_where, e.early_offset, source_path, source) +
                                  " comes before this; legacy would stop there without reaching it" +
                                  (e.early_where == 0 ? " (exit 0)" : "") + "\n";
        return;
    }
    if (!link.message.empty()) {  // a link-level program error with legacy's text
        stats.program_message = link.message;
        stats.program_note = "note: the program was not executed: no part of it ran\n";
        return;
    }
    if (link.reason == "import-scope:invocation-init") {  // a program error legacy fails on when it runs
        std::string name = "args";
        if (link.offset + 3 <= w.source->size() && w.source->compare(link.offset, 3, "cmd") == 0) name = "cmd";
        stats.program_message = "an imported module reads the entry script's `" + name +
                                "` while it initialises; `args` and `cmd` are visible to its functions only after "
                                "the import";
        stats.program_note = "note: the program was not executed: no part of it ran\n";
        if (link.note) {
            const Where n = unit_of(link, link.note_where, source_path, source);
            stats.program_note += "note: reached while " + n.path.generic_string() + " initialises, at " +
                                  at(link, link.note_where, link.note_offset, source_path, source) + "\n";
        }
    }
}

} // namespace nift::rt
