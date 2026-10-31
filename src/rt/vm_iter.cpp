#include "rt/vm_iter.h"

#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/string.h"

// Default `for` iterator opcodes (CP12C slice 2). The iterator itself lives in
// registers: the entry length/key list and the index. The source place is
// re-read by compiler-generated code for every step (so mutations of existing
// elements and reassignment of the binding are observed, and no second owning
// reference to the collection is held across the body). These opcodes only do
// the heap-free element/key lookups and the legacy form/type errors.

namespace nift::rt {

namespace {
const Note* note_at(const Proto* proto, const Instr* pc) {
    return proto->note(static_cast<std::size_t>(pc - proto->code.data()));
}
} // namespace

bool Vm::op_iter_check(const Instr& in, Value* R, Fault& fault) {
    Value& source = R[in.a];
    // D-063: the loop's snapshot of a set or stack becomes the array legacy
    // materialises (members in order; a stack top first). A map stays: the
    // pair form reads its typed keys and values (ObjectKeys/ObjectLookup).
    if (source.is(Kind::Collection)) {
        const CollectionKind kind = collection_kind(source);
        if (kind == CollectionKind::Map) {
            if (in.b == 1) return true;
            if (in.b == 0 || in.b == 5)
                return raise(fault, StopReason::Error, "map @for syntax is @for((key, val) : map){...}");
            return internal(fault, "for mode over a map");
        }
        if (in.b != 0)  // the pair form (or its forced error): legacy's array message, nothing converted
            return raise(fault, StopReason::Error, "array @for syntax is @for(item : array){...}");
        const auto& items = collection_items(source);
        Value array = make_array(items.size());
#if defined(NIFT_COLLECTIONS_MUTANT) && NIFT_COLLECTIONS_MUTANT == 7
        const bool reverse = false;  // M7: a stack iterated bottom first
#else
        const bool reverse = kind == CollectionKind::Stack;
#endif
        if (reverse) for (std::size_t k = items.size(); k-- > 0;) array_push(array, items[k]);
        else for (const Value& item : items) array_push(array, item);
        source = std::move(array);  // R[a] is the loop's own snapshot register
        return memory_ok(fault);
    }
    switch (in.b) {
        case 0:  // single binding: an array
            if (source.is(Kind::Array)) return true;
            if (source.is(Kind::Object))
                return raise(fault, StopReason::Error, "object @for syntax is @for((key, val) : object){...}");
            break;
        case 1:  // pair binding: an object
            if (source.is(Kind::Object)) return true;
            if (source.is(Kind::Array))
                return raise(fault, StopReason::Error, "array @for syntax is @for(item : array){...}");
            break;
        case 2:  // forced non-iterable error (the static kinds prove it)
            break;
        case 3:  // forced array-pair error
            return raise(fault, StopReason::Error, "array @for syntax is @for(item : array){...}");
        case 4:  // forced object-single error
            return raise(fault, StopReason::Error, "object @for syntax is @for((key, val) : object){...}");
        case 5:  // forced map-single error (D-063)
            return raise(fault, StopReason::Error, "map @for syntax is @for((key, val) : map){...}");
        default: return internal(fault, "for mode");
    }
    return raise(fault, StopReason::Error, "@for can only iterate over JSON arrays or objects");
}

// R[a] = R[b][R[c]] for an array; R[a+1] is the found flag. Never raises: a
// vanished element is the legacy lazy location reference, represented here by
// a null value plus `found = false` (the compiler raises at its first use).
bool Vm::op_array_elem(const Instr& in, Value* R, Fault& fault) {
    if (static_cast<std::size_t>(in.a) + 1 >= stack_.size() || in.b >= stack_.size() || in.c >= stack_.size())
        return internal(fault, "array element operands");
    const Value& source = R[in.b];
    if (!source.is(Kind::Array)) return internal(fault, "array element source");
    const std::int64_t index = R[in.c].is(Kind::Int) ? R[in.c].as_int() : -1;
    const auto size = static_cast<std::int64_t>(array_size(source));
    if (index >= 0 && index < size) {
        R[in.a] = array_items(source)[static_cast<std::size_t>(index)];
        R[in.a + 1] = Value::boolean(true);
    } else {
        R[in.a] = Value();
        R[in.a + 1] = Value::boolean(false);
    }
    return true;
}

// As ArrayElem but without the found flag: the caller proved the source
// length cannot change in the body, so the index is always in range.
bool Vm::op_array_elem_value(const Instr& in, Value* R, Fault& fault) {
    const Value& source = R[in.b];
    if (!source.is(Kind::Array)) return internal(fault, "array element source");
    const std::int64_t index = R[in.c].is(Kind::Int) ? R[in.c].as_int() : -1;
    if (index < 0 || static_cast<std::uint64_t>(index) >= array_size(source))
        return internal(fault, "safe array element index");
    R[in.a] = array_items(source)[static_cast<std::size_t>(index)];
    return true;
}

// R[a] = insertion-ordered array of the object R[b]'s keys.
bool Vm::op_object_keys(const Instr& in, Value* R, Fault& fault) {
    const Value& source = R[in.b];
    if (source.is(Kind::Collection) && collection_kind(source) == CollectionKind::Map) {  // D-063: typed keys
        const auto& items = collection_items(source);
        Value keys = make_array(items.size() / 2);
        for (std::size_t k = 0; k < items.size(); k += 2) array_push(keys, items[k]);
        R[in.a] = std::move(keys);
        return true;
    }
    if (!source.is(Kind::Object)) return internal(fault, "object keys source");
    const auto& entries = object_entries(source);
    Value keys = make_array(entries.size());
#if defined(NIFT_FOR_MUTANT) && NIFT_FOR_MUTANT == 7
    for (std::size_t k = entries.size(); k-- > 0;)  // M6: object order changed
        array_push(keys, make_string(atom_text(entries[k].first)));
#else
    for (const auto& entry : entries) array_push(keys, make_string(atom_text(entry.first)));
#endif
    R[in.a] = std::move(keys);
    return true;
}

// R[a] = R[b][R[c]] for an object; R[a+1] is the found flag. Never raises.
bool Vm::op_object_lookup(const Instr& in, Value* R, Fault& fault) {
    if (static_cast<std::size_t>(in.a) + 1 >= stack_.size() || in.b >= stack_.size() || in.c >= stack_.size())
        return internal(fault, "object lookup operands");
    const Value& source = R[in.b];
    if (source.is(Kind::Collection) && collection_kind(source) == CollectionKind::Map) {  // D-063
        const std::ptrdiff_t at = collection_find(source, R[in.c]);
        R[in.a] = at < 0 ? Value() : collection_items(source)[static_cast<std::size_t>(at) * 2 + 1];
        R[in.a + 1] = Value::boolean(at >= 0);
        return true;
    }
    if (!source.is(Kind::Object)) return internal(fault, "object lookup source");
    const Value& key = R[in.c];
    if (key.is(Kind::String)) {
        Atom atom;
        if (AtomTable::current().find(string_view_of(key), atom)) {
            if (const Value* member = object_get(source, atom)) {
                R[in.a] = *member;
                R[in.a + 1] = Value::boolean(true);
                return true;
            }
        }
    }
    R[in.a] = Value();
    R[in.a + 1] = Value::boolean(false);
    return true;
}

// As ObjectLookup but without the found flag: keys came from the entry list
// and the caller proved the source's keys cannot change in the body.
bool Vm::op_object_lookup_value(const Instr& in, Value* R, Fault& fault) {
    const Value& source = R[in.b];
    if (source.is(Kind::Collection) && collection_kind(source) == CollectionKind::Map) {  // D-063
        const std::ptrdiff_t at = collection_find(source, R[in.c]);
        if (at < 0) return internal(fault, "safe map key missing");
        R[in.a] = collection_items(source)[static_cast<std::size_t>(at) * 2 + 1];
        return true;
    }
    if (!source.is(Kind::Object)) return internal(fault, "object lookup source");
    const Value& key = R[in.c];
    if (!key.is(Kind::String)) return internal(fault, "safe object key");
    Atom atom;
    if (!AtomTable::current().find(string_view_of(key), atom)) return internal(fault, "safe object atom");
    const Value* member = object_get(source, atom);
    if (member == nullptr) return internal(fault, "safe object key missing");
    R[in.a] = *member;
    return true;
}

bool Vm::op_iter_missing(const Instr& in, Fault& fault, const Proto* proto) {
    const Note* note = note_at(proto, &in);
    return raise(fault, StopReason::Error,
                 "reference target no longer exists: " + std::string(note ? note->text : ""));
}

bool Vm::op_iter(const Instr& in, Value* R, const Proto* proto, Fault& fault) {
    static_cast<void>(proto);
    switch (in.op) {
        case OpCode::IterCheck:
            if (!op_iter_check(in, R, fault)) return false;  // may materialise a set/stack (D-063)
            return true;
        case OpCode::ArrayElem:
            if (!op_array_elem(in, R, fault)) return false;
            return true;
        case OpCode::ArrayElemValue:
            if (!op_array_elem_value(in, R, fault)) return false;
            return true;
        case OpCode::ObjectKeys:
            if (!op_object_keys(in, R, fault) || !memory_ok(fault)) return false;
            return true;
        case OpCode::ObjectLookup:
            if (!op_object_lookup(in, R, fault)) return false;
            return true;
        case OpCode::ObjectLookupValue:
            if (!op_object_lookup_value(in, R, fault)) return false;
            return true;
        case OpCode::IterMissing:
            if (!op_iter_missing(in, fault, proto)) return false;
            return true;
        default: return internal(fault, "iter opcode");
    }
}

} // namespace nift::rt
