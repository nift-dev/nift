#pragma once
// Opt-in execution of whole script programs on the Runtime 2.0 VM (CP11).
//
// Environment:
//   NIFT_VM=1        compile and run eligible scripts on the VM; everything
//                    else (parse errors, Opaque nodes, unsupported constructs,
//                    run-time bails) runs on the legacy engine unchanged.
//   NIFT_VM=require  as 1, but never use the legacy engine: an ineligible
//                    script or a run-time bail is reported as an error
//                    (honest benchmarking; diagnostics are VM-native).
//   NIFT_VM_STATS=1  one JSON line on stderr per script: outcome, fallback or
//                    bail reason, parse/compile/execute microseconds.
//   NIFT_VM_STATS=<path>  the same line appended to <path> instead.
//   NIFT_VM_DUMP=1   the compiled bytecode on stderr.
// Unset (or 0), nothing here runs and nift behaves exactly as before.
#include <filesystem>
#include <string>

namespace nift::rt {

enum class VmMode { Off, On, Require };
VmMode vm_mode_from_environment();

// Returns true when the script was handled here (its exit status in
// `exit_code`); false when the caller must run it on the legacy engine.
// `source` is the program text after shebang removal; `source_path` is the
// path legacy diagnostics name.
bool run_script_on_vm(const std::string& source, const std::filesystem::path& source_path, int& exit_code);

} // namespace nift::rt
