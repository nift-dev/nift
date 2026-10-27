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
    // The highest register allocated since the last reset_high (exclusive):
    // a block's register range is [its starting top, high()).
    std::uint16_t high() const { return high_; }
    std::uint16_t reset_high() { const std::uint16_t was = high_; high_ = top_; return was; }
    void merge_high(std::uint16_t outer) { if (outer > high_) high_ = outer; }

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
    // Attaches a note (error-message text) to the instruction at `at`;
    // returns the note's index.
    std::size_t note(std::size_t at, std::string text);
    std::size_t note_index(std::size_t at) const { return proto_->notes[at] - 1u; }
    Instr& at(std::size_t index) { return proto_->code[index]; }
    void set_params(std::uint16_t count) { proto_->nparams = count; }

    // True when `operand` is a constant -1 or 2 (a text type that defers to the value).
    bool is_untyped_constant(std::uint16_t operand) const;
    // Removes BindType instructions whose type register is never read (and
    // closes the gaps, re-targeting jumps).
    // Run once, after the last emit.
    void drop_unread_types();
    // A type register read from elsewhere (main's global bindings, read by
    // functions through GetGlobal, CP12b): drop_unread_types keeps it.
    void keep_type(std::uint16_t reg) { kept_types_.push_back(reg); }

    std::unique_ptr<Proto> finish();
    const std::string& error() const { return error_; }

private:
    std::unique_ptr<Proto> proto_;
    std::uint16_t top_ = 0;
    std::uint16_t high_ = 0;
    std::uint32_t offset_ = 0;
    std::uint8_t flags_ = 0;
    std::string error_;
    std::vector<std::uint16_t> kept_types_;
};

} // namespace nift::rt
