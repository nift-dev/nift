#pragma once
// Heap object layouts for rt::Value (CP09). Each starts with HeapObject and is
// created with one reference owned by the caller (wrap it with Value::adopt).
#include "rt/atom.h"
#include "rt/value.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace nift::rt {

// Immutable text; strings longer than Value::kInlineStringCapacity live here.
struct StringObject : HeapObject {
    std::string text;
    explicit StringObject(std::string t) : HeapObject(Kind::String), text(std::move(t)) {}
};

// Exact decimal number (large or high-precision JSON numbers). `spelling` is
// the original decimal text; `approx` is its nearest double, which the legacy
// engine also used for inexact arithmetic.
struct DecimalObject : HeapObject {
    std::string spelling;
    double approx;
    DecimalObject(std::string s, double a) : HeapObject(Kind::Decimal), spelling(std::move(s)), approx(a) {}
};

struct BytesObject : HeapObject {
    std::vector<std::uint8_t> data;
    explicit BytesObject(std::vector<std::uint8_t> d) : HeapObject(Kind::Bytes), data(std::move(d)) {}
};

// Identity summary (D-040 V16, the cycle rule): bit 7 of the header's third
// `counted` byte (accounting uses the low 23 bits) means "this aggregate may
// contain a callable or a handle, directly or through a child with the bit".
// It is conservative: set whenever a store might add identity-bearing content
// (array_mut hands out raw storage, so it sets it too) and cleared only by
// the cycle checker after it has verified the direct children (rt/value_cycle.h).
// Plain data therefore never needs a walk: an aggregate whose bit is clear
// holds no callable or handle anywhere below it.
inline constexpr std::uint8_t kMayHoldIdentity = 0x80;
inline bool may_hold_identity(const HeapObject* object) noexcept {
    return (object->counted[2] & kMayHoldIdentity) != 0;
}
inline void mark_may_hold_identity(HeapObject* object) noexcept { object->counted[2] |= kMayHoldIdentity; }
inline void clear_may_hold_identity(HeapObject* object) noexcept {
    object->counted[2] = static_cast<std::uint8_t>(object->counted[2] & ~kMayHoldIdentity);
}

// Copy-on-write aggregates: a Value copy shares the object; mutation through
// rt::array_mut / rt::object_mut clones it first when refcount > 1.
struct ArrayObject : HeapObject {
    std::vector<Value> items;
    ArrayObject() : HeapObject(Kind::Array) {}
};

// Insertion-ordered map from atoms to values. Small objects are searched
// linearly; from kIndexThreshold entries an open-addressing index (atom ->
// position + 1) is maintained.
struct ObjectObject : HeapObject {
    static constexpr std::size_t kIndexThreshold = 8;
    std::vector<std::pair<Atom, Value>> entries;
    std::vector<std::uint32_t> index;  // empty below the threshold
    ObjectObject() : HeapObject(Kind::Object) {}
};

// Callables have identity semantics (aliases compare equal, distinct
// closures do not). `code` and `environment` are opaque until the compiler
// (CP11) defines protos; natives use `native_id`.
struct FunctionObject : HeapObject {
    std::string name;
    const void* code = nullptr;
    std::uint32_t native_id = 0;
    std::vector<Value> upvalues;
    FunctionObject() : HeapObject(Kind::Function) {}
};

// Struct instances are values (D-040 V9): copies share the object until one
// of them is written, then rt::struct_set / struct_mut clone it (copy-on-write,
// like arrays and objects). Fields are kept in declaration order, private
// fields included; equality is structural (type and every field).
struct StructObject : HeapObject {
    Atom type_name;
    std::vector<std::pair<Atom, Value>> fields;
    explicit StructObject(Atom type) : HeapObject(Kind::Struct), type_name(type) {}
};

// Immutable error value (legacy RuntimeErrorData): message, code, category,
// origin and an optional cause chain (at most 16 links).
struct ErrorObject : HeapObject {
    std::string message, code, category, source;
    std::size_t line = 0, column = 0;
    Value cause;  // Null or Error
    ErrorObject() : HeapObject(Kind::Error) {}
};

} // namespace nift::rt
