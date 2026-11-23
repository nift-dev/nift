#include "rt/builtin_bind.h"
#include "rt/native_qualifier.h"

// rt-lint: cold-file names are resolved here at compile time only, never on the VM hot path

namespace nift::rt {

using natives::BuiltinSpec;
using natives::Receiver;

Receiver receiver_family(KindSet kinds) {
    if (kinds == 0 || (kinds & kKindUnknown)) return Receiver::None;
    if (kinds == kKindString) return Receiver::String;
    if (kinds == kKindArray) return Receiver::Array;
    if (kinds == kKindObject) return Receiver::Object;
    if ((kinds & ~kKindNumber) == 0) return Receiver::Number;
    if (kinds == kKindBool) return Receiver::Bool;
    if (kinds == kKindNull) return Receiver::Null;
    if (kinds == kKindFile) return Receiver::File;
    if (kinds == kKindStream) return Receiver::Stream;
    if (kinds == kKindMutex) return Receiver::Mutex;
    if ((kinds & ~kKindAtomic) == 0) return Receiver::Atomic;
    if (kinds == kKindChannel) return Receiver::Channel;
    if (kinds == kKindMap) return Receiver::Map;      // D-063
    if (kinds == kKindSet) return Receiver::Set;
    if (kinds == kKindStack) return Receiver::Stack;
    return Receiver::None;
}

namespace {

bool stream_bytes_builtin(const BuiltinSpec& spec) {
    using natives::BuiltinId;
    return spec.id == BuiltinId::StreamReadBytes || spec.id == BuiltinId::StreamReadAllBytes;
}

const BuiltinSpec* first_method(std::string_view name) {
    for (std::size_t i = 0; i < natives::builtin_count(); ++i) {
        const BuiltinSpec& spec = natives::builtin_spec(static_cast<natives::BuiltinId>(i));
        if (spec.receiver != Receiver::None && spec.name == name && !stream_bytes_builtin(spec)) return &spec;
    }
    return nullptr;
}

} // namespace

MethodBinding bind_method(KindSet kinds, std::string_view name, const BuiltinSpec*& spec) {
    spec = nullptr;
    // Only a proven stream gets the new methods. Other receivers retain
    // their existing method refusal and never enter generic dispatch.
    if (kinds == kKindStream) {
        const BuiltinSpec* stream = natives::find_builtin(Receiver::Stream, name);
        if (stream && stream_bytes_builtin(*stream)) {
            spec = stream;
            return MethodBinding::Bound;
        }
    }
    const BuiltinSpec* any_name = first_method(name);
    if (!any_name) return MethodBinding::NotBuiltin;
    if (kinds == 0) {
        // Never executed (e.g. a parameter of a function no call reaches). The
        // native checks its receiver kind anyway and reports Internal (exit
        // 70) if that promise were ever broken.
        spec = any_name;
        return MethodBinding::Dead;
    }
    const Receiver family = receiver_family(kinds);
    if (family != Receiver::None) spec = natives::find_builtin(family, name);
    if (!spec && !(kinds & kKindUnknown) && !(kinds & kKindHandle))
        spec = natives::find_builtin(Receiver::Any, name);
    return spec ? MethodBinding::Bound : MethodBinding::Receiver;
}

bool session_method_candidates(KindSet kinds, std::string_view name, std::vector<SessionMethodCandidate>& out) {
    out.clear();
    if (!kinds || (kinds & ~(kKindScalar | kKindAggregate)) || receiver_family(kinds) != Receiver::None)
        return false;
    const Receiver families[] = {Receiver::Null, Receiver::Bool, Receiver::Number, Receiver::Number,
        Receiver::String, Receiver::Array, Receiver::Object};
    for (unsigned tag = 0; tag < 7; ++tag) {
        const auto branch = static_cast<KindSet>(kinds & ((tag == 2 || tag == 3) ? kKindNumber : (1u << tag)));
        if (!branch) continue;
        const auto* spec = natives::find_builtin(families[tag], name);
        if (!spec) { out.clear(); return false; }
        out.push_back({branch, static_cast<std::int64_t>(tag), spec});
    }
    return out.size() > 1;
}

// L6 (D-075 #2): `native::<name>` binds only the seven natives; in VM-default execution the unqualified seven bind
// no built-in (a user function, or the `native-unqualified` refusal). Every name-based analysis goes through here.
const BuiltinSpec* bind_function(std::string_view name) {
    if (const std::string_view qualified = native_qualified(name); !qualified.empty()) {
#if !(defined(NIFT_L6_MUTANT) && NIFT_L6_MUTANT == 1)  // test-only: the qualifier accepts any built-in name
        if (!native_function_name(qualified)) return nullptr;
#endif
        return natives::find_builtin(Receiver::None, qualified);
    }
#if !(defined(NIFT_L6_MUTANT) && NIFT_L6_MUTANT == 2)  // test-only: the unqualified seven stay bound
    if (native_function_name(name)) return nullptr;
#endif
    return natives::find_builtin(Receiver::None, name);
}

bool pure_method_name(std::string_view name) { return first_method(name) != nullptr; }

bool presentation_builtin(const BuiltinSpec& spec) { return spec.receiver == Receiver::Any; }

bool bytes_builtin(const BuiltinSpec& spec) {
    // The constructor and managed stream reads may produce Bytes.
    if (spec.id == natives::BuiltinId::FnBytes || stream_bytes_builtin(spec)) return false;
    return (spec.rule == natives::ResultRule::Fixed && (spec.result & natives::kBytes) != 0) ||
           spec.receiver == Receiver::Bytes;
}

bool plain_arguments_only(const BuiltinSpec& spec) {
    using natives::BuiltinId;
    return spec.id == BuiltinId::FnValidate || spec.id == BuiltinId::FnCopy || spec.id == BuiltinId::FnDeepcopy;
}

} // namespace nift::rt
