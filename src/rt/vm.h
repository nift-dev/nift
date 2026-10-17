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
// `output_limit` it is flushed early and the run becomes committed (see
// VmResult::committed).
#include "rt/proto.h"
#include "rt/vm_ops.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nift::rt {

struct VmOptions {
    std::size_t max_frames = 100000;
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
    void reserve_registers(std::size_t needed);
    bool cold(const Instr& in, Value* R, const Value* K, std::string& output, Fault& fault);
    bool native(const Instr& in, Value* R, std::string& output, Fault& fault);
    bool flush_if_needed(std::string& output);

    const Program& program_;
    VmOptions options_;
    std::vector<Value> stack_;
    std::vector<Frame> frames_;
    std::vector<const Proto*> protos_;
    bool committed_ = false;
};

} // namespace nift::rt
