#include "rt/infer_builtins.h"

#include "rt/builtin_bind.h"
#include "rt/native.h"

// Result types of CP14 built-ins for the kind inference (rt/infer.cpp).
// rt-lint: cold-file compile-time analysis, once per unit

namespace nift::rt::infer_detail {

namespace {

// A container of `kinds` whose elements or members are unknown: every
// kind at every depth below it.
TyRef open_ty(std::uint8_t kinds) {
    auto t = std::make_shared<Ty>();
    t->kinds = kinds;
    t->any = true;
    return t;
}
TyRef array_of(const TyRef& elem) {
    auto t = std::make_shared<Ty>();
    t->kinds = kKindArray;
    t->elem = elem;
    return t;
}

// CP14: the result type of a bound built-in (BuiltinSpec::rule, W4 of
// the wiring plan). `recv` is null for a free function.
TyRef builtin_result(const Ast& ast_, const natives::BuiltinSpec& spec, const TyRef& recv,
                     const std::vector<TyRef>& values, const Node& n) {
    using natives::BuiltinId;
    const NodeId* args = ast_.list(n);
    // CP15 W8: bytes results have no CP12 kind bit; the compiler refuses them.
    if (bytes_builtin(spec)) return any_ty();
    const auto elements = [](const TyRef& t) { return element_of(t, nullptr, false); };
    const auto members = [](const TyRef& t) { return element_of(t, nullptr, true); };
    switch (spec.rule) {
        case natives::ResultRule::ElementOf: return elements(recv);
        case natives::ResultRule::ElementsOf: return array_of(elements(recv));
        case natives::ResultRule::MemberOf: {  // get(k[, default])
            TyRef out = values.size() > 1 ? values[1] : kinds_ty(kKindNull);
            const Node& key = ast_.at(args[0]);
            if (key.kind == NodeKind::String && key.count == 0 && ast_.text(key).size() >= 2 && !recv->any) {
                const std::string k(ast_.text(key).substr(1, ast_.text(key).size() - 2));
                if (k.find('\\') == std::string::npos) return unite(out, element_of(recv, &k, true));
            }
            return unite(out, members(recv));
        }
        case natives::ResultRule::Merged: {  // merge / merge_deep: members of both
            const TyRef both = unite(recv, values.empty() ? empty_ty() : values[0]);
            if (both->any) return open_ty(kKindObject);
            auto t = std::make_shared<Ty>(*both);
            t->kinds = kKindObject;
            t->elem = nullptr;
            t->tuple = false;
            t->items.clear();
            return t;
        }
        case natives::ResultRule::Selected: {  // free min / max; CP15 validate, copy, deepcopy
            if (spec.id == BuiltinId::FnValidate) return values.size() == 2 ? values[1] : any_ty();
            if (spec.id == BuiltinId::FnCopy || spec.id == BuiltinId::FnDeepcopy)
                return values.size() == 1 ? values[0] : any_ty();
            TyRef out = empty_ty();
            if (values.size() == 1 && (values[0]->kinds & ~kKindUnknown) == kKindArray && !values[0]->any)
                return elements(values[0]);
            for (const TyRef& v : values) {
                out = unite(out, v);
                if (v->kinds & kKindArray) out = unite(out, elements(v));
            }
            return out;
        }
        case natives::ResultRule::Fixed: break;
    }
    switch (spec.id) {
        case BuiltinId::StrSplit: case BuiltinId::ObjKeys: return array_of(kinds_ty(kKindString));
        case BuiltinId::ObjValues: return array_of(members(recv));
        case BuiltinId::ObjEntries: {
            auto entry = std::make_shared<Ty>();
            entry->kinds = kKindObject;
            entry->fields["key"] = kinds_ty(kKindString);
            entry->fields["value"] = members(recv);
            return array_of(entry);
        }
        case BuiltinId::ArrChunk: return array_of(array_of(elements(recv)));
        case BuiltinId::FnRange: return array_of(kinds_ty(kKindInt));
        case BuiltinId::ObjPick: case BuiltinId::ObjOmit: {  // a subset of the receiver's members
            if (recv->any) return open_ty(kKindObject);
            auto t = std::make_shared<Ty>(*recv);
            t->kinds = kKindObject;
            return t;
        }
        default: break;
    }
    // Arrays and objects whose contents the rule does not describe
    // (flatten, from_entries): open, never "no members".
    if (spec.result & (kKindArray | kKindObject)) return open_ty(spec.result);
    return kinds_ty(spec.result);
}


} // namespace

TyRef method_result(const Ast& ast, const Node& n, const TyRef& recv, const std::vector<TyRef>& values) {
    const std::string_view name = ast.text(ast.at(ast.at(n.a).b));
    if ((name == "length" || name == "size") && n.count == 0)  // rt-lint: allow-string-compare compile time
        return kinds_ty(kKindInt);  // the Length opcode
    const natives::BuiltinSpec* spec = nullptr;
    switch (bind_method(recv->kinds, name, spec)) {
        case MethodBinding::Bound: return builtin_result(ast, *spec, recv, values, n);
        case MethodBinding::Dead: return empty_ty();  // never executed
        default: return any_ty();  // refused by the compiler
    }
}

TyRef function_result(const Ast& ast, const Node& n, const std::vector<TyRef>& values) {
    const std::string_view name = ast.text(ast.at(n.a));
    NativeInfo native{};
    if (lookup_native(name, native)) return kinds_ty(kKindNull);
    if (const natives::BuiltinSpec* spec = bind_function(name)) return builtin_result(ast, *spec, nullptr, values, n);
    return any_ty();
}

} // namespace nift::rt::infer_detail
