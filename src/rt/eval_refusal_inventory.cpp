#include "rt/eval_refusal_inventory.h"
#include "rt/compiler_eval.h"
#include "rt/parser.h"
#include "rt/refusal_mask.h"
#include "rt/unit_check.h"

// rt-lint: cold-file non-executing eval inventory tooling
namespace nift::rt {
RefusalInventory inventory_eval_refusals(std::string_view source, std::size_t max_rounds) {
    RefusalInventory out;
    Ast ast(source);
    const NodeId expression = parse_eval_expression(ast, 0, static_cast<std::uint32_t>(source.size()));
    normalize_eval_callbacks(ast, expression);
    const NodeId root = make_eval_program(ast, expression);
    inventory::Masker masker(ast, root);
    for (; out.rounds < max_rounds;) {
        ++out.rounds;
        const NodeId opaque = first_opaque_node(ast, root);
        std::string reason;
        std::uint32_t offset = 0;
        if (opaque != kNoNode) {
            reason = std::string("opaque:") + opaque_reason_name(ast.at(opaque).reason);
            offset = ast.at(opaque).begin;
        } else {
            const auto compiled = compile_eval_program(ast, root);
            if (compiled.program) return out;
            reason = compiled.reason;
            offset = compiled.offset;
        }
        const Location at = locate(source, offset);
        InventoryItem item;
        item.reason = reason;
        item.offset = offset;
        item.line = static_cast<std::uint32_t>(at.line);
        item.column = static_cast<std::uint32_t>(at.column);
        item.consequential = masker.consequential(offset, reason);
        out.items.push_back(std::move(item));
        if (reason == "nesting-limit" || reason.rfind("limit:", 0) == 0 ||
            reason.rfind("analysis-limit:", 0) == 0) {
            out.complete = false; out.stop = reason; return out;
        }
        const bool masked = opaque != kNoNode ? masker.mask_opaque(opaque) : masker.mask_at(offset, reason);
        out.items.back().detail = masker.detail();
        for (auto& extra : masker.take_extra()) {
            InventoryItem additional = out.items.back();
            additional.reason = std::move(extra);
            out.items.push_back(std::move(additional));
        }
        if (!masked) {
            out.complete = false; out.stop = "unmaskable:" + reason; return out;
        }
    }
    out.complete = false;
    out.stop = "round-limit";
    return out;
}
} // namespace nift::rt
