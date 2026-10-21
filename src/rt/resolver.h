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

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nift::rt {

struct Binding {
    std::uint16_t reg = 0;
    std::uint16_t type_reg = 0;
    bool typed = false;  // has a binding-type register (only bindings that are ever assigned)
    // CP12 review round 2: the inferred kinds of the declaring value (or the
    // parameter's arguments) and whether the declaring text may give the
    // binding type double; 0xff = unknown.
    std::uint8_t decl_kinds = 0xff;
    bool decl_text_double = false;  // the declaring text is always typed double
    bool decl_may_double = false;   // ... or may be (a binding it names may be double)
    // CP12b globals: a top-level binding a function reaches as a free name
    // (rt/globals.h); `reg`/`type_reg` are then main's frame slots, which
    // functions read and write with GetGlobal/SetGlobal (main's frame is at
    // stack base 0).
    bool global = false;
};

class Scopes {
public:
    void push() { scopes_.emplace_back(); }
    void pop() { scopes_.pop_back(); }
    // False when `name` is already declared in the innermost scope.
    bool declare(std::string_view name, const Binding& binding);
    const Binding* find(std::string_view name) const;
    bool empty() const { return scopes_.empty(); }

private:
    std::vector<std::vector<std::pair<std::string_view, Binding>>> scopes_;
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
