#pragma once
// Construction of fresh aggregates (tooling checkpoint, component 2;
// docs/runtime-2/checkpoints/tooling.md section 2). Included at the end of
// rt/aggregate.h, so every translation unit that sees rt::Value's aggregates
// sees these complete definitions (none can be redefined).
//
// Three kinds of write, three authorities:
//   construction  a builder (build::Array, build::ArrayBulk, build::Object,
//                 build::Struct) owns a new value no other code can reach; it
//                 is the only way to populate one. A builder is move-only, has
//                 no constructor from an existing Value, and publish() &&
//                 moves the value out: from then on the builder holds nothing
//                 (its Value is null), so no use of it, before or after any
//                 number of moves, can reach the published value. Using a
//                 spent builder aborts (a harmless, deterministic error).
//   in place      the few constructions that cannot use a builder (a value
//                 under construction in a VM register or a resumable native's
//                 state slot, the post-order fill of a graph copy, a cell's
//                 private descriptor; tooling.md 2.4) take an InPlaceKey,
//                 which only their named owners can create.
//   mutation      a published value changes only through the mutation
//                 protocol (rt/mutation.h).
// The primitives below are the former free construction functions, unchanged
// and out of line (same calls, same allocations); only builders, key holders
// and the storage-level tests' accessor reach them.
#include "rt/aggregate.h"
#include "rt/heap.h"
#include "rt/heap_account.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <vector>

