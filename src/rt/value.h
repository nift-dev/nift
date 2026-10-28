#pragma once
// Runtime 2.0 value model (CP09). A Value is 16 bytes: an 8-byte payload, a
// 6-byte auxiliary area and two tag bytes (aux, kind) at the end. Scalars are
// immediate; heap kinds point at a refcounted HeapObject. Refcounts are not
// atomic: a heap belongs to one isolate (architecture decision 7), and values
// cross isolates only by copy or freeze.
//
// Layout (byte offsets):
//   0..7   payload  (bool, int64, double, HeapObject*, handle id) or the first
//                   8 characters of an inline string
//   8..13  extra    (handle generation, inline string characters 8..13)
//   14     aux      (inline string length, handle kind, function flags)
//   15     kind
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>

namespace nift::rt {

class Value;
Value make_string(std::string_view text);  // rt/string.h; befriended for inline strings

// Order matters: every kind that can carry identity (D-040 V16) sorts at or
// after Array (rt/aggregate.cpp relies on it for a one-compare fast path).
enum class Kind : std::uint8_t {
    Null, Bool, Int, Double, Decimal, String, Bytes, Array, Object, Function, Struct, Handle, Error, Cell,
    Future  // CP22: the value an async call returns (rt/future.h); the last free 4-bit tag
};
const char* kind_name(Kind kind);

// Every heap kind starts with this header. `kind` duplicates the Value tag so
// a HeapObject can be destroyed without the owning Value.
struct HeapObject {
    std::uint32_t refcount = 1;
    // Low 4 bits: the Kind (all kinds fit). Bit 7: the D-040 identity summary
    // ("may contain a callable or handle", rt/heap.h). Bits 4-6 are unused.
    std::uint8_t tag;
    // Live-heap accounting (rt/heap_account.h): the footprint last counted
    // for this object, in 64-byte granules (24 bits; objects of 1 GiB or more
    // are counted exactly in a side table); 0 = not counted. Uses the
    // header's padding: HeapObject stays 8 bytes.
    std::uint8_t counted[3] = {0, 0, 0};
    static constexpr std::uint8_t kKindMask = 0x0F;
    explicit HeapObject(Kind k) : tag(static_cast<std::uint8_t>(k)) {}
    Kind kind() const noexcept { return static_cast<Kind>(tag & kKindMask); }
};
static_assert(sizeof(HeapObject) == 8, "HeapObject header must stay 8 bytes");
static_assert(static_cast<unsigned>(Kind::Cell) <= HeapObject::kKindMask, "every Kind must fit the tag's low bits");
static_assert(static_cast<unsigned>(Kind::Cell) == 13, "Cell is the 14th kind");
static_assert(static_cast<unsigned>(Kind::Future) == 14 && static_cast<unsigned>(Kind::Future) <= HeapObject::kKindMask,
              "Future is the 15th kind (4-bit tag; one value remains)");
void destroy_heap_object(HeapObject* object) noexcept;

// Handle kinds are typed tags, never strings (decision 5). Values carry
// (kind, id, generation); registries own the resource (rt/handle.h).
// Values are persisted nowhere, but keep existing numbers stable: append new kinds.
enum class HandleKind : std::uint8_t {
    None, Timer, File, Process, Module, Future, Mutex, Atomic, Ffi, Custom,
    Stream,   // CP16: an open Host stream (legacy "\x1fnift:stream:N")
    Command,  // CP16: a cmd() pipeline builder (legacy "\x1fnift:cmd:N"); started groups are Process
};

// CP17/D-035: a counted handle boxed on the VM heap. The box holds one
// handle-table reference; its refcount (per isolate, like every heap object)
// counts the VM's references. When the last VM reference dies the box is
// destroyed and `release` drops the table reference, which schedules cleanup
// without blocking. The Value keeps (kind, generation) inline for the fast
// checks; the id lives in the box. Built only by rt/handles/handle_box.h.
struct HandleBoxObject : HeapObject {
    std::uint64_t id = 0;
    std::uint32_t generation = 0;
    HandleKind handle_kind = HandleKind::None;
    std::shared_ptr<void> table_owner;  // VM boxes pin storage, never the owning context
    void* table = nullptr;
    void (*release)(void* table, HandleKind kind, std::uint64_t id, std::uint32_t generation) = nullptr;
    HandleBoxObject() : HeapObject(Kind::Handle) {}
};

class Value {
public:
    static constexpr std::size_t kInlineStringCapacity = 14;

