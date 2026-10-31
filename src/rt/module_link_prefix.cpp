#include "rt/module_link_prefix.h"

#include "rt/parser.h"
#include "rt/unit_check.h"

// rt-lint: cold-file compile-time linking, once per refused unit

namespace nift::rt {

namespace fs = std::filesystem;

// The statements before the entry statement a program error happens under,
// linked: an earlier invocation-init (a program error) replaces the graph's;
// the caller compiles the rest for an earlier compile-time program error.
void link_prefix(LinkedUnit& out, const Ast& entry, NodeId root, std::string_view source, const fs::path& path,
                 const std::vector<std::string_view>& names) {
    const Node& program = entry.at(root);
    const std::uint32_t item = out.graph.error.entry_item;
    if (item == 0 || item >= program.count) return;
#if defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 20  // test-only: no prefix (packages/mutants.sh)
    return;
#endif
    std::string text(source);
    for (std::size_t k = entry.at(entry.list(program)[item]).begin; k < text.size(); ++k)
        if (text[k] != '\n') text[k] = ' ';
    out.prefix_source = std::move(text);
    auto ast = std::make_shared<Ast>(out.prefix_source);
    const ParseResult parsed = parse_script(*ast);
    if (!parsed.ok || first_opaque_node(*ast, parsed.root) != kNoNode) { out.prefix_source.clear(); return; }
    auto pre = std::make_shared<LinkedUnit>(link_unit(*ast, parsed.root, out.prefix_source, path, names));
    if (pre->linked && !pre->ok) {
        if (pre->reason == "import-scope:invocation-init") {  // earlier in execution order: it wins
            out.program_error = false;
            out.reason = pre->reason;
            out.where = pre->where;
            out.offset = pre->offset;
            out.positioned = pre->positioned;
            out.note = pre->note;
            out.note_where = pre->note_where;
            out.note_offset = pre->note_offset;
            out.graph.modules.swap(pre->graph.modules);  // positions refer to the prefix's modules
        }
        out.prefix_source.clear();
        return;
    }
    out.prefix_ast = std::move(ast);
    out.prefix_root = parsed.root;
    out.prefix_link = std::move(pre);
}

} // namespace nift::rt
