#include "rt/natives/json_methods.h"

#include "JsonSchema.h"
#include "Json.h"
#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/natives/support.h"
#include "rt/number.h"
#include "rt/string.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace nift::rt::natives {

namespace {

// Legacy runtime_from_json, straight to rt values (no RuntimeValue).
bool from_document(const Context& context, json::Document& doc, Value& out) {
    switch (doc.type) {
        case json::Type::Null: out = Value(); return true;
        case json::Type::Boolean: out = Value::boolean(doc.boolean); return true;
        case json::Type::Number: out = Value::number(doc.num); return true;
        case json::Type::StrNumber: out = make_decimal(static_cast<std::string&&>(doc.string), doc.num); return true;
        case json::Type::String: out = make_string(static_cast<std::string&&>(doc.string)); return true;
        case json::Type::Array: {
            ArrayBuilder items(doc.array.size());
            for (json::Document& item : doc.array) {
                Value v;
                if (!from_document(context, item, v) || !items.push(context, static_cast<Value&&>(v))) return false;
            }
            out = items.finish();
            return true;
        }
        case json::Type::Object: {
            Value object = make_object();
            for (auto& entry : doc.object) {
                Value v;
                if (!from_document(context, entry.second, v)) return false;
                object_set(object, atom(entry.first), static_cast<Value&&>(v));
            }
            out = static_cast<Value&&>(object);
            return !context.over_budget();
        }
    }
    return false;
}

constexpr std::int64_t kExact = std::int64_t(1) << 53;

// Legacy runtime_to_json for plain values. Int beyond 2^53 is legacy's exact
// StrNumber; bytes are "bytes values are not JSON serializable".
// Nesting cap, as the parser's (review R2): legacy recurses without a bound
// and crashes on very deep values; here deeper values stop with Status::Limit.
constexpr int kMaxDocumentDepth = 512;

bool to_document(const Value& v, json::Document& doc, std::string& error, bool& bad_kind, bool& limit, int depth) {
    if (depth > kMaxDocumentDepth) {
        limit = true;
        error = "value nesting exceeds 512 levels";
        return false;
    }
    switch (v.kind()) {
        case Kind::Null: doc = json::Document(nullptr); return true;
        case Kind::Bool: doc = json::Document(v.as_bool()); return true;
        case Kind::Int:
            doc.type = v.as_int() >= -kExact && v.as_int() <= kExact ? json::Type::Number : json::Type::StrNumber;
            doc.num = static_cast<double>(v.as_int());
            if (doc.type == json::Type::StrNumber) doc.string = std::to_string(v.as_int());
            return true;
        case Kind::Double: doc.type = json::Type::Number; doc.num = v.as_double(); return true;
        case Kind::Decimal:
            doc.type = json::Type::StrNumber;
            doc.num = number_approx(v);
            doc.string = std::string(decimal_spelling(v));
            return true;
        case Kind::String: doc.type = json::Type::String; doc.string = std::string(string_view_of(v)); return true;
        case Kind::Bytes: error = "bytes values are not JSON serializable"; return false;
        case Kind::Error: error = "Error values are not JSON serializable"; return false;
        case Kind::Array:
            doc = json::Document::make_array();
            doc.array.reserve(array_size(v));
            for (const Value& item : array_items(v)) {
                doc.array.emplace_back();
                if (!to_document(item, doc.array.back(), error, bad_kind, limit, depth + 1)) return false;
            }
            return true;
        case Kind::Object:
            doc = json::Document::make_object();
            doc.object.reserve(object_size(v));
            for (const auto& entry : object_entries(v)) {
                doc.object.emplace_back(std::string(atom_text(entry.first)), json::Document());
                json::Document& member = doc.object.back().second;
                if (!to_document(entry.second, member, error, bad_kind, limit, depth + 1)) return false;
            }
            return true;
        default: bad_kind = true; error = "json argument kind"; return false;
    }
}

} // namespace

Status json_parse(const Context& context, std::string_view text, Value& out, std::string& error) {
    json::Document document;
    if (!nift_json::parse(std::string(text), document, error)) return Status::Error;
    Value result;
    if (!from_document(context, document, result)) return Status::Memory;
    out = static_cast<Value&&>(result);
    return Status::Ok;
}

Status fn_validate(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc != 2) return fail(error, "validate: expected schema and value");
    json::Document schema, instance;
    bool bad_kind = false, limit = false;
    std::string conversion;
    if (!to_document(args[0], schema, conversion, bad_kind, limit, 0) ||
        !to_document(args[1], instance, conversion, bad_kind, limit, 0)) {
        error = "validate: " + conversion;
        return bad_kind ? Status::Internal : limit ? Status::Limit : Status::Error;
    }
    std::string message;
    if (!jsonschema::schema_valid(schema, message)) return fail(error, "validate: " + message);
    if (!jsonschema::validate(instance, schema, message)) {
        error = "validate: " + message;
        return Status::Recoverable;  // legacy: catchable Error, code schema.rejected
    }
    out = args[1];
    return Status::Ok;
}

// copy(v) and deepcopy(v): a recursively independent value. Copy-on-write
// already gives that for plain values (a later mutation of either side
// unshares exactly the path it changes), so the result shares storage.
// Structs (identity, legacy re-creates them) and collection handles are
// CP12 phase B / CP17.
Status fn_copy(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc != 1) return fail(error, "copy: expected one value");
    // Functions and Error values are immutable or identity values: legacy
    // copies them as they are (review D1). A struct or handle anywhere in the
    // value would have to be re-created (legacy clones structs): CP17. The
    // compiler admits copy only for provably plain arguments (wiring rule
    // W11), so finding one here is a compiler defect, reported loudly (D2).
    std::vector<const Value*> work{&args[0]};
    while (!work.empty()) {
        const Value* v = work.back();
        work.pop_back();
        if (v->is(Kind::Struct) || v->is(Kind::Handle)) {
            error = "copy of a struct or handle (CP17)";
            return Status::Internal;
        }
        if (v->is(Kind::Array))
            for (const Value& item : array_items(*v)) work.push_back(&item);
        else if (v->is(Kind::Object))
            for (const auto& entry : object_entries(*v)) work.push_back(&entry.second);
    }
    out = args[0];
    return Status::Ok;
}

} // namespace nift::rt::natives
