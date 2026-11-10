#include "rt/vm_element_path.h"

#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/mutation.h"
#include "rt/number.h"
#include "rt/string.h"
#include "rt/vm_ops.h"

#include <cstdint>
#include <string>

// rt-lint: cold-file assignment-path steps and object stores, once per assignment

namespace nift::rt {

namespace {
std::string key_text(const Note* note) { return note ? note->text : std::string(); }
} // namespace

bool path_step(const Note* note, const Value& object, const Value& key, Value& out, Fault& fault) {
    if (object.is(Kind::Array) && key.is_number()) {
        std::int64_t i = 0;
        const bool integral = number_to_i64(key, i) && i >= 0;
        if (integral && static_cast<std::uint64_t>(i) < array_size(object)) {
            out = array_view(object)[static_cast<std::size_t>(i)];
            return true;
        }
        const bool beyond_int64 =  // an integral Decimal (-INT64_MIN), as set_index
            key.is(Kind::Decimal) && number_is_integer(key) && decimal_spelling(key).substr(0, 1) != "-";
        if (integral || beyond_int64) return raise(fault, StopReason::Error, "index out of range: " + key_text(note));
        return raise(fault, StopReason::Error,
                     "assignment index does not address an array or object: " + key_text(note));
    }
    if (object.is(Kind::Object) && key.is(Kind::String)) {
        Atom atom;
        if (AtomTable::current().find(string_view_of(key), atom)) {
            if (const Value* member = object_find(object, atom)) {
                out = *member;
                return true;
            }
        }
        return raise(fault, StopReason::Error, "object has no member: " + std::string(string_view_of(key)));
    }
    return internal(fault, "assignment path");
}

bool set_member(const Note* note, Value& object, const Value& key, const Value& value, Fault& fault,
                const CycleGraph* graph, bool* cycle) {
    if (!object.is(Kind::Object) || !key.is(Kind::String)) return internal(fault, "member assignment");
    mutation::Env env;
    env.graph = graph;
    env.receiver_text = note ? std::string_view(note->text) : std::string_view();
    const mutation::Outcome out = mutation::object_put(object, atom(string_view_of(key)), value, env);
    if (out.ok()) return true;
    if (out.failure == mutation::Failure::Cycle && cycle) {
        *cycle = true;
        return false;
    }
    if (out.failure == mutation::Failure::Cycle)
        return raise(fault, StopReason::Error, "reference cycle: the stored value would contain itself");
    return raise_refusal(fault, out);
}

} // namespace nift::rt
