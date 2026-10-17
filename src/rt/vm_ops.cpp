#include "rt/vm_ops.h"

#include "rt/aggregate.h"
#include "rt/compare.h"
#include "rt/heap.h"
#include "rt/number.h"
#include "rt/render.h"
#include "rt/string.h"

#include <cmath>

namespace nift::rt {

bool raise(Fault& fault, BailReason reason, const char* message) {
    fault.reason = reason;
    fault.message = message;
    return false;
}

namespace {

bool in_range(const Value& v) {
    if (v.is(Kind::Int)) return safe_integer(v.as_int());
    if (v.is(Kind::Double)) return std::isfinite(v.as_double()) && std::fabs(v.as_double()) < double(kSafeInteger);
    return false;
}

bool negative(const Value& v) {
    if (v.is(Kind::Int)) return v.as_int() < 0;
    return v.is(Kind::Double) && std::signbit(v.as_double());
}

bool is_zero(const Value& v) {
    return (v.is(Kind::Int) && v.as_int() == 0) || (v.is(Kind::Double) && v.as_double() == 0);
}

bool negative_zero(const Value& v) { return v.is(Kind::Double) && v.as_double() == 0 && std::signbit(v.as_double()); }

ArithOp arith_op(OpCode op) {
    switch (op) {
        case OpCode::Sub: return ArithOp::Sub;
        case OpCode::Mul: return ArithOp::Mul;
        case OpCode::Div: return ArithOp::Div;
        case OpCode::Mod: return ArithOp::Mod;
        default: return ArithOp::Add;
    }
}

// The legacy loop path computes `+ - * %` in doubles, the top-level path in
// int64 when both operands are integral. Their results differ only in the
// sign of a zero.
bool zero_sign_risk(OpCode op, const Value& left, const Value& right, const Value& result) {
    if (!is_zero(result) || op == OpCode::Div) return false;
    if (negative_zero(left) || negative_zero(right)) return true;
    if (op == OpCode::Mul) return negative(left) || negative(right);
    if (op == OpCode::Mod) return negative(left);
    return false;
}

bool concatenate(const Value& left, const Value& right, Value& out, Fault& fault) {
    std::string text;
    if (!append_rendered(left, text) || !append_rendered(right, text))
        return raise(fault, BailReason::Error, "string concatenation requires renderable scalar values");
    out = make_string(std::move(text));
    return true;
}

} // namespace

bool arith_slow(OpCode op, const Value& left, const Value& right, Value& out, Fault& fault) {
    if (op == OpCode::Add && (left.is(Kind::String) || right.is(Kind::String)))
        return concatenate(left, right, out, fault);
    if (!left.is_number() || !right.is_number())
        return raise(fault, BailReason::Error, "arithmetic operators require numeric operands");
    if (!in_range(left) || !in_range(right)) return raise(fault, BailReason::NumberRange, "number outside 2^53");
    Value result;
    std::string error;
    if (!arithmetic(arith_op(op), left, right, result, error)) {
        fault.reason = BailReason::Error;
        fault.message = error;
        return false;
    }
    if (!in_range(result)) return raise(fault, BailReason::NumberRange, "number outside 2^53");
    if (zero_sign_risk(op, left, right, result)) return raise(fault, BailReason::NumberRange, "integer -0");
    out = static_cast<Value&&>(result);
    return true;
}

bool add_assign_slow(const Value& left, const Value& right, Value& out, Fault& fault) {
    if (left.is(Kind::String) && right.is(Kind::String)) {
        // `s += t` on an unshared heap string appends in place (legacy does the
        // same when the binding holds the only reference); amortised O(len(t)).
        if (&out == &left && left.is_heap() && left.refcount() == 1) {
            append_in_place(out, right);  // out is left
            return true;
        }
        return concatenate(left, right, out, fault);
    }
    if (!left.is_number() || !right.is_number())
        return raise(fault, BailReason::Error, "arithmetic operators require numeric operands");
    return arith_slow(OpCode::Add, left, right, out, fault);
}

bool compare_slow(OpCode op, const Value& left, const Value& right, bool& result, std::size_t depth, Fault& fault) {
    if (op == OpCode::Eq || op == OpCode::Ne) {
        // Legacy's recursive equality answers up to some 30,000 levels and
        // crashes beyond; the VM decides up to `depth` (VmOptions, default
        // 10,000: well inside what legacy answers) and hands deeper
        // comparisons to legacy.
        const Equality same = equal_within(left, right, depth);
        if (same == Equality::TooDeep)
            return raise(fault, BailReason::NestingDepth, "equality on values nested too deeply");
        result = (same == Equality::Equal) == (op == OpCode::Eq);
        return true;
    }
    const bool numbers = left.is_number() && right.is_number();
    const bool strings = left.is(Kind::String) && right.is(Kind::String);
    if (!numbers && !strings)
        return raise(fault, BailReason::Error, "ordering comparisons require two numbers or two strings");
    int cmp = 0;
    if (!compare_relational(left, right, cmp)) { result = false; return true; }  // NaN
    switch (op) {
        case OpCode::Lt: result = cmp < 0; break;
        case OpCode::Le: result = cmp <= 0; break;
        case OpCode::Gt: result = cmp > 0; break;
        default: result = cmp >= 0; break;
    }
    return true;
}

bool negate_slow(const Value& value, Value& out, Fault& fault) {
    if (value.is(Kind::Int)) {
        if (value.as_int() == 0) return raise(fault, BailReason::NumberRange, "integer -0");
        out = Value::integer(-value.as_int());
        return true;
    }
    if (value.is(Kind::Double)) { out = Value::number(-value.as_double()); return true; }
    if (value.is(Kind::Decimal)) return raise(fault, BailReason::NumberRange, "decimal");
    return raise(fault, BailReason::Error, "unary arithmetic operators require a numeric operand");
}

bool positive_slow(const Value& value, Value& out, Fault& fault) {
    if (!value.is_number())
        return raise(fault, BailReason::Error, "unary arithmetic operators require a numeric operand");
    out = value;
    return true;
}

std::int64_t binding_tag(const Value& value) {
    switch (value.kind()) {
        case Kind::Null: return 0;
        case Kind::Bool: return 1;
        case Kind::Int: return 2;
        case Kind::Double: case Kind::Decimal: return number_is_integer(value) ? 2 : 3;
        case Kind::String: return 4;
        case Kind::Array: return 5;
        case Kind::Object: return 6;
        case Kind::Bytes: return 7;
        case Kind::Error: return 9;
        default: return 16 + static_cast<std::int64_t>(value.kind());
    }
}

bool check_assign(const Value& value, std::int64_t binding_type, std::int64_t text_type, Fault& fault) {
    const bool by_text = tag_assignable(text_binding_type(text_type, value), binding_type);
    const bool by_value = tag_assignable(binding_tag(value), binding_type);
    if (by_text && by_value) return true;
    if (by_text != by_value) return raise(fault, BailReason::Unverified, "binding type differs between legacy paths");
    return raise(fault, BailReason::Error, "cannot assign a value of another type to a typed binding");
}

bool is_aggregate(const Value& value) { return value.is(Kind::Array) || value.is(Kind::Object); }

bool get_index(const Value& object, const Value& key, Value& out, Fault& fault) {
    if (object.is(Kind::Array) && key.is_number()) {
        std::int64_t i = 0;
        const auto& items = array_items(object);
        if (number_to_i64(key, i) && i >= 0 && static_cast<std::uint64_t>(i) < items.size()) {
            out = items[static_cast<std::size_t>(i)];
            return true;
        }
    } else if (object.is(Kind::Object) && key.is(Kind::String)) {
        Atom atom;
        if (AtomTable::current().find(string_view_of(key), atom)) {
            if (const Value* member = object_get(object, atom)) {
                out = *member;
                return true;
            }
        }
    }
    return raise(fault, BailReason::Error, "invalid index");
}

bool set_index(Value& object, const Value& key, const Value& value, Fault& fault) {
    if (is_aggregate(value)) return raise(fault, BailReason::Aliasing, "aggregate element assignment");
    std::int64_t i = 0;
    if (!object.is(Kind::Array) || !key.is_number() || !number_to_i64(key, i) || i < 0 ||
        static_cast<std::uint64_t>(i) >= array_size(object))
        return raise(fault, BailReason::Unverified, "index assignment outside an existing array element");
    const auto at = static_cast<std::size_t>(i);
    if (binding_tag(array_items(object)[at]) != binding_tag(value))
        return raise(fault, BailReason::Unverified, "index assignment changing the element type");
    Value copy = value;
    array_mut(object)[at] = static_cast<Value&&>(copy);
    return true;
}

bool get_member(const Value& object, Atom key, Value& out, Fault& fault) {
    if (object.is(Kind::Object)) {
        if (const Value* member = object_get(object, key)) {
            out = *member;
            return true;
        }
    }
    return raise(fault, BailReason::Error, "value has no member");
}

bool push_value(Value& array, const Value& value, Fault& fault) {
    if (!array.is(Kind::Array)) return raise(fault, BailReason::Error, "push requires an array");
    if (is_aggregate(value)) return raise(fault, BailReason::Aliasing, "aggregate push");
    array_push(array, value);
    return true;
}

bool length_of(const Value& value, bool size, Value& out, Fault& fault) {
    if (value.is(Kind::Array)) {
        out = Value::integer(static_cast<std::int64_t>(array_size(value)));
        return true;
    }
    // Objects: size() works everywhere, length() only in loop bodies (legacy
    // context dependence); both are replayed.
    if (value.is(Kind::Object)) return raise(fault, BailReason::Unverified, "length/size of an object");
    if (size) return raise(fault, BailReason::Error, "size: unsupported for this value");
    if (value.is(Kind::String)) {
        std::int64_t count = 0;
        for (char c : string_view_of(value))
            if ((static_cast<unsigned char>(c) & 0xc0u) != 0x80u) ++count;
        out = Value::integer(count);
        return true;
    }
    return raise(fault, BailReason::Error, "length: unsupported for this value");
}

} // namespace nift::rt