namespace nift::rt {

class VmInPlace;
struct Fault;
struct Note;
class CycleGraph;
enum class CollectionMethod : std::uint16_t;
enum class CopyStatus;
namespace natives {
struct Context;
enum class Status : std::uint8_t;
}
// The owners of the in-place construction capability (tooling.md 2.4), declared
// here so the key can befriend them; each is defined once, in its own file.
bool collection_step(CollectionMethod, std::uint16_t, Value*, const natives::Context&, Fault&);
bool collection_sort_init(Value*, const natives::Context&, Fault&, std::size_t);
bool collection_plain_sort(Value*, const natives::Context&, Fault&);
CopyStatus deep_copy_tuple(const Value*, std::size_t, Value*, std::size_t, bool);
bool cell_read(const Value&, Value&, std::string&);
bool set_index(const Note*, Value&, const Value&, const Value&, Fault&, const CycleGraph*, bool*);
namespace natives {
Status obj_merge(const Context&, const Value*, std::uint32_t, Value&, std::string&);
}

namespace build {
class Array;
class ArrayBulk;
class Object;
class Struct;

// Using a builder after publish() (or after it was moved from). A spent
// builder holds no value (its Value is null, its storage pointer null), so it
// cannot reach the published value whether or not this check runs: that is
// the guarantee, not the check. Checked builds (sanitizers, NIFT_RT_CHECKED_REFS:
// aggregate_store.h) abort deterministically here; release builds do not check
// (local callgrind: 10 to 20 instructions per literal or native result,
// tooling.md 2.6), and a use there dereferences the builder's own null value.
// NIFT_RT_BUILDER_UNCHECKED (measurement only) drops the check everywhere.
// Checked builds: the condition of aggregate_store.h's NIFT_RT_RELOCATE_ON_MUTATION
// (that header stays private to the storage layer; aggregate.cpp asserts the two agree).
#if defined(__SANITIZE_ADDRESS__) || defined(NIFT_RT_CHECKED_REFS)
#define NIFT_RT_BUILDER_CHECKED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define NIFT_RT_BUILDER_CHECKED 1
#endif
#endif
[[noreturn]] inline void spent() noexcept { std::abort(); }
// How a builder gives up its value: publish() moves it out, a move hands it on;
// either way the builder is left holding nothing. Two test-only mutants
// (tests/rt/build_unit.cpp, make test-rt-build) break exactly that:
//   B-M1 (NIFT_RT_BUILDER_MUTANT=1) publish() copies the value out and the
//        builder keeps it (storage authority retained after publication);
//   B-M2 (NIFT_RT_BUILDER_MUTANT=2) a move copies, so the moved-from builder
//        keeps the value.
#if defined(NIFT_RT_BUILDER_MUTANT) && NIFT_RT_BUILDER_MUTANT == 1
#define NIFT_RT_BUILDER_RELEASE(v) (v)
#else
#define NIFT_RT_BUILDER_RELEASE(v) static_cast<Value&&>(v)
#endif
#if defined(NIFT_RT_BUILDER_MUTANT) && NIFT_RT_BUILDER_MUTANT == 2
#define NIFT_RT_BUILDER_TRANSFER(v) (v)
#else
#define NIFT_RT_BUILDER_TRANSFER(v) static_cast<Value&&>(v)
#endif
inline void check_live(bool live) noexcept {
#if defined(NIFT_RT_BUILDER_CHECKED) && !defined(NIFT_RT_BUILDER_UNCHECKED)
    if (__builtin_expect(!live, 0)) spent();
#else
    (void)live;
#endif
}

// The in-place construction capability. Its only constructor is private and
// user-provided (not an aggregate), it is not copyable or movable and not
// trivially copyable (no bit-copy), and only these owners can construct one:
//   the Vm members Vm::cold (ObjectSet) and Vm::struct_op (StructAdd), through
//     VmInPlace (rt/vm_in_place.h, which befriends exactly those members; and
//     Vm::op_keyed_mutate in the R14 test mutant build only), collection_step
//     (the resumable collection natives' result and scratch slots),
//     collection_sort_init (the keep-alive scratch), collection_plain_sort (the
//     order scratch), deep_copy_tuple (the post-order fill of a graph copy),
//     cell_read (a selector cell's private descriptor); set_index and
//     natives::obj_merge only in their M-B2 and R-B5 test mutant builds.
// This refuses construction, not deliberate forgery: a reference to a key can
// be forged (a cast, an inactive union member) and explicit instantiation
// reaches the private primitives without one, as for mutation::SortKey
// (tooling.md 2.4, 4.2). The destructor is public (an owner's local is
// destroyed in its own scope) and user-provided (no bit-copy).
class InPlaceKey {
    InPlaceKey() {}
    InPlaceKey(const InPlaceKey&) = delete;
    InPlaceKey& operator=(const InPlaceKey&) = delete;
    friend class nift::rt::VmInPlace;
    friend bool nift::rt::collection_step(CollectionMethod, std::uint16_t, Value*, const natives::Context&, Fault&);
    friend bool nift::rt::collection_sort_init(Value*, const natives::Context&, Fault&, std::size_t);
    friend bool nift::rt::collection_plain_sort(Value*, const natives::Context&, Fault&);
    friend CopyStatus nift::rt::deep_copy_tuple(const Value*, std::size_t, Value*, std::size_t, bool);
    friend bool nift::rt::cell_read(const Value&, Value&, std::string&);
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 30
    friend bool nift::rt::set_index(const Note*, Value&, const Value&, const Value&, Fault&, const CycleGraph*,
                                    bool*);
#endif
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 26
    friend natives::Status nift::rt::natives::obj_merge(const natives::Context&, const Value*, std::uint32_t,
                                                         Value&, std::string&);
#endif

public:
    ~InPlaceKey() {}
};
} // namespace build

class Construction {
    friend class build::Array;
    friend class build::ArrayBulk;
    friend class build::Object;
    friend class build::Struct;
    friend struct StorageTestAccess;  // rt-lint: test-access-decl (the tests' construction shims)
    // The former free construction functions (out of line, unchanged; checked
    // builds abort unless the receiver is fresh: aggregate_store.h check_fresh).
    // The stored values are taken by rvalue reference: the caller's by-value
    // parameter (a builder operation, a key forwarder) is built exactly where the
    // former by-value parameter was, so no Value is moved more than before.
    static void array_push(Value& array, Value&& item);
    static std::vector<Value>& array_storage(Value& array);  // marks it as possibly holding references
    static void array_set(Value& array, std::size_t index, Value&& item);
    static void object_set(Value& object, Atom key, Value&& value);
    static Value* object_slot(Value& object, Atom key);  // null when `key` is missing
    static void struct_add_field(Value& instance, Atom field, Value&& value);
    // The collection primitives keep their former by-value parameters: they share
    // their bodies with Storage's map_set/set_add/stack_push (the mutation
    // protocol's commits), and the same signature keeps that code identical
    // (tooling.md 2.6); their key forwarders below forward the arguments as given.
    static void map_set(Value& map, Value key, Value value);
    static void set_add(Value& set, Value member);
    static void stack_push(Value& stack, Value item);

public:
    // In-place construction: the same primitives, for an InPlaceKey holder
    // (inline: the key costs nothing).
    using Key = build::InPlaceKey;
    static void array_push(const Key&, Value& array, Value item) { array_push(array, static_cast<Value&&>(item)); }
    static std::vector<Value>& array_storage(const Key&, Value& array) { return array_storage(array); }
    static void array_set(const Key&, Value& array, std::size_t index, Value item) {
        array_set(array, index, static_cast<Value&&>(item));
    }
    static void object_set(const Key&, Value& object, Atom key, Value value) {
        object_set(object, key, static_cast<Value&&>(value));
    }
    static Value* object_slot(const Key&, Value& object, Atom key) { return object_slot(object, key); }
    static void struct_add_field(const Key&, Value& instance, Atom field, Value value) {
        struct_add_field(instance, field, static_cast<Value&&>(value));
    }
    template <class K, class V> static void map_set(const Key&, Value& map, K&& key, V&& value) {
        map_set(map, static_cast<K&&>(key), static_cast<V&&>(value));
    }
    template <class M> static void set_add(const Key&, Value& set, M&& member) {
        set_add(set, static_cast<M&&>(member));
    }
    template <class I> static void stack_push(const Key&, Value& stack, I&& item) {
        stack_push(stack, static_cast<I&&>(item));
    }
};

namespace build {

// A new array, element by element (each push as the former array_push: the
// heap tally is updated per push).
class Array {
public:
    explicit Array(std::size_t reserve = 0) : v_(make_array(reserve)) {}
    // A new array with `source`'s elements (array_shallow_clone), to change some.
    static Array shallow_clone(const Value& source) { return Array(array_shallow_clone(source)); }
    Array(Array&& other) noexcept : v_(NIFT_RT_BUILDER_TRANSFER(other.v_)) {}
    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;
    Array& operator=(Array&&) = delete;

