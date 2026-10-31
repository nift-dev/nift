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
    if (!collection_inspection_ok(node, spec, receiver)) return false;
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
// D-063. same() (D-040 #4, owner ruling 2026-11-01) compares identity; and
// legacy's collections are tagged strings, so the built-in operations that
// render, compare or key on one, or store one into shared state, would see
// (or carry) the tag where the VM has a value. Only those are refused before
// execution; plain nesting and the other built-ins run.
bool UnitCompiler::collection_inspection_ok(const Node& node, const natives::BuiltinSpec& spec, NodeId receiver) {
    using natives::BuiltinId;
    static_cast<void>(receiver);  // unused in mutant 12
    const NodeId* args = ast_.list(node);
    const auto id = spec.id;
    if (id == BuiltinId::FnSame) {
        // Legacy reads each argument's text as a binding name first (a
        // location reference, a shared array binding) and evaluates the
        // others up to twice: `same("f", "f")` compares the binding f,
        // `same(g(), x)` runs g() twice. Only plain names keep one meaning.
#if !defined(NIFT_COLLECTIONS_MUTANT) || NIFT_COLLECTIONS_MUTANT != 11
        for (std::uint32_t k = 0; k < node.count; ++k)
            if (ast_.at(args[k]).kind != NodeKind::Ident) return fail(ast_.at(args[k]), "same:argument-shape");
#endif
        return true;  // ordinary values: the D-040 #4 error at run time
    }
    if (!unit_makes_collections()) return true;
    // Built-ins that only report kinds, copy, or are the collection methods.
    const bool value_ok = id == BuiltinId::FnType || id == BuiltinId::FnCopy || id == BuiltinId::FnDeepcopy ||
                          (id >= BuiltinId::FnIsNull && id <= BuiltinId::FnIsEnum) ||
                          spec.receiver == natives::Receiver::Map || spec.receiver == natives::Receiver::Set ||
                          spec.receiver == natives::Receiver::Stack;
    if (value_ok) return true;
    for (std::uint32_t k = 0; k < node.count; ++k)
        if ((kinds(args[k]) & kKindCollection) && !(kinds(args[k]) & kKindUnknown))
            return fail(ast_.at(args[k]), "collection-argument:" + std::string(spec.name));
#if !defined(NIFT_COLLECTIONS_MUTANT) || NIFT_COLLECTIONS_MUTANT != 12
    // Only the operations whose legacy meaning sees the tag (audited against
    // NIFT_VM=0; plain nesting runs, e.g. `[[m]].contains([m])`, `[a, m]`):
    // comparisons, rendering and keys over direct elements of the receiver,
    // or of min()/max()'s array argument ...
    const bool inspects = id == BuiltinId::ArrContains || id == BuiltinId::ArrIndexOf ||
                          id == BuiltinId::ArrJoin || id == BuiltinId::ArrUnique || id == BuiltinId::ArrSum ||
                          id == BuiltinId::ArrMin || id == BuiltinId::ArrMax || id == BuiltinId::ArrFromEntries ||
                          id == BuiltinId::ObjMergeDeep;
    if (inspects && receiver != kNoNode && kinds_.collection_elements_of.count(receiver))
        return fail(node, "collection-element:" + std::string(spec.name));
    if (id == BuiltinId::FnMin || id == BuiltinId::FnMax)
        for (std::uint32_t k = 0; k < node.count; ++k)
            if (kinds_.collection_elements_of.count(args[k]))
                return fail(ast_.at(args[k]), "collection-argument:" + std::string(spec.name));
    // ... and stores into shared state, where legacy refuses a collection at
    // any depth ("set value contains a non-transferable resource").
    if (id == BuiltinId::MutexSet || id == BuiltinId::SharedSend)
        for (std::uint32_t k = 0; k < node.count; ++k)
            if (kinds_.collection_holders.count(args[k]))
                return fail(ast_.at(args[k]), "collection-argument:" + std::string(spec.name));
#endif
    return true;
}
} // namespace nift::rt::detail
