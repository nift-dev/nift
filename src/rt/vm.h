#pragma once
// The Runtime 2.0 register VM (CP11): executes a compiled Program.
//
// Frames live on a heap frame stack (std::vector<Frame>) and registers on a
// heap register stack; a Nift-to-Nift call pushes a frame and continues in
// the same dispatch loop, so calls never recurse on the C++ stack
// (architecture.md #4). A callee's registers start at the caller's argument
// window (arguments are passed without copying). Recursion depth is a frame
// budget (semantics.md "Recursion", approved change D-001).
//
// A run either completes or stops with a BailReason. Output is appended to a
// buffer and only committed by the caller after the run (script_runner.h);
// together with the subset having no other effects, this makes a bail
// unobservable: the caller re-runs the unit on the legacy engine (D-001
// whole-unit fallback, decided at run time). If the buffer exceeds
// `output_limit` the run stops with BailReason::OutputLimit (and is replayed
// from the start; review D7) unless a `flush` hook is set (NIFT_VM=require
// only): then it is flushed early and the run becomes committed.
//
// Memory (review R1, round 2 R5): the register stack is bounded
// (`max_registers`) and the whole run is bounded by `heap_budget`: the exact
// live rt heap of the isolate (rt/heap_account.h: every string, array,
// object, ... however deeply nested, counted when created or resized) plus
// the register and frame stacks. It is checked after every instruction that
// can allocate (all on slow paths) and when the stacks grow. Exceeding it
// deeper than legacy's 64 frames stops the run with RecursionBudget (legacy
// rejects such a program with "callable recursion depth exceeded": the
// approved D-001#4 behaviour); at a depth legacy also reaches it is an
// Internal failure, reported (exit 70), never replayed. Either way the
// process stops before the host runs out of memory.
#include "rt/proto.h"
#include "rt/vm_ops.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nift::rt {

// Legacy limit of nested user-callable frames (ParserHelpers.cpp kMaxCallableDepth).
inline constexpr std::size_t kLegacyMaxDepth = 64;

struct VmOptions {
    std::size_t max_frames = 100000;
    std::size_t max_registers = std::size_t(16) << 20;      // 16M values, 256 MiB
    std::size_t heap_budget = std::size_t(1) << 30;  // live rt heap + register and frame stacks (bytes)
    std::size_t output_limit = std::size_t(64) << 20;
    std::size_t equality_depth = 10000;  // `==` deeper than this bails (NestingDepth); legacy answers ~30k
    // Called with the buffered output when it exceeds output_limit.
    void (*flush)(std::string& output, void* context) = nullptr;
    void* flush_context = nullptr;
};

struct VmResult {
    BailReason reason = BailReason::None;  // None: the program completed
    std::string message;                   // fault description (legacy wording where known)
    std::uint32_t offset = 0;              // source offset of the faulting instruction
    bool returned = false;                 // the script ended with `return value`
    std::string return_text;               // rendering of that value
    bool committed = false;                // output was flushed before the run ended
    bool beyond_legacy_depth = false;      // the call depth exceeded legacy's 64 frames
};

class Vm {
public:
    Vm(const Program& program, VmOptions options = VmOptions());
    VmResult run(std::string& output);

private:
    struct Frame {
        const Instr* ret = nullptr;
        std::size_t base = 0;
        const Proto* proto = nullptr;
    };
    bool reserve_registers(std::size_t needed);
    bool frame_check(Fault& fault);
#if defined(__GNUC__)
    __attribute__((noinline, cold))
#endif
    bool call_slow(std::size_t callee_base, std::size_t nregs, Fault& fault);
    bool cold(const Instr& in, Value* R, const Value* K, std::string& output, Fault& fault);
    bool native(const Instr& in, Value* R, std::string& output, Fault& fault);
    bool flush_if_needed(std::string& output, Fault& fault);
    bool memory_ok(Fault& fault);
#if defined(__GNUC__)
    __attribute__((noinline, cold))
#endif
    bool memory_exceeded(Fault& fault);
    void update_heap_limit();

    const Program& program_;
    VmOptions options_;
    std::vector<Value> stack_;
    std::vector<Frame> frames_;
    std::vector<const Proto*> protos_;
    bool committed_ = false;
    bool beyond_legacy_depth_ = false;
    std::size_t next_frame_check_ = kLegacyMaxDepth;  // Call takes its slow path at this depth
    std::size_t heap_limit_ = 0;  // heap_budget minus the stacks' bytes: the live-heap bound
};

} // namespace nift::rt
