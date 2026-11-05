#pragma once
// B4 (docs/runtime-2/checkpoints/b4-dispatch.md): the dispatch-defect counter,
// without the admission interface's compiler dependencies (defined in
// rt/op_admission.cpp). A defect is a method native, or an operator's
// defensive path, meeting a receiver an unflagged site promised away; counted
// process-wide (NIFT_VM_STATS "dispatch_defects"), fatal in checked builds.
// Supporting evidence only: mutant A5 and the wrong-kind generators are the
// primary evidence that no internal failure is reachable.
#include <cstdint>

namespace nift::rt {
std::uint64_t dispatch_defect_count();
void note_dispatch_defect();
} // namespace nift::rt
