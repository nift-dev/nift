// B4 (docs/runtime-2/checkpoints/b4-dispatch.md): the operation admission
// table and the run-time dispatch precondition, at the value layer.
//   admission  Static/Dynamic/Refuse decisions for receiver unions, the
//              failure classes and their legacy texts
//   siblings   for every built-in and every run-time receiver kind, the
//              dispatch table selects a native of exactly that family and
//              name, or none (invariant 1: never dispatch on a kind the native
//              does not take); no entry outside the plain and collection
//              families; the precondition's fast mask agrees with receiver_of
//   internal   every native the table can select for a receiver of its own
//              family is called with that receiver: none returns
//              Status::Internal (the spec x kind invariant at the native layer)
//   failures   every family without a native of the name has a defined
//              failure class; Dynamic admission never includes a Refuse one
#include "rt/aggregate.h"
#include "rt/aggregate_collection.h"
#include "rt/atom.h"
#include "rt/natives/builtin.h"
#include "rt/op_admission.h"
#include "rt/string.h"
#include "rt/struct_type.h"
#include "storage_access.h"  // the construction shims (tests only)
#include "rt_test.h"

#include <string>
#include <vector>

using namespace nift::rt;
using natives::BuiltinSpec;
using natives::Receiver;

namespace {

struct Sample {
    const char* name;
    Value value;
};

std::vector<Sample> samples() {
    std::vector<Sample> out;
    out.push_back({"null", Value()});
    out.push_back({"bool", Value::boolean(true)});
    out.push_back({"int", Value::integer(5)});
    out.push_back({"double", Value::number(2.5)});
    out.push_back({"string", make_string(" a,b ")});
    Value list = make_array();
    array_push(list, Value::integer(3));
    array_push(list, Value::integer(1));
    out.push_back({"array", list});
    Value object = make_object();
    object_set(object, atom("a"), Value::integer(1));
    out.push_back({"object", object});
    Value map = make_collection(CollectionKind::Map);
    StorageTestAccess::map_set(map, make_string("k"), Value::integer(1));
    out.push_back({"map", map});
    Value set = make_collection(CollectionKind::Set);
    StorageTestAccess::set_add(set, Value::integer(1));
    out.push_back({"set", set});
    Value stack = make_collection(CollectionKind::Stack);
    StorageTestAccess::stack_push(stack, Value::integer(1));
    out.push_back({"stack", stack});
    const auto type = StructType::make("C", {"a"}, {false});
    Value instance = make_struct(type, 1);
    struct_add_field(instance, atom("a"), Value::integer(1));
    out.push_back({"struct", instance});
    // Handles: receiver_of and the sibling table only (no registry entry).
    out.push_back({"file", Value::handle(HandleKind::File, 1, 1)});
    out.push_back({"stream", Value::handle(HandleKind::Stream, 1, 1)});
    out.push_back({"mutex", Value::handle(HandleKind::Mutex, 1, 1)});
    out.push_back({"atomic", Value::handle(HandleKind::Atomic, 1, 1)});
    out.push_back({"channel", Value::handle(HandleKind::Channel, 1, 1)});
    out.push_back({"timer", Value::handle(HandleKind::Timer, 1, 1)});
    return out;
}

bool dispatchable(Receiver r) {
    return r == Receiver::Null || r == Receiver::Bool || r == Receiver::Number || r == Receiver::String ||
           r == Receiver::Array || r == Receiver::Object || r == Receiver::Map || r == Receiver::Set ||
           r == Receiver::Stack;
}

void receivers() {
    const auto all = samples();
    const Receiver expected[] = {Receiver::Null,  Receiver::Bool,   Receiver::Number, Receiver::Number,
                                 Receiver::String, Receiver::Array, Receiver::Object, Receiver::Map,
                                 Receiver::Set,   Receiver::Stack,  Receiver::None,   Receiver::File,
                                 Receiver::Stream, Receiver::Mutex, Receiver::Atomic, Receiver::Channel,
                                 Receiver::None};
    CHECK(sizeof expected / sizeof expected[0] == all.size());
    for (std::size_t k = 0; k < all.size(); ++k) CHECK(natives::receiver_of(all[k].value) == expected[k]);
}

// Invariant 1 at the table: the selected native always takes that family.
void siblings() {
    const auto all = samples();
    for (std::size_t i = 0; i < natives::builtin_count(); ++i) {
        const BuiltinSpec& spec = natives::builtin_spec(static_cast<natives::BuiltinId>(i));
        if (spec.receiver == Receiver::None || spec.receiver == Receiver::Any) continue;
        // The precondition's fast mask agrees with receiver_of on every kind.
        for (const Sample& s : all) {
            const std::uint32_t accept = natives::receiver_accept(spec);
            const bool fast = ((accept >> static_cast<unsigned>(s.value.kind())) & 1u) &&
                              (!(accept & natives::kAcceptSubkind) || natives::receiver_of(s.value) == spec.receiver);
            CHECK(fast == (natives::receiver_of(s.value) == spec.receiver));
        }
        // Redirection exists only between the families a Dynamic site names.
        if (!dispatchable(spec.receiver)) {
            for (std::size_t r = 0; r < natives::kReceiverCount; ++r)
                CHECK(natives::dispatch_sibling(spec, static_cast<Receiver>(r)) == nullptr);
            continue;
        }
        const bool collection = spec.receiver == Receiver::Map || spec.receiver == Receiver::Set ||
                                spec.receiver == Receiver::Stack;
        const bool redirects = !collection || spec.name == "size" || spec.name == "empty";
        CHECK(natives::dispatch_sibling(spec, spec.receiver) == (redirects ? &spec : nullptr));
        for (const Sample& s : all) {
            const Receiver r = natives::receiver_of(s.value);
            const BuiltinSpec* chosen = natives::dispatch_sibling(spec, r);
            // No entry crosses the plain and collection groups.
            const bool r_collection = r == Receiver::Map || r == Receiver::Set || r == Receiver::Stack;
            if (chosen) CHECK(r_collection == collection);
            if (chosen == nullptr) {
                // No native of that name for this family: a defined failure
                // class (None would mean the table lost a native).
                if (dispatchable(r)) CHECK(dispatch_failure(r, spec.name) != DispatchFailure::None);
                continue;
            }
            CHECK(chosen->receiver == r);
            CHECK(chosen->name == spec.name);
        }
    }
}

// The spec x kind invariant at the native layer: whatever the table selects
// for a receiver of a dispatchable family never returns Status::Internal.
void no_internal() {
    const auto all = samples();
    long calls = 0;
    for (std::size_t i = 0; i < natives::builtin_count(); ++i) {
        const BuiltinSpec& spec = natives::builtin_spec(static_cast<natives::BuiltinId>(i));
        if (!dispatchable(spec.receiver)) continue;
        for (const Sample& s : all) {
            const Receiver r = natives::receiver_of(s.value);
            if (!dispatchable(r)) continue;
            const BuiltinSpec* chosen = r == spec.receiver ? &spec : natives::dispatch_sibling(spec, r);
            if (chosen == nullptr) continue;
            for (std::uint32_t extra = chosen->min_args; extra <= chosen->min_args + 1u && extra <= 3u; ++extra) {
                if (chosen->max_args != natives::kVariadic && extra > chosen->max_args) break;
                for (int arg_kind = 0; arg_kind < 3; ++arg_kind) {
                    std::vector<Value> args{s.value};
                    for (std::uint32_t a = 0; a < extra; ++a)
                        args.push_back(arg_kind == 0 ? make_string("a") : arg_kind == 1 ? Value::integer(1) : Value());
                    natives::Context context;
                    Value out;
                    std::string error;
                    const natives::Status status = natives::call_builtin(*chosen, context, args.data(),
                                                                         static_cast<std::uint32_t>(args.size()), out,
                                                                         error);
                    ++calls;
                    if (status == natives::Status::Internal)
                        std::fprintf(stderr, "internal: %s on %s (%u args): %s\n", std::string(chosen->name).c_str(),
                                     s.name, extra, error.c_str());
                    CHECK(status != natives::Status::Internal);
                }
            }
        }
    }
    std::printf("no-internal: %ld native calls\n", calls);
    CHECK(calls > 300);
}

KindSet K(std::initializer_list<KindSet> parts) {
    KindSet k = 0;
    for (KindSet p : parts) k |= p;
    return k;
}

void admission() {
    // A union whose every family has the native: Dynamic, no failing branch.
    auto a = admit_dynamic_method(K({kKindString, kKindArray}), "contains", 1);
    CHECK(a.admission == Admission::Dynamic && !a.fails() && a.branches.size() == 2);
    // A union with a family lacking it: Dynamic with that family's failure.
    a = admit_dynamic_method(K({kKindString, kKindInt}), "trim", 0);
    CHECK(a.admission == Admission::Dynamic && a.fails());
    bool unsupported = false;
    for (const auto& b : a.branches)
        if (!b.spec) unsupported = b.failure == DispatchFailure::Unsupported && b.kinds == kKindInt;
    CHECK(unsupported);
    a = admit_dynamic_method(K({kKindString, kKindNull}), "trim", 0);
    CHECK(a.admission == Admission::Dynamic);
    for (const auto& b : a.branches)
        if (!b.spec) CHECK(b.failure == DispatchFailure::Malformed);
    a = admit_dynamic_method(K({kKindNumber, kKindArray}), "to_string", 0);
    CHECK(a.admission == Admission::Dynamic);
    for (const auto& b : a.branches)
        if (!b.spec) CHECK(b.failure == DispatchFailure::Composite);
    // Refused: unknown kinds, non-plain kinds, context-dependent pairs,
    // no native at all, a single family, wrong arity, unknown names.
    CHECK(admit_dynamic_method(K({kKindString, kKindUnknown}), "trim", 0).admission == Admission::Refuse);
    CHECK(admit_dynamic_method(K({kKindString, kKindStruct}), "trim", 0).admission == Admission::Refuse);
    CHECK(admit_dynamic_method(K({kKindString, kKindMutex}), "trim", 0).admission == Admission::Refuse);
    CHECK(admit_dynamic_method(K({kKindString, kKindFunction}), "trim", 0).admission == Admission::Refuse);
    CHECK(admit_dynamic_method(K({kKindString, kKindInt}), "to_double", 0).admission == Admission::Refuse);
    CHECK(admit_dynamic_method(K({kKindArray, kKindObject}), "length", 0).admission == Admission::Refuse);
    CHECK(admit_dynamic_method(K({kKindInt, kKindNull}), "trim", 0).admission == Admission::Refuse);
    CHECK(admit_dynamic_method(kKindArray, "trim", 0).admission == Admission::Refuse);
    CHECK(admit_dynamic_method(K({kKindString, kKindArray}), "contains", 2).admission == Admission::Refuse);
    CHECK(admit_dynamic_method(K({kKindString, kKindArray}), "nosuch", 0).admission == Admission::Refuse);
    CHECK(admit_dynamic_method(K({kKindString, kKindArray}), "close", 0).admission == Admission::Refuse);
    CHECK(admit_dynamic_method(K({kKindString, kKindArray}), "encode", 1).admission == Admission::Refuse);
    // Collections: the read-only natives every member family has, only.
    CHECK(admit_dynamic_method(K({kKindMap, kKindStack}), "size", 0).admission == Admission::Dynamic);
    CHECK(admit_dynamic_method(K({kKindMap, kKindSet}), "empty", 0).admission == Admission::Dynamic);
    CHECK(admit_dynamic_method(K({kKindMap, kKindStack}), "contains", 1).admission == Admission::Refuse);
    CHECK(admit_dynamic_method(K({kKindMap, kKindString}), "size", 0).admission == Admission::Refuse);
    // Failure texts and their one error class.
    CHECK(dispatch_failure_message(DispatchFailure::Unsupported, "trim", "v.trim()") ==
          "trim: unsupported for this value");
    CHECK(dispatch_failure_message(DispatchFailure::Malformed, "trim", "v.trim()") ==
          "unknown value or malformed expression: v.trim()");
    CHECK(dispatch_failure_message(DispatchFailure::Composite, "to_string", "v.to_string()") ==
          "to_string: use stringify() for composite values");
    CHECK(!dispatch_failure_catchable(DispatchFailure::Unsupported));
    CHECK(dispatch_failure(Receiver::Map, "trim") == DispatchFailure::Refuse);
    CHECK(dispatch_failure(Receiver::String, "keys") == DispatchFailure::Unsupported);
    CHECK(dispatch_failure(Receiver::Bool, "join") == DispatchFailure::Malformed);
    CHECK(dispatch_failure(Receiver::Object, "to_string") == DispatchFailure::Composite);
}

} // namespace

int main() {
    receivers();
    siblings();
    no_internal();
    admission();
    return rt_test::finish("op-admission-unit");
}
