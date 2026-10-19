#pragma once
// Opt-in execution of whole script programs on the Runtime 2.0 VM (CP11).
//
// Environment:
//   NIFT_VM=1        compile and run eligible scripts on the VM; everything
//                    else (parse errors, Opaque nodes, unsupported constructs)
//                    runs on the legacy engine unchanged. The choice is made
//                    before anything executes and is final (CP12, D-013): a
//                    unit runs entirely on one engine, exactly once; program
//                    errors on the VM are reported by the VM as legacy would.
//   NIFT_VM=require  as 1, but never use the legacy engine: an ineligible
//                    script is reported as an error (honest benchmarking).
//   NIFT_VM=classify test tooling: parse and compile only, report the outcome
//                    (`compiled` or `fallback`, NIFT_VM_STATS) and exit 0
//                    without running the script on either engine.
//   NIFT_VM=classify-all  as classify, and the statistics line also lists every
//                    refusal reason the unit would hit (`reasons`, CP12 phase B
//                    inventory, rt/refusal_inventory.h); outcome and reason
//                    are classify's. Nothing executes.
//   NIFT_VM_FLUSH=<bytes>  test-only: the output streaming threshold (64 KiB).
//   An unexpected VM failure (internal error, std::bad_alloc, any C++
//   exception) is reported as "NIFT_VM internal failure" with exit status 70;
//   it is never disguised as a legacy run.
//   NIFT_VM_STATS=1  one JSON line on stderr per script: outcome, fallback
//                    reason, parse/compile/execute microseconds, how often the
//                    unit executed on each engine (vm_executions,
//                    legacy_executions: 0 or 1, never both).
//   NIFT_VM_STATS=<path>  the same line appended to <path> instead.
//   NIFT_VM_DUMP=1   the compiled bytecode on stderr.
// Unset (or 0), nothing here runs and nift behaves exactly as before.
#include <filesystem>
#include <string>

namespace nift::rt {

enum class VmMode { Off, On, Require, Classify, ClassifyAll };
VmMode vm_mode_from_environment();

// Returns true when the script was handled here (its exit status in
// `exit_code`); false when the caller must run it on the legacy engine.
// `source` is the program text after shebang removal; `source_path` is the
// path legacy diagnostics name.
bool run_script_on_vm(const std::string& source, const std::filesystem::path& source_path, int& exit_code);

} // namespace nift::rt
