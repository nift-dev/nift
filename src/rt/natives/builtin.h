#pragma once
// Built-in natives of Runtime 2.0 (CP14): the call convention every built-in
// family shares and the static description the compiler binds against.
//
// Call convention (docs/runtime-2/checkpoints/cp14-builtins-design.md):
//   * A native is a plain function over typed rt::Values. Methods receive the
//     receiver as args[0]; the declared arguments follow. There is no source
//     text, no re-parsing and no string conversion of arguments.
//   * Arguments are borrowed for the duration of the call. A pure native never
//     mutates them and never keeps a reference into them after it returns
//     (CP09 reference contract). Results are built in fresh values and stored
//     into `out` last, so `out` may alias an argument register.
//   * Status::Error is a program error: `error` holds legacy's message, the VM
//     reports it at the call's source offset (CP12 error path, StopReason::
//     Error). Status::Internal means the native met an argument the compiler
//     promised it would never pass (a compiler defect; exit 70, never guessed).
//     Status::Memory means the heap budget in the context was exceeded while
//     building the result (the VM's memory_exceeded path).
//     Status::Recoverable (CP15) is a program error legacy raises as a
//     *catchable* Error value (e.g. `validate` rejecting an instance:
//     code "schema.rejected"); `error` holds the message and
//     recoverable_code(id) names the code. Until CP13's try/catch the VM treats
//     it as Error (uncaught legacy behaviour is identical).
//     Status::Limit (CP15 review R2) is a fatal, uncatchable resource-limit
//     error, code "limit.exceeded" (D-022/D-025), e.g. a value nested deeper
//     than 512 levels given to validate.
//   * Every heap object a native creates goes through the CP09 constructors
//     (make_string, make_array, array_push, object_set), so the live-heap total
//     stays exact (heap_account.h).
#include "rt/handles/native_access.h"
#include "rt/host/host_call.h"
#include "rt/value.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace nift::rt::natives {

enum class Status : std::uint8_t { Ok, Error, Internal, Memory, Recoverable, Limit };

// Per-call context. heap_limit bounds the live heap the native may grow to;
// kNoHeapLimit (the default, for unit tests and host calls without a budget)
// disables the check. 0 means no room at all: the VM passes 0 when its
// register and frame stacks already use the whole budget (C3 follow-up: 0 used
// to mean "disabled", so such calls ran unchecked).
struct Context {
    static constexpr std::size_t kNoHeapLimit = static_cast<std::size_t>(-1);
    std::size_t heap_limit = kNoHeapLimit;
    host::HostCallEnv host;  // trace/path policy; no observer for standalone scripts
    handles::HandleAccess handles;  // CP17: table + execution context for handle natives
    // N1: the code of the failing HostCall in this call (empty when the native
    // has no HostCall, e.g. validate's schema.rejected).
    std::string host_error_code;
    // Natives that build results proportional to more than their inputs check
    // the budget every kBudgetStride items.
    static constexpr std::size_t kBudgetStride = 1024;
    bool over_budget() const noexcept;
};

using NativeFn = Status (*)(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                            std::string& error);

// Kind sets the compiler's kind inference uses (CP12 rt/infer.h bit layout:
// null, bool, integral number, non-integral number, string, array, object),
// plus bytes (CP15), which CP12's 8-bit set has no bit for (its 0x80 is
// Unknown): wiring CP15 widens CP12's set (design record cp15, W8).
// Duplicated here, not included, so CP14 does not depend on CP12's in-flight
// headers; the wiring patch static_asserts that both layouts agree.
using KindSet = std::uint16_t;
inline constexpr KindSet kNull = 1u << 0;
inline constexpr KindSet kBool = 1u << 1;
inline constexpr KindSet kInt = 1u << 2;
inline constexpr KindSet kFrac = 1u << 3;
inline constexpr KindSet kString = 1u << 4;
inline constexpr KindSet kArray = 1u << 5;
inline constexpr KindSet kObject = 1u << 6;
inline constexpr KindSet kNumber = kInt | kFrac;
inline constexpr KindSet kScalar = kNull | kBool | kNumber | kString;
inline constexpr KindSet kBytes = 1u << 7;
// CP17 W13: handle family (bit 11) and its file/stream refinements. Handles
// are not part of kAny: a native family must opt in explicitly.
inline constexpr KindSet kHandle = 1u << 11;
inline constexpr KindSet kFile = kHandle | (1u << 12);
inline constexpr KindSet kStream = kHandle | (1u << 13);
inline constexpr KindSet kAny = 0xff;

