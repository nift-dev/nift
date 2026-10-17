#include "rt/codegen.h"

#include "rt/string.h"

#include <cstring>

namespace nift::rt {

FunctionBuilder::FunctionBuilder(std::string name) : proto_(std::make_unique<Proto>()) {
    proto_->name = std::move(name);
}

bool FunctionBuilder::alloc(std::uint16_t& reg) {
    if (top_ >= kMaxRegisters) { error_ = "too many registers"; return false; }
    reg = top_++;
    if (top_ > proto_->nregs) proto_->nregs = top_;
    return true;
}

namespace {

// Constants are deduplicated only when they are the same kind and have the
// same representation (Int 1 and Double 1.0 stay distinct).
bool same_constant(const Value& a, const Value& b) {
    if (a.kind() != b.kind()) return false;
    switch (a.kind()) {
        case Kind::Int: return a.as_int() == b.as_int();
        case Kind::Double: {
            const double x = a.as_double(), y = b.as_double();
            return std::memcmp(&x, &y, sizeof x) == 0;
        }
        case Kind::String: return string_view_of(a) == string_view_of(b);
        case Kind::Bool: return a.as_bool() == b.as_bool();
        case Kind::Null: return true;
        default: return false;
    }
}

} // namespace

bool FunctionBuilder::constant(const Value& value, std::uint16_t& index) {
    auto& pool = proto_->constants;
    for (std::size_t k = 0; k < pool.size(); ++k) {
        if (same_constant(pool[k], value)) { index = static_cast<std::uint16_t>(k | kConstantBit); return true; }
    }
    if (pool.size() >= kMaxConstants) { error_ = "too many constants"; return false; }
    pool.push_back(value);
    index = static_cast<std::uint16_t>((pool.size() - 1) | kConstantBit);
    return true;
}

std::size_t FunctionBuilder::emit(OpCode op, std::uint16_t a, std::uint16_t b, std::uint16_t c) {
    Instr in;
    in.op = op;
    in.a = a;
    in.b = b;
    in.c = c;
    proto_->code.push_back(in);
    proto_->offsets.push_back(offset_);
    return proto_->code.size() - 1;
}

bool FunctionBuilder::patch(std::size_t at, std::size_t target) {
    const long distance = static_cast<long>(target) - static_cast<long>(at + 1);
    if (distance < -32768 || distance > 32767) { error_ = "jump distance exceeds 16 bits"; return false; }
    proto_->code[at].c = jump_operand(static_cast<int>(distance));
    return true;
}

std::unique_ptr<Proto> FunctionBuilder::finish() { return std::move(proto_); }

} // namespace nift::rt