    void push(Value item) & { Construction::array_push(live(), static_cast<Value&&>(item)); }
    void set(std::size_t index, Value item) & { Construction::array_set(live(), index, static_cast<Value&&>(item)); }
    void drop_last() & { Construction::array_storage(live()).pop_back(); }
    std::size_t size() const { return array_size(live_view()); }
    // The new value; the builder holds nothing afterwards.
    [[nodiscard]] Value publish() && { live(); return NIFT_RT_BUILDER_RELEASE(v_); }

private:
    explicit Array(Value fresh) : v_(static_cast<Value&&>(fresh)) {}
    Value& live() { check_live(v_.is(Kind::Array)); return v_; }
    const Value& live_view() const { check_live(v_.is(Kind::Array)); return v_; }
    Value v_;
};

// A new array filled through its raw storage, the natives' bulk form (as
// natives::ArrayBuilder always did): one storage hand-out at construction (the
// array is then conservatively marked as possibly holding references), plain
// appends, and the heap tally when the caller asks (tally()).
class ArrayBulk {
public:
    explicit ArrayBulk(std::size_t reserve) : v_(make_array(reserve)), items_(&Construction::array_storage(v_)) {}
    ArrayBulk(ArrayBulk&& other) noexcept : v_(NIFT_RT_BUILDER_TRANSFER(other.v_)), items_(other.items_) {
#if !(defined(NIFT_RT_BUILDER_MUTANT) && NIFT_RT_BUILDER_MUTANT == 2)
        other.items_ = nullptr;
#endif
    }
    ArrayBulk(const ArrayBulk&) = delete;
    ArrayBulk& operator=(const ArrayBulk&) = delete;
    ArrayBulk& operator=(ArrayBulk&&) = delete;

