#pragma once
// The diagnostic of a unit the linker did not admit (rt/module_link.h): its
// reported file and position, and for program errors (D-NEXT-packages:
// graph errors, import-scope:invocation-init) legacy's text and notes. Kept
// out of script_runner.cpp so that file stays small.
#include "rt/module_link.h"
#include "rt/script_runner_stats.h"

#include <filesystem>
#include <string>
#include <vector>

namespace nift::rt {

struct InvocationBinding;

void describe_link_refusal(const LinkedUnit& link, const std::filesystem::path& source_path, const std::string& source,
                           detail::Stats& stats, std::filesystem::path& report_path);

// A program error found by the graph: compiles the entry's statements before
// the one it happens under (LinkedUnit::prefix_link) and, when that finds a
// compile-time program error (static-error:*, invocation-assign:*), which is
// earlier in execution order, fills `stats`/`report_path` with it and returns
// true (it is reported instead).
bool earlier_compile_error(const LinkedUnit& link, const std::filesystem::path& source_path,
                           const std::string& source, std::vector<InvocationBinding>* invocation,
                           detail::Stats& stats, std::filesystem::path& report_path);

} // namespace nift::rt
