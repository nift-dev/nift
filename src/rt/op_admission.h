#pragma once
// B4: the operation admission interface (docs/runtime-2/checkpoints/b4-dispatch.md).
//
// One table decides, for a built-in method call `receiver.name(args)`, how the
// compiler admits it from the receiver's inferred kinds:
//   Static   the kinds are proven: the call binds one native (CallNative);
//   Dynamic  the kinds are a closed union of plain families: the call is a
//            flagged CallNative (kDynamicDispatch) and the VM selects, from the
//            receiver's run-time family, the native of that family with the
//            same name, or raises legacy's ordinary error for that family
//            (dispatch_failure: the one place that fixes the text and the
//            error class);
//   Refuse   anything else: the existing refusal reason, before any effect.
// The kind inference types a Dynamic call from the same admission (the union of
// its candidates' results), so compiler and inference agree by construction.
//
// Run time (rt/vm_builtin.cpp): a flagged call site first checks that the
// receiver's run-time family is the spec's (the dispatch precondition) and on
// a mismatch re-dispatches. Unflagged sites are not checked: a native meeting
// a foreign receiver there reports its own internal failure (exit 70). The
// defect counter (dispatch_defect_count, NIFT_VM_STATS "dispatch_defects";
// fatal in checked builds) counts a flagged site meeting a family the
// admission did not admit there, and the B0 defensive paths (scalar_to_string,
// add_assign_slow), only. Review L2; the B5 prerequisite on unflagged sites: b4-dispatch.md.
#include "rt/dispatch_defect.h"
#include "rt/infer.h"
#include "rt/native.h"
#include "rt/natives/builtin.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nift::rt {

// kDynamicDispatch / kCallWindowMask: rt/native.h (CallNative's c operand).

// Legacy's failure for a receiver family that has no native of the name
// (probe table: b4-dispatch.md section 3; identical in loop code, function
// code and functions called from loops).
enum class DispatchFailure : std::uint8_t {
    None,         // the family has a native of that name
    Unsupported,  // "<name>: unsupported for this value" (string, number, array)
    Malformed,    // "unknown value or malformed expression: <call text>" (null, bool, object)
    Composite,    // "to_string: use stringify() for composite values" (array, object)
    Unrenderable, // interpolation's to_string on a value with no text form (B0's text by kind)
    Refuse,       // legacy's behaviour is context-dependent or not reproduced: not admitted
};

enum class Admission : std::uint8_t { Static, Dynamic, Refuse };

struct DispatchCandidate {
    KindSet kinds;                      // the receiver kinds this branch covers
    const natives::BuiltinSpec* spec;   // null for a failing family
    DispatchFailure failure;            // None when spec is set
};

struct MethodAdmission {
    Admission admission = Admission::Refuse;
    const natives::BuiltinSpec* spec = nullptr;  // Static: the native; Dynamic: the first candidate's
    std::vector<DispatchCandidate> branches;     // Dynamic: one per receiver family in the kinds
    bool fails() const;                          // some family fails at run time
};

// Dynamic admission of `.name(...)` (argc arguments) on a receiver of `kinds`
// that bind_method did not bind (MethodBinding::Receiver). Static binding stays
// bind_method's (unchanged). Collections: a union of map/set/stack kinds for
// the read-only natives every member family has (`collections`).
MethodAdmission admit_dynamic_method(KindSet kinds, std::string_view name, std::uint32_t argc);

// The receiver kinds of a plain natives family (Null, Bool, Number, String,
// Array, Object, Map, Set, Stack), or 0.
KindSet family_kinds(natives::Receiver family);

// ---- run time (rt/vm_builtin.cpp) ----

// Legacy's failure class for a receiver family with no native of `name`.
DispatchFailure dispatch_failure(natives::Receiver family, std::string_view name);
// B5: dispatch_failure, plus a struct instance (no built-in family) for a plain method name.
DispatchFailure tagged_dispatch_failure(const Value& receiver, natives::Receiver family, std::string_view name);
// Its message (call_text: the call's source text, for Malformed; receiver: the
// value, for Unrenderable).
std::string dispatch_failure_message(DispatchFailure failure, std::string_view name, std::string_view call_text,
                                     const Value* receiver = nullptr);
// Whether the error is catchable (the owner's ruling on invariant 4 is set
// here, and only here): provisionally legacy's class, an ordinary uncatchable
// error like every other native error.
bool dispatch_failure_catchable(DispatchFailure failure);

// dispatch_defect_count / note_dispatch_defect: rt/dispatch_defect.h.

} // namespace nift::rt
