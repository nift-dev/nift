#include "rt/compiler_native_args.h"
#include "rt/builtin_bind.h"
#include "rt/compiler_handles.h"
// rt-lint: cold-file shared native argument evaluation and error restrictions
namespace nift::rt::detail {
bool UnitCompiler::builtin_arguments_ok(const Node& node, const natives::BuiltinSpec& spec, NodeId receiver) {
    const NodeId* args = ast_.list(node);
    // CP15 W8: no bytes kind in CP12's inference; bytes built-ins are refused.
    if (bytes_builtin(spec)) return fail(node, "builtin-bytes:" + std::string(spec.name));
    if (plain_arguments_only(spec))
        for (std::uint32_t k = 0; k < node.count; ++k) {
            const Node& arg = ast_.at(args[k]);
            // CP15 W10 (J-Q2): legacy strips the quotes of a string-literal
            // validate argument at top level (and not in loop code).
            if (spec.id == natives::BuiltinId::FnValidate && arg.kind == NodeKind::String)
                return fail(arg, "builtin-literal-arg:validate");
            // CP15 W11: provably plain values only (until CP17's structs).
            const KindSet k_arg = kinds(args[k]);
            if ((k_arg & kKindUnknown) && !unknown_defers(k_arg))
                return fail(arg, "builtin-plain-arg:" + std::string(spec.name));
        }
    // D-063 / D-040 #4: same() compares identity. Shared concurrency handles
    // and task handles are identity values here but an error in legacy: not
    // compiled (no unapproved difference). Ordinary values (arrays, objects,
    // structs, collections) are the approved D-040 #4 error at run time.
    if (spec.id == natives::BuiltinId::FnSame)
        for (std::uint32_t k = 0; k < node.count; ++k)
            if (kinds(args[k]) & (kKindShared | kKindTask))
                return fail(ast_.at(args[k]), "same:shared-handle");
    // D-063: legacy's collections are tagged strings. A built-in that renders,
    // hashes, converts or compares them as such (html_escape(m), sha256(m),
    // validate(s, m), [m].contains(m), [m].join(",")) would observe the tag;
    // only these accept a map/set/stack argument (or element) here.
    if (unit_makes_collections()) {
        using natives::BuiltinId;
        const auto id = spec.id;
        const bool value_ok = id == BuiltinId::FnType || id == BuiltinId::FnCopy || id == BuiltinId::FnDeepcopy ||
                              id == BuiltinId::FnSame || (id >= BuiltinId::FnIsNull && id <= BuiltinId::FnIsEnum) ||
                              spec.receiver == natives::Receiver::Map || spec.receiver == natives::Receiver::Set ||
                              spec.receiver == natives::Receiver::Stack;
        if (!value_ok)
            for (std::uint32_t k = 0; k < node.count; ++k)
                if ((kinds(args[k]) & kKindCollection) && !(kinds(args[k]) & kKindUnknown))
                    return fail(ast_.at(args[k]), "collection-argument:" + std::string(spec.name));
        const bool inspects = id == BuiltinId::ArrContains || id == BuiltinId::ArrIndexOf ||
                              id == BuiltinId::ArrJoin || id == BuiltinId::ArrUnique || id == BuiltinId::ArrSum ||
                              id == BuiltinId::ArrMin || id == BuiltinId::ArrMax || id == BuiltinId::ArrFromEntries ||
                              id == BuiltinId::ObjMergeDeep;
        if (inspects && receiver != kNoNode) {
            const auto found = kinds_.receiver_elements.find(receiver);
            if (found != kinds_.receiver_elements.end() && (found->second & kKindCollection))
                return fail(node, "collection-element:" + std::string(spec.name));
        }
    }
    // W1: legacy checks arity before or between argument evaluations (Q10).
    if (node.count < spec.min_args || (spec.max_args != natives::kVariadic && node.count > spec.max_args))
        return fail(node, "builtin-arity:" + std::string(spec.name));
    const auto literal_write = write_literal_argument(ast_, node, spec);
    if (literal_write != kNoNode) return fail(ast_.at(literal_write), "host-literal-arg");
    // W2 / W4: legacy validates lazily and may never evaluate later arguments.
    if (spec.may_fail)
        for (std::uint32_t k = 1; k < node.count; ++k)
            if (!unobservable_argument(args[k]))
                return fail(ast_.at(args[k]), spec.id == natives::BuiltinId::ObjGet
                                                  ? "builtin-default:get"
                                                  : "builtin-lazy-args:" + std::string(spec.name));
    // D2 ruling (VM subset; register L56): in textual code legacy swallows a
    // fault while evaluating a checked method argument and reports the
    // method's type message instead (`s.contains(o.zz)`: "contains: argument
    // must be a string"), in loop code the fault itself. Refused there when
    // the argument can fault; later arguments are unobservable (above), and
    // methods that accept any value (may_fail false) report the fault too.
    if (receiver != kNoNode && spec.may_fail && !loop_reachable() && node.count > 0 && may_fault(args[0]))
        return fail(ast_.at(args[0]), "builtin-arg-fault-context");
    return true;
}
// D-063: does the unit construct a map, set or stack anywhere (the only
// source of collections: the unit is closed, threads and modules included)?
// Without one, "any kind" values (top) hold no collection.
bool UnitCompiler::unit_makes_collections() {
    if (makes_collections_ < 0) {
        makes_collections_ = 0;
        for (NodeId id = 0; id < ast_.size() && !makes_collections_; ++id) {
            const Node& n = ast_.at(id);
            if (n.kind != NodeKind::Call || n.count != 0 || ast_.at(n.a).kind != NodeKind::Ident) continue;
            const std::string_view name = text(ast_.at(n.a));
            if (name == "map" || name == "set" || name == "stack") makes_collections_ = 1;
        }
    }
    return makes_collections_ == 1;
}
} // namespace nift::rt::detail
