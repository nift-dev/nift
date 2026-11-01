#include "rt/link_diagnostic.h"

#include "rt/parser.h"

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

} // namespace

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
