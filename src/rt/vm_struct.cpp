#include "rt/vm_struct.h"

#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/cell.h"
#include "rt/vm_ops.h"

// rt-lint: cold-file struct instance operations (CP12b): construction, field
// reads and writes through an absolute stack slot (a method's `this`), and
// the field binding-type check. Instances are values (D-040 V9); the
// compiler admits only units where no instance is observably shared.

namespace nift::rt {

namespace {
std::string note_of(const Note* note) { return note == nullptr ? std::string() : note->text; }

const Value& rk(const Value* R, const Value* K, std::uint16_t x) {
    return (x & kConstantBit) ? K[x & 0x7fffu] : R[x];
}
} // namespace

Value* Vm::struct_location(std::int64_t slot) {
    if (slot >= 0) return static_cast<std::size_t>(slot) < stack_.size() ? &stack_[slot] : nullptr;
    const auto index = static_cast<std::uint64_t>(-(slot + 1));
    if (!options_.session_globals || index >= options_.session_globals->size()) return nullptr;
    auto& value = (*options_.session_globals)[index];
    return is_cell(value) ? &cell_of(value)->value : nullptr;
}

bool Vm::struct_op(const Instr& in, const Note* note, Value* R, const Value* K, Fault& fault) {
    switch (in.op) {
        case OpCode::SlotOf:
            R[in.a] = Value::integer(static_cast<std::int64_t>((R - stack_.data()) + in.b));
            return true;
        case OpCode::NewStruct: R[in.a] = make_struct(Atom{in.b}); return true;
        case OpCode::StructAdd: struct_add_field(R[in.a], Atom{in.b}, rk(R, K, in.c)); return true;
        case OpCode::FieldGet: {
            const auto* instance = struct_location(R[in.b].as_int());
            if (!instance) return internal(fault, "struct location");
            const Value* field = instance->is(Kind::Struct) ? struct_get(*instance, Atom{in.c}) : nullptr;
            if (field == nullptr) return raise(fault, StopReason::Error, "struct has no field: " + note_of(note));
            R[in.a] = *field;
            return true;
        }
        case OpCode::FieldSet: {
            auto* instance = struct_location(R[in.a].as_int());
            if (!instance) return internal(fault, "struct location");
            if (!instance->is(Kind::Struct) || !struct_set(*instance, Atom{in.b}, rk(R, K, in.c)))
                return raise(fault, StopReason::Error, "struct has no field: " + note_of(note));
            return true;
        }
        case OpCode::CheckField: {
            // Legacy's three wordings: through a member (`this.x = v`, `p.x = v`),
            // an implicit field in a method (`x = v`), its compound (`x += v`).
            const std::int64_t from = fast_tag(rk(R, K, in.a)), to = rk(R, K, in.b).as_int();
            if (tag_assignable(from, to)) return true;
            if (in.c == 0) return raise(fault, StopReason::Error, "cannot change struct field type: " + note_of(note));
            const char* what = in.c == 1 ? " struct field '" : " binding '";
            return raise(fault, StopReason::Error, std::string("cannot assign ") + binding_type_name(from) + " to " +
                                                       binding_type_name(to) + what + note_of(note) + "'");
        }
        default: return raise(fault, StopReason::Internal, "unexpected opcode");
    }
}

} // namespace nift::rt
