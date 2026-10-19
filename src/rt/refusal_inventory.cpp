#include "rt/refusal_inventory.h"

#include "rt/compiler.h"
#include "rt/globals.h"
#include "rt/legacy_names.h"
#include "rt/parser.h"
#include "rt/refusal_mask.h"
#include "rt/unit_check.h"

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

// CP12b: free names refined by the globals analysis (rt/globals.h) on the
// unmasked unit: `free-name:global` when G1-G3 hold, else its refusal. The
// function list is taken in program order (functions are hoisted, as
// plan_unit requires).
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

} // namespace

RefusalInventory inventory_refusals(std::string_view source, std::size_t max_rounds) {
    RefusalInventory inventory;
    Ast ast(source);
    const ParseResult parsed = parse_script(ast);
    if (!parsed.ok) {
        inventory.items.push_back({"parse-error", parsed.error.offset, false, {}});
        inventory.complete = false;
        inventory.stop = "parse-error";
        return inventory;
    }
    inventory::Masker masker(ast, parsed.root);
    const FreeNames free_names(source);
    // Opaque nodes first, in the runner's order (pre-order), each masked.
    for (;;) {
        const NodeId opaque = first_opaque_node(ast, parsed.root);
        if (opaque == kNoNode) break;
        inventory.items.push_back(
            {std::string("opaque:") + opaque_reason_name(ast.at(opaque).reason), ast.at(opaque).begin, false, {}});
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
        const CompileResult compiled = compile_unit(ast, parsed.root);
        ++inventory.rounds;
        if (compiled.program) break;
        const bool consequential = masker.consequential(compiled.offset, compiled.reason);
        inventory.items.push_back(
            {free_names.refine(compiled.reason, compiled.offset), compiled.offset, consequential, {}});
        if (terminal(compiled.reason)) {
            inventory.complete = false;
            inventory.stop = compiled.reason;
            break;
        }
        const bool masked = masker.mask_at(compiled.offset, compiled.reason);
        inventory.items.back().detail = masker.detail();
        for (std::string& extra : masker.take_extra())
            inventory.items.push_back({std::move(extra), compiled.offset, false, {}});
        if (!masked) {
            inventory.complete = false;
            inventory.stop = "unmaskable:" + compiled.reason;
            break;
        }
    }
    return inventory;
}

} // namespace nift::rt
