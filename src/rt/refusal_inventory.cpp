#include "rt/refusal_inventory.h"

#include "rt/compiler.h"
#include "rt/globals.h"
#include "rt/legacy_names.h"
#include "rt/module_host_op.h"
#include "rt/module_link.h"
#include "rt/parser.h"
#include "rt/refusal_mask.h"
#include "rt/unit_check.h"

#include <algorithm>
#include <cctype>

// rt-lint: cold-file test tooling (NIFT_VM=classify-all), never on an execution path

namespace nift::rt {

namespace {

bool starts_with(const std::string& text, std::string_view prefix) {
    return text.size() >= prefix.size() && std::string_view(text).substr(0, prefix.size()) == prefix;
}

// Unit-level refusals: masking cannot get past them.
bool terminal(const std::string& reason) {
    return reason == "nesting-limit" || reason == "too-many-functions" || starts_with(reason, "analysis-limit:") ||
           starts_with(reason, "limit:");
}

// The source line containing `offset` (at most 160 bytes), for reports.
std::string line_text(std::string_view source, std::uint32_t offset) {
    const std::size_t begin = source.rfind('\n', offset == 0 ? 0 : offset - 1);
    const std::size_t from = begin == std::string_view::npos || offset == 0 ? 0 : begin + 1;
    const std::size_t end = source.find('\n', offset);
    const std::size_t count =
        std::min<std::size_t>((end == std::string_view::npos ? source.size() : end) - from, 160);
    return std::string(source.substr(from, count));
}

// CP12b: free names refined by the globals analysis (rt/globals.h) on the
// compiled (linked) unit: `free-name:global` when G1-G3 hold, else its
// refusal. The function list is taken in program order (functions are
// hoisted, as plan_unit requires).
class FreeNames {
public:
    explicit FreeNames(std::string_view source) : ast_(source) {
        const ParseResult parsed = parse_script(ast_);
        if (!parsed.ok) return;
        const Node& program = ast_.at(parsed.root);
        for (std::uint32_t k = 0; k < program.count; ++k) {
            const NodeId id = ast_.list(program)[k];
            const Node& fn = ast_.at(id);
            if (fn.kind != NodeKind::FnDecl || fn.a == kNoNode || plan_.index.count(ast_.text(ast_.at(fn.a))))
                continue;
            plan_.functions.push_back(id);
            plan_.index.emplace(ast_.text(ast_.at(fn.a)), static_cast<std::uint16_t>(plan_.functions.size()));
        }
        globals_ = plan_globals(ast_, parsed.root, plan_);
        ok_ = true;
    }
    std::string refine(const std::string& reason, std::uint32_t offset) const {
        if (!ok_) return reason;
        if (reason == "unresolved-name") {
            std::size_t end = offset;
            const std::string_view src = ast_.source();
            while (end < src.size() && (std::isalnum(static_cast<unsigned char>(src[end])) || src[end] == '_')) ++end;
            return legacy_special_name(src.substr(offset, end - offset)) ? reason + ":legacy-binding" : reason;
        }
        if (reason != "free-name" && reason != "assign-free-name") return reason;
        for (std::size_t f = 0; f < plan_.functions.size(); ++f) {
            const Node& fn = ast_.at(plan_.functions[f]);
            if (offset < fn.begin || offset >= fn.end) continue;
            std::size_t end = offset;
            const std::string_view src = ast_.source();
            while (end < src.size() && (std::isalnum(static_cast<unsigned char>(src[end])) || src[end] == '_')) ++end;
            const auto ref = globals_.free[f].find(src.substr(offset, end - offset));
            if (ref == globals_.free[f].end()) return reason + ":unanalysed";
            return ref->second.ok ? reason + ":global" : reason + ref->second.refusal.substr(9);
        }
        return reason;
    }

private:
    Ast ast_;
    UnitPlan plan_;
    GlobalPlan globals_;
    bool ok_ = false;
};

struct MappedSource {
    std::string_view source;
    std::string path;  // empty: the entry unit
    std::uint32_t offset = 0;
};

} // namespace

RefusalInventory inventory_refusals(std::string_view source, const std::filesystem::path& source_path,
                                    std::size_t max_rounds) {
    RefusalInventory inventory;
    Ast entry(source);
    const ParseResult parsed = parse_script(entry);
    if (!parsed.ok) {
        const Location where = locate(source, parsed.error.offset);
        inventory.items.push_back({"parse-error", parsed.error.offset, false, {}, {},
                                   static_cast<std::uint32_t>(where.line),
                                   static_cast<std::uint32_t>(where.column), line_text(source, parsed.error.offset)});
        inventory.complete = false;
        inventory.stop = "parse-error";
        return inventory;
    }

    const auto record = [&](std::string reason, MappedSource at, bool consequential, std::string detail) {
        const Location where = locate(at.source, at.offset);
        inventory.items.push_back({std::move(reason), at.offset, consequential, std::move(detail), std::move(at.path),
                                   static_cast<std::uint32_t>(where.line),
                                   static_cast<std::uint32_t>(where.column), line_text(at.source, at.offset)});
    };
    const auto record_entry = [&](std::string reason, std::uint32_t offset, bool consequential, std::string detail) {
        record(std::move(reason), MappedSource{source, {}, offset}, consequential, std::move(detail));
    };

    // run_unit checks the entry's opaque nodes before linking, so the
    // inventory does the same. Masking can still find later blockers, but
    // those are outside the linked pipeline that classify would reach.
    const bool entry_opaque = first_opaque_node(entry, parsed.root) != kNoNode;

    // Same front end as run_unit (rt/script_runner.cpp): link static imports,
    // then parse and compile the linked text. The mutant build deliberately
    // inventories the unlinked source, to keep the regression sensitive.
    LinkedUnit link;
#ifdef NIFT_INVENTORY_MUTANT
    const bool use_link = false;
#else
    const bool use_link = true;
#endif
    if (!entry_opaque && use_link) {
        link = link_unit(entry, parsed.root, source, source_path);
    }

    if (link.linked && !link.ok) {
        MappedSource at;
        if (link.where == 0) {
            at = {source, {}, link.offset};
        } else {
            const Module& module = *link.graph.modules[link.where];
            at = {module.source, module.path.generic_string(), static_cast<std::uint32_t>(link.offset)};
        }
        record(link.reason, at, false, {});
        inventory.complete = false;
        inventory.stop = link.reason;
        return inventory;
    }

    std::string_view unit_source = source;
    std::unique_ptr<Ast> linked_ast;
    NodeId unit_root = parsed.root;
    CompileLimits limits;
    if (link.linked) {
        linked_ast = std::make_unique<Ast>(link.text);
        const ParseResult relinked = parse_script(*linked_ast);
        if (!relinked.ok || first_opaque_node(*linked_ast, relinked.root) != kNoNode) {
            // run_unit's invariant: every module parsed alone, so linked text
            // parses too. Its error is the entry's `import-link` refusal.
            record_entry("import-link", 0, false, {});
            inventory.complete = false;
            inventory.stop = "import-link";
            return inventory;
        }
        unit_source = link.text;
        unit_root = relinked.root;
        limits.module_names = link.module_names;
    }
    Ast& ast = link.linked ? *linked_ast : entry;
    const auto map_linked = [&](std::uint32_t offset) -> MappedSource {
        if (!link.linked) return {source, {}, offset};
        const LinkedUnit::Location at = link.locate(offset);
        if (at.module == 0) return {source, {}, static_cast<std::uint32_t>(at.offset)};
        const Module& module = *link.graph.modules[at.module];
        return {module.source, module.path.generic_string(), static_cast<std::uint32_t>(at.offset)};
    };
    const auto record_linked = [&](std::string reason, std::uint32_t offset, bool consequential, std::string detail) {
        record(std::move(reason), map_linked(offset), consequential, std::move(detail));
    };

    inventory::Masker masker(ast, unit_root);
    const FreeNames free_names(unit_source);
    // Opaque nodes first, in the runner's order (pre-order), each masked.
    for (;;) {
        const NodeId opaque = first_opaque_node(ast, unit_root);
        if (opaque == kNoNode) break;
        record_linked(std::string("opaque:") + opaque_reason_name(ast.at(opaque).reason),
                      ast.at(opaque).begin, false, {});
        if (inventory.items.size() >= max_rounds || !masker.mask_opaque(opaque)) {
            inventory.complete = false;
            inventory.stop = "opaque-limit";
            return inventory;
        }
    }
    for (;;) {
        if (inventory.rounds >= max_rounds) {
            inventory.complete = false;
            inventory.stop = "round-limit";
            break;
        }
        const CompileResult compiled = compile_unit(ast, unit_root, limits);
        ++inventory.rounds;
        if (compiled.program) {
            // R1: run_unit refuses a linked unit that can call a host
            // built-in after it compiles (rt/module_host_op.h); the inventory
            // reports the same first reason. The unit compiled, so there are
            // no later compile blockers: the inventory stays complete.
#if !defined(NIFT_R1_MUTANT) && !defined(NIFT_INVENTORY_HOST_MUTANT)  // the latter: test-only control
            std::string host_reason;
            std::uint32_t host_offset = 0;
            if (link.linked && first_module_host_op(*compiled.program, host_reason, host_offset))
                record_linked(std::move(host_reason), host_offset, false, {});
#endif
            break;
        }
        const bool consequential = masker.consequential(compiled.offset, compiled.reason);
        record_linked(free_names.refine(compiled.reason, compiled.offset), compiled.offset, consequential, {});
        if (terminal(compiled.reason)) {
            inventory.complete = false;
            inventory.stop = compiled.reason;
            break;
        }
        const bool masked = masker.mask_at(compiled.offset, compiled.reason);
        inventory.items.back().detail = masker.detail();
        for (std::string& extra : masker.take_extra())
            record_linked(std::move(extra), compiled.offset, false, {});
        if (!masked) {
            inventory.complete = false;
            inventory.stop = "unmaskable:" + compiled.reason;
            break;
        }
    }
    return inventory;
}

} // namespace nift::rt
