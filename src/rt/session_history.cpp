#include "rt/session_history.h"
#include "rt/session_shape.h"
#include <algorithm>
#include <unordered_set>
// rt-lint: cold-file dormant proof facts; no executable Values or Program ownership
namespace nift::rt {
namespace {
using TyRef = infer_detail::TyRef;
const std::string* source_identity(const std::shared_ptr<const std::string>& source) {
#if defined(NIFT_SESSION_ADMISSION_MUTANT) && NIFT_SESSION_ADMISSION_MUTANT == 19
    return nullptr;
#else
    return source.get();
#endif
}
std::uint64_t span(std::uint32_t begin, std::uint32_t end) { return (std::uint64_t(begin) << 32) | end; }
bool contains_function(const TyRef& type) {
    if (!type) return false;
    if (type->kinds & kKindFunction) return true;
    if (contains_function(type->elem)) return true;
    for (const auto& field : type->fields) if (contains_function(field.second)) return true;
    for (const auto& item : type->items) if (contains_function(item)) return true;
    return false;
}
bool needs_facts(const Proto& proto) {
    for (const auto& type : proto.session_parameter_shapes) if (contains_function(type)) return true;
    for (const auto& type : proto.session_capture_shapes) if (contains_function(type)) return true;
    return false;
}
}
bool session_staging_covered(const std::shared_ptr<const SessionCallableShape>& shape) {
    if (!shape || shape->complete) return true;
    const auto targets = session_callable_targets(shape);
    if (targets.empty()) return false;
    for (const auto& target : targets)
        for (const auto& capture : target.captures)
            if (capture && !capture->complete && !target.call_facts) return false;
    return true;
}
std::string validate_session_history(const UnitKinds& kinds, std::size_t main) {
    if (!kinds.session_proof) return {};
    const auto& proof = *kinds.session_proof;
    if (!proof.unproved_current_calls.empty()) return "session:current-callable-proof";
    if (proof.historical_calls.empty()) return {};
    std::unordered_set<std::size_t> reachable;
    std::vector<std::size_t> pending{main};
    while (!pending.empty()) {
        const auto unit = pending.back(); pending.pop_back();
        if (!reachable.insert(unit).second) continue;
        const auto edges = proof.call_edges.find(unit);
        if (edges != proof.call_edges.end())
            for (auto target : edges->second) pending.push_back(target);
    }
#if !defined(NIFT_SESSION_ADMISSION_MUTANT) || (NIFT_SESSION_ADMISSION_MUTANT != 15 && \
    NIFT_SESSION_ADMISSION_MUTANT != 17)
    for (const auto& use : proof.historical_owners)
        if (reachable.count(use.second)) return "session:active-historical-call";
#endif
    return {};
}
void prepare_session_history(const Ast& ast, const SessionText& text, const SessionCompileContext& current,
                             SessionCompileContext& pending) {
    pending.call_result_seeds.clear();
#if defined(NIFT_SESSION_ADMISSION_MUTANT) && NIFT_SESSION_ADMISSION_MUTANT == 14
    return;
#endif
    if (text.imports.empty()) return;
    std::unordered_map<const std::string*, std::unordered_map<std::uint64_t, TyRef>> facts;
    std::unordered_set<const void*> seen;
    for (const auto& symbol : current.symbols)
        for (const auto& target : session_callable_targets(symbol.callable_shape))
            if (target.call_facts && seen.insert(target.call_facts.get()).second)
                for (const auto& fact : *target.call_facts) {
                    auto& type = facts[source_identity(fact.source)][span(fact.begin, fact.end)];
                    type = infer_detail::unite(type, fact.result);
                }
    if (facts.empty()) return;
    std::vector<std::uint32_t> starts;
    std::uint32_t position = 0;
    for (const auto& piece : text.pieces) { starts.push_back(position); position += piece.length; }
    for (NodeId id = 0; id < ast.size(); ++id) {
        const auto& node = ast.at(id);
        if (node.kind != NodeKind::Call) continue;
        auto index = std::upper_bound(starts.begin(), starts.end(), node.begin);
        if (index == starts.begin()) continue;
        const auto at = static_cast<std::size_t>(index - starts.begin() - 1);
        const auto& piece = text.pieces[at];
        if (node.end > starts[at] + piece.length) continue;
        const auto source = facts.find(source_identity(piece.source));
        if (source == facts.end()) continue;
        const auto fact = source->second.find(span(node.begin - starts[at] + piece.source_begin,
                                                   node.end - starts[at] + piece.source_begin));
        if (fact != source->second.end()) pending.call_result_seeds.emplace(span(node.begin, node.end), fact->second);
    }
}
void record_session_history(const Ast& ast, const UnitKinds& kinds, const SessionCompileContext& context,
                            Program& program) {
    if (std::none_of(program.protos.begin(), program.protos.end(),
        [](const auto& proto) { return needs_facts(*proto); })) return;
    auto facts = std::make_shared<std::vector<SessionCallFact>>();
    for (NodeId id = 0; id < ast.size(); ++id) if (ast.at(id).kind == NodeKind::Call) {
        const auto type = kinds.session_shapes.find(id);
        if (type != kinds.session_shapes.end() && session_proved_shape(type->second))
            facts->push_back({context.source_owner, ast.at(id).begin, ast.at(id).end, session_shape(type->second)});
    }
    // Transient staging only; map_session_history distributes facts by definition.
    program.protos.front()->session_call_facts = std::move(facts);
}
void map_session_history(const SessionText& text, Program& program) {
    if (program.protos.empty() || !program.protos.front()->session_call_facts) return;
    std::vector<SessionCallFact> mapped;
    for (const auto& fact : *program.protos.front()->session_call_facts) {
        std::uint32_t position = 0;
        for (const auto& piece : text.pieces) {
            if (fact.begin >= position && fact.end <= position + piece.length) {
                mapped.push_back({piece.source, fact.begin - position + piece.source_begin,
                    fact.end - position + piece.source_begin, fact.result});
                break;
            }
            position += piece.length;
        }
    }
    program.protos.front()->session_call_facts.reset();
    for (auto& proto : program.protos) {
        const auto& definition = proto->session_definition;
        if (!definition || !needs_facts(*proto)) continue;
        auto facts = std::make_shared<std::vector<SessionCallFact>>();
        for (const auto& fact : mapped)
            if (fact.source == definition->source && fact.begin >= definition->begin && fact.end <= definition->end)
                facts->push_back(fact);
        {
            proto->session_call_facts = std::move(facts);
#if defined(NIFT_SESSION_ADMISSION_MUTANT) && NIFT_SESSION_ADMISSION_MUTANT == 20
            static std::vector<std::shared_ptr<const std::vector<SessionCallFact>>> leaked;
            leaked.push_back(proto->session_call_facts);
#endif
        }
    }
}
}
