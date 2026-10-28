#include "rt/vm_session.h"
#include <algorithm>
#if defined(NIFT_SESSION_MUTANT) && NIFT_SESSION_MUTANT == 3
#include "rt/source_map.h"
#endif
#include <stdexcept>

// rt-lint: cold-file session unit entry and temporary execution teardown
namespace nift::rt {
void Vm::retire_session_slots(const std::vector<std::uint16_t>& slots) {
    if (!frames_.empty() || closure_) throw std::logic_error("cannot retire active session code");
    for (const auto slot : slots) {
        if (slot < named_functions_.size()) named_functions_[slot] = Value();
        if (slot < protos_.size()) protos_[slot] = nullptr;
    }
}
void Vm::clear_session_execution() {
    // Values in this stack are command temporaries. Persistent cells live in
    // session_globals; Programs outlive all their closure values.
    closure_ = nullptr;
    frames_.clear();
    handlers_.clear();
    named_functions_.clear();  // canonical identities survive in the namespace, not in a stale code-slot cache
    for (auto& value : stack_) value = Value();
    global_taken_ = 0;
}
VmResult Vm::run_session_unit(const Program& unit, const std::vector<const Proto*>& linked, std::string& output) {
    if (!options_.session_globals) throw std::logic_error("missing session namespace");
    clear_session_execution();
    protos_ = linked;
    protos_.front() = &unit.main();
#if defined(NIFT_SESSION_MUTANT) && NIFT_SESSION_MUTANT == 3
    const auto newest = unit.main().session_source->at(unit.main().offsets.back());
    const auto wrong_map = std::make_shared<const SourceMap>(SourceMap::single(newest.path, ""));
    for (const auto* proto : linked) if (proto)
        const_cast<Proto*>(proto)->session_source = wrong_map;  // M3: latest command for every old origin
#endif
    named_functions_.resize(protos_.size());
    global_ready_.resize(options_.session_globals->size());
    for (std::size_t k = 0; k < global_ready_.size(); ++k)
        global_ready_[k] = !(*options_.session_globals)[k].is_null();
    program_has_collection_boundaries_ = unit.has_collection_boundaries;
    for (const auto* proto : linked)
        if (proto && proto->collection_entry) program_has_collection_boundaries_ = true;
    large_numbers_ = large_in_loop_path_ = lines_ = 0;
    large_lines_.clear(); derived_lines_.clear();
    evidence_truncated_ = beyond_legacy_depth_ = false;
    next_frame_check_ = std::min(kLegacyMaxDepth, options_.max_frames);
    const auto budget = options_.heap_budget;
    const auto storage = options_.session_globals->capacity() * sizeof(Value);
    options_.heap_budget = storage < budget ? budget - storage : 0;
    Fault budget_fault;
    VmResult result;
    if (!memory_exceeded(budget_fault)) {
        result.reason = budget_fault.reason; result.message = budget_fault.message;
    } else result = run(output);
    options_.heap_budget = budget;
    update_heap_limit();
    // D-056: an uncaught command abandons its temporaries, not persistent cells.
#if !defined(NIFT_SESSION_RESOURCE_MUTANT) || NIFT_SESSION_RESOURCE_MUTANT != 2
    if (result.reason != StopReason::None)
        for (auto& value : stack_) {
            if (value.is(Kind::Handle)) abandon_unwound_handle(value);
            value = Value();
        }
#endif
    clear_session_execution();
    if (handle_table_.reports_lost()) throw std::bad_alloc();
    const auto reports = result.reason == StopReason::None ?
        handle_table_.take_failures(handle_context_) : handles::CloseReport{};
    if (reports.cleanup_failed()) {
        result.reason = StopReason::Error;
        result.message = reports.failures.front().message;
        result.code = reports.failures.front().code;
        left_open_summary(reports, result);
    }
    return result;
}
} // namespace nift::rt
