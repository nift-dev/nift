#include "rt/vm_ops.h"

#include "rt/aggregate.h"
#include "rt/compare.h"
#include "rt/heap.h"
#include "rt/number.h"
#include "rt/render.h"
#include "rt/string.h"

#include <charconv>
#include <cmath>

// rt-lint: cold-file slow and error paths of the VM; the hot paths are inline in vm.cpp

namespace nift::rt {

bool raise(Fault& fault, StopReason reason, std::string message) {
    fault.reason = reason;
    fault.message = std::move(message);
    return false;
}

bool internal(Fault& fault, const char* what) {
    return raise(fault, StopReason::Internal, std::string("unexpected operands (compiler defect): ") + what);
}

bool is_large(const Value& value) {
    switch (value.kind()) {
        case Kind::Int: return !safe_integer(value.as_int());
        case Kind::Double: return std::fabs(value.as_double()) >= double(kSafeInteger);
        case Kind::Decimal: return true;
        default: return false;
    }
}

namespace {

ArithOp arith_op(OpCode op) {
    switch (op) {
        case OpCode::Sub: return ArithOp::Sub;
        case OpCode::Mul: return ArithOp::Mul;
        case OpCode::Div: return ArithOp::Div;
        case OpCode::Mod: return ArithOp::Mod;
        default: return ArithOp::Add;
    }
}

bool concatenate(const Value& left, const Value& right, Value& out, Fault& fault) {
    std::string text;
    if (!append_rendered(left, text) || !append_rendered(right, text)) return internal(fault, "concatenation");
    out = make_string(std::move(text));
    return true;
}

std::string integer_text(std::int64_t v) {
    char buffer[24];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, v);
    return std::string(buffer, static_cast<std::size_t>(result.ptr - buffer));
}

const std::string& note_text(const Note* note) {
    static const std::string empty;
    return note ? note->text : empty;
}

} // namespace

bool arith_slow(const Instr& in, OpCode op, const Value& left, const Value& right, Value& out, Fault& fault) {
    static_cast<void>(in);
    if (op == OpCode::Add && (left.is(Kind::String) || right.is(Kind::String)))
        return concatenate(left, right, out, fault);
    if (!left.is_number() || !right.is_number()) return internal(fault, "arithmetic");
    // Evidence for the D-018 number policy (VM stats): an operand or result
    // legacy calls large, or a result out of range.
    const bool large = is_large(left) || is_large(right);
    Value result;
    std::string error;
    if (!arithmetic(arith_op(op), left, right, result, error)) {
        if (large || error.find("overflow") != std::string::npos || error.find("not finite") != std::string::npos)
            fault.large = true;
        return raise(fault, StopReason::Error, error);
    }
    if (large || is_large(result)) fault.large = true;
    out = static_cast<Value&&>(result);
    return true;
}

bool add_assign_slow(const Instr& in, Value& target, const Value& right, Fault& fault) {
    if (target.is(Kind::String) && right.is(Kind::String)) {
        // `s += t` on an unshared heap string appends in place (legacy does the
        // same when the binding holds the only reference); amortised O(len(t)).
        if (target.is_heap() && target.refcount() == 1) {
            append_in_place(target, right);
            return true;
        }
        return concatenate(target, right, target, fault);
    }
    if (!target.is_number() || !right.is_number()) return internal(fault, "compound assignment");
    return arith_slow(in, OpCode::Add, target, right, target, fault);
}

bool compare_slow(OpCode op, const Value& left, const Value& right, bool& result, Fault& fault) {
    if (op == OpCode::Eq || op == OpCode::Ne) {
        result = equal(left, right) == (op == OpCode::Eq);
        return true;
    }
    const bool numbers = left.is_number() && right.is_number();
    const bool strings = left.is(Kind::String) && right.is(Kind::String);
    if (!numbers && !strings) return internal(fault, "ordering comparison");
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

bool negate_slow(const Instr& in, const Value& value, Value& out, Fault& fault) {
    if (!value.is_number()) return internal(fault, "negation");
    static_cast<void>(in);
    if (is_large(value)) fault.large = true;
    out = negate(value);
    return true;
}

bool positive_slow(const Value& value, Value& out, Fault& fault) {
    if (!value.is_number()) return internal(fault, "unary plus");
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
        default: return -1;
    }
}

