#include "rt/script_runner_options.h"
#include <cstdlib>
// rt-lint: cold-file shared process-level VM limit parsing
namespace nift::rt::detail {
// Test-only knobs: NIFT_VM_HEAP_BUDGET=<bytes> lowers the memory budget
// (review round 2 R5 tests); NIFT_VM_FLUSH=<bytes> the streaming threshold.
static bool size_from_environment(const char* name, std::size_t& out) {
    const char* value = std::getenv(name);
    if (!value || !*value) return false;
    char* end = nullptr;
    const unsigned long long limit = std::strtoull(value, &end, 10);
    if (!end || *end != '\0' || limit == 0) return false;
    out = static_cast<std::size_t>(limit);
    return true;
}

void limits_from_environment(VmOptions& options) {
    size_from_environment("NIFT_VM_HEAP_BUDGET", options.heap_budget);
    size_from_environment("NIFT_VM_FLUSH", options.flush_threshold);
}

} // namespace nift::rt::detail
