#include "rt/vm_session.h"
#include <algorithm>
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
    for (auto& value : stack_) value = Value();
    global_taken_ = 0;
}
VmResult Vm::run_session_unit(const Program& unit, const std::vector<const Proto*>& linked, std::string& output) {
    if (!options_.session_globals) throw std::logic_error("missing session namespace");
    clear_session_execution();
    protos_ = linked;
    protos_.front() = &unit.main();
#if defined(NIFT_SESSION_MUTANT) && NIFT_SESSION_MUTANT == 3
    for (const auto* proto : linked) if (proto)
        const_cast<Proto*>(proto)->session_source = unit.main().session_source;  // M3: newest source for old code
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
    auto result = run(output);
    clear_session_execution();
    return result;
}
} // namespace nift::rt
