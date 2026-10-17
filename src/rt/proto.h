#pragma once
// Compiled code of the Runtime 2.0 VM (CP11): immutable function prototypes
// (Proto) and the whole compiled unit (Program).
//
// A Proto is produced once by the compiler and never changed afterwards: its
// code, constant pool and register count are fixed, so one Proto can be
// shared by every activation (and, later, by isolates; architecture.md #2).
// `offsets` maps every instruction to the source byte offset it came from;
// it is only read on the cold error path.
#include "rt/opcodes.h"
#include "rt/value.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nift::rt {

// Why execution stopped before the end of the program. Every reason other
// than None means "this run is not committed": the script is re-run on the
// legacy engine, which then produces the observable behaviour (D-001
// whole-unit fallback; see vm.h).
enum class BailReason : std::uint8_t {
    None,
    Error,          // a run-time error (legacy reports its own diagnostic)
    NumberRange,    // a number at or beyond 2^53, or an integer -0: legacy loop and top-level paths differ
    Aliasing,       // an aggregate bound from a place (legacy location reference)
    RecursionBudget,// the VM frame budget was exhausted
    StaticError,    // an instruction the compiler knows always fails (e.g. redeclaration)
    Unverified,     // a case whose legacy result the VM does not reproduce (yet)
    OutputLimit,    // the output buffer reached VmOptions::output_limit without a flush hook
    Internal,       // an internal VM failure (bad opcode, unknown native): reported, never replayed
};
const char* bail_reason_name(BailReason reason);

struct Proto {
    std::string name;
    std::uint16_t nparams = 0;
    std::uint16_t nregs = 0;
    std::vector<Instr> code;
    std::vector<std::uint32_t> offsets;
    std::vector<Value> constants;
};

struct Program {
    std::vector<std::unique_ptr<Proto>> protos;  // protos[0] is the top-level program
    const Proto& main() const { return *protos.front(); }
};

std::string disassemble(const Program& program);

} // namespace nift::rt
