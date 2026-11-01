#pragma once
// The diagnostic of a unit the linker did not admit (rt/module_link.h): its
// reported file and position, and for program errors (D-NEXT-packages:
// graph errors, import-scope:invocation-init) legacy's text and notes. Kept
// out of script_runner.cpp so that file stays small.
#include "rt/module_link.h"
#include "rt/script_runner_stats.h"

#include <filesystem>
#include <string>

namespace nift::rt {

void describe_link_refusal(const LinkedUnit& link, const std::filesystem::path& source_path, const std::string& source,
                           detail::Stats& stats, std::filesystem::path& report_path);

} // namespace nift::rt
