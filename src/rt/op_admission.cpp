#include "rt/op_admission.h"

#include "rt/aggregate_store.h"  // NIFT_RT_RELOCATE_ON_MUTATION: checked builds abort on a defect
#include "rt/builtin_bind.h"

#include <atomic>
#include <cstdlib>

// rt-lint: cold-file admission is decided at compile time; the run-time failure text is built only on an error path

namespace nift::rt {

using natives::BuiltinSpec;
using natives::Receiver;

namespace {

struct Family {
    Receiver receiver;
    KindSet kinds;
};

constexpr Family kPlainFamilies[] = {
    {Receiver::Null, kKindNull},     {Receiver::Bool, kKindBool},   {Receiver::Number, kKindNumber},
    {Receiver::String, kKindString}, {Receiver::Array, kKindArray}, {Receiver::Object, kKindObject},
};
constexpr Family kCollectionFamilies[] = {
    {Receiver::Map, kKindMap}, {Receiver::Set, kKindSet}, {Receiver::Stack, kKindStack},
};

bool plain_family(Receiver r) {
    return r == Receiver::Null || r == Receiver::Bool || r == Receiver::Number || r == Receiver::String ||
           r == Receiver::Array || r == Receiver::Object;
}

// A method name of some plain family (the names the legacy probe table covers).
bool plain_method_name(std::string_view name) {
    for (std::size_t i = 0; i < natives::builtin_count(); ++i) {
        const BuiltinSpec& spec = natives::builtin_spec(static_cast<natives::BuiltinId>(i));
        if (plain_family(spec.receiver) && spec.name == name) return true;
    }
    return false;
}

bool arity_fits(const BuiltinSpec& spec, std::uint32_t argc) {
    return argc >= spec.min_args && (spec.max_args == natives::kVariadic || argc <= spec.max_args);
}

std::atomic<std::uint64_t> g_dispatch_defects{0};

} // namespace

bool MethodAdmission::fails() const {
    for (const DispatchCandidate& b : branches)
        if (b.spec == nullptr) return true;
    return false;
}

KindSet family_kinds(Receiver family) {
    for (const Family& f : kPlainFamilies)
        if (f.receiver == family) return f.kinds;
    for (const Family& f : kCollectionFamilies)
        if (f.receiver == family) return f.kinds;
    return 0;
}

DispatchFailure dispatch_failure(Receiver family, std::string_view name) {
#if defined(NIFT_B4_MUTANT) && NIFT_B4_MUTANT == 6
    // A6: the unsupported and malformed families are swapped.
    if (family == Receiver::String || family == Receiver::Number) return DispatchFailure::Malformed;
    if (family == Receiver::Null || family == Receiver::Bool) return DispatchFailure::Unsupported;
#endif
    const bool composite = family == Receiver::Array || family == Receiver::Object;
    if (composite && name == "to_string") return DispatchFailure::Composite;
    // Context-dependent in legacy (prepared against canonical evaluation):
    // a number's to_double, an object's length.
    if (family == Receiver::Number && name == "to_double") return DispatchFailure::Refuse;
    if (family == Receiver::Object && name == "length") return DispatchFailure::Refuse;
    switch (family) {
        case Receiver::String: case Receiver::Number: case Receiver::Array: return DispatchFailure::Unsupported;
        case Receiver::Null: case Receiver::Bool: case Receiver::Object: return DispatchFailure::Malformed;
        default: return DispatchFailure::Refuse;
    }
}

std::string dispatch_failure_message(DispatchFailure failure, std::string_view name, std::string_view call_text) {
    switch (failure) {
        case DispatchFailure::Malformed:
            return "unknown value or malformed expression: " + std::string(call_text);
        case DispatchFailure::Composite: return "to_string: use stringify() for composite values";
        default: return std::string(name) + ": unsupported for this value";
    }
}

bool dispatch_failure_catchable(DispatchFailure) {
    // Provisional (coordinator ruling 1, pending the owner): legacy's class.
    return false;
}

std::uint64_t dispatch_defect_count() { return g_dispatch_defects.load(std::memory_order_relaxed); }

void note_dispatch_defect() {
    g_dispatch_defects.fetch_add(1, std::memory_order_relaxed);
#if defined(NIFT_RT_RELOCATE_ON_MUTATION)
    std::abort();  // checked builds: a compiler defect fails loudly
#endif
}

MethodAdmission admit_dynamic_method(KindSet kinds, std::string_view name, std::uint32_t argc) {
    MethodAdmission out;
#if defined(NIFT_B4_MUTANT) && NIFT_B4_MUTANT == 3
    (void)kinds; (void)name; (void)argc;
    return out;  // A3: every Dynamic case is refused
#endif
    if (kinds == 0 || (kinds & kKindUnknown)) return out;
    const bool plain = (kinds & ~(kKindScalar | kKindAggregate)) == 0;
    const bool collections = (kinds & ~kKindCollection) == 0;
    if (!plain && !collections) return out;
    if (plain && !plain_method_name(name)) return out;
    // Mixed collections: only the read-only natives every family has.
    if (collections && !(argc == 0 && (name == "size" || name == "empty"))) return out;
    std::size_t families = 0;
    bool any_native = false;
    const auto consider = [&](const Family& f) {
        if (!(kinds & f.kinds)) return true;
        ++families;
        const KindSet branch = static_cast<KindSet>(kinds & f.kinds);
        if (const BuiltinSpec* spec = natives::find_builtin(f.receiver, name)) {
            if (bytes_builtin(*spec) || !arity_fits(*spec, argc)) return false;
            out.branches.push_back({branch, spec, DispatchFailure::None});
            if (!out.spec) out.spec = spec;
            any_native = true;
            return true;
        }
        const DispatchFailure failure = dispatch_failure(f.receiver, name);
        if (failure == DispatchFailure::Refuse || collections) return false;
        out.branches.push_back({branch, nullptr, failure});
        return true;
    };
    bool ok = true;
    if (plain)
        for (const Family& f : kPlainFamilies) ok = ok && consider(f);
    else
        for (const Family& f : kCollectionFamilies) ok = ok && consider(f);
    if (!ok || families < 2 || !any_native) {
        out.branches.clear();
        out.spec = nullptr;
        return out;
    }
    out.admission = Admission::Dynamic;
    return out;
}

} // namespace nift::rt
