#include "rt/eval_result.h"
#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/heap.h"
#include "rt/render.h"
#include "rt/natives/format_methods.h"
#include "rt/shared_atomic.h"

// rt-lint: cold-file CLI result serialization after execution
namespace nift::rt {
namespace {
bool convert(const Value& value, nift::RuntimeValue& out, std::string& error, std::size_t depth, bool json) {
    if (depth > kMaxBridgeDepth) {
        error = "value nesting exceeds " + std::to_string(kMaxBridgeDepth) + " levels";
        return false;
    }
    // B4 (b4-dispatch.md 6, owner): a function has no shown form; its
    // internal tag is not disclosed (stringify's ordinary error).
    if (value.is(Kind::Function)) {
        if (json) {  // review L1: --json output unchanged
            out = nift::RuntimeValue(callable_marker(*value.heap_as<FunctionObject>()));
            return true;
        }
#if defined(NIFT_B4_MUTANT) && NIFT_B4_MUTANT == 11
        out = nift::RuntimeValue(callable_marker(*value.heap_as<FunctionObject>()));  // A11: the tag again
        return true;
#endif
        error = "opaque runtime value is not serializable";
        return false;
    }
    // B4: an atomic is shown by its current value: one sequentially
    // consistent load through the runtime's atomic access (as print does).
    if (!json && value.is(Kind::Handle) && value.handle_kind() == HandleKind::Atomic) {
        SharedOpError failure;
        Value loaded;
        if (!shared_atomic_load(value, loaded, failure)) {
            error = failure.message;
            return false;
        }
        return to_legacy(loaded, out, error);
    }
    if (value.is(Kind::Array)) {
        out = nift::RuntimeValue::make_array();
        out.array.reserve(array_size(value));
        for (const auto& item : array_items(value)) {
            nift::RuntimeValue child;
            if (!convert(item, child, error, depth + 1, json)) return false;
            out.array.push_back(std::move(child));
        }
        return true;
    }
    if (value.is(Kind::Object)) {
        out = nift::RuntimeValue::make_object();
        out.object.reserve(object_size(value));
        for (const auto& entry : object_entries(value)) {
            nift::RuntimeValue child;
            if (!convert(entry.second, child, error, depth + 1, json)) return false;
            out.object.emplace_back(std::string(atom_text(entry.first)), std::move(child));
        }
        return true;
    }
    // B0: a shared handle or a future shown as a result: the VM's wording for
    // an opaque value (a timer keeps its legacy form through to_legacy).
    if ((value.is(Kind::Handle) && value.handle_kind() != HandleKind::Timer) || value.is(Kind::Future)) {
        error = "opaque runtime value is not serializable";
        return false;
    }
    return to_legacy(value, out, error);
}
} // namespace
bool eval_result_to_legacy(const Value& value, nift::RuntimeValue& out, std::string& error, bool json) {
    return convert(value, out, error, 0, json);
}
bool session_presented(const Value& value, std::string& text) {
    if (!value.is(Kind::Struct) && !value.is(Kind::Collection)) return false;
    std::string error;
    bool internal = false;
    text.clear();
    return natives::serialize(value, false, text, error, internal);
}
} // namespace nift::rt