// The receiver a built-in is called on: a method of a string, array,
// object, number, bool or null, a method of any value (Any: the presentation
// methods), or a free function (None).
enum class Receiver : std::uint8_t { None, String, Array, Object, Number, Bool, Null, Any, Bytes, File, Stream };

// How the result kinds follow from the argument kinds (for kind inference).
enum class ResultRule : std::uint8_t {
    Fixed,        // `result`
    ElementOf,    // an element of the receiver array (array element kinds)
    ElementsOf,   // an array of the receiver's elements
    MemberOf,     // a member of the receiver object, or the default argument
    Merged,       // an object made from the receiver's and the argument's members
    Selected,     // one of the arguments, or an element of the single array argument
};

enum class BuiltinId : std::uint16_t {
#define NIFT_RT_BUILTIN(id, name, receiver, min, max, result, rule, may_fail, guard) id,
#include "rt/natives/builtin_list.inc"
#undef NIFT_RT_BUILTIN
    Count
};

struct BuiltinSpec {
    BuiltinId id;
    std::string_view name;
    Receiver receiver;
    std::uint8_t min_args;  // declared arguments, receiver excluded
    std::uint8_t max_args;  // 255 = variadic
    KindSet result;         // possible result kinds (ResultRule::Fixed)
    ResultRule rule;
    bool may_fail;          // can stop the program with a legacy error for some valid argument
    std::uint32_t guard;    // instruction-guard target per call (fixed part), see the design record
    NativeFn fn;
};

inline constexpr std::uint8_t kVariadic = 255;

// CP16 host built-ins: the contiguous block HostOpen .. HostSecureRandomBytes,
// last in builtin_list.inc. Used by the R1 refusal of linked units
// (script_runner.cpp). A built-in appended after the block must be added here
// deliberately (the assert fails until it is).
static_assert(static_cast<std::uint16_t>(BuiltinId::StreamEof) + 1 ==
                  static_cast<std::uint16_t>(BuiltinId::Count),
              "host_builtin(): review the host block when appending built-ins");
inline constexpr bool host_builtin(BuiltinId id) {
    return id >= BuiltinId::HostOpen && id <= BuiltinId::StreamEof;
}

// The whole table, indexed by BuiltinId.
const BuiltinSpec& builtin_spec(BuiltinId id);
std::size_t builtin_count();
// Compile-time lookup by receiver and name; nullptr if CP14 has no native for
// it (the compiler then refuses the unit, as today).
const BuiltinSpec* find_builtin(Receiver receiver, std::string_view name);
// The error code of a Status::Recoverable result of `id` ("schema.rejected"
// for validate); empty for built-ins that never return Recoverable.
std::string_view recoverable_code(BuiltinId id);
// True when `code` is the recoverable code of any built-in, or of a handle
// status classed Recoverable (handle_code_recoverable, D-035). Derived from
// recoverable_code and handle_error_class; no second classification table.
bool is_recoverable_code(std::string_view code);

// Calls the native. Each native checks its own arity and reports legacy's
// per-function message ("split: expected one delimiter", "<m>: expected no
// arguments", ...); min_args/max_args are for the compiler.
Status call_builtin(const BuiltinSpec& spec, const Context& context, const Value* args, std::uint32_t argc,
                    Value& out, std::string& error);

} // namespace nift::rt::natives
