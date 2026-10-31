#pragma once
// Private shared-entry reporting data; not a script-visible Error or VM API.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nift::rt { struct InvocationBinding; }

namespace nift::rt::detail {

struct Stats {
    std::string outcome;  // vm | vm-error | fallback | require-error | internal-error | compiled
    std::string reason;
    std::size_t line = 0, column = 0;
    // true only where a refusal site records a real source position; false:
    // line/column are not a recorded position (never shown as one). Never a
    // default of true: a site that forgets it reports no position (Review 8).
    bool positioned = false;
    double parse_us = 0, compile_us = 0, execute_us = 0;
    std::size_t protos = 0, instructions = 0, source_bytes = 0;
    bool beyond_legacy_depth = false;
    int executions = 0;         // times the unit started executing on the VM (0 or 1)
    int legacy_executions = 0;  // 1 when the unit was handed to legacy (before anything executed)
    // Evidence for the D-018 difference policies (rt/vm.h).
    std::uint64_t large_numbers = 0, large_in_loop_path = 0;
    bool error_in_loop_path = false;
    std::vector<std::uint64_t> large_lines, derived_lines;
    bool evidence_truncated = false;
    std::size_t heap_peak = 0, heap_live = 0;
    std::string path;
    std::string code;        // vm-error: the diagnostic code when the stop has one (CP15)
    bool catchable = false;  // vm-error: legacy raises it as a catchable Error
    bool error_origin = false;  // vm-error: the stop carries an Error origin
    std::string inventory_json;  // NIFT_VM=classify-all: ,"reasons":[...] etc.
    std::string coro_json;  // accepted CP22 coroutine statistics
    std::size_t heap_bytes = 0;  // live VM heap when the outcome is reported
};

std::string json_escape(const std::string& text);
void report(const Stats& stats);
// NIFT_VM=classify-all: every refusal reason of the unit (rt/refusal_inventory.h)
// into stats.inventory_json, compiled with the run's invocation bindings.
void add_inventory(const std::string& source, const std::filesystem::path& source_path, Stats& stats, bool eval,
                   const std::vector<InvocationBinding>* invocation);

} // namespace nift::rt::detail
