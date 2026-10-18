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
// CP12: a run either completes or stops with a program error that the VM
// reports itself (legacy's message format, the position of the failing
// expression, exit status 1; D-018c), or with an internal failure (exit 70). Nothing is ever re-run
// on the legacy engine: the engine was selected before the first
// instruction. Output is streamed: the buffer is handed to `flush` whenever
// it grows past `flush_threshold` and at the end of the run.
//
// Recursion (approved change D-001#4): the frame budget replaces legacy's
// 64-frame limit; beyond it the run stops with the error "callable recursion
// depth exceeded: <function>" (legacy fails the same program at depth 64
// with that error family).
//
// Memory (review R1, round 2 R5): the register stack is bounded
// (`max_registers`) and the whole run is bounded by `heap_budget`: the exact
// live rt heap of the isolate (rt/heap_account.h: every string, array,
// object, ... however deeply nested, counted when created or resized) plus
// the register and frame stacks. It is checked after every instruction that
// can allocate (all on slow paths) and when the stacks grow. Exceeding it
// deeper than legacy's 64 frames is the recursion error above; at a depth
// legacy also reaches it is an Internal failure (exit 70). Either way the
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
    std::size_t flush_threshold = std::size_t(64) << 10;
    // Called with the buffered output when it exceeds flush_threshold (and
    // by the caller at the end). Null: everything stays in the buffer.
    void (*flush)(std::string& output, void* context) = nullptr;
    void* flush_context = nullptr;
};

struct VmResult {
    StopReason reason = StopReason::None;  // None: the program completed
    std::string message;                   // legacy's message (Error) or the failure (Internal)
    std::uint32_t offset = 0;              // source offset of the failing expression
    bool returned = false;                 // the script ended with `return value`
    std::string return_text;               // rendering of that value
    bool beyond_legacy_depth = false;      // the call depth exceeded legacy's 64 frames
    // Evidence for the D-018 difference policies (never a change of
    // behaviour): arithmetic that met a number legacy calls large (or went
    // out of range), how much of it ran in code legacy runs on its prepared
    // (loop) evaluator, and whether the error did.
    std::uint64_t large_numbers = 0;
    std::uint64_t large_in_loop_path = 0;
    bool error_in_loop_path = false;
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
    bool frame_check(const Proto* callee, Fault& fault);
#if defined(__GNUC__)
    __attribute__((noinline, cold))
#endif
    bool call_slow(const Proto* callee, std::size_t callee_base, Fault& fault);
    bool cold(const Instr& in, const Note* note, Value* R, const Value* K, std::string& output, Fault& fault);
    bool native(const Instr& in, Value* R, std::string& output, Fault& fault);
    void flush_if_needed(std::string& output);
    bool in_loop_path(const Instr* pc) const;
    void note_large(const Instr* pc, Fault& fault);
    void error_result(VmResult& result, const Fault& fault, const Proto* proto, const Instr* pc) const;
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
    std::uint64_t large_numbers_ = 0;
    std::uint64_t large_in_loop_path_ = 0;
    bool beyond_legacy_depth_ = false;
    std::size_t next_frame_check_ = kLegacyMaxDepth;  // Call takes its slow path at this depth
    std::size_t heap_limit_ = 0;  // heap_budget minus the stacks' bytes: the live-heap bound
};

} // namespace nift::rt