    void append(Value&& item) & { live().push_back(static_cast<Value&&>(item)); }
    void append(const Value& item) & { live().push_back(item); }
    std::size_t size() const { check_live(items_ != nullptr); return items_->size(); }
    void tally() & { live(); heap_retally(v_.heap()); }
    // The exact summary bit when the caller knows no element holds a reference.
    void holds_no_reference() & { live(); clear_may_hold_identity(v_.heap()); }
    [[nodiscard]] Value publish() && {
        live();
#if !(defined(NIFT_RT_BUILDER_MUTANT) && NIFT_RT_BUILDER_MUTANT == 1)
        items_ = nullptr;
#endif
        return NIFT_RT_BUILDER_RELEASE(v_);
    }

private:
    std::vector<Value>& live() { check_live(items_ != nullptr); return *items_; }
    Value v_;
    std::vector<Value>* items_;
};

// A new insertion-ordered object (set as the former object_set: insert at the
// end or replace in place).
class Object {
public:
    Object() : v_(make_object()) {}
    // A new object with `source`'s entries and index (object_shallow_clone), to change some; as
    // build::Array::shallow_clone (owner 2026-11-10 08:57: merge/merge_deep's one-step clone).
    static Object shallow_clone(const Value& source) { return Object(object_shallow_clone(source)); }
    Object(Object&& other) noexcept : v_(NIFT_RT_BUILDER_TRANSFER(other.v_)) {}
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object& operator=(Object&&) = delete;

    void set(Atom key, Value value) & { Construction::object_set(live(), key, static_cast<Value&&>(value)); }
    const Value* get(Atom key) const { return object_get(live_view(), key); }
    [[nodiscard]] Value publish() && { live(); return NIFT_RT_BUILDER_RELEASE(v_); }

private:
    explicit Object(Value fresh) : v_(static_cast<Value&&>(fresh)) {}
    Value& live() { check_live(v_.is(Kind::Object)); return v_; }
    const Value& live_view() const { check_live(v_.is(Kind::Object)); return v_; }
    Value v_;
};

// A new struct instance, fields in declaration order.
class Struct {
public:
    Struct(const std::shared_ptr<const StructType>& type, std::size_t reserve) : v_(make_struct(type, reserve)) {}
    Struct(Struct&& other) noexcept : v_(NIFT_RT_BUILDER_TRANSFER(other.v_)) {}
    Struct(const Struct&) = delete;
    Struct& operator=(const Struct&) = delete;
    Struct& operator=(Struct&&) = delete;

    void add(Atom field, Value value) & {
        check_live(v_.is(Kind::Struct));
        Construction::struct_add_field(v_, field, static_cast<Value&&>(value));
    }
    [[nodiscard]] Value publish() && {
        check_live(v_.is(Kind::Struct));
        return NIFT_RT_BUILDER_RELEASE(v_);
    }

private:
    Value v_;
};

} // namespace build

#ifdef NIFT_PRODUCTION_TU
// Tooling checkpoint (component 3, src/build/production_tu.h): in a production
// translation unit the storage-level tests' accessor is unreachable. This is the
// end of rt/aggregate.h and rt/build.h whichever is included first, so it
// follows their uses of the name. No
// definition may precede this header (an earlier definition could otherwise
// name Storage through a template instantiated later), and after it the name is
// poisoned: any later definition, declaration or use is a compile error.
namespace test_access_detail {
template <class T, class = void> struct complete : std::false_type {};
template <class T> struct complete<T, decltype(void(sizeof(T)))> : std::true_type {};
} // namespace test_access_detail
static_assert(!test_access_detail::complete<StorageTestAccess>::value,  // rt-lint: test-access-decl
              "the storage-level tests' accessor is defined in a production translation unit");
#pragma GCC poison StorageTestAccess  // rt-lint: test-access-decl
#endif
} // namespace nift::rt
