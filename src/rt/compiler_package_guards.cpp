// rt-lint: cold-file package compatibility dispatch runs at compile time; no runtime string comparisons here
#include "rt/compiler_package_guards.h"

#include "rt/builtin_bind.h"
#include "rt/module_data.h"
#include "rt/module_link.h"
#include "rt/native.h"
#include "rt/op_admission.h"

namespace nift::rt::detail {

bool UnitCompiler::package_method_call(const Node& node, NodeId receiver, std::string_view name,
                                       std::uint16_t dst, bool& matched) {
    matched = false;
        const KindSet k = kinds(receiver);
        // Imported package code may encode/decode unproved values with the
        // literal UTF-8 encoding; the native validates the reached receiver.
        const bool conversion = name == "encode" || name == "decode";
        if (conversion && !session_ && in_function_ && !plan_.coroutines &&
            imported_module_data(plan_, node) && node.count == 1 && !(k & kKindStruct)) {
            const auto receiver_kind = name == "encode" ? kKindString : kKindBytes;
            if (k == 0 || (k & (kKindUnknown | receiver_kind)) != 0) {
                const Node& label = ast_.at(ast_.list(node)[0]);
                if (label.kind == NodeKind::String && text(label) == R"("utf-8")") {
                    const auto family = name == "encode" ? natives::Receiver::String : natives::Receiver::Bytes;
                    if (const auto* spec = natives::find_builtin(family, name)) {
                        matched = true;
                        return builtin_call(node, *spec, receiver, dst);
                        }
                }
            }
        }
        // Legacy's `to_int` takes no arguments; the reached call is a fatal
        // program error before any effect (Uncalled package helpers keep it).
        if (name == "to_int" && node.count != 0 && imported_module_data(plan_, node)) {
            matched = true;
            return legacy_fatal(node, "to_int: expected no arguments", dst);
            }
        // Imported package code may convert an unproved number/string at run
        // time; the VM dispatches to the number or string native by family.
        if (name == "to_int" && !session_ && in_function_ && !plan_.coroutines &&
            imported_module_data(plan_, node) &&
            (k == 0 || (k & (kKindUnknown | kKindNumber | kKindString)) != 0)) {
            const MethodAdmission admitted =
                admit_dynamic_method(static_cast<KindSet>(kKindNumber | kKindString), "to_int", node.count);
            if (admitted.admission == Admission::Dynamic && admitted.spec) {
                matched = true;
                return builtin_call(node, *admitted.spec, receiver, dst, true);
                }
        }
        // Presentation on an unproved package value: the flagged site picks
        // the run-time family's native (legacy's own reached text).
        if (!session_ && in_function_ && !plan_.coroutines && imported_module_data(plan_, node) &&
            name == "to_string") {
            const MethodAdmission admitted = admit_dynamic_method(
                static_cast<KindSet>(kKindString | kKindBool | kKindNull | kKindNumber), "to_string", node.count);
            if (admitted.admission == Admission::Dynamic && admitted.spec) {
                matched = true;
                return builtin_call(node, *admitted.spec, receiver, dst, true);
                }
        }
        // String methods on an unproved package value: the flagged site
        // re-dispatches by the receiver's run-time family (legacy's reached
        // error for a non-string receiver).
        if (!session_ && in_function_ && !plan_.coroutines && imported_module_data(plan_, node) &&
            (k == 0 || (k & (kKindUnknown | kKindNull | kKindString)) != 0)) {
            const bool string_method = name == "substr" || name == "split" || name == "replace" ||
                                       name == "replace_once" || name == "trim" || name == "trim_start" ||
                                       name == "trim_end" || name == "starts_with" || name == "ends_with" ||
                                       name == "index_of" || name == "last_index_of" || name == "to_upper" ||
                                       name == "to_lower";
            if (string_method) {
                const MethodAdmission admitted = admit_dynamic_method(kKindString, name, node.count);
                if (admitted.admission == Admission::Dynamic && admitted.spec) {
                    matched = true;
                    return builtin_call(node, *admitted.spec, receiver, dst, true);
                    }
                if (const auto* spec = natives::find_builtin(natives::Receiver::String, name)) {
                    matched = true;
                    return builtin_call(node, *spec, receiver, dst, true);
                    }
            }
        }
        if (!session_ && in_function_ && !plan_.coroutines && imported_module_data(plan_, node) &&
            (k == 0 || (k & kKindUnknown)) && name == "contains") {
            const MethodAdmission admitted =
                admit_dynamic_method(static_cast<KindSet>(kKindString | kKindArray), "contains", node.count);
            if (admitted.admission == Admission::Dynamic && admitted.spec) {
                matched = true;
                return builtin_call(node, *admitted.spec, receiver, dst, true);
                }
        }
        // Array methods on an unproved value: the flagged site re-dispatches
        // (legacy's reached error when the receiver is not an array).
        const bool array_unproved = k == 0 || (k & kKindUnknown) != 0;
        if (!session_ && !plan_.coroutines &&
            (array_unproved || (imported_module_data(plan_, node) && (k & kKindArray) != 0))) {
            const bool array_method = name == "join" || name == "slice" || name == "unique" ||
                                      name == "flatten" || name == "sum" || name == "first" ||
                                      name == "last" || name == "index_of" || name == "take" || name == "drop";
            if (array_method)
                if (const auto* spec = natives::find_builtin(natives::Receiver::Array, name)) {
                    matched = true;
                    return builtin_call(node, *spec, receiver, dst, true);
                    }
        }
        // Object methods on an unproved package value: the flagged site
        // re-dispatches by the receiver's run-time family.
        const NodeKind object_receiver_kind = ast_.at(receiver).kind;
        const bool path_receiver = object_receiver_kind == NodeKind::Member || object_receiver_kind == NodeKind::Index;
        const bool object_unproved = k == 0 || (k & kKindUnknown) != 0;
        const bool object_scope = imported_module_data(plan_, node)
                                      ? (object_unproved || (k & kKindObject) != 0)
                                      : object_unproved;
        if (!session_ && !plan_.coroutines && (path_receiver || imported_module_data(plan_, node)) && object_scope) {
            const bool object_method = name == "omit" || name == "pick" || name == "keys" ||
                                       name == "values" || name == "entries" || name == "has" ||
                                       name == "get" || name == "merge" || name == "merge_deep";
            if (object_method)
                if (const auto* spec = natives::find_builtin(natives::Receiver::Object, name)) {
                    matched = true;
                    return builtin_call(node, *spec, receiver, dst, true);
                    }
        }
        // Numeric conversions/rounding on an unproved package value: the
        // flagged site re-dispatches by the receiver's run-time family.
        if (!session_ && in_function_ && !plan_.coroutines && imported_module_data(plan_, node) &&
            (k == 0 || (k & (kKindUnknown | kKindNumber)) != 0)) {
            const bool numeric_method = name == "floor" || name == "ceil" || name == "round" ||
                                        name == "abs";
            if (numeric_method)
                if (const auto* spec = natives::find_builtin(natives::Receiver::Number, name)) {
                    matched = true;
                    return builtin_call(node, *spec, receiver, dst, true);
                    }
        }

    return true;
}

} // namespace nift::rt::detail