    Value() noexcept { std::memset(bytes_, 0, sizeof bytes_); }
    Value(const Value& other) noexcept { std::memcpy(bytes_, other.bytes_, sizeof bytes_); retain(); }
    Value(Value&& other) noexcept {
        std::memcpy(bytes_, other.bytes_, sizeof bytes_);
        std::memset(other.bytes_, 0, sizeof other.bytes_);
    }
    Value& operator=(const Value& other) noexcept {
        if (this != &other) { Value copy(other); swap(copy); }
        return *this;
    }
    Value& operator=(Value&& other) noexcept {
        if (this != &other) { Value moved(static_cast<Value&&>(other)); swap(moved); }
        return *this;
    }
    ~Value() { release(); }
    void swap(Value& other) noexcept {
        unsigned char tmp[16];
        std::memcpy(tmp, bytes_, 16); std::memcpy(bytes_, other.bytes_, 16); std::memcpy(other.bytes_, tmp, 16);
    }

    static Value null() noexcept { return Value(); }
    static Value boolean(bool b) noexcept { Value v; v.set_kind(Kind::Bool); v.set_payload(b ? 1u : 0u); return v; }
    static Value integer(std::int64_t i) noexcept {
        Value v; v.set_kind(Kind::Int); std::uint64_t u; std::memcpy(&u, &i, 8); v.set_payload(u); return v;
    }
    static Value number(double d) noexcept {
        Value v; v.set_kind(Kind::Double); std::uint64_t u; std::memcpy(&u, &d, 8); v.set_payload(u); return v;
    }
    // Takes ownership of one reference to `object` (kind taken from the object).
    static Value adopt(HeapObject* object) noexcept {
        Value v; v.set_kind(object->kind()); v.set_pointer(object); return v;
    }
    static Value handle(HandleKind kind, std::uint64_t id, std::uint32_t generation) noexcept {
        Value v; v.set_kind(Kind::Handle); v.bytes_[14] = static_cast<unsigned char>(kind);
        v.set_payload(id); std::memcpy(v.bytes_ + 8, &generation, 4); return v;
    }
    // Takes ownership of one reference to `box` (a counted handle).
    static Value adopt_handle_box(HandleBoxObject* box) noexcept {
        Value v; v.set_kind(Kind::Handle); v.bytes_[14] = static_cast<unsigned char>(box->handle_kind);
        v.set_pointer(box); std::memcpy(v.bytes_ + 8, &box->generation, 4); v.bytes_[12] = kBoxedHandle; return v;
    }

    Kind kind() const noexcept { return static_cast<Kind>(bytes_[15]); }
    bool is(Kind k) const noexcept { return kind() == k; }
    bool is_null() const noexcept { return kind() == Kind::Null; }
    bool is_number() const noexcept { return kind() == Kind::Int || kind() == Kind::Double || kind() == Kind::Decimal; }
    bool is_heap() const noexcept { return (heap_kind(kind()) && !inline_string()) || boxed_handle(); }
    bool boxed_handle() const noexcept { return kind() == Kind::Handle && bytes_[12] == kBoxedHandle; }
    bool inline_string() const noexcept { return kind() == Kind::String && (bytes_[14] & kInlineFlag) != 0; }

    bool as_bool() const noexcept { return payload() != 0; }
    std::int64_t as_int() const noexcept { std::int64_t i; std::memcpy(&i, bytes_, 8); return i; }
    double as_double() const noexcept { double d; std::memcpy(&d, bytes_, 8); return d; }
    HeapObject* heap() const noexcept { HeapObject* p; std::memcpy(&p, bytes_, sizeof p); return p; }
    template <class T> T* heap_as() const noexcept { return static_cast<T*>(heap()); }
    HandleKind handle_kind() const noexcept { return static_cast<HandleKind>(bytes_[14]); }
    std::uint64_t handle_id() const noexcept {
        return boxed_handle() ? static_cast<const HandleBoxObject*>(heap())->id : payload();
    }
    // The plain (unboxed) identity of a handle value, for the handle table.
    Value handle_identity() const noexcept { return handle(handle_kind(), handle_id(), handle_generation()); }
    std::uint32_t handle_generation() const noexcept { std::uint32_t g; std::memcpy(&g, bytes_ + 8, 4); return g; }
    std::uint32_t refcount() const noexcept { return is_heap() ? heap()->refcount : 0; }

