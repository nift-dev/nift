#pragma once
// Bytecode emission for one function of the Runtime 2.0 compiler (CP11):
// instruction buffer, source offsets, constant pool, register allocation and
// jump patching. Knows nothing about the AST.
#include "rt/proto.h"

#include <cstdint>
#include <memory>
#include <string>

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
    void set_params(std::uint16_t count) { proto_->nparams = count; }

    std::unique_ptr<Proto> finish();
    const std::string& error() const { return error_; }

private:
    std::unique_ptr<Proto> proto_;
    std::uint16_t top_ = 0;
    std::uint32_t offset_ = 0;
    std::string error_;
};

} // namespace nift::rt
