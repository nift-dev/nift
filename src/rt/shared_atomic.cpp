#include "rt/shared_atomic.h"

#include "rt/shared_state_internal.h"

#include <limits>

namespace nift::rt {

using namespace shared_detail;

namespace {


enum class AtomicOp { Add, Sub, Mod, And, Or, Xor };

bool atomic_update(const std::shared_ptr<Entry>& entry, AtomicOp op, std::int64_t operand, std::int64_t& before_out,
                   std::int64_t& result, const char* label, SharedOpError& error) {
    if (entry->kind != EntryKind::AtomicInt) {
        fail(error, "runtime.invalid_argument", std::string(label) + ": only valid for atomic<int>");
        return false;
    }
    if (op == AtomicOp::Mod && operand == 0) {
        fail(error, "runtime.invalid_argument", std::string(label) + ": modulo by zero");
        return false;
    }
    std::int64_t before = entry->integer.load();
    for (;;) {
        std::int64_t after = 0;
        bool ok = true;
        switch (op) {
            case AtomicOp::Add:
                ok = !__builtin_add_overflow(before, operand, &after);
                break;
            case AtomicOp::Sub:
                ok = !__builtin_sub_overflow(before, operand, &after);
                break;
            case AtomicOp::Mod:
                after = (before == std::numeric_limits<std::int64_t>::min() && operand == -1) ? 0 : before % operand;
                break;
            case AtomicOp::And: after = before & operand; break;
            case AtomicOp::Or: after = before | operand; break;
            case AtomicOp::Xor: after = before ^ operand; break;
        }
        if (!ok) {
            fail(error, "runtime.atomic_overflow", std::string(label) + ": integer overflow");
            return false;
        }
        if (entry->integer.compare_exchange_weak(before, after)) {
            before_out = before;
            result = after;
            return true;
        }
    }
}

}  // namespace

bool shared_atomic_load(const Value& handle, Value& out, SharedOpError& error) {
    const auto entry = find_entry(handle, HandleKind::Atomic);
    if (!entry) {
        fail(error, "runtime.invalid_argument", "atomic: invalid handle");
        return false;
    }
    if (entry->kind == EntryKind::AtomicBool) out = Value::boolean(entry->boolean.load());
    else out = Value::integer(entry->integer.load());
    return true;
}

bool shared_atomic_store(const Value& handle, const Value& value, SharedOpError& error) {
    const auto entry = find_entry(handle, HandleKind::Atomic);
    if (!entry) {
        fail(error, "runtime.invalid_argument", "atomic: invalid handle");
        return false;
    }
    if (entry->kind == EntryKind::AtomicBool) {
        if (!value.is(Kind::Bool)) {
            fail(error, "runtime.invalid_argument", "store: atomic<bool> requires bool");
            return false;
        }
        entry->boolean.store(value.as_bool());
        return true;
    }
    std::int64_t v = 0;
    if (!int_operand(value, "store", v, error)) return false;
    entry->integer.store(v);
    return true;
}

bool shared_atomic_exchange(const Value& handle, const Value& value, Value& old, SharedOpError& error) {
    const auto entry = find_entry(handle, HandleKind::Atomic);
    if (!entry) {
        fail(error, "runtime.invalid_argument", "atomic: invalid handle");
        return false;
    }
    if (entry->kind == EntryKind::AtomicBool) {
        if (!value.is(Kind::Bool)) {
            fail(error, "runtime.invalid_argument", "exchange: atomic<bool> requires bool");
            return false;
        }
        old = Value::boolean(entry->boolean.exchange(value.as_bool()));
        return true;
    }
    std::int64_t v = 0;
    if (!int_operand(value, "exchange", v, error)) return false;
    old = Value::integer(entry->integer.exchange(v));
    return true;
}

bool shared_atomic_fetch(const Value& handle, const Value& operand, bool subtract, Value& out,
                         SharedOpError& error) {
    const auto entry = find_entry(handle, HandleKind::Atomic);
    if (!entry) {
        fail(error, "runtime.invalid_argument", "atomic: invalid handle");
        return false;
    }
    std::int64_t v = 0;
    const char* label = subtract ? "fetch_sub" : "fetch_add";
    if (!int_operand(operand, label, v, error)) return false;
    if (entry->kind != EntryKind::AtomicInt) {
        fail(error, "runtime.invalid_argument", std::string(label) + ": only valid for atomic<int>");
        return false;
    }
    std::int64_t after = 0, before = 0;
    if (!atomic_update(entry, subtract ? AtomicOp::Sub : AtomicOp::Add, v, before, after, label, error))
        return false;
    (void)after;
    out = Value::integer(before);
    return true;
}

bool shared_atomic_compare_exchange(const Value& handle, const Value& expected, const Value& desired, bool& out,
                                    SharedOpError& error) {
    const auto entry = find_entry(handle, HandleKind::Atomic);
    if (!entry) {
        fail(error, "runtime.invalid_argument", "atomic: invalid handle");
        return false;
    }
    if (entry->kind == EntryKind::AtomicBool) {
        if (!expected.is(Kind::Bool) || !desired.is(Kind::Bool)) {
            fail(error, "runtime.invalid_argument", "compare_exchange: atomic<bool> requires bool values");
            return false;
        }
        bool e = expected.as_bool();
        out = entry->boolean.compare_exchange_strong(e, desired.as_bool());
        return true;
    }
    std::int64_t e = 0, d = 0;
    if (!int_operand(expected, "compare_exchange", e, error) ||
        !int_operand(desired, "compare_exchange", d, error))
        return false;
    out = entry->integer.compare_exchange_strong(e, d);
    return true;
}

bool shared_atomic_compound(const Value& handle, int op, const Value& operand, bool return_old, Value& out,
                            SharedOpError& error) {
    const auto entry = find_entry(handle, HandleKind::Atomic);
    if (!entry) {
        fail(error, "runtime.invalid_argument", "atomic: invalid handle");
        return false;
    }
    if (entry->kind != EntryKind::AtomicInt) {
        fail(error, "runtime.invalid_argument", "atomic operation requires atomic<int>");
        return false;
    }
    std::int64_t v = 0;
    if (!int_operand(operand, "atomic operation", v, error)) return false;
    static const char* const labels[] = {"+=", "-=", "%=", "&=", "|=", "^=", "*=", "/="};
    const char* label = (op >= 0 && op < 8) ? labels[op] : "atomic operation";
    if (op < 0 || op > 5) {
        fail(error, "runtime.invalid_argument", std::string(label) + ": not supported for atomic<int>");
        return false;
    }
    const AtomicOp which = static_cast<AtomicOp>(op);
    std::int64_t result = 0, before = 0;
    if (!atomic_update(entry, which, v, before, result, label, error)) return false;
    out = Value::integer(return_old ? before : result);
    return true;
}


}  // namespace nift::rt
