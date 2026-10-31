#include "rt/natives/collection_methods.h"

#include "rt/aggregate.h"
#include "rt/compare.h"
#include "rt/natives/support.h"

#include <utility>

// rt-lint: cold-file collection natives (D-063); hot paths are the per-call bodies only

namespace nift::rt::natives {

namespace {

Status internal(std::string& error, std::string what) {
    error = std::move(what);
    return Status::Internal;
}

Status make(CollectionKind kind, std::uint32_t argc, Value& out, std::string& error, const char* name) {
    if (argc != 0) return internal(error, std::string(name) + ": arguments");  // compiler: exact `name()`
    out = make_collection(kind);
    return Status::Ok;
}

const Value* receiver(const Value* args, std::uint32_t argc) {
    return argc != 0 && args[0].is(Kind::Collection) ? &args[0] : nullptr;
}

} // namespace

// Legacy accepts bool, number and string keys; its collections and
// callables are tagged strings, so they are accepted too (a collection key
// compares structurally in legacy, a callable by identity: rt::equal does
// the same). Structs and handles are also tagged strings in legacy: the
// compiler refuses them as keys (struct equality is a value comparison here).
bool collection_key_ok(const Value& key) noexcept {
    return key.is(Kind::Bool) || key.is_number() || key.is(Kind::String) || key.is(Kind::Collection) ||
           key.is(Kind::Function);
}

Status fn_map(const Context&, const Value*, std::uint32_t argc, Value& out, std::string& error) {
    return make(CollectionKind::Map, argc, out, error, "map");
}
Status fn_set(const Context&, const Value*, std::uint32_t argc, Value& out, std::string& error) {
    return make(CollectionKind::Set, argc, out, error, "set");
}
Status fn_stack(const Context&, const Value*, std::uint32_t argc, Value& out, std::string& error) {
    return make(CollectionKind::Stack, argc, out, error, "stack");
}

// map.get(k): the value, or legacy's fatal "get: key not found".
Status coll_get(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const Value* c = receiver(args, argc);
    if (!c || collection_kind(*c) != CollectionKind::Map) return internal(error, "get receiver");
    if (argc != 2) return fail(error, "get: expected one key");
    const std::ptrdiff_t at = collection_find(*c, args[1]);
    if (at < 0) return fail(error, "get: key not found");
    out = collection_items(*c)[static_cast<std::size_t>(at) * 2 + 1];
    return Status::Ok;
}

// contains(v): a map key, a set member (bool/number/string only, else legacy's
// error), or a stack element by structural equality.
Status coll_contains(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const Value* c = receiver(args, argc);
    if (!c) return internal(error, "contains receiver");
    const CollectionKind kind = collection_kind(*c);
    if (argc != 2) return fail(error, kind == CollectionKind::Map ? "contains: expected one key"
                                                                  : "contains: expected one value");
    if (kind == CollectionKind::Set && !collection_key_ok(args[1]))
        return fail(error, "contains: set values must be bool, number, or string");
    bool found = false;
    if (kind == CollectionKind::Stack) {
        for (const Value& item : collection_items(*c))
            if (equal(args[1], item)) { found = true; break; }
    } else {
        found = collection_find(*c, args[1]) >= 0;
    }
    out = Value::boolean(found);
    return Status::Ok;
}

Status coll_size(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const Value* c = receiver(args, argc);
    if (!c) return internal(error, "size receiver");
    if (!no_arguments(argc, "size", error)) return Status::Error;
    out = Value::integer(static_cast<std::int64_t>(collection_size(*c)));
    return Status::Ok;
}

Status coll_empty(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const Value* c = receiver(args, argc);
    if (!c) return internal(error, "empty receiver");
    if (!no_arguments(argc, "empty", error)) return Status::Error;
    out = Value::boolean(collection_size(*c) == 0);
    return Status::Ok;
}

// stack.top(): the last pushed element; stack.front(): the first (legacy).
Status coll_top(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const Value* c = receiver(args, argc);
    if (!c || collection_kind(*c) != CollectionKind::Stack) return internal(error, "top receiver");
    if (!no_arguments(argc, "top", error)) return Status::Error;
    if (collection_items(*c).empty()) return fail(error, "top: collection is empty");
    out = collection_items(*c).back();
    return Status::Ok;
}

Status coll_front(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const Value* c = receiver(args, argc);
    if (!c || collection_kind(*c) != CollectionKind::Stack) return internal(error, "front receiver");
    if (!no_arguments(argc, "front", error)) return Status::Error;
    if (collection_items(*c).empty()) return fail(error, "front: collection is empty");
    out = collection_items(*c).front();
    return Status::Ok;
}

// same(a, b) (D-040 #4, D-063): identity of identity-bearing values:
// callables and file/stream handles (legacy-equal), and mutex, atomic,
// channel, future and thread (owner ruling 2026-11-01; legacy errors) by
// their stable handle or task record (rt::same). The compiler admits plain
// names only. Any other operand (ordinary values: arrays, objects, structs,
// collections) is legacy's fatal error text.
Status fn_same(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc != 2) return fail(error, "same: expected two values");
#if defined(NIFT_COLLECTIONS_MUTANT) && NIFT_COLLECTIONS_MUTANT == 10
    if (args[0].is(Kind::Collection) && args[1].is(Kind::Collection)) {  // M10: storage identity exposed
        out = Value::boolean(args[0].heap() == args[1].heap());
        return Status::Ok;
    }
#endif
    switch (same(args[0], args[1])) {
        case Sameness::Same: out = Value::boolean(true); return Status::Ok;
        case Sameness::Different: out = Value::boolean(false); return Status::Ok;
        case Sameness::NotIdentity: break;
    }
    return fail(error, "same: operands must be identity-bearing values");
}

} // namespace nift::rt::natives
