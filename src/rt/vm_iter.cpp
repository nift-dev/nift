#include "rt/vm_iter.h"

#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/natives/support.h"
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

// D5 snapshots (D-NEXT-refsem): the budget for `n` copied Values (and a
// map's index) before the copy is made, so a budget stop leaves nothing half
// built (the VM's ordinary budget stop text).
bool Vm::reserve_snapshot(std::size_t n, Fault& fault) {
    natives::Context context;
    context.heap_limit = heap_limit_;
    if (natives::reserve_result(context, n * (2 * sizeof(Value) + sizeof(std::uint32_t)) + 128)) return true;
    return raise(fault, StopReason::Internal, "memory budget exceeded");
}

// B2 (b2-refrep.md, D5): a loop over an expression's result (c = 1: the
// loop holds the value in its own register and never re-reads a place)
// iterates a shallow snapshot taken here when anything else holds the array
// or object: under copy-on-write the register was a value, and the body's
// in-place mutation of the original must not reach the active loop. A
// uniquely held value is the loop's alone (nothing else can reach it).
// Loops over a binding (c = 0) re-read it live, as before (the full D5
// snapshot is B5's).
bool Vm::entry_snapshot(const Instr& in, Value& source, Fault& fault) {
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 27
    return true;  // R-B3: the loop iterates the live array or object
#endif
    if (in.c == 0 || source.refcount() <= 1) return true;
    const std::size_t n = source.is(Kind::Array) ? array_size(source) : object_size(source);
    if (!reserve_snapshot(n, fault)) return false;
    source = source.is(Kind::Array) ? array_shallow_clone(source) : object_shallow_clone(source);
    return memory_ok(fault);
}

bool Vm::op_iter_check(const Instr& in, Value* R, Fault& fault) {
    Value& source = R[in.a];
    // D-063: the loop's snapshot of a set or stack becomes the array legacy
    // materialises (members in order; a stack top first). A map stays: the
    // pair form reads its typed keys and values (ObjectKeys/ObjectLookup).
    if (source.is(Kind::Collection)) {
        const CollectionKind kind = collection_kind(source);
        if (kind == CollectionKind::Map) {
            if (in.b == 1) {
                // D5 (D-NEXT-refsem #6): the loop iterates the entries at loop
                // entry. The map is a shared reference that the body may
                // mutate in place, so the snapshot is an explicit shallow copy
                // (O(n), budget-checked), never the live object.
#if !(defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 8)
                if (!reserve_snapshot(collection_item_count(source), fault)) return false;
                source = collection_shallow_clone(source);  // R[a] is the loop's own snapshot register
#endif
                return memory_ok(fault);
            }
            if (in.b == 0 || in.b == 5)
                return raise(fault, StopReason::Error, "map @for syntax is @for((key, val) : map){...}");
            return internal(fault, "for mode over a map");
        }
        if (in.b != 0)  // the pair form (or its forced error): legacy's array message, nothing converted
            return raise(fault, StopReason::Error, "array @for syntax is @for(item : array){...}");
        const auto& items = collection_view(source);
        if (!reserve_snapshot(items.size(), fault)) return false;
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
            if (source.is(Kind::Array)) return entry_snapshot(in, source, fault);
            if (source.is(Kind::Object))
                return raise(fault, StopReason::Error, "object @for syntax is @for((key, val) : object){...}");
            break;
        case 1:  // pair binding: an object
            if (source.is(Kind::Object)) return entry_snapshot(in, source, fault);
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
        R[in.a] = array_view(source)[static_cast<std::size_t>(index)];
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
    R[in.a] = array_view(source)[static_cast<std::size_t>(index)];
    return true;
}

// R[a] = insertion-ordered array of the object R[b]'s keys.
bool Vm::op_object_keys(const Instr& in, Value* R, Fault& fault) {
    const Value& source = R[in.b];
    if (source.is(Kind::Collection) && collection_kind(source) == CollectionKind::Map) {  // D-063: typed keys
        const auto& items = collection_view(source);
        Value keys = make_array(items.size() / 2);
        for (std::size_t k = 0; k < items.size(); k += 2) array_push(keys, items[k]);
        R[in.a] = std::move(keys);
        return true;
    }
    if (!source.is(Kind::Object)) return internal(fault, "object keys source");
    const auto& entries = object_view(source);
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
        R[in.a] = at < 0 ? Value() : collection_view(source)[static_cast<std::size_t>(at) * 2 + 1];
        R[in.a + 1] = Value::boolean(at >= 0);
        return true;
    }
    if (!source.is(Kind::Object)) return internal(fault, "object lookup source");
    const Value& key = R[in.c];
    if (key.is(Kind::String)) {
        Atom atom;
        if (AtomTable::current().find(string_view_of(key), atom)) {
            if (const Value* member = object_find(source, atom)) {
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
        R[in.a] = collection_view(source)[static_cast<std::size_t>(at) * 2 + 1];
        return true;
    }
    if (!source.is(Kind::Object)) return internal(fault, "object lookup source");
    const Value& key = R[in.c];
    if (!key.is(Kind::String)) return internal(fault, "safe object key");
    Atom atom;
    if (!AtomTable::current().find(string_view_of(key), atom)) return internal(fault, "safe object atom");
    const Value* member = object_find(source, atom);
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
