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
//   Error     a run-time error of the program, reported like legacy reports it
//             (message, position, exit status 1): the VM reproduces every
//             run-time error the compiled subset can reach (the compiler
//             refuses constructs whose legacy error it cannot reproduce).
//   Internal  an unexpected VM failure (bad opcode, broken invariant):
//             reported as such (exit 70), never disguised as a legacy run.
//   Reeval    internal only, never returned by Vm::run: a prepared-context
//             statement produced a number legacy re-evaluates textually.
enum class StopReason : std::uint8_t { None, Error, Internal, Reeval };
const char* stop_reason_name(StopReason reason);

// Instruction flags (Instr::flags). Read only on slow and error paths.
enum : std::uint8_t {
    kInstrPrepared = 1u << 0,      // legacy's prepared (loop) evaluator semantics, else textual
    kInstrReeval = 1u << 1,        // prepared compound/increment: a large operand re-evaluates the statement
    kInstrPathText = 1u << 2,      // index/member/length errors use the textual (JSON path) message
    kInstrReturnPrefix = 1u << 3,  // inside a textual `return` statement: errors get a "return: " prefix
    kInstrCallerSite = 1u << 4,    // a statement-level error of a prepared function: reported at its call
};

// Per-instruction note (cold): text used by error messages (an expression's
// source, a binding or function name) and the re-evaluation target of a
// kInstrReeval instruction.
struct Note {
    std::string text;
    std::uint32_t alt = 0;  // re-evaluation target (pc)
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
