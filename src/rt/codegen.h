#pragma once
// Bytecode emission for one function of the Runtime 2.0 compiler (CP11):
// instruction buffer, source offsets, constant pool, register allocation and
// jump patching. Knows nothing about the AST.
#include "rt/proto.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nift::rt {

class FunctionBuilder {
public:
    explicit FunctionBuilder(std::string name);

    // Registers: a stack. `alloc` returns the next free register; `release_to`
    // frees every register at or above `mark`.
    bool alloc(std::uint16_t& reg);
    std::uint16_t top() const { return top_; }
    void release_to(std::uint16_t mark) { top_ = mark; }

    // Constants (deduplicated). Returns false when the pool is full.
    bool constant(const Value& value, std::uint16_t& index);

    std::size_t emit(OpCode op, std::uint16_t a = 0, std::uint16_t b = 0, std::uint16_t c = 0);
    std::size_t here() const { return proto_->code.size(); }
    // Sets the c operand of the jump at `at` so that it lands on `target`.
    // Returns false when the distance does not fit 16 bits.
    bool patch(std::size_t at, std::size_t target);
    void set_offset(std::uint32_t offset) { offset_ = offset; }
    std::uint32_t offset() const { return offset_; }
    // Flags (kInstr*) given to every instruction emitted from now on.
    void set_flags(std::uint8_t flags) { flags_ = flags; }
    std::uint8_t flags() const { return flags_; }
    // Attaches a note (error-message text, re-evaluation target) to the
    // instruction at `at`; returns the note's index for set_note_alt.
    std::size_t note(std::size_t at, std::string text);
    void set_note_alt(std::size_t note_index, std::size_t target);
    std::size_t note_index(std::size_t at) const { return proto_->notes[at] - 1u; }
    Instr& at(std::size_t index) { return proto_->code[index]; }
    void set_params(std::uint16_t count) { proto_->nparams = count; }

    // True when `operand` is a constant -1 or 2 (a text type that defers to the value).
    bool is_untyped_constant(std::uint16_t operand) const;
    // Marks [begin, end) as cold code entered only by jumps (legacy's textual
    // re-evaluation of a prepared statement): `skip` is the jump over it on
    // the normal path. layout_cold moves every cold block after the function
    // body (each followed by a jump back to `end`) and deletes `skip`, so the
    // normal path runs no extra instruction.
    void cold_block(std::size_t skip, std::size_t begin, std::size_t end) { cold_.push_back({skip, begin, end}); }
    // Removes BindType instructions whose type register is never read (and
    // closes the gaps, re-targeting jumps), after laying out the cold blocks.
    // Run once, after the last emit.
    void drop_unread_types();

    std::unique_ptr<Proto> finish();
    const std::string& error() const { return error_; }

private:
    struct Cold {
        std::size_t skip, begin, end;
    };
    void layout_cold();
    std::vector<Cold> cold_;
    std::unique_ptr<Proto> proto_;
    std::uint16_t top_ = 0;
    std::uint32_t offset_ = 0;
    std::uint8_t flags_ = 0;
    std::string error_;
};

} // namespace nift::rt
