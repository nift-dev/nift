#include "rt/codegen.h"

#include "rt/string.h"

#include <cstring>
#include <vector>

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
    in.flags = flags_;
    proto_->code.push_back(in);
    proto_->offsets.push_back(offset_);
    proto_->notes.push_back(0);
    return proto_->code.size() - 1;
}

std::size_t FunctionBuilder::note(std::size_t at, std::string text) {
    proto_->note_table.push_back(Note{std::move(text)});
    proto_->notes[at] = static_cast<std::uint32_t>(proto_->note_table.size());
    return proto_->note_table.size() - 1;
}

bool FunctionBuilder::patch(std::size_t at, std::size_t target) {
    const long distance = static_cast<long>(target) - static_cast<long>(at + 1);
    if (distance < -32768 || distance > 32767) { error_ = "jump distance exceeds 16 bits"; return false; }
    proto_->code[at].c = jump_operand(static_cast<int>(distance));
    return true;
}

bool FunctionBuilder::is_untyped_constant(std::uint16_t operand) const {
    if (!(operand & kConstantBit)) return false;
    const Value& v = proto_->constants[operand & ~kConstantBit];
    return v.is(Kind::Int) && (v.as_int() == -1 || v.as_int() == 2);
}

namespace {

bool is_jump(OpCode op) {
    switch (op) {
        case OpCode::Jmp: case OpCode::JmpFalse: case OpCode::JmpTrue: case OpCode::JmpNotNull:
        case OpCode::JmpNotLt: case OpCode::JmpNotLe: case OpCode::JmpNotGt: case OpCode::JmpNotGe:
        case OpCode::JmpNotEq: case OpCode::JmpNotNe: case OpCode::JmpIncLt: return true;
        default: return false;
    }
}

void mark_register(std::vector<bool>& read, std::uint16_t operand) {
    if (!(operand & kConstantBit) && operand < read.size()) read[operand] = true;
}

} // namespace

void FunctionBuilder::drop_unread_types() {
    auto& code = proto_->code;
    std::vector<bool> read(proto_->nregs + 1u, false);
    for (std::uint16_t reg : kept_types_) mark_register(read, reg);
    for (const Instr& in : code) {
        switch (in.op) {
            case OpCode::CheckType: mark_register(read, in.b); break;
            case OpCode::CheckAssign: case OpCode::TypeMeet:
                mark_register(read, in.b);
                mark_register(read, in.c);
                break;
            case OpCode::BindType: mark_register(read, in.c); break;
            case OpCode::NewCell: case OpCode::CellStore: case OpCode::SetUpvalue: mark_register(read, in.c); break;
            case OpCode::Move: mark_register(read, in.b); break;  // a textual call passing a binding's type
            default: break;
        }
    }
    std::vector<std::size_t> position(code.size() + 1);
    std::size_t kept = 0;
    for (std::size_t i = 0; i < code.size(); ++i) {
        position[i] = kept;
        if (!(code[i].op == OpCode::BindType && !read[code[i].a])) ++kept;
    }
    position[code.size()] = kept;
    if (kept == code.size()) return;
    std::vector<Instr> out;
    std::vector<std::uint32_t> offsets, notes;
    out.reserve(kept);
    for (std::size_t i = 0; i < code.size(); ++i) {
        if (code[i].op == OpCode::BindType && !read[code[i].a]) continue;
        Instr in = code[i];
        if (is_jump(in.op)) {
            const long target = static_cast<long>(i) + 1 + jump_offset(in.c);
            in.c = jump_operand(static_cast<int>(static_cast<long>(position[static_cast<std::size_t>(target)]) -
                                                 static_cast<long>(out.size() + 1)));
        }
        out.push_back(in);
        offsets.push_back(proto_->offsets[i]);
        notes.push_back(proto_->notes[i]);
    }
    code = std::move(out);
    proto_->offsets = std::move(offsets);
    proto_->notes = std::move(notes);
}

std::unique_ptr<Proto> FunctionBuilder::finish() { return std::move(proto_); }

} // namespace nift::rt
