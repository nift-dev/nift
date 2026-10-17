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
// Memory (review R1): the register stack is bounded (`max_registers`) and,
// once the call depth passes the legacy limit of 64 frames, the bytes held by
// live registers are measured at geometrically spaced depths; beyond
// `frame_heap_budget` the run stops with RecursionBudget. Any bail at a depth
// beyond 64 frames is a program legacy rejects with "callable recursion depth
// exceeded", so the approved D-001#4 behaviour applies before memory runs out.
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
    std::size_t frame_heap_budget = std::size_t(256) << 20;  // bytes held by registers, checked past depth 64
    std::size_t output_limit = std::size_t(64) << 20;
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
    bool frame_check(std::size_t top, Fault& fault);
#if defined(__GNUC__)
    __attribute__((noinline, cold))
#endif
    bool call_slow(std::size_t callee_base, std::size_t nregs, Fault& fault);
    bool cold(const Instr& in, Value* R, const Value* K, std::string& output, Fault& fault);
    bool native(const Instr& in, Value* R, std::string& output, Fault& fault);
    bool flush_if_needed(std::string& output, Fault& fault);

    const Program& program_;
    VmOptions options_;
    std::vector<Value> stack_;
    std::vector<Frame> frames_;
    std::vector<const Proto*> protos_;
    bool committed_ = false;
    bool beyond_legacy_depth_ = false;
    std::size_t next_frame_check_ = kLegacyMaxDepth;  // Call takes its slow path at this depth
};

} // namespace nift::rt
