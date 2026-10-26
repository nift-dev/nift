#include "rt/eval_result.h"
#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/heap.h"

// rt-lint: cold-file CLI result serialization after execution
namespace nift::rt {
namespace {
bool convert(const Value& value, nift::RuntimeValue& out, std::string& error, std::size_t depth) {
    if (depth > kMaxBridgeDepth) {
        error = "value nesting exceeds " + std::to_string(kMaxBridgeDepth) + " levels";
        return false;
    }
    if (value.is(Kind::Function)) {
        const auto* function = value.heap_as<FunctionObject>();
        out = nift::RuntimeValue(std::string("\x1fnift:callable:") +
            (function->is_lambda ? "lambda:" + std::to_string(function->display_id)
                                 : "named:" + function->name));
        return true;
    }
    if (value.is(Kind::Array)) {
        out = nift::RuntimeValue::make_array();
        out.array.reserve(array_size(value));
        for (const auto& item : array_items(value)) {
            nift::RuntimeValue child;
            if (!convert(item, child, error, depth + 1)) return false;
            out.array.push_back(std::move(child));
        }
        return true;
    }
    if (value.is(Kind::Object)) {
        out = nift::RuntimeValue::make_object();
        out.object.reserve(object_size(value));
        for (const auto& entry : object_entries(value)) {
            nift::RuntimeValue child;
            if (!convert(entry.second, child, error, depth + 1)) return false;
            out.object.emplace_back(std::string(atom_text(entry.first)), std::move(child));
        }
        return true;
    }
    return to_legacy(value, out, error);
}
} // namespace
bool eval_result_to_legacy(const Value& value, nift::RuntimeValue& out, std::string& error) {
    return convert(value, out, error, 0);
}
} // namespace nift::rt
