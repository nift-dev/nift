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

// Why execution stopped before the end of the program (CP12: there is no
// replay; engine selection is final before the first instruction runs).
//   Error     a run-time error of the program, reported at the failing
//             expression (message, position, exit status 1): the VM reports
//             every run-time error the compiled subset can reach (the
//             compiler refuses constructs whose meaning it cannot fix).
//   Internal  an unexpected VM failure (bad opcode, broken invariant):
//             reported as such (exit 70), never disguised as a legacy run.
enum class StopReason : std::uint8_t { None, Error, Internal };
const char* stop_reason_name(StopReason reason);

// Instruction flags (Instr::flags). Evidence only: they never change what an
// instruction computes (CP12, D-018: one meaning in every context).
enum : std::uint8_t {
    kInstrLoopPath = 1u << 0,  // code legacy runs on its prepared (loop) evaluator: VM stats only
    kInstrCallMember = 1u << 1,  // CallValue through an object field: legacy member diagnostics
    kInstrLambdaExprBody = 1u << 2,  // Closure whose body is an expression (diagnostic prefix)
    kInstrReturnCall = 1u << 3,  // a call evaluated inside a return expression (legacy return: prefix)
    kInstrIndexKey = 1u << 4,  // an expression evaluated as an index key (legacy call-site diagnostics)
};

// Per-instruction note (cold): text used by error messages (an expression's
// source, a binding or function name).
struct Note {
    std::string text;
};

struct Proto {
    std::string name;
    std::uint16_t nparams = 0;
    std::uint16_t nregs = 0;
    std::vector<Instr> code;
    std::vector<std::uint32_t> offsets;
    std::vector<Value> constants;
    std::vector<std::uint32_t> notes;  // parallel to code: index into note_table + 1, 0 = none
    std::vector<Note> note_table;
    const Note* note(std::size_t pc) const { return notes[pc] ? &note_table[notes[pc] - 1] : nullptr; }
};

struct Program {
    std::vector<std::unique_ptr<Proto>> protos;  // protos[0] is the top-level program
    const Proto& main() const { return *protos.front(); }
};

std::string disassemble(const Program& program);

} // namespace nift::rt
