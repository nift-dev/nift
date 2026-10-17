#include "rt/legacy_bridge.h"

#include "rt/aggregate.h"
#include "rt/heap.h"
#include "rt/heap_account.h"
#include "rt/string.h"

#include <limits>

namespace nift::rt {

namespace {

bool error_from_legacy(const std::shared_ptr<const nift::RuntimeErrorData>& data, int depth, Value& out,
                       std::string& error) {
    if (depth >= 16) { error = "Error cause chain exceeds 16 values"; return false; }
    auto* object = new ErrorObject();
    Value holder = Value::adopt(object);
    object->message = data->message;
    object->code = data->code;
    object->category = data->category;
    object->source = data->source;
    object->line = data->line;
    object->column = data->column;
    if (data->cause && !error_from_legacy(data->cause, depth + 1, object->cause, error)) return false;
    heap_retally(object);
    out = holder;
    return true;
}

std::shared_ptr<const nift::RuntimeErrorData> error_to_legacy(const ErrorObject& e) {
    std::shared_ptr<const nift::RuntimeErrorData> cause;
    if (e.cause.is(Kind::Error)) cause = error_to_legacy(*e.cause.heap_as<ErrorObject>());
    return std::make_shared<const nift::RuntimeErrorData>(e.message, e.code, e.category, e.source, e.line, e.column,
                                                           cause);
}

bool convert_from(const nift::RuntimeValue& legacy, Value& out, std::string& error, std::size_t depth);
bool convert_to(const Value& value, nift::RuntimeValue& out, std::string& error, std::size_t depth);

bool object_from_legacy(const nift::RuntimeValue& legacy, Value& out, std::string& error, std::size_t depth) {
    out = make_object();
    for (const auto& entry : legacy.object) {
        const Atom key = atom(entry.first);
        if (object_get(out, key)) {  // see legacy_bridge.h: rt objects have unique keys
            error = "object has duplicate key '" + entry.first + "'";
            return false;
        }
        Value v;
        if (!convert_from(entry.second, v, error, depth + 1)) return false;
        object_set(out, key, static_cast<Value&&>(v));
    }
    return true;
}

bool too_deep(std::size_t depth, std::string& error) {
    if (depth < kMaxBridgeDepth) return false;
    error = "value nesting exceeds " + std::to_string(kMaxBridgeDepth) + " levels";
    return true;
}

bool convert_from(const nift::RuntimeValue& legacy, Value& out, std::string& error, std::size_t depth) {
    if (too_deep(depth, error)) return false;
    switch (legacy.type) {
        case nift::RuntimeType::Null: out = Value(); return true;
        case nift::RuntimeType::Boolean: out = Value::boolean(legacy.boolean); return true;
        case nift::RuntimeType::Number: out = Value::number(legacy.num); return true;
        case nift::RuntimeType::StrNumber: out = make_decimal(legacy.string, legacy.num); return true;
        case nift::RuntimeType::String: out = make_string(legacy.string); return true;
        case nift::RuntimeType::Bytes:
            out = make_bytes(legacy.bytes ? *legacy.bytes : std::vector<std::uint8_t>{});
            return true;
        case nift::RuntimeType::Array: {
            out = make_array(legacy.array.size());
            for (const auto& item : legacy.array) {
                Value v;
                if (!convert_from(item, v, error, depth + 1)) return false;
                array_push(out, static_cast<Value&&>(v));
            }
            return true;
        }
        case nift::RuntimeType::Object: return object_from_legacy(legacy, out, error, depth);
        case nift::RuntimeType::Timer:
            if (!legacy.timer || legacy.timer->owner > std::numeric_limits<std::uint32_t>::max()) {
                error = "timer owner does not fit a handle generation";
                return false;
            }
            out = Value::handle(HandleKind::Timer, legacy.timer->instance,
                                static_cast<std::uint32_t>(legacy.timer->owner));
            return true;
        case nift::RuntimeType::Error:
            if (!legacy.error) { error = "invalid Error value"; return false; }
            return error_from_legacy(legacy.error, 0, out, error);
    }
    error = "unknown legacy value type";
    return false;
}

bool convert_to(const Value& value, nift::RuntimeValue& out, std::string& error, std::size_t depth) {
    if (too_deep(depth, error)) return false;
    switch (value.kind()) {
        case Kind::Null: out = nift::RuntimeValue(nullptr); return true;
        case Kind::Bool: out = nift::RuntimeValue(value.as_bool()); return true;
        case Kind::Int: out = nift::runtime_integer(value.as_int()); return true;
        case Kind::Double: out = nift::RuntimeValue(value.as_double()); return true;
        case Kind::Decimal:
            out = nift::RuntimeValue(decimal_approx(value));
            out.type = nift::RuntimeType::StrNumber;
            out.string = std::string(decimal_spelling(value));
            return true;
        case Kind::String: out = nift::RuntimeValue(std::string(string_view_of(value))); return true;
        case Kind::Bytes: out = nift::RuntimeValue(nift::RuntimeBytes(bytes_of(value))); return true;
        case Kind::Array: {
            out = nift::RuntimeValue::make_array();
            out.array.reserve(array_size(value));
            for (const auto& item : array_items(value)) {
                nift::RuntimeValue converted;
                if (!convert_to(item, converted, error, depth + 1)) return false;
                out.array.push_back(std::move(converted));
            }
            return true;
        }
        case Kind::Object: {
            out = nift::RuntimeValue::make_object();
            out.object.reserve(object_size(value));
            for (const auto& entry : object_entries(value)) {
                nift::RuntimeValue converted;
                if (!convert_to(entry.second, converted, error, depth + 1)) return false;
                out.object.emplace_back(std::string(atom_text(entry.first)), std::move(converted));
            }
            return true;
        }
        case Kind::Error:
            out = nift::RuntimeValue::make_error(error_to_legacy(*value.heap_as<ErrorObject>()));
            return true;
        case Kind::Handle:
            if (value.handle_kind() == HandleKind::Timer) {
                out = nift::RuntimeValue::make_timer(value.handle_generation(), value.handle_id());
                return true;
            }
            error = "typed handles have no legacy representation";
            return false;
        default:
            error = std::string(kind_name(value.kind())) + " values have no legacy representation";
            return false;
    }
}

} // namespace

bool from_legacy(const nift::RuntimeValue& legacy, Value& out, std::string& error) {
    return convert_from(legacy, out, error, 0);
}

bool to_legacy(const Value& value, nift::RuntimeValue& out, std::string& error) {
    return convert_to(value, out, error, 0);
}

} // namespace nift::rt