const char* binding_type_name(std::int64_t tag) {
    switch (tag) {
        case 0: return "null";
        case 1: return "bool";
        case 2: return "int";
        case 3: return "double";
        case 4: return "string";
        case 5: return "array";
        case 6: return "json";
        case 7: return "bytes";
        case 8: return "timer";
        case 9: return "error";
        default: return "unknown";
    }
}

bool binding_type_error(std::int64_t assigned, std::int64_t binding, const Note* note, Fault& fault) {
    return raise(fault, StopReason::Error,
                 std::string("cannot assign ") + binding_type_name(assigned) + " to " + binding_type_name(binding) +
                     " binding '" + note_text(note) + "'");
}

bool get_index(const Note* note, const Value& object, const Value& key, Value& out, Fault& fault) {
    if (object.is(Kind::Array) && key.is_number()) {
        const auto& items = array_items(object);
        std::int64_t i = 0;
        const bool integral = number_to_i64(key, i) && i >= 0;
        if (integral && static_cast<std::uint64_t>(i) < items.size()) {
            out = items[static_cast<std::size_t>(i)];
            return true;
        }
        if (integral)
            return raise(fault, StopReason::Error,
                         "JSON array index " + integer_text(i) + " is out of range in '" + note_text(note) + "'");
        // An integral Decimal beyond int64 (-INT64_MIN, review D3): out of
        // range with its spelling, as legacy.
        if (key.is(Kind::Decimal) && number_is_integer(key) && decimal_spelling(key).substr(0, 1) != "-")
            return raise(fault, StopReason::Error, "JSON array index " + std::string(decimal_spelling(key)) +
                                                       " is out of range in '" + note_text(note) + "'");
        return raise(fault, StopReason::Error,
                     "JSON array indices must be non-negative integers in '" + note_text(note) + "'");
    }
    if (object.is(Kind::Object) && key.is(Kind::String)) {
        Atom atom;
        if (AtomTable::current().find(string_view_of(key), atom)) {
            if (const Value* member = object_get(object, atom)) {
                out = *member;
                return true;
            }
        }
        return raise(fault, StopReason::Error, "JSON object has no key '" + std::string(string_view_of(key)) + "'");
    }
    return internal(fault, "index");
}

bool set_index(const Note* note, Value& object, const Value& key, const Value& value, Fault& fault) {
    if (!object.is(Kind::Array) || !key.is_number()) return internal(fault, "element assignment");
    std::int64_t i = 0;
    const bool integral = number_to_i64(key, i) && i >= 0;
    if (!integral || static_cast<std::uint64_t>(i) >= array_size(object)) {
        const bool beyond_int64 =  // an integral Decimal (-INT64_MIN, review D3)
            key.is(Kind::Decimal) && number_is_integer(key) && decimal_spelling(key).substr(0, 1) != "-";
        if (integral || beyond_int64) return raise(fault, StopReason::Error, "index out of range: " + note_text(note));
        return raise(fault, StopReason::Error,
                     "assignment index does not address an array or object: " + note_text(note));
    }
    Value copy = value;
    array_mut(object)[static_cast<std::size_t>(i)] = static_cast<Value&&>(copy);
    return true;
}

bool get_member(const Value& object, Atom key, Value& out, Fault& fault) {
    if (!object.is(Kind::Object)) return internal(fault, "member");
    if (const Value* member = object_get(object, key)) {
        out = *member;
        return true;
    }
    return raise(fault, StopReason::Error, "value has no member: " + std::string(atom_text(key)));
}

bool push_value(Value& array, const Value& value, Fault& fault) {
    if (!array.is(Kind::Array)) return internal(fault, "push");
    array_push(array, value);
    return true;
}

bool length_of(const Note* note, const Value& value, bool size, Value& out, Fault& fault) {
    if (value.is(Kind::Array)) {
        out = Value::integer(static_cast<std::int64_t>(array_size(value)));
        return true;
    }
    if (value.is(Kind::Object)) {
        // size() counts members; length() rejects an object.
        if (!size)
            return raise(fault, StopReason::Error, "unknown value or malformed expression: " + note_text(note));
        out = Value::integer(static_cast<std::int64_t>(object_size(value)));
        return true;
    }
    if (!size && value.is(Kind::String)) {
        std::int64_t count = 0;
        for (char c : string_view_of(value))
            if ((static_cast<unsigned char>(c) & 0xc0u) != 0x80u) ++count;
        out = Value::integer(count);
        return true;
    }
    return internal(fault, "length/size");
}

} // namespace nift::rt
