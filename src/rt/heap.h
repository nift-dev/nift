#pragma once
// Heap object layouts for rt::Value (CP09). Each starts with HeapObject and is
// created with one reference owned by the caller (wrap it with Value::adopt).
#include "rt/atom.h"
#include "rt/value.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace nift::rt {
struct Program;

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

// Identity summary (D-040 V16, the cycle rule): bit 7 of the header's tag
// byte (the Kind uses the low 4 bits) means "this aggregate may contain an
// identity-bearing value (a callable, a handle, a cell, a future and, since
// D-NEXT-refsem, a struct instance or a collection), directly or through a
// child with the bit". It is
// conservative: set whenever a store might add identity-bearing content
// (array_mut hands out raw storage, so it sets it too), copied by
// copy-on-write clones, and cleared only by the cycle checker after it has
// verified the direct children (rt/value_cycle.h). Removing the last
// identity-bearing value from a container leaves the bit set (conservative,
// never missing). Plain data therefore never needs a walk: an aggregate whose
// bit is clear holds no identity-bearing value anywhere below it.
inline constexpr std::uint8_t kMayHoldIdentity = 0x80;
inline bool may_hold_identity(const HeapObject* object) noexcept { return (object->tag & kMayHoldIdentity) != 0; }
inline void mark_may_hold_identity(HeapObject* object) noexcept { object->tag |= kMayHoldIdentity; }
inline void clear_may_hold_identity(HeapObject* object) noexcept {
    object->tag = static_cast<std::uint8_t>(object->tag & ~kMayHoldIdentity);
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
    bool is_lambda = false;
    bool expression_body = false;
    bool selector_call_site = false;  // returned expression body lacks a legacy source view
    std::vector<Value> upvalues;
    std::shared_ptr<const Program> program_owner;  // session code lifetime, never a state bridge
    std::uint64_t session_identity = 0;  // stable across session named-function redefinition
    std::uint64_t display_id = 0;  // CLI presentation only; callable identity remains the Value
    FunctionObject() : HeapObject(Kind::Function) {}
    // Keep optional Program-owner cleanup in the existing destruction path;
    // an out-of-line default destructor adds cost even for ordinary closures.
#if defined(__GNUC__)
    __attribute__((always_inline))
#endif
    ~FunctionObject() = default;
};

// CP12C: a captured binding. Internal only: never reachable from script values,
// natives or Host; the VM's cell opcodes and the D-040 cycle graph are its only
// users. `binding_type` is the legacy binding tag fixed at declaration, or
// kSelectorCell for a GC-visible location descriptor (see cell.h).
struct CellObject : HeapObject {
    Value value;
    std::int64_t binding_type = -1;
    CellObject() : HeapObject(Kind::Cell) {}
};

// Struct instances are identity-bearing mutable references (D-NEXT-refsem
// #1, superseding D-040 V9 and D-046 #3): every Value holding the instance
// shares the object, and rt::struct_set mutates it in place. Fields are kept
// in declaration order, private fields included; equality is identity
// (legacy's rule, D-NEXT-refsem #2).
struct StructObject : HeapObject {
    Atom type_name;
    std::vector<std::pair<Atom, Value>> fields;
    explicit StructObject(Atom type) : HeapObject(Kind::Struct), type_name(type) {}
};

// Weekend collections (D-063): map(), set() and stack(). Identity-bearing
// mutable references (D-NEXT-refsem #1, superseding D-063 #1): every Value
// holding the collection shares the object, and the mutators of
// rt/aggregate_collection.h change it in place. `items` holds, for a map,
// key/value pairs flattened
// (key at 2i, value at 2i + 1, insertion order); for a set its members
// (insertion order); for a stack its elements bottom to top. Map keys and set
// members are bool, number, string or callable (legacy also accepts its
// tagged collections; the VM refuses mutable collection keys before any
// effect, D-NEXT-refsem #6). From kIndexThreshold
// entries an open-addressing index (rt::hash of the key -> entry + 1) is kept.
enum class CollectionKind : std::uint8_t { Map, Set, Stack };
struct CollectionObject : HeapObject {
    static constexpr std::size_t kIndexThreshold = 8;
    CollectionKind sub;
    std::vector<Value> items;
    std::vector<std::uint32_t> index;  // map/set only; empty below the threshold
    explicit CollectionObject(CollectionKind k) : HeapObject(Kind::Collection), sub(k) {}
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