    std::string_view inline_view() const noexcept {
        return std::string_view(reinterpret_cast<const char*>(bytes_), bytes_[14] & 0x7Fu);
    }

    // Replaces this heap value's object with `adopted` (which must have the
    // same kind), releasing the previous reference. Used by copy-on-write.
    void replace_heap(HeapObject* adopted) noexcept {
        release(); set_kind(adopted->kind()); set_pointer(adopted);
    }

    const unsigned char* raw() const noexcept { return bytes_; }

    // Used by destroy_heap_object to free nested values without recursion:
    // drops this value's reference and, if it was the last one, returns the
    // object (now owned by the caller, not yet destroyed); the value becomes
    // Null either way.
    HeapObject* release_detached() noexcept {
        HeapObject* last = nullptr;
        if (is_heap()) {
            HeapObject* p = heap();
            if (p->refcount != kImmortal && --p->refcount == 0) last = p;
        }
        std::memset(bytes_, 0, sizeof bytes_);
        return last;
    }

    // destroy_heap_object only (pointer reversal, rt/value.cpp): parks a raw,
    // non-owning pointer in a dead child slot as a Null-kind value (so no
    // release ever touches it) and takes it back. Nothing else may call these.
    void park_pointer(HeapObject* p) noexcept { std::memset(bytes_, 0, sizeof bytes_); set_pointer(p); }
    HeapObject* take_parked() noexcept {
        HeapObject* p = heap();
        std::memset(bytes_, 0, sizeof bytes_);
        return p;
    }

    // Refcounts saturate: an object whose count reaches kImmortal is never
    // freed (a leak instead of a use-after-free). Reaching it needs 2^32 - 1
    // live references to one object.
    static constexpr std::uint32_t kImmortal = 0xFFFFFFFFu;

private:
    friend Value make_string(std::string_view text);
    // Inline strings: up to 14 bytes in bytes_[0..13]; aux (byte 14) holds
    // the length with kInlineFlag set (heap strings keep aux == 0). Private:
    // only make_string (which checks the length) may build one, because a
    // longer text would overwrite the aux and kind bytes.
    static Value inline_text(std::string_view text) noexcept {
        Value v; v.set_kind(Kind::String);
        // memcpy with a null source is undefined even for size 0, and an empty
        // std::string_view may carry a null data pointer.
        if (!text.empty()) std::memcpy(v.bytes_, text.data(), text.size());
        v.bytes_[14] = static_cast<unsigned char>(kInlineFlag | text.size());
        return v;
    }

    static constexpr unsigned char kInlineFlag = 0x80;
    static constexpr unsigned char kBoxedHandle = 0x01;  // bytes_[12] of a boxed handle
    static bool heap_kind(Kind k) noexcept {
        switch (k) {
            case Kind::Decimal: case Kind::String: case Kind::Bytes: case Kind::Array: case Kind::Object:
            case Kind::Function: case Kind::Struct: case Kind::Error: case Kind::Cell: case Kind::Future:
                return true;
            default: return false;
        }
    }
    std::uint64_t payload() const noexcept { std::uint64_t u; std::memcpy(&u, bytes_, 8); return u; }
    void set_payload(std::uint64_t u) noexcept { std::memcpy(bytes_, &u, 8); }
    void set_pointer(HeapObject* p) noexcept { std::memcpy(bytes_, &p, sizeof p); }
    void set_kind(Kind k) noexcept { bytes_[15] = static_cast<unsigned char>(k); }
    void retain() const noexcept {
        if (is_heap() && heap()->refcount != kImmortal) ++heap()->refcount;
    }
    void release() noexcept {
        if (!is_heap()) return;
        HeapObject* p = heap();
        if (p->refcount != kImmortal && --p->refcount == 0) destroy_heap_object(p);
    }

    alignas(8) unsigned char bytes_[16];
};

static_assert(sizeof(Value) == 16, "rt::Value must be 16 bytes");
static_assert(alignof(Value) == 8, "rt::Value must be 8-byte aligned");

} // namespace nift::rt
