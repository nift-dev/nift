#include "rt/vm_struct.h"

#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/cell.h"
#include "rt/mutation.h"
#include "rt/vm_ops.h"

// rt-lint: cold-file struct instance operations (CP12b): construction, field
// reads and writes through a register holding the instance, and the field
// binding-type check. Instances are identity-bearing references
// (D-NEXT-refsem #1): every register or binding holding one shares the
// object, and FieldSet changes it in place, visible through every alias.

namespace nift::rt {

namespace {
std::string note_of(const Note* note) { return note == nullptr ? std::string() : note->text; }

const Value& rk(const Value* R, const Value* K, std::uint16_t x) {
    return (x & kConstantBit) ? K[x & 0x7fffu] : R[x];
}
} // namespace

bool Vm::struct_op(const Instr& in, const Note* note, Value* R, const Value* K, Fault& fault, const Proto* proto) {
    switch (in.op) {
        case OpCode::NewStruct: R[in.a] = make_struct(Atom{in.b}); return true;
        // rt-lint: storage-ok fresh, the instance NewStruct just made
        case OpCode::StructAdd: struct_add_field(R[in.a], Atom{in.b}, rk(R, K, in.c)); return true;
        case OpCode::FieldGet: {
            const Value& instance = R[in.b];
            const Value* field = instance.is(Kind::Struct) ? struct_get(instance, Atom{in.c}) : nullptr;
            if (field == nullptr) return raise(fault, StopReason::Error, "struct has no field: " + note_of(note));
            R[in.a] = *field;
            return true;
        }
        case OpCode::FieldSet: {
            Value& instance = R[in.a];
            if (!instance.is(Kind::Struct) || !struct_get(instance, Atom{in.b}))
                return raise(fault, StopReason::Error, "struct has no field: " + note_of(note));
            const Value& stored = rk(R, K, in.c);
            // B1: through the mutation protocol (immutability, then the
            // D-NEXT-refsem #4 cycle check before the field changes).
            const mutation::Outcome out = mutation::field_set(instance, Atom{in.b}, stored, mutation_env(note));
            if (out.ok()) return true;
            return mutation_failed(fault, proto, in, out, instance.heap(), stored,
                                   "assignment would create a cyclic reference: " + note_of(note));
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
