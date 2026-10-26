#pragma once
// Name resolution for the Runtime 2.0 compiler (CP11): lexical block scopes
// mapping local names to registers, the unit's function table, and the
// unit-level admission rules for functions (D-001 whole-unit fallback).
//
// CP11 resolves only names that are lexically visible: parameters and locals
// of the enclosing function (or top-level program) and top-level `fn`
// declarations. Legacy resolves free names in a function through the
// caller's scopes at run time (semantics.md "Free names"); a unit that needs
// that (GETDYN, CP12) is not compiled.
#include "rt/ast.h"
#include "rt/value.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nift::rt {

struct Binding {
    // Loop reads use the source location until explicit reassignment. The
    // per-iteration cache is a separate slot (indexed selector writes use it).
    // Captures copy the live descriptor or share the detached/held slot; they
    // do not infer value-vs-alias behavior from scalar/aggregate kinds.
    NodeId decl_node = kNoNode;
    std::uint16_t reg = 0;
    std::uint16_t type_reg = 0;
    bool typed = false;  // has a binding-type register (only bindings that are ever assigned)
    // CP12 review round 2: the inferred kinds of the declaring value (or the
    // parameter's arguments) and whether the declaring text may give the
    // binding type double; 0xff = unknown.
    std::uint16_t decl_kinds = 0x7ff;  // CP12 KindSet (widened in R6)
    bool decl_text_double = false;  // the declaring text is always typed double
    bool decl_may_double = false;   // ... or may be (a binding it names may be double)
    // CP12b globals: a top-level binding a function reaches as a free name
    // (rt/globals.h); `reg`/`type_reg` are then main's frame slots, which
    // functions read and write with GetGlobal/SetGlobal (main's frame is at
    // stack base 0).
    bool global = false;
    // CP12C closures: a captured local/parameter lives in a Cell; a lambda
    // binding is an upvalue (index into the running closure's cells).
    bool cell = false;
    bool upvalue = false;
    bool const_array = false;  // CP14B narrow array const admission
    bool readonly = false;  // CP13B: a catch binding (body-scoped, never assigned)
    std::uint16_t cell_reg = 0;
    std::uint16_t upvalue_index = 0;
    // CP12C slice 2 lazy `for` read binding (array single form).
    bool lazy_for = false;
    bool loop_binding = false;
    // A named iterable's slot is boxed only if an escaping selector needs it.
    // This internal owner does not imply mutation through a user closure.
    bool selector_root_only = false;
    bool location_source = false;
    bool selector = false;
    std::uint16_t selector_reg = 0;
    bool lazy_pair = false;
    std::uint16_t lazy_keys = 0;
    std::string_view lazy_source_name;
    std::string_view lazy_name;
    std::uint16_t lazy_index = 0;
    std::uint16_t lazy_active = 0;
    std::shared_ptr<const Binding> lazy_source_binding;
    // CP12b structs: an instance of UnitPlan::structs[struct_id] (-1: none).
    int struct_id = -1;
};

class Scopes {
public:
    void push() { scopes_.emplace_back(); }
    void pop() { scopes_.pop_back(); }
    // False when `name` is already declared in the innermost scope.
    bool declare(std::string_view name, const Binding& binding);
    const Binding* find(std::string_view name) const;
    bool update(std::string_view name, const Binding& binding);
    bool empty() const { return scopes_.empty(); }

private:
    std::vector<std::vector<std::pair<std::string_view, Binding>>> scopes_;
};

// CP12b structs (rt/structs.h): a top-level `struct (Name) { ... }`.
struct StructFieldInfo {
    std::string_view name;
    NodeId node = kNoNode;  // the StructField
    bool priv = false;
    Value value;            // the initialiser (a constant)
    std::int64_t type = 0;  // the field's binding type (legacy's, from the initialiser)
};
struct StructMethodInfo {
    std::string_view name;
    bool priv = false;
    std::size_t function = 0;  // index into UnitPlan::functions
};
struct StructInfo {
    std::string_view name;
    NodeId decl = kNoNode;
    std::vector<StructFieldInfo> fields;
    std::vector<StructMethodInfo> methods;
    int ctor = -1;  // index into methods of the constructor (named like the struct)
    const StructFieldInfo* field(std::string_view n) const {
        for (const auto& f : fields) if (f.name == n) return &f;
        return nullptr;
    }
    const StructMethodInfo* method(std::string_view n) const {
        for (const auto& m : methods) if (m.name == n) return &m;
        return nullptr;
    }
};

struct UnitPlan {
    bool ok = true;
    std::string reason;
    std::uint32_t offset = 0;
    std::vector<NodeId> functions;                           // FnDecl nodes, proto index = position + 1
    std::unordered_map<std::string_view, std::uint16_t> index;  // function name -> proto index
    // escaping[f][k]: parameter k of functions[f] may be mutated or rebound
    // through (alias.h); filled by plan_unit, empty when no call passes a place.
    std::vector<std::vector<bool>> escaping;
    // CP12b structs: the unit's structs; their methods are functions too
    // (after the top-level ones, not in `index`): method_of[f] is the struct
    // of functions[f], -1 for a top-level function.
    std::vector<StructInfo> structs;
    std::unordered_map<std::string_view, std::size_t> struct_index;
    std::vector<int> method_of;
    bool is_method(std::size_t f) const { return f < method_of.size() && method_of[f] >= 0; }
    // CP22: the unit declares an async function or contains an async lambda
    // (a coroutine unit: Spawn/CallValueAny/Await/Sleep/Yield are emitted).
    bool coroutines = false;
};

// Collects the top-level functions of `root` (a Program) and checks the
// admission rules for them. A declaration may follow statements (CP12 phase
// B) when no earlier top-level statement can reach that function through
// calls (else `fn-called-before-declaration`): legacy registers a function
// when its declaration runs, and compile-time binding is exact then.
UnitPlan plan_unit(const Ast& ast, NodeId root);

// Every name that is the target of `=`, a compound assignment or `++`/`--`
// anywhere inside the statements of `node`.
void collect_assigned(const Ast& ast, NodeId node, std::unordered_set<std::string_view>& names);

} // namespace nift::rt
