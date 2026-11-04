#pragma once
// Storage-level unit tests only (B2, b2-refrep.md, owner requirement B): the
// value-layer tests exercise the storage commits directly (relocation,
// bookkeeping, heap accounting, destruction), which production code reaches
// only through the mutation protocol (rt::Storage's members are private; its
// production friend is mutation::Protocol). This header defines Storage's
// second friend and the B1 free-function names over it. rt-lint flags any use
// of StorageTestAccess under src/.
#ifdef NIFT_PRODUCTION_TU
#error "tests/rt/storage_access.h is a test-only header: a production translation unit cannot include it (tooling.md 3)"
#endif
#include "rt/aggregate.h"

#include <cstddef>
#include <vector>

namespace nift::rt {

struct StorageTestAccess {
    static std::vector<Value>& array_mut(Value& a) { return Storage::array_mut(a); }
    static void array_push(Value& a, Value v) { Storage::array_push(a, static_cast<Value&&>(v)); }
    static void array_set(Value& a, std::size_t i, Value v) { Storage::array_set(a, i, static_cast<Value&&>(v)); }
    static Value* array_slot_mut(Value& a, std::size_t i) { return Storage::array_slot_mut(a, i); }
    static void object_set(Value& o, Atom k, Value v) { Storage::object_set(o, k, static_cast<Value&&>(v)); }
    static bool object_remove(Value& o, Atom k) { return Storage::object_remove(o, k); }
    static Value* object_slot_mut(Value& o, Atom k) { return Storage::object_slot_mut(o, k); }
    static bool struct_set(Value& s, Atom f, Value v) { return Storage::struct_set(s, f, static_cast<Value&&>(v)); }
    static Value* struct_slot_mut(Value& s, Atom f) { return Storage::struct_slot_mut(s, f); }
    static void map_set(Value& m, Value k, Value v) {
        Storage::map_set(m, static_cast<Value&&>(k), static_cast<Value&&>(v));
    }
    static void set_add(Value& s, Value v) { Storage::set_add(s, static_cast<Value&&>(v)); }
    static void collection_erase(Value& c, std::size_t at) { Storage::collection_erase(c, at); }
    static void stack_push(Value& s, Value v) { Storage::stack_push(s, static_cast<Value&&>(v)); }
    static void stack_pop(Value& s) { Storage::stack_pop(s); }
    static void collection_clear(Value& c) { Storage::collection_clear(c); }
};

// The commits under their B1 names (receiver forms: no freshness check).
inline std::vector<Value>& array_mut(Value& a) { return StorageTestAccess::array_mut(a); }
inline void array_set(Value& a, std::size_t i, Value v) { StorageTestAccess::array_set(a, i, static_cast<Value&&>(v)); }
inline Value* array_slot_mut(Value& a, std::size_t i) { return StorageTestAccess::array_slot_mut(a, i); }
inline bool object_remove(Value& o, Atom k) { return StorageTestAccess::object_remove(o, k); }
inline Value* object_slot_mut(Value& o, Atom k) { return StorageTestAccess::object_slot_mut(o, k); }
inline bool struct_set(Value& s, Atom f, Value v) { return StorageTestAccess::struct_set(s, f, static_cast<Value&&>(v)); }
inline Value* struct_slot_mut(Value& s, Atom f) { return StorageTestAccess::struct_slot_mut(s, f); }
inline void map_set(Value& m, Value k, Value v) {
    StorageTestAccess::map_set(m, static_cast<Value&&>(k), static_cast<Value&&>(v));
}
inline void set_add(Value& s, Value v) { StorageTestAccess::set_add(s, static_cast<Value&&>(v)); }
inline void collection_erase(Value& c, std::size_t at) { StorageTestAccess::collection_erase(c, at); }
inline void stack_push(Value& s, Value v) { StorageTestAccess::stack_push(s, static_cast<Value&&>(v)); }
inline void stack_pop(Value& s) { StorageTestAccess::stack_pop(s); }
inline void collection_clear(Value& c) { StorageTestAccess::collection_clear(c); }
// A receiver append / member store (the public array_push / object_set are
// construction and require a fresh value).
inline void array_append(Value& a, Value v) { StorageTestAccess::array_push(a, static_cast<Value&&>(v)); }
inline void object_put(Value& o, Atom k, Value v) { StorageTestAccess::object_set(o, k, static_cast<Value&&>(v)); }

} // namespace nift::rt
