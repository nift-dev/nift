#pragma once
// Structural types of the unit kind inference (rt/infer.h), internal (CP12).
#include "rt/ast.h"
#include "rt/infer.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nift::rt::infer_detail {

struct Ty;
using TyRef = std::shared_ptr<const Ty>;

struct Ty {
    KindSet kinds = 0;
    bool historical_callable = false, untracked_callable = false;
    bool legacy_text_number = false;  // may have moved-from StrNumber storage
    bool collection_array = false;  // CP14B array result provenance
    bool collection_object = false;  // CP14B result-container provenance
    bool any = false;  // every kind at every depth
    TyRef elem;        // arrays: the elements
    // Arrays that are all literals of one length never changed in place:
    // the type of each position (an index written as an integer literal).
    bool tuple = false;
    std::vector<TyRef> items;
    std::map<std::string, TyRef, std::less<>> fields;  // objects: members by key
    // CP12C: callable units this value may be (named functions and lambdas).
    // A Function kind with no units is an untracked callable (Unknown).
    std::vector<std::uint32_t> callables;
    // CP12b structs: an instance of UnitPlan::structs[struct_id] (-1: none;
    // -2: instances of different structs, or an instance and other values).
    int struct_id = -1;
};

const TyRef& empty_ty();
// Every kind at every depth ("top"), e.g. beyond the depth cap.
const TyRef& top_ty();
// The value of something the compiler refuses anyway (kKindUnknown).
const TyRef& any_ty();
TyRef kinds_ty(KindSet kinds);
TyRef with_text_number(const TyRef&,bool maybe);
TyRef unite(const TyRef& a, const TyRef& b, int depth = 0);
bool same(const TyRef& a, const TyRef& b);
// The type of `value[key]` (key: a literal string, or null when unknown;
// position: an integer literal index, or -1).
TyRef element_of(const TyRef& t, const std::string* key, bool by_member, long position = -1);
// Can control reach the end of `block` (a Block node)?
bool block_falls_through(const Ast& ast, NodeId block);
// The value of an integer literal index (`a[2]`), or -1.
long literal_position(std::string_view digits);
// Folded literal arithmetic per node (doubles: only the kind is used).
struct Constants {
    std::vector<double> values;  // NaN: not constant
    KindSet set(std::size_t nodes, NodeId id, double v);  // returns the kind (Int / Frac)
    bool has(NodeId id) const;
    double at(NodeId id) const { return values[id]; }
};
// `l op r` on literal values (finite result, no division or modulo by zero).
bool fold_constant(Op op, double l, double r, double& out);
// Result kinds of `l op r` for an arithmetic operator.
KindSet arithmetic_kinds(Op op, KindSet l, KindSet r);
// CP22: the value of calling a unit with these flags whose returns are
// `returns` (an async unit returns a future of them, kept as its `elem`);
// the value of `await` on a value of type `t`; whether `t` is or may contain
// a future at any depth.
TyRef async_result(std::uint32_t flags, const TyRef& returns);
TyRef awaited(const TyRef& t);
bool holds_future(const TyRef& t, int depth = 0);

} // namespace nift::rt::infer_detail
