#include "rt/session_inventory.h"
#include "rt/refusal_mask.h"
#include "rt/script_runner_stats.h"
#include "rt/unit_check.h"
// rt-lint: cold-file session classify-all tooling, never execution or namespace publication
namespace nift::rt {
std::string session_inventory_json(Ast ast, NodeId root, bool parsed,
                                   const SessionCompileContext& context, const SourceMap& source,
                                   bool expression, std::size_t max_rounds) {
    std::string reasons = ",\"reasons\":[", stop;
    std::size_t rounds = 0, count = 0;
    bool complete = false;
    inventory::Masker masker(ast, root);
    while (rounds < max_rounds) {
        ++rounds;
        std::string reason;
        std::uint32_t offset = 0;
        const auto opaque = parsed ? first_opaque_node(ast, root) : kNoNode;
        if (!parsed) reason = "session:parse";
        else if (opaque != kNoNode) {
            reason = "opaque:" + std::string(opaque_reason_name(ast.at(opaque).reason));
            offset = ast.at(opaque).begin;
        } else {
            auto trial = context;  // every retry begins with the original private namespace
            CompileLimits limits; limits.session = &trial;
            const auto compiled = expression ? compile_eval_program(ast, root, limits)
                                             : compile_unit(ast, root, limits);
            if (compiled.program) { complete = true; break; }
            reason = compiled.reason; offset = compiled.offset;
        }
        const auto where = source.at(offset);
        const bool consequential = parsed && masker.consequential(offset, reason);
        auto add = [&](const std::string& label, const std::string& annotation) {
            reasons += (count++ ? ",{" : "{") + std::string("\"reason\":\"") + detail::json_escape(label) +
                "\",\"line\":" + std::to_string(where.line) + ",\"column\":" + std::to_string(where.column) +
                ",\"path\":\"" + detail::json_escape(where.path) + "\",\"consequential\":" +
                (consequential ? "true" : "false") +
                (annotation.empty() ? std::string() : ",\"detail\":\"" + detail::json_escape(annotation) + "\"") + "}";
        };
        const bool terminal = !parsed || reason == "nesting-limit" || reason.rfind("limit:", 0) == 0 ||
            reason.rfind("analysis-limit:", 0) == 0;
        if (terminal) { add(reason, ""); stop = reason; break; }
        const bool masked = opaque != kNoNode ? masker.mask_opaque(opaque) : masker.mask_at(offset, reason);
        add(reason, masker.detail());
        for (const auto& extra : masker.take_extra()) add(extra, masker.detail());
        if (!masked) { stop = "unmaskable:" + reason; break; }
    }
    if (!complete && stop.empty()) stop = "round-limit";
    return reasons + "],\"inventory_complete\":" + (complete ? "true" : "false") +
        ",\"inventory_stop\":\"" + detail::json_escape(stop) + "\",\"inventory_rounds\":" + std::to_string(rounds);
}
} // namespace nift::rt
