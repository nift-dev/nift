#include "rt/builtin_bind.h"

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
    return Receiver::None;
}

namespace {

const BuiltinSpec* first_method(std::string_view name) {
    for (std::size_t i = 0; i < natives::builtin_count(); ++i) {
        const BuiltinSpec& spec = natives::builtin_spec(static_cast<natives::BuiltinId>(i));
        if (spec.receiver != Receiver::None && spec.name == name) return &spec;
    }
    return nullptr;
}

} // namespace

MethodBinding bind_method(KindSet kinds, std::string_view name, const BuiltinSpec*& spec) {
    spec = nullptr;
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

const BuiltinSpec* bind_function(std::string_view name) { return natives::find_builtin(Receiver::None, name); }

bool pure_method_name(std::string_view name) { return first_method(name) != nullptr; }

bool presentation_builtin(const BuiltinSpec& spec) { return spec.receiver == Receiver::Any; }

bool bytes_builtin(const BuiltinSpec& spec) {
    // `Selected` results (validate, copy, deepcopy) are their arguments'
    // kinds, which never include bytes; only fixed results can be bytes.
    return (spec.rule == natives::ResultRule::Fixed && (spec.result & natives::kBytes) != 0) ||
           spec.receiver == Receiver::Bytes;
}

bool plain_arguments_only(const BuiltinSpec& spec) {
    using natives::BuiltinId;
    return spec.id == BuiltinId::FnValidate || spec.id == BuiltinId::FnCopy || spec.id == BuiltinId::FnDeepcopy;
}

} // namespace nift::rt
